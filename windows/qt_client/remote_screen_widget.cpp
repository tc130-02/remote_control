#include "remote_screen_widget.h"

#include "packet.h"

#include <QEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QSizePolicy>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <utility>

RemoteScreenWidget::RemoteScreenWidget(QWidget* parent)
    : QWidget(parent),
      message_("Connect to a server to view its screen."),
      hasLastRemotePoint_(false)
{
    setMinimumSize(480, 270);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    mouseMoveTimer_.start();
}

void RemoteScreenWidget::setFrame(const QImage& frame)
{
    frame_ = frame;
    message_.clear();
    update();
}

void RemoteScreenWidget::clearFrame(const QString& message)
{
    frame_ = QImage();
    message_ = message;
    hasLastRemotePoint_ = false;
    update();
}

void RemoteScreenWidget::setMouseEventHandler(MouseEventHandler handler)
{
    mouseEventHandler_ = std::move(handler);
}

void RemoteScreenWidget::setKeyEventHandler(KeyEventHandler handler)
{
    keyEventHandler_ = std::move(handler);
}

void RemoteScreenWidget::setReleaseInputHandler(ReleaseInputHandler handler)
{
    releaseInputHandler_ = std::move(handler);
}

QSize RemoteScreenWidget::sizeHint() const
{
    return QSize(960, 540);
}

bool RemoteScreenWidget::event(QEvent* event)
{
    if (event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Tab
            || keyEvent->key() == Qt::Key_Backtab) {
            keyPressEvent(keyEvent);
            return true;
        }
    }

    if (event->type() == QEvent::KeyRelease) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Tab
            || keyEvent->key() == Qt::Key_Backtab) {
            keyReleaseEvent(keyEvent);
            return true;
        }
    }

    return QWidget::event(event);
}

void RemoteScreenWidget::focusOutEvent(QFocusEvent* event)
{
    if (releaseInputHandler_) {
        releaseInputHandler_();
    }
    QWidget::focusOutEvent(event);
}

void RemoteScreenWidget::keyPressEvent(QKeyEvent* event)
{
    sendKeyEvent(event, KEY_STATUS_DOWN);
}

void RemoteScreenWidget::keyReleaseEvent(QKeyEvent* event)
{
    sendKeyEvent(event, KEY_STATUS_UP);
}

void RemoteScreenWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    setFocus(Qt::MouseFocusReason);
    sendMouseButtonEvent(event, MOUSE_ACTION_DOWN);
}

void RemoteScreenWidget::mouseMoveEvent(QMouseEvent* event)
{
    int remoteX = 0;
    int remoteY = 0;
    if (!mouseEventHandler_
        || !mapToRemote(event->position(), remoteX, remoteY)) {
        return;
    }

    if (mouseMoveTimer_.elapsed() < 20
        || (hasLastRemotePoint_
            && lastRemotePoint_ == QPoint(remoteX, remoteY))) {
        return;
    }

    mouseEventHandler_(MOUSE_ACTION_MOVE, 0, remoteX, remoteY);
    lastRemotePoint_ = QPoint(remoteX, remoteY);
    hasLastRemotePoint_ = true;
    mouseMoveTimer_.restart();
}

void RemoteScreenWidget::mousePressEvent(QMouseEvent* event)
{
    setFocus(Qt::MouseFocusReason);
    sendMouseButtonEvent(event, MOUSE_ACTION_DOWN);
}

void RemoteScreenWidget::mouseReleaseEvent(QMouseEvent* event)
{
    sendMouseButtonEvent(event, MOUSE_ACTION_UP);
}

void RemoteScreenWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    if (frame_.isNull()) {
        painter.setPen(QColor(180, 180, 180));
        painter.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, message_);
        return;
    }

    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(frameDisplayRect(), frame_);
}

void RemoteScreenWidget::wheelEvent(QWheelEvent* event)
{
    int remoteX = 0;
    int remoteY = 0;
    if (!mouseEventHandler_
        || !mapToRemote(event->position(), remoteX, remoteY)) {
        return;
    }

    const int delta = event->angleDelta().y();
    if (delta == 0) {
        return;
    }

    const int button = delta > 0 ? 4 : 5;
    const int steps = std::max(1, std::abs(delta) / 120);
    for (int step = 0; step < steps; ++step) {
        mouseEventHandler_(MOUSE_ACTION_CLICK, button, remoteX, remoteY);
    }
    lastRemotePoint_ = QPoint(remoteX, remoteY);
    hasLastRemotePoint_ = true;
    event->accept();
}

