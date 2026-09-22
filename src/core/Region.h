#pragma once

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace justnbt {
struct RegionPos {
    int x = 0;
    int z = 0;
};

std::optional<RegionPos> parseRegionFileName(const QString& fileName);

class RegionFile {
public:
    static constexpr int kChunksPerSide = 32;

    explicit RegionFile(const QString& path);
    RegionFile(const QString& path, QByteArray data);

    int compressionType(int localX, int localZ) const;

    const QString& path() const { return path_; }
    RegionPos position() const { return pos_; }
    bool hasChunk(int localX, int localZ) const;
    uint32_t timestamp(int localX, int localZ) const;

    std::vector<uint8_t> chunkData(int localX, int localZ) const;

    static void writeChunk(const QString& path, int localX, int localZ, std::span<const uint8_t> nbt,
                           int compressionType);

    static int deleteChunks(const QString& path, const std::vector<std::pair<int, int>>& localChunks);

private:
    uint32_t location(int localX, int localZ) const;

    QString path_;
    RegionPos pos_;
    QByteArray data_;
};
}
