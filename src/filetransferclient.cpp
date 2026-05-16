#include "filetransferclient.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTcpSocket>

namespace {

QString makeUniqueRelativePath(const QString &candidate, QSet<QString> *usedPaths)
{
    if (!usedPaths->contains(candidate)) {
        usedPaths->insert(candidate);
        return candidate;
    }

    const int dotIndex = candidate.lastIndexOf('.');
    const bool hasExtension = dotIndex > 0 && !candidate.mid(dotIndex + 1).contains('/');
    const QString base = hasExtension ? candidate.left(dotIndex) : candidate;
    const QString suffix = hasExtension ? candidate.mid(dotIndex) : QString();

    for (int index = 2; ; ++index) {
        const QString next = QStringLiteral("%1_%2%3").arg(base).arg(index).arg(suffix);
        if (!usedPaths->contains(next)) {
            usedPaths->insert(next);
            return next;
        }
    }
}

}

FileTransferClient::FileTransferClient(QObject *parent)
    : QObject(parent)
{
}

void FileTransferClient::send(const QString &host,
                              int port,
                              const QStringList &files,
                              const QStringList &directories,
                              qint64 chunkSize)
{
    QList<FileItem> fileItems;
    QStringList directoryItems;
    quint64 totalBytes = 0;
    QString errorMessage;

    if (!collectTransferItems(files, directories, &fileItems, &directoryItems, &totalBytes, &errorMessage)) {
        emit finished(false, errorMessage);
        return;
    }

    QTcpSocket socket;
    emit logMessage(QStringLiteral("正在连接 %1:%2 ...").arg(host).arg(port));
    socket.connectToHost(host, quint16(port));
    if (!socket.waitForConnected(10000)) {
        emit finished(false, QStringLiteral("连接失败: %1").arg(socket.errorString()));
        return;
    }

    emit logMessage(QStringLiteral("连接成功，准备发送 %1 个目录、%2 个文件。")
                    .arg(directoryItems.size())
                    .arg(fileItems.size()));

    for (int i = 0; i < directoryItems.size(); ++i) {
        if (!sendFrame(&socket, Protocol::PacketMkdir, Protocol::encodePath(directoryItems.at(i)))) {
            emit finished(false, QStringLiteral("目录信息发送失败: %1").arg(socket.errorString()));
            return;
        }
    }

    quint64 sentTotal = 0;
    for (int i = 0; i < fileItems.size(); ++i) {
        if (!sendOneFile(&socket, fileItems.at(i), chunkSize, &sentTotal, totalBytes)) {
            emit finished(false, QStringLiteral("发送中断: %1").arg(socket.errorString()));
            return;
        }
    }

    if (!sendFrame(&socket, Protocol::PacketBatchEnd, QByteArray())) {
        emit finished(false, QStringLiteral("结束帧发送失败: %1").arg(socket.errorString()));
        return;
    }

    socket.disconnectFromHost();
    socket.waitForDisconnected(3000);
    emit totalProgress(qint64(totalBytes), qint64(totalBytes));
    emit finished(true, QStringLiteral("传输完成"));
}

bool FileTransferClient::collectTransferItems(const QStringList &files,
                                              const QStringList &directories,
                                              QList<FileItem> *fileItems,
                                              QStringList *directoryItems,
                                              quint64 *totalBytes,
                                              QString *errorMessage) const
{
    QSet<QString> usedTopLevelPaths;
    QSet<QString> seenFiles;

    for (int i = 0; i < directories.size(); ++i) {
        const QFileInfo rootInfo(directories.at(i));
        if (!rootInfo.exists() || !rootInfo.isDir()) {
            *errorMessage = QStringLiteral("目录不存在: %1").arg(directories.at(i));
            return false;
        }

        bool ok = false;
        const QString rootRelative = Protocol::normalizeRelativePath(rootInfo.fileName(), &ok);
        if (!ok) {
            *errorMessage = QStringLiteral("目录名不合法: %1").arg(rootInfo.fileName());
            return false;
        }

        const QString uniqueRootRelative = makeUniqueRelativePath(rootRelative, &usedTopLevelPaths);
        directoryItems->append(uniqueRootRelative);

        if (!appendDirectory(rootInfo.absoluteFilePath(), uniqueRootRelative,
                             fileItems, directoryItems, totalBytes, errorMessage)) {
            return false;
        }
    }

    for (int i = 0; i < files.size(); ++i) {
        const QFileInfo fileInfo(files.at(i));
        if (!fileInfo.exists() || !fileInfo.isFile()) {
            *errorMessage = QStringLiteral("文件不存在: %1").arg(files.at(i));
            return false;
        }

        bool ok = false;
        const QString relativePath = Protocol::normalizeRelativePath(fileInfo.fileName(), &ok);
        if (!ok) {
            *errorMessage = QStringLiteral("文件名不合法: %1").arg(fileInfo.fileName());
            return false;
        }

        const QString key = QFileInfo(fileInfo.absoluteFilePath()).canonicalFilePath();
        if (seenFiles.contains(key)) {
            continue;
        }
        seenFiles.insert(key);

        const QString uniqueRelativePath = makeUniqueRelativePath(relativePath, &usedTopLevelPaths);

        FileItem item;
        item.absolutePath = fileInfo.absoluteFilePath();
        item.relativePath = uniqueRelativePath;
        item.size = quint64(fileInfo.size());
        item.lastModifiedSecs = fileInfo.lastModified().toSecsSinceEpoch();
        fileItems->append(item);
        *totalBytes += item.size;
    }

    if (fileItems->isEmpty() && directoryItems->isEmpty()) {
        *errorMessage = QStringLiteral("请先选择要发送的文件或目录");
        return false;
    }

    return true;
}

