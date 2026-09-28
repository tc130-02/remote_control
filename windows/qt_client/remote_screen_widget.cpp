#include "remote_screen_widget.h"

#include <QPainter>
#include <QPaintEvent>
#include <QSizePolicy>

RemoteScreenWidget::RemoteScreenWidget(QWidget* parent)
    : QWidget(parent),
      message_("Connect to a server to view its screen.")
{
    setMinimumSize(480, 270);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
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
    update();
}

QSize RemoteScreenWidget::sizeHint() const
{
    return QSize(960, 540);
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

    QSize displaySize = frame_.size();
    displaySize.scale(size(), Qt::KeepAspectRatio);

    QRect displayRect(QPoint(0, 0), displaySize);
    displayRect.moveCenter(rect().center());

    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(displayRect, frame_);
}
