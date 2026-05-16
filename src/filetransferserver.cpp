#include "filetransferserver.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

#include <climits>

FileTransferServer::ClientSession::ClientSession()
    : currentFile(0),
      currentHash(0),
      expectedSize(0),
      receivedSize(0)
{
}

FileTransferServer::FileTransferServer(QObject *parent)
    : QObject(parent),
      m_server(new QTcpServer(this))
{
    connect(m_server, SIGNAL(newConnection()), this, SLOT(acceptConnection()));
}

FileTransferServer::~FileTransferServer()
{
    stop();
}

bool FileTransferServer::isListening() const
{
    return m_server->isListening();
}

void FileTransferServer::start(quint16 port, const QString &saveDirectory)
{
    if (m_server->isListening()) {
        stop();
    }

    QDir dir(saveDirectory);
    if (!dir.exists() && !dir.mkpath(".")) {
        emit serverStateChanged(false, QStringLiteral("保存目录创建失败"));
        return;
    }

    m_saveDirectory = dir.absolutePath();
    if (!m_server->listen(QHostAddress::AnyIPv4, port)) {
        emit serverStateChanged(false, QStringLiteral("监听失败: %1").arg(m_server->errorString()));
        return;
    }

    emit logMessage(QStringLiteral("接收端已启动，端口 %1，保存目录 %2").arg(port).arg(m_saveDirectory));
    emit serverStateChanged(true, QStringLiteral("正在监听端口 %1").arg(port));
}

void FileTransferServer::stop()
{
    if (m_server->isListening()) {
        m_server->close();
    }

    QList<QTcpSocket *> sockets = m_sessions.keys();
    for (int i = 0; i < sockets.size(); ++i) {
        QTcpSocket *socket = sockets.at(i);
        socket->disconnect(this);
        socket->disconnectFromHost();
        cleanupSession(socket);
        socket->deleteLater();
    }

    emit serverStateChanged(false, QStringLiteral("接收端未启动"));
}

void FileTransferServer::acceptConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        m_sessions.insert(socket, new ClientSession);

        connect(socket, SIGNAL(readyRead()), this, SLOT(socketReadyRead()));
        connect(socket, SIGNAL(disconnected()), this, SLOT(socketDisconnected()));

        emit logMessage(QStringLiteral("客户端接入: %1:%2")
                        .arg(socket->peerAddress().toString())
                        .arg(socket->peerPort()));
    }
}

void FileTransferServer::socketReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket || !m_sessions.contains(socket)) {
        return;
    }

    ClientSession *session = m_sessions.value(socket);
    session->buffer.append(socket->readAll());

    while (session->buffer.size() >= Protocol::HeaderSize) {
        Protocol::Header header;
        QString errorMessage;
        if (!Protocol::decodeHeader(session->buffer.left(Protocol::HeaderSize), &header, &errorMessage)) {
            failSocket(socket, errorMessage);
            return;
        }

        const quint64 frameSize = quint64(Protocol::HeaderSize) + header.payloadSize;
        if (frameSize > quint64(INT_MAX)) {
            failSocket(socket, QStringLiteral("数据帧超过本程序处理上限"));
            return;
        }

        if (quint64(session->buffer.size()) < frameSize) {
            return;
        }

        const QByteArray payload = session->buffer.mid(Protocol::HeaderSize, int(header.payloadSize));
        session->buffer.remove(0, int(frameSize));

        if (!handleFrame(socket, session, header, payload)) {
            return;
        }
    }
}

void FileTransferServer::socketDisconnected()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket) {
        return;
    }

    emit logMessage(QStringLiteral("客户端断开: %1:%2")
                    .arg(socket->peerAddress().toString())
                    .arg(socket->peerPort()));
    cleanupSession(socket);
    socket->deleteLater();
}

