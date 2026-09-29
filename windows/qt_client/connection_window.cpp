#include "connection_window.h"

#include "heartbeat.h"
#include "packet.h"
#include "reconnect.h"
#include "remote_screen_widget.h"

#include <QAbstractSocket>
#include <QCheckBox>
#include <QCloseEvent>
#include <QFormLayout>
#include <QFrame>
#include <QFutureWatcher>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkProxy>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTcpSocket>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <cstring>
#include <utility>
#include <vector>

namespace {

constexpr qint64 MAX_SCREEN_FRAME_BYTES = 256LL * 1024 * 1024;

Packet buildTextPacket(std::int32_t command, const QByteArray& text)
{
    Packet packet = {};
    packet.magic = PACKET_MAGIC;
    packet.cmd = command;
    packet.body_len = qMin(text.size(), PACKET_DATA_SIZE);

    if (packet.body_len > 0) {
        std::memcpy(packet.data, text.constData(), packet.body_len);
    }

    return packet;
}

}

ConnectionWindow::ConnectionWindow(QWidget* parent)
    : QWidget(parent),
      hostEdit_(new QLineEdit(this)),
      portSpin_(new QSpinBox(this)),
      connectButton_(new QPushButton(this)),
      inputEnabledCheckBox_(new QCheckBox(this)),
      statusValue_(new QLabel(this)),
      detailsValue_(new QLabel(this)),
      frameInfoValue_(new QLabel(this)),
      screenWidget_(new RemoteScreenWidget(this)),
      socket_(new QTcpSocket(this)),
      heartbeatTimer_(new QTimer(this)),
      reconnectTimer_(new QTimer(this)),
      decodeWatcher_(new QFutureWatcher<QImage>(this)),
      lastReceiveMs_(0),
      lastPingMs_(0),
      heartbeatSequence_(1),
      receivedFrameBytes_(0),
      screenGeneration_(0),
      droppedDecodeFrames_(0),
      lastRemoteX_(0),
      lastRemoteY_(0),
      reconnectAttempt_(0),
      reconnecting_(false),
      connectedSession_(false),
      disconnectHandled_(true),
      manualDisconnect_(false)
{
    setWindowTitle(QStringLiteral("远程控制 - 控制端"));
    resize(1100, 780);
    setMinimumSize(700, 560);

    auto* title = new QLabel(QStringLiteral("远程控制控制端"), this);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 5);
    titleFont.setBold(true);
    title->setFont(titleFont);

    auto* subtitle = new QLabel(
        QStringLiteral("连接 Windows 或 Linux 被控端，查看并控制远程桌面。"),
        this
    );
    subtitle->setWordWrap(true);

    QSettings settings;
    hostEdit_->setText(settings.value("connection/host", "127.0.0.1").toString());
    hostEdit_->setPlaceholderText(QStringLiteral("IP 地址或主机名"));

    portSpin_->setRange(1, 65535);
    portSpin_->setValue(settings.value("connection/port", 9999).toInt());

    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("服务地址"), hostEdit_);
    form->addRow(QStringLiteral("端口"), portSpin_);

    auto* separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);

    auto* statusCaption = new QLabel(QStringLiteral("连接状态"), this);
    QFont statusFont = statusCaption->font();
    statusFont.setBold(true);
    statusCaption->setFont(statusFont);

    detailsValue_->setWordWrap(true);
    detailsValue_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    frameInfoValue_->setText(QStringLiteral("尚未收到远程画面。"));
    inputEnabledCheckBox_->setText(QStringLiteral("启用远程键盘和鼠标控制"));
    inputEnabledCheckBox_->setChecked(true);
    inputEnabledCheckBox_->setToolTip(
        QStringLiteral("关闭后只查看远程画面，不发送键盘和鼠标操作。")
    );
    screenWidget_->setToolTip(
        QStringLiteral("点击远程画面后即可发送键盘操作。")
    );
    screenWidget_->setMouseEventHandler(
        [this](int action, int button, int x, int y) {
            if (inputEnabledCheckBox_->isChecked()) {
                sendRemoteMouseEvent(action, button, x, y);
            }
        }
    );
    screenWidget_->setKeyEventHandler(
        [this](int status, const QString& key) {
            if (inputEnabledCheckBox_->isChecked()) {
                sendRemoteKeyEvent(status, key);
            }
        }
    );
    screenWidget_->setReleaseInputHandler([this]() {
        releaseRemoteInputs();
    });

    connectButton_->setDefault(true);
    connectButton_->setMinimumHeight(36);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);
    layout->addWidget(title);
    layout->addWidget(subtitle);
    layout->addSpacing(6);
    layout->addLayout(form);
    layout->addWidget(connectButton_);
    layout->addWidget(separator);
    layout->addWidget(statusCaption);
    layout->addWidget(statusValue_);
    layout->addWidget(detailsValue_);
    layout->addWidget(frameInfoValue_);
    layout->addWidget(inputEnabledCheckBox_);
    layout->addWidget(screenWidget_, 1);

    heartbeatTimer_->setInterval(HEARTBEAT_POLL_MS);
    reconnectTimer_->setSingleShot(true);
    socket_->setProxy(QNetworkProxy::NoProxy);

    connect(connectButton_, &QPushButton::clicked, this, [this]() {
        if (socket_->state() != QAbstractSocket::UnconnectedState) {
            stopConnection();
            return;
        }

        reconnectTimer_->stop();
        reconnectAttempt_ = 0;
        startConnect(false);
    });
    connect(
        inputEnabledCheckBox_,
        &QCheckBox::toggled,
        this,
        [this](bool enabled) {
            if (!enabled) {
                releaseRemoteInputs();
                screenWidget_->clearFocus();
            }
        }
    );
    connect(socket_, &QTcpSocket::connected, this, [this]() {
        handleConnected();
    });
    connect(socket_, &QTcpSocket::disconnected, this, [this]() {
        handleDisconnected();
    });
    connect(socket_, &QTcpSocket::readyRead, this, [this]() {
        readIncomingData();
    });
    connect(socket_, &QTcpSocket::errorOccurred, this, [this]() {
        handleSocketError();
    });
    connect(heartbeatTimer_, &QTimer::timeout, this, [this]() {
        pollHeartbeat();
    });
    connect(reconnectTimer_, &QTimer::timeout, this, [this]() {
        startConnect(true);
    });
    connect(
        decodeWatcher_,
        &QFutureWatcher<QImage>::finished,
        this,
        [this]() {
            handleFrameDecoded();
        }
    );

    setStatus(
        QStringLiteral("未连接"),
        QStringLiteral("请输入被控端地址和端口，然后点击连接。")
    );
    updateControls();
}

