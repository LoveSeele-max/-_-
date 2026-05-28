#include "mainwindow.h"

#include "filetransferclient.h"
#include "protocol.h"

#include <QApplication>
#include <QAbstractSocket>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMetaObject>
#include <QNetworkInterface>
#include <QProgressBar>
#include <QPushButton>
#include <QTextEdit>
#include <QThread>
#include <QVBoxLayout>
#include <QAbstractItemView>

namespace {

QString readableBytes(qint64 bytes)
{
    double value = double(bytes);
    QString unit = QStringLiteral("B");
    if (value >= 1024.0) {
        value /= 1024.0;
        unit = QStringLiteral("KB");
    }
    if (value >= 1024.0) {
        value /= 1024.0;
        unit = QStringLiteral("MB");
    }
    if (value >= 1024.0) {
        value /= 1024.0;
        unit = QStringLiteral("GB");
    }

    return QStringLiteral("%1 %2").arg(value, 0, 'f', value >= 10.0 ? 1 : 2).arg(unit);
}

bool readPort(const QLineEdit *edit, int *port)
{
    bool ok = false;
    const int value = edit->text().trimmed().toInt(&ok);
    if (!ok || value < 1 || value > 65535) {
        return false;
    }
    *port = value;
    return true;
}

}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_server(new FileTransferServer(this)),
      m_transferThread(0),
      m_portEdit(0),
      m_saveDirEdit(0),
      m_serverStateLabel(0),
      m_localAddressLabel(0),
      m_startServerButton(0),
      m_stopServerButton(0),
      m_hostEdit(0),
      m_remotePortEdit(0),
      m_pathList(0),
      m_sendButton(0),
      m_currentFileLabel(0),
      m_currentProgress(0),
      m_totalProgress(0),
      m_logEdit(0)
{
    buildUi();

    connect(m_server, SIGNAL(logMessage(QString)), this, SLOT(appendLog(QString)));
    connect(m_server, SIGNAL(serverStateChanged(bool,QString)), this, SLOT(updateServerState(bool,QString)));
    connect(m_server, SIGNAL(currentProgress(QString,qint64,qint64)),
            this, SLOT(updateCurrentProgress(QString,qint64,qint64)));
    updateServerState(false, QStringLiteral("接收端未启动"));
    updateButtons();
}

