#pragma once

#include <QImage>
#include <QElapsedTimer>
#include <QPoint>
#include <QString>
#include <QWidget>

#include <functional>

class QEvent;
class QFocusEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QWheelEvent;

class RemoteScreenWidget : public QWidget
{
public:
    using MouseEventHandler = std::function<void(int, int, int, int)>;
    using KeyEventHandler = std::function<void(int, const QString&)>;
    using ReleaseInputHandler = std::function<void()>;

    explicit RemoteScreenWidget(QWidget* parent = nullptr);

    void setFrame(const QImage& frame);
    void clearFrame(const QString& message);
    void setMouseEventHandler(MouseEventHandler handler);
    void setKeyEventHandler(KeyEventHandler handler);
    void setReleaseInputHandler(ReleaseInputHandler handler);
    QSize sizeHint() const override;

protected:
    bool event(QEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    QRect frameDisplayRect() const;
    bool mapToRemote(const QPointF& position, int& remoteX, int& remoteY);
    int protocolMouseButton(Qt::MouseButton button) const;
    QString protocolKeyName(int key) const;
    void sendKeyEvent(QKeyEvent* event, int status);
    void sendMouseButtonEvent(QMouseEvent* event, int action);

    QImage frame_;
    QString message_;
    MouseEventHandler mouseEventHandler_;
    KeyEventHandler keyEventHandler_;
    ReleaseInputHandler releaseInputHandler_;
    QElapsedTimer mouseMoveTimer_;
    QPoint lastRemotePoint_;
    bool hasLastRemotePoint_;
};