void ConnectionWindow::setServerAddress(const QString& host, int port)
{
    if (!host.trimmed().isEmpty()) {
        hostEdit_->setText(host.trimmed());
    }

    if (port >= portSpin_->minimum() && port <= portSpin_->maximum()) {
        portSpin_->setValue(port);
    }
}

void ConnectionWindow::connectToServer()
{
    if (socket_->state() != QAbstractSocket::UnconnectedState) {
        return;
    }

    reconnectTimer_->stop();
    reconnectAttempt_ = 0;
    startConnect(false);
}

void ConnectionWindow::closeEvent(QCloseEvent* event)
{
    manualDisconnect_ = true;
    reconnectTimer_->stop();
    heartbeatTimer_->stop();
    releaseRemoteInputs();
    socket_->abort();
    event->accept();
}

void ConnectionWindow::startConnect(bool reconnectAttempt)
{
    const QString host = hostEdit_->text().trimmed();
    if (host.isEmpty()) {
        setStatus(
            QStringLiteral("连接失败"),
            QStringLiteral("服务地址不能为空。")
        );
        updateControls();
        return;
    }

    QSettings settings;
    settings.setValue("connection/host", host);
    settings.setValue("connection/port", portSpin_->value());

    reconnectTimer_->stop();
    receiveBuffer_.clear();
    lastFailure_.clear();
    resetScreenPipeline(QStringLiteral("正在等待第一帧远程画面……"));
    reconnecting_ = reconnectAttempt;
    connectedSession_ = false;
    disconnectHandled_ = false;
    manualDisconnect_ = false;

    if (reconnectAttempt) {
        setStatus(
            QStringLiteral("正在重新连接"),
            QStringLiteral("第 %1 次尝试连接 %2:%3")
                .arg(reconnectAttempt_)
                .arg(host)
                .arg(portSpin_->value())
        );
    } else {
        setStatus(
            QStringLiteral("正在连接"),
            QStringLiteral("正在连接 %1:%2").arg(host).arg(portSpin_->value())
        );
    }

    socket_->connectToHost(host, static_cast<quint16>(portSpin_->value()));
    updateControls();
}

