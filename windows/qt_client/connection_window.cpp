#include "connection_window.h"

#include "heartbeat.h"
#include "packet.h"
#include "reconnect.h"

#include <QAbstractSocket>
#include <QCloseEvent>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTcpSocket>
#include <QTimer>
#include <QVBoxLayout>

#include <cstring>
#include <vector>

namespace {

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
      statusValue_(new QLabel(this)),
      detailsValue_(new QLabel(this)),
      socket_(new QTcpSocket(this)),
      heartbeatTimer_(new QTimer(this)),
      reconnectTimer_(new QTimer(this)),
      lastReceiveMs_(0),
      lastPingMs_(0),
      heartbeatSequence_(1),
      reconnectAttempt_(0),
      reconnecting_(false),
      connectedSession_(false),
      disconnectHandled_(true),
      manualDisconnect_(false)
{
    setWindowTitle("Remote Control - Qt Client");
    setMinimumWidth(460);

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

    const bool shouldReconnect = connectedSession_ || reconnecting_;
    connectedSession_ = false;

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
