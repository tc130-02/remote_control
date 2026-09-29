#pragma once

#include <QWidget>

class ConnectionWindow;
class QTabWidget;
class ServerWindow;

class MainWindow : public QWidget
{
public:
    explicit MainWindow(QWidget* parent = nullptr);

    ConnectionWindow* controller() const;

private:
    QTabWidget* tabs_;
    ConnectionWindow* controller_;
    ServerWindow* server_;
};