void ConnectionWindow::stopConnection()
{
    manualDisconnect_ = true;
    reconnectTimer_->stop();
    heartbeatTimer_->stop();
    releaseRemoteInputs();
    socket_->abort();
    handleDisconnected();
}

void ConnectionWindow::handleConnected()
{
    const std::int64_t now = heartbeatNowMs();
    lastReceiveMs_ = now;
    lastPingMs_ = now - HEARTBEAT_IDLE_MS;
    heartbeatSequence_ = 1;
    reconnectAttempt_ = 0;
    reconnecting_ = false;
    connectedSession_ = true;
    disconnectHandled_ = false;
    lastFailure_.clear();

    socket_->setSocketOption(QAbstractSocket::LowDelayOption, 1);

    if (!sendHello()) {
        lastFailure_ = socketErrorText();
        socket_->abort();
        handleDisconnected();
        return;
    }
    heartbeatTimer_->start();
    setStatus(
        QStringLiteral("已连接"),
        QStringLiteral("已连接到 %1:%2")
            .arg(hostEdit_->text().trimmed())
            .arg(portSpin_->value())
    );
    updateControls();
}

void ConnectionWindow::handleDisconnected()
{
    if (disconnectHandled_) {
        return;
    }

    disconnectHandled_ = true;
    heartbeatTimer_->stop();
    receiveBuffer_.clear();
    resetScreenPipeline(QStringLiteral("已断开远程画面。"));

    const bool shouldReconnect = connectedSession_ || reconnecting_;
    connectedSession_ = false;
    pressedMouseButtons_.clear();
    pressedKeys_.clear();

    if (manualDisconnect_) {
        manualDisconnect_ = false;
        reconnecting_ = false;
        reconnectAttempt_ = 0;
        setStatus(
            QStringLiteral("未连接"),
            QStringLiteral("连接已由用户关闭。")
        );
    } else if (shouldReconnect) {
        scheduleReconnect();
    } else {
        reconnecting_ = false;
        setStatus(
            QStringLiteral("连接失败"),
            lastFailure_.isEmpty()
                ? QStringLiteral("无法连接到被控端。")
                : lastFailure_
        );
    }

    updateControls();
}

void ConnectionWindow::handleSocketError()
{
    lastFailure_ = socketErrorText();
    if (socket_->state() == QAbstractSocket::UnconnectedState) {
        handleDisconnected();
    } else {
        setStatus(QStringLiteral("连接异常"), lastFailure_);
    }
}

void ConnectionWindow::readIncomingData()
{
    receiveBuffer_.append(socket_->readAll());
    processReceiveBuffer();
}

