#ifndef FILETRANSFERSERVER_H
#define FILETRANSFERSERVER_H

#include "protocol.h"

#include <QHash>
#include <QObject>

class QFile;
class QTcpServer;
class QTcpSocket;
class QCryptographicHash;

class FileTransferServer : public QObject
{
    Q_OBJECT

public:
    explicit FileTransferServer(QObject *parent = 0);
    ~FileTransferServer();

    bool isListening() const;

public slots:
    void start(quint16 port, const QString &saveDirectory);
    void stop();

signals:
    void logMessage(const QString &message);
    void serverStateChanged(bool listening, const QString &message);
    void currentProgress(const QString &path, qint64 receivedBytes, qint64 totalBytes);
    void fileReceived(const QString &path);

private slots:
    void acceptConnection();
    void socketReadyRead();
    void socketDisconnected();

private:
    struct ClientSession {
        ClientSession();

        QByteArray buffer;
        QFile *currentFile;
        QCryptographicHash *currentHash;
        QString currentRelativePath;
        QString currentOutputPath;
        QByteArray expectedSha256;
        quint64 expectedSize;
        quint64 receivedSize;
    };

    bool handleFrame(QTcpSocket *socket,
                     ClientSession *session,
                     const Protocol::Header &header,
                     const QByteArray &payload);
    void failSocket(QTcpSocket *socket, const QString &message);
    void cleanupSession(QTcpSocket *socket);

    QTcpServer *m_server;
    QString m_saveDirectory;
    QHash<QTcpSocket *, ClientSession *> m_sessions;
};

#endif // FILETRANSFERSERVER_H