bool FileTransferClient::appendDirectory(const QString &rootPath,
                                         const QString &rootRelativePath,
                                         QList<FileItem> *fileItems,
                                         QStringList *directoryItems,
                                         quint64 *totalBytes,
                                         QString *errorMessage) const
{
    QSet<QString> localDirectories;
    for (int i = 0; i < directoryItems->size(); ++i) {
        localDirectories.insert(directoryItems->at(i));
    }
    const QDir rootDir(rootPath);
    QDirIterator iterator(rootPath,
                          QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);

    while (iterator.hasNext()) {
        iterator.next();
        const QFileInfo info = iterator.fileInfo();
        QString relativeToRoot = rootDir.relativeFilePath(info.absoluteFilePath());
        relativeToRoot.replace('\\', '/');
        const QString combined = rootRelativePath + "/" + relativeToRoot;

        bool ok = false;
        const QString relativePath = Protocol::normalizeRelativePath(combined, &ok);
        if (!ok) {
            *errorMessage = QStringLiteral("路径不合法: %1").arg(combined);
            return false;
        }

        if (info.isDir()) {
            if (!localDirectories.contains(relativePath)) {
                localDirectories.insert(relativePath);
                directoryItems->append(relativePath);
            }
            continue;
        }

        if (info.isFile()) {
            FileItem item;
            item.absolutePath = info.absoluteFilePath();
            item.relativePath = relativePath;
            item.size = quint64(info.size());
            item.lastModifiedSecs = info.lastModified().toSecsSinceEpoch();
            fileItems->append(item);
            *totalBytes += item.size;
        }
    }

    return true;
}

QByteArray FileTransferClient::calculateSha256(const QString &path, QString *errorMessage) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *errorMessage = QStringLiteral("无法读取文件用于校验: %1").arg(path);
        return QByteArray();
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray block = file.read(1024 * 1024);
        if (block.isEmpty() && file.error() != QFile::NoError) {
            *errorMessage = QStringLiteral("读取文件失败: %1").arg(file.errorString());
            return QByteArray();
        }
        hash.addData(block);
    }

    return hash.result();
}

bool FileTransferClient::sendFrame(QTcpSocket *socket, Protocol::PacketType type, const QByteArray &payload)
{
    const QByteArray header = Protocol::encodeHeader(type, quint64(payload.size()));
    if (socket->write(header) != header.size()) {
        return false;
    }
    if (!payload.isEmpty() && socket->write(payload) != payload.size()) {
        return false;
    }

    while (socket->bytesToWrite() > 0) {
        if (!socket->waitForBytesWritten(30000)) {
            return false;
        }
    }
    return true;
}

bool FileTransferClient::sendOneFile(QTcpSocket *socket,
                                     const FileItem &item,
                                     qint64 chunkSize,
                                     quint64 *sentTotal,
                                     quint64 totalBytes)
{
    QString errorMessage;
    emit logMessage(QStringLiteral("计算 SHA-256: %1").arg(item.relativePath));
    const QByteArray sha256 = calculateSha256(item.absolutePath, &errorMessage);
    if (sha256.size() != 32) {
        emit logMessage(errorMessage);
        return false;
    }

    Protocol::FileMeta meta;
    meta.relativePath = item.relativePath;
    meta.size = item.size;
    meta.sha256 = sha256;
    meta.lastModifiedSecs = item.lastModifiedSecs;

    if (!sendFrame(socket, Protocol::PacketBeginFile, Protocol::encodeFileMeta(meta))) {
        return false;
    }

    QFile file(item.absolutePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit logMessage(QStringLiteral("无法打开文件: %1").arg(item.absolutePath));
        return false;
    }

    emit logMessage(QStringLiteral("开始发送: %1 (%2 bytes)").arg(item.relativePath).arg(item.size));
    quint64 sentCurrent = 0;
    while (!file.atEnd()) {
        const QByteArray block = file.read(chunkSize);
        if (block.isEmpty() && file.error() != QFile::NoError) {
            emit logMessage(QStringLiteral("读取文件失败: %1").arg(file.errorString()));
            return false;
        }

        if (!sendFrame(socket, Protocol::PacketFileChunk, block)) {
            return false;
        }

        sentCurrent += quint64(block.size());
        *sentTotal += quint64(block.size());
        emit currentProgress(item.relativePath, qint64(sentCurrent), qint64(item.size));
        emit totalProgress(qint64(*sentTotal), qint64(totalBytes));
    }

    if (!sendFrame(socket, Protocol::PacketEndFile, Protocol::encodePath(item.relativePath))) {
        return false;
    }

    emit currentProgress(item.relativePath, qint64(item.size), qint64(item.size));
    emit logMessage(QStringLiteral("文件发送完成: %1").arg(item.relativePath));
    return true;
}
