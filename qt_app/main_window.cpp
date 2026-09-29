#include "main_window.h"

#include "connection_window.h"
#include "server_window.h"

#include <QTabWidget>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget* parent)
    : QWidget(parent),
      tabs_(new QTabWidget(this)),
      controller_(new ConnectionWindow(this)),
      server_(new ServerWindow(this))
{
    setWindowTitle(QStringLiteral("远程控制"));
    resize(1120, 820);
    setMinimumSize(760, 620);

    tabs_->addTab(controller_, QStringLiteral("控制其他设备"));
    tabs_->addTab(server_, QStringLiteral("允许远程控制"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(tabs_);
}

ConnectionWindow* MainWindow::controller() const
{
    return controller_;
}