void ConnectionWindow::processReceiveBuffer()
{
    while (receiveBuffer_.size() >= PACKET_HEADER_SIZE) {
        std::int32_t magic = 0;
        std::int32_t bodyLength = 0;
        std::memcpy(&magic, receiveBuffer_.constData(), sizeof(magic));
        std::memcpy(
            &bodyLength,
            receiveBuffer_.constData() + sizeof(std::int32_t) * 2,
            sizeof(bodyLength)
        );

        if (magic != PACKET_MAGIC
            || bodyLength < 0
            || bodyLength > PACKET_DATA_SIZE) {
            lastFailure_ = QStringLiteral("被控端发送了无效协议数据。");
            socket_->abort();
            handleDisconnected();
            return;
        }

        const int packetSize = PACKET_HEADER_SIZE + bodyLength;
        if (receiveBuffer_.size() < packetSize) {
            return;
        }

        const Packet packet = decodePacket(receiveBuffer_.constData());
        receiveBuffer_.remove(0, packetSize);
        lastReceiveMs_ = heartbeatNowMs();
        handlePacket(packet);
    }
}

void ConnectionWindow::handlePacket(const Packet& packet)
{
    if (packet.cmd == CMD_SCREEN_BEGIN) {
        beginScreenFrame(packet);
        return;
    }

    if (packet.cmd == CMD_SCREEN_CHUNK) {
        appendScreenChunk(packet);
        return;
    }

    if (packet.cmd == CMD_SCREEN_END) {
        finishScreenFrame(packet);
        return;
    }

    if (packet.cmd == CMD_HEARTBEAT_PING) {
        HeartbeatPayload payload = {};
        if (!readHeartbeatPayload(packet, payload)) {
            lastFailure_ = QStringLiteral("被控端发送了无效心跳数据。");
            socket_->abort();
            handleDisconnected();
            return;
        }

        Packet pong = packet;
        pong.cmd = CMD_HEARTBEAT_PONG;
        if (!sendPacket(pong)) {
            lastFailure_ = socketErrorText();
            socket_->abort();
            handleDisconnected();
        }
        return;
    }

    if (packet.cmd == CMD_HEARTBEAT_PONG) {
        HeartbeatPayload payload = {};
        if (!readHeartbeatPayload(packet, payload)) {
            lastFailure_ = QStringLiteral("被控端发送了无效心跳数据。");
            socket_->abort();
            handleDisconnected();
            return;
        }

        setStatus(
            QStringLiteral("已连接"),
            QStringLiteral("心跳往返延迟：%1 毫秒")
                .arg(heartbeatNowMs() - payload.sent_at_ms)
        );
    }
}

void ConnectionWindow::beginScreenFrame(const Packet& packet)
{
    discardReceivingFrame();

    if (packet.body_len != sizeof(ScreenFrameInfo)) {
        return;
    }

    ScreenFrameInfo info = {};
    std::memcpy(&info, packet.data, sizeof(info));

    const qint64 rawSize =
        static_cast<qint64>(info.width) * info.height * 4;
    if (info.width <= 0
        || info.height <= 0
        || info.total_size <= 0
        || rawSize <= 0
        || rawSize > MAX_SCREEN_FRAME_BYTES
        || info.total_size > MAX_SCREEN_FRAME_BYTES) {
        return;
    }

    if (info.format != SCREEN_FORMAT_JPEG
        && info.format != SCREEN_FORMAT_BGRA32) {
        return;
    }

    if (info.format == SCREEN_FORMAT_BGRA32
        && rawSize != info.total_size) {
        return;
    }

    receivingFrame_.frameId = info.frame_id;
    receivingFrame_.width = info.width;
    receivingFrame_.height = info.height;
    receivingFrame_.format = info.format;
    receivingFrame_.generation = screenGeneration_;
    receivingFrame_.data.resize(info.total_size);
    receivedFrameBytes_ = 0;
}

