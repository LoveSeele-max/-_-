#ifndef FILETRANSFERCLIENT_H
#define FILETRANSFERCLIENT_H

#include "protocol.h"

#include <QObject>
#include <QStringList>

class QTcpSocket;

class FileTransferClient : public QObject
{
    Q_OBJECT

public:
    explicit FileTransferClient(QObject *parent = 0);

public slots:
    void send(const QString &host,
              int port,
              const QStringList &files,
              const QStringList &directories,
              qint64 chunkSize);

signals:
    void logMessage(const QString &message);
    void currentProgress(const QString &path, qint64 sentBytes, qint64 totalBytes);
    void totalProgress(qint64 sentBytes, qint64 totalBytes);
    void finished(bool ok, const QString &message);

private:
    struct FileItem {
        QString absolutePath;
        QString relativePath;
        quint64 size;
        qint64 lastModifiedSecs;
    };

    bool collectTransferItems(const QStringList &files,
                              const QStringList &directories,
                              QList<FileItem> *fileItems,
                              QStringList *directoryItems,
                              quint64 *totalBytes,
                              QString *errorMessage) const;
    bool appendDirectory(const QString &rootPath,
                         const QString &rootRelativePath,
                         QList<FileItem> *fileItems,
                         QStringList *directoryItems,
                         quint64 *totalBytes,
                         QString *errorMessage) const;
    QByteArray calculateSha256(const QString &path, QString *errorMessage) const;
    bool sendFrame(QTcpSocket *socket, Protocol::PacketType type, const QByteArray &payload);
    bool sendOneFile(QTcpSocket *socket,
                     const FileItem &item,
                     qint64 chunkSize,
                     quint64 *sentTotal,
                     quint64 totalBytes);
};

#endif // FILETRANSFERCLIENT_H
