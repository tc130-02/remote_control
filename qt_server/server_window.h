#pragma once

#include <QWidget>

class QCloseEvent;
class QLabel;
class QPlainTextEdit;
class QProcess;
class QPushButton;
class QString;

class ServerWindow : public QWidget
{
public:
    explicit ServerWindow(QWidget* parent = nullptr);
    ~ServerWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void startServer();
    void stopServer();
    void appendServerOutput();
    void updateControls();
    void shutdownServer();
    QString serverExecutablePath() const;

    QLabel* statusValue_;
    QLabel* executableValue_;
    QPlainTextEdit* logView_;
    QPushButton* startStopButton_;
    QPushButton* clearLogButton_;
    QProcess* serverProcess_;
};