QRect RemoteScreenWidget::frameDisplayRect() const
{
    if (frame_.isNull()) {
        return QRect();
    }

    QSize displaySize = frame_.size();
    displaySize.scale(size(), Qt::KeepAspectRatio);

    QRect displayRect(QPoint(0, 0), displaySize);
    displayRect.moveCenter(rect().center());
    return displayRect;
}

bool RemoteScreenWidget::mapToRemote(
    const QPointF& position,
    int& remoteX,
    int& remoteY
)
{
    const QRect displayRect = frameDisplayRect();
    if (displayRect.isEmpty() || !displayRect.contains(position.toPoint())) {
        return false;
    }

    remoteX = static_cast<int>(
        (position.x() - displayRect.x()) * frame_.width()
        / displayRect.width()
    );
    remoteY = static_cast<int>(
        (position.y() - displayRect.y()) * frame_.height()
        / displayRect.height()
    );
    remoteX = std::clamp(remoteX, 0, frame_.width() - 1);
    remoteY = std::clamp(remoteY, 0, frame_.height() - 1);
    return true;
}

int RemoteScreenWidget::protocolMouseButton(Qt::MouseButton button) const
{
    if (button == Qt::LeftButton) {
        return 1;
    }
    if (button == Qt::MiddleButton) {
        return 2;
    }
    if (button == Qt::RightButton) {
        return 3;
    }
    return 0;
}

QString RemoteScreenWidget::protocolKeyName(int key) const
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return QString(QChar('A' + key - Qt::Key_A));
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return QString(QChar('0' + key - Qt::Key_0));
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12) {
        return QString("F%1").arg(key - Qt::Key_F1 + 1);
    }

    switch (key) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return "Return";
    case Qt::Key_Backspace:
        return "BackSpace";
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
        return "Tab";
    case Qt::Key_Escape:
        return "Escape";
    case Qt::Key_Space:
        return "space";
    case Qt::Key_Left:
        return "Left";
    case Qt::Key_Right:
        return "Right";
    case Qt::Key_Up:
        return "Up";
    case Qt::Key_Down:
        return "Down";
    case Qt::Key_Shift:
        return "Shift_L";
    case Qt::Key_Control:
        return "Control_L";
    case Qt::Key_Alt:
        return "Alt_L";
    case Qt::Key_Delete:
        return "Delete";
    case Qt::Key_Insert:
        return "Insert";
    case Qt::Key_Home:
        return "Home";
    case Qt::Key_End:
        return "End";
    case Qt::Key_PageUp:
        return "Page_Up";
    case Qt::Key_PageDown:
        return "Page_Down";
    case Qt::Key_CapsLock:
        return "Caps_Lock";
    case Qt::Key_Minus:
        return "minus";
    case Qt::Key_Equal:
        return "equal";
    case Qt::Key_BracketLeft:
        return "bracketleft";
    case Qt::Key_BracketRight:
        return "bracketright";
    case Qt::Key_Semicolon:
        return "semicolon";
    case Qt::Key_Apostrophe:
        return "apostrophe";
    case Qt::Key_Comma:
        return "comma";
    case Qt::Key_Period:
        return "period";
    case Qt::Key_Slash:
        return "slash";
    case Qt::Key_Backslash:
        return "backslash";
    case Qt::Key_QuoteLeft:
        return "grave";
    default:
        return QString();
    }
}

void RemoteScreenWidget::sendKeyEvent(QKeyEvent* event, int status)
{
    if (event->isAutoRepeat()) {
        event->accept();
        return;
    }

    const QString keyName = protocolKeyName(event->key());
    if (keyName.isEmpty() || !keyEventHandler_) {
        event->ignore();
        return;
    }

    keyEventHandler_(status, keyName);
    event->accept();
}

void RemoteScreenWidget::sendMouseButtonEvent(
    QMouseEvent* event,
    int action
)
{
    const int button = protocolMouseButton(event->button());
    if (button == 0 || !mouseEventHandler_) {
        return;
    }

    int remoteX = 0;
    int remoteY = 0;
    if (!mapToRemote(event->position(), remoteX, remoteY)) {
        if (action != MOUSE_ACTION_UP || !hasLastRemotePoint_) {
            return;
        }
        remoteX = lastRemotePoint_.x();
        remoteY = lastRemotePoint_.y();
    } else {
        lastRemotePoint_ = QPoint(remoteX, remoteY);
        hasLastRemotePoint_ = true;
    }

    mouseEventHandler_(action, button, remoteX, remoteY);
    event->accept();
}
