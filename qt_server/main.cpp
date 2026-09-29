#include "server_window.h"

#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>

int main(int argc, char* argv[])
{
#if defined(Q_OS_LINUX)
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", QByteArray("xcb"));
    }
#endif
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("RemoteControl");
    QCoreApplication::setApplicationName("QtServer");

    ServerWindow window;
    window.show();
    return app.exec();
}