void ConnectionWindow::appendScreenChunk(const Packet& packet)
{
    if (!receivingFrame_.valid()
        || packet.body_len < sizeof(ScreenChunkHeader)) {
        discardReceivingFrame();
        return;
    }

    ScreenChunkHeader header = {};
    std::memcpy(&header, packet.data, sizeof(header));

    const qint64 chunkEnd =
        static_cast<qint64>(header.offset) + header.data_len;
    if (header.frame_id != receivingFrame_.frameId
        || header.data_len <= 0
        || header.offset != receivedFrameBytes_
        || sizeof(ScreenChunkHeader) + header.data_len != packet.body_len
        || chunkEnd > receivingFrame_.data.size()) {
        discardReceivingFrame();
        return;
    }

    std::memcpy(
        receivingFrame_.data.data() + header.offset,
        packet.data + sizeof(ScreenChunkHeader),
        header.data_len
    );
    receivedFrameBytes_ += header.data_len;
}

void ConnectionWindow::finishScreenFrame(const Packet& packet)
{
    if (packet.body_len != sizeof(std::int32_t)) {
        discardReceivingFrame();
        return;
    }

    std::int32_t frameId = -1;
    std::memcpy(&frameId, packet.data, sizeof(frameId));

    if (!receivingFrame_.valid()
        || frameId != receivingFrame_.frameId
        || receivedFrameBytes_ != receivingFrame_.data.size()) {
        discardReceivingFrame();
        return;
    }

    ScreenFrame completed = std::move(receivingFrame_);
    receivedFrameBytes_ = 0;
    queueFrameForDecode(std::move(completed));
}

void ConnectionWindow::discardReceivingFrame()
{
    receivingFrame_ = ScreenFrame();
    receivedFrameBytes_ = 0;
}

void ConnectionWindow::queueFrameForDecode(ScreenFrame frame)
{
    if (decodeWatcher_->isRunning()) {
        if (pendingDecodeFrame_.valid()) {
            ++droppedDecodeFrames_;
        }
        pendingDecodeFrame_ = std::move(frame);
        return;
    }

    startFrameDecode(std::move(frame));
}

void ConnectionWindow::startFrameDecode(ScreenFrame frame)
{
    decodingFrame_ = std::move(frame);
    const ScreenFrame decodeInput = decodingFrame_;

    decodeWatcher_->setFuture(QtConcurrent::run([decodeInput]() {
        QImage image;
        if (decodeInput.format == SCREEN_FORMAT_JPEG) {
            image = QImage::fromData(decodeInput.data, "JPEG");
        } else if (decodeInput.format == SCREEN_FORMAT_BGRA32) {
            image = QImage(
                reinterpret_cast<const uchar*>(decodeInput.data.constData()),
                decodeInput.width,
                decodeInput.height,
                decodeInput.width * 4,
                QImage::Format_ARGB32
            ).copy();
        }

        if (image.width() != decodeInput.width
            || image.height() != decodeInput.height) {
            return QImage();
        }

        return image;
    }));
}

void ConnectionWindow::handleFrameDecoded()
{
    const QImage image = decodeWatcher_->result();
    if (decodingFrame_.generation == screenGeneration_) {
        if (!image.isNull()) {
            screenWidget_->setFrame(image);
            frameInfoValue_->setText(
                QStringLiteral(
                    "帧 %1 | %2 × %3 | %4 KiB | 丢弃 %5 | "
                    "点击画面开始控制"
                )
                    .arg(decodingFrame_.frameId)
                    .arg(decodingFrame_.width)
                    .arg(decodingFrame_.height)
                    .arg(decodingFrame_.data.size() / 1024)
                    .arg(droppedDecodeFrames_)
            );
        } else {
            frameInfoValue_->setText(
                QStringLiteral("无法解码第 %1 帧。")
                    .arg(decodingFrame_.frameId)
            );
        }
    }

    decodingFrame_ = ScreenFrame();
    if (pendingDecodeFrame_.valid()) {
        ScreenFrame nextFrame = std::move(pendingDecodeFrame_);
        pendingDecodeFrame_ = ScreenFrame();
        if (nextFrame.generation == screenGeneration_) {
            startFrameDecode(std::move(nextFrame));
        }
    }
}