MainWindow::~MainWindow()
{
    if (m_transferThread) {
        m_transferThread->quit();
        m_transferThread->wait(3000);
    }
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("Qt 网络文件传输工具 - Liyiguang"));
    resize(980, 680);

    QWidget *central = new QWidget(this);
    QVBoxLayout *rootLayout = new QVBoxLayout(central);

    QHBoxLayout *topLayout = new QHBoxLayout;
    rootLayout->addLayout(topLayout);

    QGroupBox *receiveGroup = new QGroupBox(QStringLiteral("接收端"), central);
    QVBoxLayout *receiveLayout = new QVBoxLayout(receiveGroup);
    QFormLayout *receiveForm = new QFormLayout;

    m_portEdit = new QLineEdit(QStringLiteral("45454"), receiveGroup);
    m_saveDirEdit = new QLineEdit(QDir::home().absoluteFilePath(QStringLiteral("NetTransferReceived")), receiveGroup);
    QPushButton *chooseSaveButton = new QPushButton(QStringLiteral("选择"), receiveGroup);
    QHBoxLayout *saveLayout = new QHBoxLayout;
    saveLayout->addWidget(m_saveDirEdit);
    saveLayout->addWidget(chooseSaveButton);

    m_localAddressLabel = new QLabel(localAddressText(), receiveGroup);
    m_serverStateLabel = new QLabel(receiveGroup);
    receiveForm->addRow(QStringLiteral("监听端口"), m_portEdit);
    receiveForm->addRow(QStringLiteral("保存目录"), saveLayout);
    receiveForm->addRow(QStringLiteral("本机地址"), m_localAddressLabel);
    receiveForm->addRow(QStringLiteral("状态"), m_serverStateLabel);
    receiveLayout->addLayout(receiveForm);

    QHBoxLayout *serverButtonLayout = new QHBoxLayout;
    m_startServerButton = new QPushButton(QStringLiteral("启动接收"), receiveGroup);
    m_stopServerButton = new QPushButton(QStringLiteral("停止"), receiveGroup);
    serverButtonLayout->addWidget(m_startServerButton);
    serverButtonLayout->addWidget(m_stopServerButton);
    receiveLayout->addLayout(serverButtonLayout);
    topLayout->addWidget(receiveGroup, 1);

    QGroupBox *sendGroup = new QGroupBox(QStringLiteral("发送端"), central);
    QVBoxLayout *sendLayout = new QVBoxLayout(sendGroup);
    QFormLayout *sendForm = new QFormLayout;
    m_hostEdit = new QLineEdit(QStringLiteral("127.0.0.1"), sendGroup);
    m_remotePortEdit = new QLineEdit(QStringLiteral("45454"), sendGroup);
    sendForm->addRow(QStringLiteral("目标 IP"), m_hostEdit);
    sendForm->addRow(QStringLiteral("目标端口"), m_remotePortEdit);
    sendLayout->addLayout(sendForm);

    m_pathList = new QListWidget(sendGroup);
    m_pathList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    sendLayout->addWidget(m_pathList, 1);

    QHBoxLayout *pathButtons = new QHBoxLayout;
    QPushButton *addFileButton = new QPushButton(QStringLiteral("添加文件"), sendGroup);
    QPushButton *addDirButton = new QPushButton(QStringLiteral("添加目录"), sendGroup);
    QPushButton *removeButton = new QPushButton(QStringLiteral("移除"), sendGroup);
    QPushButton *clearButton = new QPushButton(QStringLiteral("清空"), sendGroup);
    m_sendButton = new QPushButton(QStringLiteral("发送"), sendGroup);
    pathButtons->addWidget(addFileButton);
    pathButtons->addWidget(addDirButton);
    pathButtons->addWidget(removeButton);
    pathButtons->addWidget(clearButton);
    pathButtons->addStretch();
    pathButtons->addWidget(m_sendButton);
    sendLayout->addLayout(pathButtons);
    topLayout->addWidget(sendGroup, 2);

    QGroupBox *progressGroup = new QGroupBox(QStringLiteral("传输进度"), central);
    QVBoxLayout *progressLayout = new QVBoxLayout(progressGroup);
    m_currentFileLabel = new QLabel(QStringLiteral("当前文件"), progressGroup);
    m_currentProgress = new QProgressBar(progressGroup);
    m_totalProgress = new QProgressBar(progressGroup);
    m_currentProgress->setRange(0, 100);
    m_totalProgress->setRange(0, 100);
    m_currentProgress->setValue(0);
    m_totalProgress->setValue(0);
    progressLayout->addWidget(m_currentFileLabel);
    progressLayout->addWidget(m_currentProgress);
    progressLayout->addWidget(new QLabel(QStringLiteral("总进度"), progressGroup));
    progressLayout->addWidget(m_totalProgress);
    rootLayout->addWidget(progressGroup);

    QGroupBox *logGroup = new QGroupBox(QStringLiteral("运行日志"), central);
    QVBoxLayout *logLayout = new QVBoxLayout(logGroup);
    m_logEdit = new QTextEdit(logGroup);
    m_logEdit->setReadOnly(true);
    logLayout->addWidget(m_logEdit);
    rootLayout->addWidget(logGroup, 1);

    setCentralWidget(central);

    connect(chooseSaveButton, SIGNAL(clicked()), this, SLOT(chooseSaveDirectory()));
    connect(m_startServerButton, SIGNAL(clicked()), this, SLOT(startServer()));
    connect(m_stopServerButton, SIGNAL(clicked()), this, SLOT(stopServer()));
    connect(addFileButton, SIGNAL(clicked()), this, SLOT(addFiles()));
    connect(addDirButton, SIGNAL(clicked()), this, SLOT(addDirectory()));
    connect(removeButton, SIGNAL(clicked()), this, SLOT(removeSelectedItems()));
    connect(clearButton, SIGNAL(clicked()), this, SLOT(clearItems()));
    connect(m_sendButton, SIGNAL(clicked()), this, SLOT(sendSelection()));
}

