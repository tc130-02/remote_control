#pragma once

#include <QByteArray>
#include <QWidget>

#include <cstdint>

class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QString;
class QTcpSocket;
class QTimer;
class QCloseEvent;

struct Packet;

class ConnectionWindow : public QWidget
{
public:
    explicit ConnectionWindow(QWidget* parent = nullptr);
    void setServerAddress(const QString& host, int port);
    void connectToServer();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void startConnect(bool reconnectAttempt);
    void stopConnection();
    void handleConnected();
    void handleDisconnected();
    void handleSocketError();
    void readIncomingData();
    void processReceiveBuffer();
    void handlePacket(const Packet& packet);
    void pollHeartbeat();
    void scheduleReconnect();
    void setStatus(const QString& status, const QString& details);
    void updateControls();
    bool sendPacket(const Packet& packet);
    bool sendHello();

    QLineEdit* hostEdit_;
    QSpinBox* portSpin_;
    QPushButton* connectButton_;
    QLabel* statusValue_;
    QLabel* detailsValue_;
    QTcpSocket* socket_;
    QTimer* heartbeatTimer_;
    QTimer* reconnectTimer_;
    QByteArray receiveBuffer_;
    QString lastFailure_;
    std::int64_t lastReceiveMs_;
    std::int64_t lastPingMs_;
    std::int64_t heartbeatSequence_;
    int reconnectAttempt_;
    bool reconnecting_;
    bool connectedSession_;
    bool disconnectHandled_;
    bool manualDisconnect_;
};
