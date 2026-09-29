#include "server_window.h"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

ServerWindow::ServerWindow(QWidget* parent)
    : QWidget(parent),
      statusValue_(new QLabel(this)),
      executableValue_(new QLabel(this)),
      logView_(new QPlainTextEdit(this)),
      startStopButton_(new QPushButton(this)),
      clearLogButton_(new QPushButton(QStringLiteral("清空日志"), this)),
      serverProcess_(new QProcess(this))
{
    setWindowTitle(QStringLiteral("远程控制 - 被控端"));
    resize(820, 560);
    setMinimumSize(620, 420);

    auto* title = new QLabel(QStringLiteral("远程控制被控端"), this);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 5);
    titleFont.setBold(true);
    title->setFont(titleFont);

    auto* subtitle = new QLabel(
        QStringLiteral(
            "启动后，本机将监听 TCP 9999 端口，等待 Windows 或 Linux 控制端连接。"
        ),
        this
    );
    subtitle->setWordWrap(true);

    auto* statusCaption = new QLabel(QStringLiteral("运行状态"), this);
    QFont captionFont = statusCaption->font();
    captionFont.setBold(true);
    statusCaption->setFont(captionFont);

    executableValue_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    executableValue_->setWordWrap(true);
    logView_->setReadOnly(true);
    logView_->setPlaceholderText(QStringLiteral("服务端运行日志将在这里显示。"));

    startStopButton_->setMinimumHeight(36);
    clearLogButton_->setMinimumHeight(36);

    auto* buttonLayout = new QHBoxLayout;
    buttonLayout->addWidget(startStopButton_, 1);
    buttonLayout->addWidget(clearLogButton_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);
    layout->addWidget(title);
    layout->addWidget(subtitle);
    layout->addWidget(statusCaption);
    layout->addWidget(statusValue_);
    layout->addWidget(new QLabel(QStringLiteral("服务程序"), this));
    layout->addWidget(executableValue_);
    layout->addLayout(buttonLayout);
    layout->addWidget(logView_, 1);

    serverProcess_->setProcessChannelMode(QProcess::MergedChannels);
#if defined(Q_OS_WIN)
    serverProcess_->setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments* arguments) {
            arguments->flags |= CREATE_NO_WINDOW;
        }
    );
#endif

    connect(startStopButton_, &QPushButton::clicked, this, [this]() {
        if (serverProcess_->state() == QProcess::NotRunning) {
            startServer();
        } else {
            stopServer();
        }
    });
    connect(clearLogButton_, &QPushButton::clicked, logView_, [this]() {
        logView_->clear();
    });
    connect(
        serverProcess_,
        &QProcess::readyReadStandardOutput,
        this,
        [this]() {
            appendServerOutput();
        }
    );
    connect(
        serverProcess_,
        &QProcess::stateChanged,
        this,
        [this]() {
            updateControls();
        }
    );
    connect(
        serverProcess_,
        &QProcess::errorOccurred,
        this,
        [this](QProcess::ProcessError error) {
            appendServerOutput();
            QString reason;
            switch (error) {
            case QProcess::FailedToStart:
                reason = QStringLiteral("无法启动服务程序");
                break;
            case QProcess::Crashed:
                reason = QStringLiteral("服务程序异常退出");
                break;
            case QProcess::Timedout:
                reason = QStringLiteral("等待服务程序响应超时");
                break;
            case QProcess::WriteError:
                reason = QStringLiteral("无法向服务程序写入数据");
                break;
            case QProcess::ReadError:
                reason = QStringLiteral("无法读取服务程序输出");
                break;
            default:
                reason = QStringLiteral("发生未知进程错误");
                break;
            }
            logView_->appendPlainText(
                QStringLiteral("[界面] 服务端启动失败：%1")
                    .arg(reason)
            );
            updateControls();
        }
    );
    connect(
        serverProcess_,
        qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
        this,
        [this](int exitCode, QProcess::ExitStatus exitStatus) {
            appendServerOutput();
            logView_->appendPlainText(
                QStringLiteral("[界面] 服务端已退出，退出码：%1，状态：%2")
                    .arg(exitCode)
                    .arg(
                        exitStatus == QProcess::NormalExit
                            ? QStringLiteral("正常")
                            : QStringLiteral("异常")
                    )
            );
            updateControls();
        }
    );

    executableValue_->setText(serverExecutablePath());
    updateControls();
    QTimer::singleShot(0, this, [this]() {
        startServer();
    });
}

ServerWindow::~ServerWindow()
{
    shutdownServer();
}

void ServerWindow::closeEvent(QCloseEvent* event)
{
    shutdownServer();
    event->accept();
}

void ServerWindow::startServer()
{
    const QString executable = serverExecutablePath();
    if (!QFileInfo::exists(executable)) {
        QMessageBox::critical(
            this,
            QStringLiteral("无法启动服务端"),
            QStringLiteral("没有找到服务程序：\n%1").arg(executable)
        );
        return;
    }

    logView_->appendPlainText(QStringLiteral("[界面] 正在启动服务端……"));
    serverProcess_->setProgram(executable);
    serverProcess_->setWorkingDirectory(QFileInfo(executable).absolutePath());
    serverProcess_->start();
    updateControls();
}

void ServerWindow::stopServer()
{
    if (serverProcess_->state() == QProcess::NotRunning) {
        return;
    }

    logView_->appendPlainText(QStringLiteral("[界面] 正在停止服务端……"));
    serverProcess_->terminate();
    updateControls();
}

void ServerWindow::appendServerOutput()
{
    const QByteArray output = serverProcess_->readAllStandardOutput();
    if (output.isEmpty()) {
        return;
    }

    const QString text = QString::fromLocal8Bit(output).trimmed();
    if (!text.isEmpty()) {
        logView_->appendPlainText(text);
    }
}

void ServerWindow::updateControls()
{
    switch (serverProcess_->state()) {
    case QProcess::Starting:
        statusValue_->setText(QStringLiteral("正在启动"));
        startStopButton_->setText(QStringLiteral("正在启动……"));
        startStopButton_->setEnabled(false);
        break;
    case QProcess::Running:
        statusValue_->setText(QStringLiteral("运行中，等待控制端连接"));
        startStopButton_->setText(QStringLiteral("停止服务端"));
        startStopButton_->setEnabled(true);
        break;
    case QProcess::NotRunning:
        statusValue_->setText(QStringLiteral("未运行"));
        startStopButton_->setText(QStringLiteral("启动服务端"));
        startStopButton_->setEnabled(true);
        break;
    }
}

void ServerWindow::shutdownServer()
{
    if (serverProcess_->state() == QProcess::NotRunning) {
        return;
    }

    serverProcess_->terminate();
    if (!serverProcess_->waitForFinished(1500)) {
        serverProcess_->kill();
        serverProcess_->waitForFinished(1000);
    }
}

QString ServerWindow::serverExecutablePath() const
{
    const QDir applicationDir(QCoreApplication::applicationDirPath());
#if defined(Q_OS_WIN)
    return applicationDir.filePath(QStringLiteral("win_server.exe"));
#else
    return applicationDir.filePath(QStringLiteral("linux_server"));
#endif
}
