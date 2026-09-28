#pragma once

#include <QImage>
#include <QString>
#include <QWidget>

class QPaintEvent;

class RemoteScreenWidget : public QWidget
{
public:
    explicit RemoteScreenWidget(QWidget* parent = nullptr);

    void setFrame(const QImage& frame);
    void clearFrame(const QString& message);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QImage frame_;
    QString message_;
};
