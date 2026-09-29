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
    setWindowTitle("Remote Control - Qt Client");
    resize(1100, 780);
    setMinimumSize(700, 560);

    auto* title = new QLabel("Remote Control", this);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 5);
    titleFont.setBold(true);
    title->setFont(titleFont);

    auto* subtitle = new QLabel(
        "Connect to a Windows or Linux remote-control server.",
        this
    );
    subtitle->setWordWrap(true);

    QSettings settings;
    hostEdit_->setText(settings.value("connection/host", "127.0.0.1").toString());
    hostEdit_->setPlaceholderText("IP address or hostname");

    portSpin_->setRange(1, 65535);
    portSpin_->setValue(settings.value("connection/port", 9999).toInt());

    auto* form = new QFormLayout;
    form->addRow("Server address", hostEdit_);
    form->addRow("Port", portSpin_);

    auto* separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);

    auto* statusCaption = new QLabel("Status", this);
    QFont statusFont = statusCaption->font();
    statusFont.setBold(true);
    statusCaption->setFont(statusFont);

    detailsValue_->setWordWrap(true);
    detailsValue_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    frameInfoValue_->setText("No remote frame received.");
    inputEnabledCheckBox_->setText("Enable remote keyboard and mouse input");
    inputEnabledCheckBox_->setChecked(true);
    inputEnabledCheckBox_->setToolTip(
        "Turn this off to view the remote screen without controlling it."
    );
    screenWidget_->setToolTip(
        "Click the remote screen to capture keyboard input."
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

    setStatus("Disconnected", "Enter a server address, then select Connect.");
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
        setStatus("Connection failed", "The server address cannot be empty.");
        updateControls();
        return;
    }

    QSettings settings;
    settings.setValue("connection/host", host);
    settings.setValue("connection/port", portSpin_->value());

    reconnectTimer_->stop();
    receiveBuffer_.clear();
    lastFailure_.clear();
    resetScreenPipeline("Waiting for the first remote frame...");
    reconnecting_ = reconnectAttempt;
    connectedSession_ = false;
    disconnectHandled_ = false;
    manualDisconnect_ = false;

    if (reconnectAttempt) {
        setStatus(
            "Reconnecting",
            QString("Attempt %1 to %2:%3")
                .arg(reconnectAttempt_)
                .arg(host)
                .arg(portSpin_->value())
        );
    } else {
        setStatus(
            "Connecting",
            QString("Opening %1:%2").arg(host).arg(portSpin_->value())
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
        lastFailure_ = socket_->errorString();
        socket_->abort();
        handleDisconnected();
        return;
    }
    heartbeatTimer_->start();
    setStatus(
        "Connected",
        QString("Connected to %1:%2")
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
    resetScreenPipeline("Disconnected from the remote screen.");

    const bool shouldReconnect = connectedSession_ || reconnecting_;
    connectedSession_ = false;
    pressedMouseButtons_.clear();
    pressedKeys_.clear();

    if (manualDisconnect_) {
        manualDisconnect_ = false;
        reconnecting_ = false;
        reconnectAttempt_ = 0;
        setStatus("Disconnected", "Connection closed by the user.");
    } else if (shouldReconnect) {
        scheduleReconnect();
    } else {
        reconnecting_ = false;
        setStatus(
            "Connection failed",
            lastFailure_.isEmpty() ? "The server could not be reached." : lastFailure_
        );
    }

    updateControls();
}

void ConnectionWindow::handleSocketError()
{
    lastFailure_ = socket_->errorString();
    if (socket_->state() == QAbstractSocket::UnconnectedState) {
        handleDisconnected();
    } else {
        setStatus("Connection problem", lastFailure_);
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
            lastFailure_ = "The server sent an invalid protocol packet.";
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
            lastFailure_ = "The server sent an invalid heartbeat.";
            socket_->abort();
            handleDisconnected();
            return;
        }

        Packet pong = packet;
        pong.cmd = CMD_HEARTBEAT_PONG;
        if (!sendPacket(pong)) {
            lastFailure_ = socket_->errorString();
            socket_->abort();
            handleDisconnected();
        }
        return;
    }

    if (packet.cmd == CMD_HEARTBEAT_PONG) {
        HeartbeatPayload payload = {};
        if (!readHeartbeatPayload(packet, payload)) {
            lastFailure_ = "The server sent an invalid heartbeat.";
            socket_->abort();
            handleDisconnected();
            return;
        }

        setStatus(
            "Connected",
            QString("Heartbeat round trip: %1 ms")
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
                QString(
                    "Frame %1 | %2 x %3 | %4 KiB | dropped %5 | "
                    "click screen to control"
                )
                    .arg(decodingFrame_.frameId)
                    .arg(decodingFrame_.width)
                    .arg(decodingFrame_.height)
                    .arg(decodingFrame_.data.size() / 1024)
                    .arg(droppedDecodeFrames_)
            );
        } else {
            frameInfoValue_->setText(
                QString("Frame %1 could not be decoded.")
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
    frameInfoValue_->setText("No remote frame received.");
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
        lastFailure_ = "Heartbeat timed out after 10 seconds.";
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
            lastFailure_ = socket_->errorString();
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
        "Waiting to reconnect",
        QString("Attempt %1 starts in %2 second(s). %3")
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
        connectButton_->setText("Disconnect");
    } else if (socketActive) {
        connectButton_->setText("Cancel");
    } else if (reconnectTimer_->isActive()) {
        connectButton_->setText("Connect now");
    } else {
        connectButton_->setText("Connect");
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
        "hello qt windows client"
    ));
}
