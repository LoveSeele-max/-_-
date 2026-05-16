#include "protocol.h"

#include <QDataStream>
#include <QDir>
#include <QIODevice>

namespace {

void configureStream(QDataStream &stream)
{
    stream.setVersion(QDataStream::Qt_5_9);
    stream.setByteOrder(QDataStream::BigEndian);
}

bool isRelativePathSafe(const QString &path)
{
    if (path.isEmpty() || path == ".") {
        return false;
    }

    if (QDir::isAbsolutePath(path) || path.contains(':')) {
        return false;
    }

    if (path == ".." || path.startsWith("../") || path.contains("/../")) {
        return false;
    }

    return true;
}

}

namespace Protocol {

QByteArray encodeHeader(PacketType type, quint64 payloadSize)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    configureStream(stream);
    stream << Magic << Version << quint16(type) << payloadSize;
    return data;
}

bool decodeHeader(const QByteArray &data, Header *header, QString *errorMessage)
{
    if (data.size() < HeaderSize) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("数据头长度不足");
        }
        return false;
    }

    QDataStream stream(data);
    configureStream(stream);
    stream >> header->magic >> header->version >> header->type >> header->payloadSize;

    if (stream.status() != QDataStream::Ok) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("数据头解析失败");
        }
        return false;
    }

    if (header->magic != Magic) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("协议魔数不匹配");
        }
        return false;
    }

    if (header->version != Version) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("协议版本不兼容");
        }
        return false;
    }

    if (header->payloadSize > MaxPayloadSize) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("单个数据帧过大");
        }
        return false;
    }

    return true;
}

QByteArray encodePath(const QString &relativePath)
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    configureStream(stream);
    stream << relativePath;
    return payload;
}

bool decodePath(const QByteArray &payload, QString *relativePath, QString *errorMessage)
{
    QDataStream stream(payload);
    configureStream(stream);
    QString path;
    stream >> path;

    if (stream.status() != QDataStream::Ok) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("路径字段解析失败");
        }
        return false;
    }

    bool ok = false;
    const QString normalized = normalizeRelativePath(path, &ok);
    if (!ok) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("收到不安全的相对路径: %1").arg(path);
        }
        return false;
    }

    *relativePath = normalized;
    return true;
}

QByteArray encodeFileMeta(const FileMeta &meta)
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    configureStream(stream);
    stream << meta.relativePath << meta.size << meta.sha256 << meta.lastModifiedSecs;
    return payload;
}

bool decodeFileMeta(const QByteArray &payload, FileMeta *meta, QString *errorMessage)
{
    QDataStream stream(payload);
    configureStream(stream);
    FileMeta parsed;
    stream >> parsed.relativePath >> parsed.size >> parsed.sha256 >> parsed.lastModifiedSecs;

    if (stream.status() != QDataStream::Ok) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("文件元信息解析失败");
        }
        return false;
    }

    bool ok = false;
    parsed.relativePath = normalizeRelativePath(parsed.relativePath, &ok);
    if (!ok) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("收到不安全的文件路径");
        }
        return false;
    }

    if (parsed.sha256.size() != 32) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("文件 SHA-256 长度错误");
        }
        return false;
    }

    *meta = parsed;
    return true;
}

QString normalizeRelativePath(const QString &path, bool *ok)
{
    QString normalized = path;
    normalized.replace('\\', '/');
    while (normalized.startsWith("./")) {
        normalized.remove(0, 2);
    }
    normalized = QDir::cleanPath(normalized);

    const bool safe = isRelativePathSafe(normalized);
    if (ok) {
        *ok = safe;
    }
    return safe ? normalized : QString();
}

QString safeDestinationPath(const QString &baseDirectory, const QString &relativePath, bool *ok)
{
    bool pathOk = false;
    const QString normalized = normalizeRelativePath(relativePath, &pathOk);
    if (!pathOk) {
        if (ok) {
            *ok = false;
        }
        return QString();
    }

    const QDir baseDir(baseDirectory);
    const QString basePath = QDir::cleanPath(baseDir.absolutePath());
    const QString outputPath = QDir::cleanPath(baseDir.absoluteFilePath(normalized));

    QString baseCompare = basePath;
    QString outputCompare = outputPath;
#ifdef Q_OS_WIN
    baseCompare = baseCompare.toLower();
    outputCompare = outputCompare.toLower();
#endif

    const bool insideBase = outputCompare.startsWith(baseCompare + "/");
    if (ok) {
        *ok = insideBase;
    }

    return insideBase ? outputPath : QString();
}

QString sha256Hex(const QByteArray &hash)
{
    return QString::fromLatin1(hash.toHex());
}

}