void MainWindow::chooseSaveDirectory()
{
    const QString directory = QFileDialog::getExistingDirectory(this,
                                                               QStringLiteral("选择保存目录"),
                                                               m_saveDirEdit->text());
    if (!directory.isEmpty()) {
        m_saveDirEdit->setText(directory);
    }
}

void MainWindow::startServer()
{
    int port = 0;
    if (!readPort(m_portEdit, &port)) {
        QMessageBox::warning(this, QStringLiteral("端口错误"), QStringLiteral("请输入 1-65535 之间的端口号。"));
        return;
    }

    m_server->start(quint16(port), m_saveDirEdit->text().trimmed());
}

void MainWindow::stopServer()
{
    m_server->stop();
}

void MainWindow::addFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(this, QStringLiteral("选择文件"));
    for (int i = 0; i < files.size(); ++i) {
        addPathItem(files.at(i), QStringLiteral("file"));
    }
}

void MainWindow::addDirectory()
{
    const QString directory = QFileDialog::getExistingDirectory(this, QStringLiteral("选择目录"));
    if (!directory.isEmpty()) {
        addPathItem(directory, QStringLiteral("dir"));
    }
}

void MainWindow::removeSelectedItems()
{
    const QList<QListWidgetItem *> items = m_pathList->selectedItems();
    for (int i = 0; i < items.size(); ++i) {
        delete items.at(i);
    }
    updateButtons();
}

void MainWindow::clearItems()
{
    m_pathList->clear();
    updateButtons();
}

void MainWindow::sendSelection()
{
    int port = 0;
    if (!readPort(m_remotePortEdit, &port)) {
        QMessageBox::warning(this, QStringLiteral("端口错误"), QStringLiteral("请输入 1-65535 之间的端口号。"));
        return;
    }

    const QString host = m_hostEdit->text().trimmed();
    if (host.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("目标错误"), QStringLiteral("请输入目标 IP 地址。"));
        return;
    }

    if (m_transferThread) {
        QMessageBox::information(this, QStringLiteral("正在传输"), QStringLiteral("请等待当前传输完成。"));
        return;
    }

    const QStringList files = selectedPaths(QStringLiteral("file"));
    const QStringList directories = selectedPaths(QStringLiteral("dir"));
    if (files.isEmpty() && directories.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("没有内容"), QStringLiteral("请先添加文件或目录。"));
        return;
    }

    m_currentProgress->setValue(0);
    m_totalProgress->setValue(0);
    m_currentProgress->setFormat(QStringLiteral("0%"));
    m_totalProgress->setFormat(QStringLiteral("0%"));

    FileTransferClient *client = new FileTransferClient;
    m_transferThread = new QThread(this);
    client->moveToThread(m_transferThread);

    connect(m_transferThread, SIGNAL(finished()), client, SLOT(deleteLater()));
    connect(client, SIGNAL(logMessage(QString)), this, SLOT(appendLog(QString)));
    connect(client, SIGNAL(currentProgress(QString,qint64,qint64)),
            this, SLOT(updateCurrentProgress(QString,qint64,qint64)));
    connect(client, SIGNAL(totalProgress(qint64,qint64)), this, SLOT(updateTotalProgress(qint64,qint64)));
    connect(client, SIGNAL(finished(bool,QString)), this, SLOT(transferFinished(bool,QString)));
    connect(client, SIGNAL(finished(bool,QString)), m_transferThread, SLOT(quit()));
    connect(m_transferThread, &QThread::finished, this, [this]() {
        m_transferThread->deleteLater();
        m_transferThread = 0;
        updateButtons();
    });

    connect(m_transferThread, &QThread::started, this, [client, host, port, files, directories]() {
        QMetaObject::invokeMethod(client, "send",
                                  Qt::QueuedConnection,
                                  Q_ARG(QString, host),
                                  Q_ARG(int, port),
                                  Q_ARG(QStringList, files),
                                  Q_ARG(QStringList, directories),
                                  Q_ARG(qint64, Protocol::DefaultChunkSize));
    });

    appendLog(QStringLiteral("准备发送任务"));
    m_transferThread->start();
    updateButtons();
}

