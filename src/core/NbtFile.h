#pragma once

#include "core/Compression.h"
#include "core/NbtIO.h"

#include <QByteArray>
#include <QString>

#include <string>
#include <utility>
#include <vector>

namespace justnbt {
struct NbtFormat {
    nbt::Endian endian = nbt::Endian::Big;
    Compression compression = Compression::Gzip;
    bool bedrockHeader = false;
    int32_t bedrockVersion = 0;
};

QString describe(const NbtFormat& f);

class NbtFile {
public:
    enum class Storage { File, DbRecord, RegionChunk, BedrockChunk };

    static std::unique_ptr<NbtFile> load(const QString& path);
    static std::unique_ptr<NbtFile> loadDbRecord(const QString& dbDir, const QByteArray& key);
    static std::unique_ptr<NbtFile> loadRegionChunk(const QString& regionPath, int chunkX, int chunkZ);
    static std::unique_ptr<NbtFile> loadBedrockChunk(const QString& dbDir, int dimension, int chunkX, int chunkZ);

    static std::unique_ptr<nbt::Tag> parse(std::span<const uint8_t> data, NbtFormat& format);

    std::unique_ptr<NbtFile> reloaded() const;

    bool isDbRecord() const { return storage == Storage::DbRecord; }
    QString identity() const;
    QString displayName() const;
    QString sourceText() const;

    std::vector<uint8_t> encode(bool compressed = true) const;

    QString save(const QString& backupSubdir);

    static QString chunkIdentity(const QString& path, int dimension, int chunkX, int chunkZ);

    Storage storage = Storage::File;
    QString path;
    QByteArray dbKey;
    int chunkX = 0;
    int chunkZ = 0;
    int dimension = 0;
    int regionCompression = 2;
    NbtFormat format;
    std::unique_ptr<nbt::Tag> root;

private:
    QString saveBedrockChunk(const QString& backupSubdir);

    bool bedrockDigpEntities_ = false;
    std::vector<std::pair<const nbt::Tag*, std::string>> bedrockEntityIds_;
    std::vector<std::pair<int, std::string>> bedrockSections_;
};

std::unique_ptr<nbt::Tag> decodeBedrockSubChunk(const std::string& value, int yFromKey);
std::string encodeBedrockSubChunk(const nbt::Tag& section);

QString keyToText(const QByteArray& key);
}