void ConnectionWindow::resetScreenPipeline(const QString& message)
{
    ++screenGeneration_;
    discardReceivingFrame();
    pendingDecodeFrame_ = ScreenFrame();
    droppedDecodeFrames_ = 0;
    frameInfoValue_->setText(QStringLiteral("尚未收到远程画面。"));
    screenWidget_->clearFrame(message);
}

void ConnectionWindow::sendRemoteMouseEvent(
    int action,
    int button,
    int x,
    int y
)
{
    if (socket_->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    MouseEvent event = {};
    event.action = action;
    event.button = button;
    event.x = x;
    event.y = y;

    Packet packet = {};
    packet.magic = PACKET_MAGIC;
    packet.cmd = CMD_MOUSE_EVENT;
    packet.body_len = sizeof(event);
    std::memcpy(packet.data, &event, sizeof(event));

    if (sendPacket(packet)) {
        lastRemoteX_ = x;
        lastRemoteY_ = y;
        if (action == MOUSE_ACTION_DOWN) {
            pressedMouseButtons_.insert(button);
        } else if (action == MOUSE_ACTION_UP) {
            pressedMouseButtons_.remove(button);
        }
    }
}

void ConnectionWindow::sendRemoteKeyEvent(
    int status,
    const QString& key
)
{
    if (socket_->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    const QByteArray keyBytes = key.toLatin1();
    if (keyBytes.isEmpty()
        || keyBytes.size() >= static_cast<int>(sizeof(KeyEvent::key))) {
        return;
    }

    KeyEvent event = {};
    event.key_status = status;
    std::memcpy(event.key, keyBytes.constData(), keyBytes.size());

    Packet packet = {};
    packet.magic = PACKET_MAGIC;
    packet.cmd = CMD_KEY_EVENT;
    packet.body_len = sizeof(event);
    std::memcpy(packet.data, &event, sizeof(event));

    if (sendPacket(packet)) {
        if (status == KEY_STATUS_DOWN) {
            pressedKeys_.insert(key);
        } else if (status == KEY_STATUS_UP) {
            pressedKeys_.remove(key);
        }
    }
}

void ConnectionWindow::releaseRemoteInputs()
{
    if (socket_->state() == QAbstractSocket::ConnectedState) {
        const QSet<int> buttons = pressedMouseButtons_;
        const QSet<QString> keys = pressedKeys_;

        for (int button : buttons) {
            sendRemoteMouseEvent(
                MOUSE_ACTION_UP,
                button,
                lastRemoteX_,
                lastRemoteY_
            );
        }
        for (const QString& key : keys) {
            sendRemoteKeyEvent(KEY_STATUS_UP, key);
        }
    }

    pressedMouseButtons_.clear();
    pressedKeys_.clear();
}

void ConnectionWindow::pollHeartbeat()
{
    if (socket_->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    const std::int64_t now = heartbeatNowMs();
    if (now - lastReceiveMs_ >= HEARTBEAT_TIMEOUT_MS) {
        lastFailure_ = QStringLiteral("连续 10 秒未收到数据，连接超时。");
        socket_->abort();
        handleDisconnected();
        return;
    }

    if (now - lastReceiveMs_ >= HEARTBEAT_IDLE_MS
        && now - lastPingMs_ >= HEARTBEAT_IDLE_MS) {
        if (!sendPacket(buildHeartbeatPacket(
                CMD_HEARTBEAT_PING,
                heartbeatSequence_++
            ))) {
            lastFailure_ = socketErrorText();
            socket_->abort();
            handleDisconnected();
            return;
        }

        lastPingMs_ = now;
    }
}

void ConnectionWindow::scheduleReconnect()
{
    ++reconnectAttempt_;
    reconnecting_ = true;
    const int delaySeconds = reconnectDelaySeconds(reconnectAttempt_);

    setStatus(
        QStringLiteral("等待重新连接"),
        QStringLiteral("第 %1 次重连将在 %2 秒后开始。%3")
            .arg(reconnectAttempt_)
            .arg(delaySeconds)
            .arg(lastFailure_)
            .trimmed()
    );
    reconnectTimer_->start(delaySeconds * 1000);
}

void ConnectionWindow::setStatus(
    const QString& status,
    const QString& details
)
{
    statusValue_->setText(status);
    detailsValue_->setText(details);
}

void ConnectionWindow::updateControls()
{
    const bool socketActive =
        socket_->state() != QAbstractSocket::UnconnectedState;

    hostEdit_->setEnabled(!socketActive);
    portSpin_->setEnabled(!socketActive);
    inputEnabledCheckBox_->setEnabled(
        socket_->state() == QAbstractSocket::ConnectedState
    );

    if (socket_->state() == QAbstractSocket::ConnectedState) {
        connectButton_->setText(QStringLiteral("断开连接"));
    } else if (socketActive) {
        connectButton_->setText(QStringLiteral("取消连接"));
    } else if (reconnectTimer_->isActive()) {
        connectButton_->setText(QStringLiteral("立即重连"));
    } else {
        connectButton_->setText(QStringLiteral("连接"));
    }
}

QString ConnectionWindow::socketErrorText() const
{
    switch (socket_->error()) {
    case QAbstractSocket::ConnectionRefusedError:
        return QStringLiteral("连接被拒绝，请确认被控端已经启动。");
    case QAbstractSocket::RemoteHostClosedError:
        return QStringLiteral("被控端已关闭连接。");
    case QAbstractSocket::HostNotFoundError:
        return QStringLiteral("无法解析服务地址。");
    case QAbstractSocket::SocketAccessError:
        return QStringLiteral("没有访问网络的权限。");
    case QAbstractSocket::SocketResourceError:
        return QStringLiteral("系统网络资源不足。");
    case QAbstractSocket::SocketTimeoutError:
        return QStringLiteral("连接被控端超时。");
    case QAbstractSocket::NetworkError:
        return QStringLiteral("网络连接发生错误。");
    case QAbstractSocket::AddressInUseError:
        return QStringLiteral("本地网络地址已被占用。");
    case QAbstractSocket::SocketAddressNotAvailableError:
        return QStringLiteral("指定的网络地址不可用。");
    case QAbstractSocket::UnsupportedSocketOperationError:
        return QStringLiteral("当前系统不支持此网络操作。");
    case QAbstractSocket::ProxyAuthenticationRequiredError:
        return QStringLiteral("代理服务器需要身份验证。");
    case QAbstractSocket::ProxyConnectionRefusedError:
        return QStringLiteral("代理服务器拒绝连接。");
    case QAbstractSocket::ProxyConnectionClosedError:
        return QStringLiteral("代理服务器已关闭连接。");
    case QAbstractSocket::ProxyConnectionTimeoutError:
        return QStringLiteral("连接代理服务器超时。");
    case QAbstractSocket::ProxyNotFoundError:
        return QStringLiteral("无法找到代理服务器。");
    case QAbstractSocket::ProxyProtocolError:
        return QStringLiteral("代理服务器返回了无效数据。");
    case QAbstractSocket::OperationError:
        return QStringLiteral("网络操作状态无效。");
    default:
        return QStringLiteral("发生未知网络错误。");
    }
}

bool ConnectionWindow::sendPacket(const Packet& packet)
{
    const std::vector<char> buffer = encodePacket(packet);
    if (buffer.empty()) {
        return false;
    }

    return socket_->write(
        buffer.data(),
        static_cast<qint64>(buffer.size())
    ) == static_cast<qint64>(buffer.size());
}

bool ConnectionWindow::sendHello()
{
    return sendPacket(buildTextPacket(
        CMD_HELLO,
        "hello qt client"
    ));
}