void MainWindow::appendLog(const QString &message)
{
    const QString line = QStringLiteral("[%1] %2")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")))
            .arg(message);
    m_logEdit->append(line);
}

void MainWindow::updateServerState(bool listening, const QString &message)
{
    m_serverStateLabel->setText(message);
    m_startServerButton->setEnabled(!listening);
    m_stopServerButton->setEnabled(listening);
}

void MainWindow::updateCurrentProgress(const QString &path, qint64 value, qint64 maximum)
{
    m_currentFileLabel->setText(path.isEmpty() ? QStringLiteral("当前文件") : path);
    setProgress(m_currentProgress, value, maximum);
}

void MainWindow::updateTotalProgress(qint64 value, qint64 maximum)
{
    setProgress(m_totalProgress, value, maximum);
}

void MainWindow::transferFinished(bool ok, const QString &message)
{
    appendLog((ok ? QStringLiteral("发送完成: ") : QStringLiteral("发送失败: ")) + message);
}

void MainWindow::updateButtons()
{
    m_sendButton->setEnabled(!m_transferThread && m_pathList->count() > 0);
}

void MainWindow::addPathItem(const QString &path, const QString &type)
{
    const QString cleanPath = QFileInfo(path).absoluteFilePath();
    for (int i = 0; i < m_pathList->count(); ++i) {
        if (m_pathList->item(i)->data(Qt::UserRole).toString() == cleanPath) {
            return;
        }
    }

    const QFileInfo info(cleanPath);
    const QString prefix = type == QStringLiteral("dir") ? QStringLiteral("[目录] ") : QStringLiteral("[文件] ");
    QListWidgetItem *item = new QListWidgetItem(prefix + info.fileName() + QStringLiteral("  -  ") + cleanPath);
    item->setData(Qt::UserRole, cleanPath);
    item->setData(Qt::UserRole + 1, type);
    m_pathList->addItem(item);
    updateButtons();
}

QStringList MainWindow::selectedPaths(const QString &type) const
{
    QStringList paths;
    for (int i = 0; i < m_pathList->count(); ++i) {
        const QListWidgetItem *item = m_pathList->item(i);
        if (item->data(Qt::UserRole + 1).toString() == type) {
            paths.append(item->data(Qt::UserRole).toString());
        }
    }
    return paths;
}

QString MainWindow::localAddressText() const
{
    QStringList addresses;
    const QList<QHostAddress> all = QNetworkInterface::allAddresses();
    for (int i = 0; i < all.size(); ++i) {
        const QHostAddress address = all.at(i);
        if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback()) {
            addresses.append(address.toString());
        }
    }

    if (addresses.isEmpty()) {
        addresses.append(QStringLiteral("127.0.0.1"));
    }
    return addresses.join(QStringLiteral(" / "));
}

void MainWindow::setProgress(QProgressBar *bar, qint64 value, qint64 maximum)
{
    if (maximum <= 0) {
        bar->setValue(0);
        bar->setFormat(QStringLiteral("0%"));
        return;
    }

    const int percent = qBound(0, int(double(value) * 100.0 / double(maximum)), 100);
    bar->setValue(percent);
    bar->setFormat(QStringLiteral("%1%  (%2 / %3)")
                   .arg(percent)
                   .arg(readableBytes(value))
                   .arg(readableBytes(maximum)));
}