bool FileTransferServer::handleFrame(QTcpSocket *socket,
                                     ClientSession *session,
                                     const Protocol::Header &header,
                                     const QByteArray &payload)
{
    QString errorMessage;
    bool ok = false;

    switch (Protocol::PacketType(header.type)) {
    case Protocol::PacketMkdir: {
        QString relativePath;
        if (!Protocol::decodePath(payload, &relativePath, &errorMessage)) {
            failSocket(socket, errorMessage);
            return false;
        }

        const QString outputPath = Protocol::safeDestinationPath(m_saveDirectory, relativePath, &ok);
        if (!ok || !QDir().mkpath(outputPath)) {
            failSocket(socket, QStringLiteral("目录创建失败: %1").arg(relativePath));
            return false;
        }

        emit logMessage(QStringLiteral("创建目录: %1").arg(relativePath));
        return true;
    }

    case Protocol::PacketBeginFile: {
        if (session->currentFile) {
            failSocket(socket, QStringLiteral("上一个文件尚未结束"));
            return false;
        }

        Protocol::FileMeta meta;
        if (!Protocol::decodeFileMeta(payload, &meta, &errorMessage)) {
            failSocket(socket, errorMessage);
            return false;
        }

        const QString outputPath = Protocol::safeDestinationPath(m_saveDirectory, meta.relativePath, &ok);
        if (!ok) {
            failSocket(socket, QStringLiteral("目标路径不安全: %1").arg(meta.relativePath));
            return false;
        }

        const QFileInfo outputInfo(outputPath);
        if (!QDir().mkpath(outputInfo.absolutePath())) {
            failSocket(socket, QStringLiteral("父目录创建失败: %1").arg(outputInfo.absolutePath()));
            return false;
        }

        QFile *file = new QFile(outputPath);
        if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            const QString message = QStringLiteral("文件写入失败: %1").arg(file->errorString());
            delete file;
            failSocket(socket, message);
            return false;
        }

        session->currentFile = file;
        session->currentHash = new QCryptographicHash(QCryptographicHash::Sha256);
        session->currentRelativePath = meta.relativePath;
        session->currentOutputPath = outputPath;
        session->expectedSha256 = meta.sha256;
        session->expectedSize = meta.size;
        session->receivedSize = 0;

        emit currentProgress(meta.relativePath, 0, qint64(meta.size));
        emit logMessage(QStringLiteral("开始接收: %1 (%2 bytes)").arg(meta.relativePath).arg(meta.size));
        return true;
    }

    case Protocol::PacketFileChunk: {
        if (!session->currentFile || !session->currentHash) {
            failSocket(socket, QStringLiteral("收到文件块，但当前没有打开的文件"));
            return false;
        }

        if (session->receivedSize + quint64(payload.size()) > session->expectedSize) {
            failSocket(socket, QStringLiteral("收到的数据超过文件声明大小"));
            return false;
        }

        if (session->currentFile->write(payload) != payload.size()) {
            failSocket(socket, QStringLiteral("写文件失败: %1").arg(session->currentFile->errorString()));
            return false;
        }

        session->currentHash->addData(payload);
        session->receivedSize += quint64(payload.size());
        emit currentProgress(session->currentRelativePath,
                             qint64(session->receivedSize),
                             qint64(session->expectedSize));
        return true;
    }

    case Protocol::PacketEndFile: {
        if (!session->currentFile || !session->currentHash) {
            failSocket(socket, QStringLiteral("收到文件结束帧，但当前没有打开的文件"));
            return false;
        }

        QString relativePath;
        if (!Protocol::decodePath(payload, &relativePath, &errorMessage)) {
            failSocket(socket, errorMessage);
            return false;
        }

        if (relativePath != session->currentRelativePath) {
            failSocket(socket, QStringLiteral("文件结束帧路径与当前文件不一致"));
            return false;
        }

        session->currentFile->flush();
        session->currentFile->close();

        const QByteArray actualHash = session->currentHash->result();
        if (session->receivedSize != session->expectedSize) {
            failSocket(socket, QStringLiteral("文件大小校验失败: %1").arg(session->currentRelativePath));
            return false;
        }

        if (actualHash != session->expectedSha256) {
            failSocket(socket, QStringLiteral("SHA-256 校验失败: %1").arg(session->currentRelativePath));
            return false;
        }

        const QString outputPath = session->currentOutputPath;
        const QString relative = session->currentRelativePath;
        delete session->currentFile;
        delete session->currentHash;
        session->currentFile = 0;
        session->currentHash = 0;
        session->currentRelativePath.clear();
        session->currentOutputPath.clear();
        session->expectedSha256.clear();
        session->expectedSize = 0;
        session->receivedSize = 0;

        emit fileReceived(outputPath);
        emit logMessage(QStringLiteral("文件接收完成: %1").arg(relative));
        return true;
    }

    case Protocol::PacketBatchEnd:
        emit logMessage(QStringLiteral("本次传输批次完成"));
        return true;
    }

    failSocket(socket, QStringLiteral("未知数据帧类型: %1").arg(header.type));
    return false;
}

void FileTransferServer::failSocket(QTcpSocket *socket, const QString &message)
{
    emit logMessage(QStringLiteral("传输错误: %1").arg(message));
    socket->disconnectFromHost();
}

void FileTransferServer::cleanupSession(QTcpSocket *socket)
{
    ClientSession *session = m_sessions.take(socket);
    if (!session) {
        return;
    }

    const QString unfinishedPath = session->currentOutputPath;
    if (session->currentFile) {
        session->currentFile->close();
        delete session->currentFile;
    }
    if (!unfinishedPath.isEmpty()) {
        QFile::remove(unfinishedPath);
    }
    delete session->currentHash;
    delete session;
}
