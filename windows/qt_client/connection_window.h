#pragma once

#include <QByteArray>
#include <QSet>
#include <QWidget>

#include <cstdint>

class QLabel;
class QLineEdit;
class QCheckBox;
class QPushButton;
class QSpinBox;
class QString;
class QTcpSocket;
class QTimer;
class QCloseEvent;
class QImage;
class RemoteScreenWidget;

template <typename T>
class QFutureWatcher;

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
    struct ScreenFrame {
        int frameId = -1;
        int width = 0;
        int height = 0;
        int format = 0;
        int generation = 0;
        QByteArray data;

        bool valid() const
        {
            return frameId >= 0 && width > 0 && height > 0 && !data.isEmpty();
        }
    };

    void startConnect(bool reconnectAttempt);
    void stopConnection();
    void handleConnected();
    void handleDisconnected();
    void handleSocketError();
    void readIncomingData();
    void processReceiveBuffer();
    void handlePacket(const Packet& packet);
    void beginScreenFrame(const Packet& packet);
    void appendScreenChunk(const Packet& packet);
    void finishScreenFrame(const Packet& packet);
    void discardReceivingFrame();
    void queueFrameForDecode(ScreenFrame frame);
    void startFrameDecode(ScreenFrame frame);
    void handleFrameDecoded();
    void resetScreenPipeline(const QString& message);
    void sendRemoteMouseEvent(int action, int button, int x, int y);
    void sendRemoteKeyEvent(int status, const QString& key);
    void releaseRemoteInputs();
    void pollHeartbeat();
    void scheduleReconnect();
    void setStatus(const QString& status, const QString& details);
    void updateControls();
    bool sendPacket(const Packet& packet);
    bool sendHello();

    QLineEdit* hostEdit_;
    QSpinBox* portSpin_;
    QPushButton* connectButton_;
    QCheckBox* inputEnabledCheckBox_;
    QLabel* statusValue_;
    QLabel* detailsValue_;
    QLabel* frameInfoValue_;
    RemoteScreenWidget* screenWidget_;
    QTcpSocket* socket_;
    QTimer* heartbeatTimer_;
    QTimer* reconnectTimer_;
    QFutureWatcher<QImage>* decodeWatcher_;
    QByteArray receiveBuffer_;
    ScreenFrame receivingFrame_;
    ScreenFrame decodingFrame_;
    ScreenFrame pendingDecodeFrame_;
    QString lastFailure_;
    std::int64_t lastReceiveMs_;
    std::int64_t lastPingMs_;
    std::int64_t heartbeatSequence_;
    int receivedFrameBytes_;
    int screenGeneration_;
    int droppedDecodeFrames_;
    int lastRemoteX_;
    int lastRemoteY_;
    QSet<int> pressedMouseButtons_;
    QSet<QString> pressedKeys_;
    int reconnectAttempt_;
    bool reconnecting_;
    bool connectedSession_;
    bool disconnectHandled_;
    bool manualDisconnect_;
};
