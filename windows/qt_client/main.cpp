#include "connection_window.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTimer>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("RemoteControl");
    QCoreApplication::setApplicationName("QtClient");

    QCommandLineParser parser;
    parser.setApplicationDescription("Qt remote-control connection client");
    parser.addHelpOption();
    parser.addOption(QCommandLineOption(
        "host",
        "Server IP address or hostname.",
        "host"
    ));
    parser.addOption(QCommandLineOption(
        "port",
        "Server TCP port.",
        "port",
        "9999"
    ));
    parser.addOption(QCommandLineOption(
        "connect",
        "Connect immediately after the window opens."
    ));
    parser.process(app);

    ConnectionWindow window;
    bool portValid = false;
    const int port = parser.value("port").toInt(&portValid);
    window.setServerAddress(
        parser.value("host"),
        portValid ? port : 9999
    );
    window.show();

    if (parser.isSet("connect")) {
        QTimer::singleShot(0, &window, [&window]() {
            window.connectToServer();
        });
    }

    return app.exec();
}
