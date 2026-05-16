#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <QByteArray>
#include <QString>
#include <QtGlobal>

namespace Protocol {

static const quint32 Magic = 0x51465431; // "QFT1"
static const quint16 Version = 1;
static const int HeaderSize = 16;
static const qint64 DefaultChunkSize = 64 * 1024;
static const quint64 MaxPayloadSize = 8 * 1024 * 1024;

enum PacketType {
    PacketMkdir = 1,
    PacketBeginFile = 2,
    PacketFileChunk = 3,
    PacketEndFile = 4,
    PacketBatchEnd = 5
};

struct Header {
    quint32 magic;
    quint16 version;
    quint16 type;
    quint64 payloadSize;
};

struct FileMeta {
    QString relativePath;
    quint64 size;
    QByteArray sha256;
    qint64 lastModifiedSecs;
};

QByteArray encodeHeader(PacketType type, quint64 payloadSize);
bool decodeHeader(const QByteArray &data, Header *header, QString *errorMessage);

QByteArray encodePath(const QString &relativePath);
bool decodePath(const QByteArray &payload, QString *relativePath, QString *errorMessage);

QByteArray encodeFileMeta(const FileMeta &meta);
bool decodeFileMeta(const QByteArray &payload, FileMeta *meta, QString *errorMessage);

QString normalizeRelativePath(const QString &path, bool *ok = 0);
QString safeDestinationPath(const QString &baseDirectory, const QString &relativePath, bool *ok = 0);
QString sha256Hex(const QByteArray &hash);

}

#endif // PROTOCOL_H
