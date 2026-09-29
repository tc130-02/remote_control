#include "main_window.h"

#include "connection_window.h"

#include <QApplication>
#include <QByteArray>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTimer>

int main(int argc, char* argv[])
{
#if defined(Q_OS_LINUX)
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", QByteArray("xcb"));
    }
#endif
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("RemoteControl");
    QCoreApplication::setApplicationName("RemoteControl");

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Windows/Linux 远程控制"));
    parser.addHelpOption();
    parser.addOption(QCommandLineOption(
        "host",
        QStringLiteral("被控端 IP 地址或主机名。"),
        QStringLiteral("主机")
    ));
    parser.addOption(QCommandLineOption(
        "port",
        QStringLiteral("被控端 TCP 端口。"),
        QStringLiteral("端口"),
        "9999"
    ));
    parser.addOption(QCommandLineOption(
        "connect",
        QStringLiteral("窗口打开后立即连接。")
    ));
    parser.process(app);

    MainWindow window;
    bool portValid = false;
    const int port = parser.value("port").toInt(&portValid);
    window.controller()->setServerAddress(
        parser.value("host"),
        portValid ? port : 9999
    );
    window.show();

    if (parser.isSet("connect")) {
        QTimer::singleShot(0, window.controller(), [&window]() {
            window.controller()->connectToServer();
        });
    }

    return app.exec();
}
