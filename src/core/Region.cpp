#include "core/Region.h"
#include "core/Tr.h"

#include "core/Compression.h"

#include <lz4.h>

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace justnbt {
namespace {
constexpr size_t kSector = 4096;
constexpr size_t kHeader = 2 * kSector;

uint32_t readBE32(const char* p)
{
    const auto* u = reinterpret_cast<const uint8_t*>(p);
    return uint32_t(u[0]) << 24 | uint32_t(u[1]) << 16 | uint32_t(u[2]) << 8 | uint32_t(u[3]);
}

uint32_t readLE32(const uint8_t* p)
{
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

std::vector<uint8_t> decodeLz4Java(std::span<const uint8_t> in)
{
    constexpr size_t kBlockHeader = 21;
    std::vector<uint8_t> out;
    size_t pos = 0;
    while (pos + kBlockHeader <= in.size()) {
        if (std::memcmp(in.data() + pos, "LZ4Block", 8) != 0)
            throw std::runtime_error(Tr::tr("LZ4: bad block header").toStdString());
        const int method = in[pos + 8] & 0xF0;
        const uint32_t compressedLen = readLE32(in.data() + pos + 9);
        const uint32_t originalLen = readLE32(in.data() + pos + 13);
        pos += kBlockHeader;
        if (originalLen == 0)
            break;
        if (compressedLen > in.size() - pos)
            throw std::runtime_error(Tr::tr("LZ4: the block is truncated").toStdString());
        const size_t base = out.size();
        out.resize(base + originalLen);
        if (method == 0x10) {
            if (compressedLen != originalLen)
                throw std::runtime_error(Tr::tr("LZ4: bad block length").toStdString());
            std::memcpy(out.data() + base, in.data() + pos, originalLen);
        } else if (method == 0x20) {
            const int n = LZ4_decompress_safe(reinterpret_cast<const char*>(in.data() + pos),
                                              reinterpret_cast<char*>(out.data() + base), int(compressedLen),
                                              int(originalLen));
            if (n != int(originalLen))
                throw std::runtime_error(Tr::tr("LZ4: damaged data").toStdString());
        } else {
            throw std::runtime_error(Tr::tr("LZ4: unknown block method").toStdString());
        }
        pos += compressedLen;
    }
    return out;
}

std::vector<uint8_t> decodeChunk(std::span<const uint8_t> payload, int type)
{
    switch (type) {
    case 1: return decompress(payload, Compression::Gzip);
    case 2: return decompress(payload, Compression::Zlib);
    case 3: return {payload.begin(), payload.end()};
    case 4: return decodeLz4Java(payload);
    case 127: throw std::runtime_error(Tr::tr("The chunk uses a custom compression (type 127), which is not supported").toStdString());
    default: throw std::runtime_error(Tr::tr("Unknown chunk compression type: ").toStdString() + std::to_string(type));
    }
}
}

std::optional<RegionPos> parseRegionFileName(const QString& fileName)
{
    static const QRegularExpression re(QStringLiteral("^r\\.(-?\\d+)\\.(-?\\d+)\\.mca$"));
    const auto m = re.match(fileName);
    if (!m.hasMatch())
        return std::nullopt;
    return RegionPos{m.captured(1).toInt(), m.captured(2).toInt()};
}

namespace {
QByteArray readRegionBytes(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        throw std::runtime_error((Tr::tr("Cannot open the region: ") + f.errorString()).toStdString());
    return f.readAll();
}

void writeBE32(char* p, uint32_t v)
{
    p[0] = char(v >> 24);
    p[1] = char(v >> 16);
    p[2] = char(v >> 8);
    p[3] = char(v);
}
}

RegionFile::RegionFile(const QString& path) : RegionFile(path, readRegionBytes(path)) {}

RegionFile::RegionFile(const QString& path, QByteArray data) : path_(path), data_(std::move(data))
{
    const auto pos = parseRegionFileName(QFileInfo(path).fileName());
    if (!pos)
        throw std::runtime_error(Tr::tr("A region file has to be named r.X.Z.mca").toStdString());
    pos_ = *pos;
    if (data_.size() > 0 && size_t(data_.size()) < kHeader)
        throw std::runtime_error(Tr::tr("The region file is truncated").toStdString());
}

int RegionFile::compressionType(int localX, int localZ) const
{
    const uint32_t loc = location(localX, localZ);
    const size_t offset = size_t(loc >> 8) * kSector;
    if (loc == 0 || offset + 5 > size_t(data_.size()))
        return 0;
    return uint8_t(data_[qsizetype(offset + 4)]);
}

void RegionFile::writeChunk(const QString& path, int localX, int localZ, std::span<const uint8_t> nbt,
                            int compressionType)
{
    if (localX < 0 || localZ < 0 || localX >= kChunksPerSide || localZ >= kChunksPerSide)
        throw std::runtime_error(Tr::tr("Bad chunk coordinates inside the region").toStdString());
    const int type = compressionType == 1 || compressionType == 3 ? compressionType : 2;

    QByteArray old = QFile::exists(path) ? readRegionBytes(path) : QByteArray();
    if (old.isEmpty())
        old = QByteArray(qsizetype(kHeader), '\0');
    const RegionFile before(path, old);
    const int cx = before.pos_.x * kChunksPerSide + localX;
    const int cz = before.pos_.z * kChunksPerSide + localZ;
    const QString mccPath = QFileInfo(path).absolutePath() + QStringLiteral("/c.%1.%2.mcc").arg(cx).arg(cz);

    const auto compressed =
        compress(nbt, type == 1 ? Compression::Gzip : type == 3 ? Compression::None : Compression::Zlib);
    const size_t payload = compressed.size() + 1;
    size_t sectors = (4 + payload + kSector - 1) / kSector;
    const bool external = sectors > 255;
    if (external)
        sectors = 1;

    QByteArray out = old;
    const int index = localX + localZ * kChunksPerSide;
    const uint32_t oldLoc = readBE32(out.constData() + 4 * index);
    const size_t oldOffset = oldLoc >> 8, oldCount = oldLoc & 0xFF;
    size_t offset;
    if (oldOffset >= 2 && oldCount >= sectors && (oldOffset + oldCount) * kSector <= size_t(out.size())) {
        offset = oldOffset;
        std::memset(out.data() + oldOffset * kSector, 0, oldCount * kSector);
    } else {
        offset = std::max<size_t>(2, (size_t(out.size()) + kSector - 1) / kSector);
    }
    if (offset + sectors >= (size_t(1) << 24))
        throw std::runtime_error(Tr::tr("The region file is too big").toStdString());
    const size_t end = (offset + sectors) * kSector;
    if (size_t(out.size()) < end)
        out.resize(qsizetype(end), '\0');

    char* p = out.data() + offset * kSector;
    writeBE32(p, external ? 1u : uint32_t(payload));
    p[4] = char(external ? (type | 0x80) : type);
    if (!external)
        std::memcpy(p + 5, compressed.data(), compressed.size());
    writeBE32(out.data() + 4 * index, uint32_t(offset << 8 | sectors));
    writeBE32(out.data() + kSector + 4 * index, uint32_t(QDateTime::currentSecsSinceEpoch()));

    for (int i = 0; i < kChunksPerSide * kChunksPerSide; ++i) {
        if (i == index)
            continue;
        const uint32_t loc = readBE32(old.constData() + 4 * i);
        if (loc != readBE32(out.constData() + 4 * i)
            || readBE32(old.constData() + kSector + 4 * i) != readBE32(out.constData() + kSector + 4 * i))
            throw std::runtime_error(Tr::tr("Check failed: the table of another chunk changed. The file was not "
                                            "written.").toStdString());
        if (loc == 0)
            continue;
        const size_t from = size_t(loc >> 8) * kSector;
        const size_t to = std::min(from + size_t(loc & 0xFF) * kSector, size_t(old.size()));
        if (from < to && std::memcmp(old.constData() + from, out.constData() + from, to - from) != 0)
            throw std::runtime_error(Tr::tr("Check failed: the data of another chunk was touched. The file was not "
                                            "written.").toStdString());
    }

    if (external) {
        QSaveFile mcc(mccPath);
        if (!mcc.open(QIODevice::WriteOnly))
            throw std::runtime_error((Tr::tr("Cannot write ") + mccPath + ": " + mcc.errorString()).toStdString());
        mcc.write(reinterpret_cast<const char*>(compressed.data()), qint64(compressed.size()));
        if (!mcc.commit())
            throw std::runtime_error((Tr::tr("Cannot write ") + mccPath + ": " + mcc.errorString()).toStdString());
    }

    const RegionFile after(path, out);
    const auto back = after.chunkData(localX, localZ);
    if (back.size() != nbt.size() || !std::equal(back.begin(), back.end(), nbt.begin()))
        throw std::runtime_error(Tr::tr("Check failed: the written chunk reads back differently. The file was not "
                                        "written.").toStdString());

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        throw std::runtime_error((Tr::tr("Cannot open the region for writing: ") + f.errorString()).toStdString());
    f.write(out);
    if (!f.commit())
        throw std::runtime_error(
            (Tr::tr("Cannot write the region (does the game or the server hold the file?): ") + f.errorString())
                .toStdString());

    const bool wasExternal = oldLoc != 0 && (before.compressionType(localX, localZ) & 0x80);
    if (wasExternal && !external)
        QFile::remove(mccPath);
}

int RegionFile::deleteChunks(const QString& path, const std::vector<std::pair<int, int>>& localChunks)
{
    if (!QFile::exists(path))
        return 0;
    const QByteArray old = readRegionBytes(path);
    if (old.isEmpty())
        return 0;
    const RegionFile before(path, old);

    QByteArray out = old;
    QStringList externalFiles;
    int deleted = 0;
    for (const auto& [lx, lz] : localChunks) {
        if (lx < 0 || lz < 0 || lx >= kChunksPerSide || lz >= kChunksPerSide)
            continue;
        const int index = lx + lz * kChunksPerSide;
        if (readBE32(out.constData() + 4 * index) == 0)
            continue;
        if (before.compressionType(lx, lz) & 0x80) {
            const int cx = before.pos_.x * kChunksPerSide + lx;
            const int cz = before.pos_.z * kChunksPerSide + lz;
            externalFiles << QFileInfo(path).absolutePath() + QStringLiteral("/c.%1.%2.mcc").arg(cx).arg(cz);
        }
        writeBE32(out.data() + 4 * index, 0);
        writeBE32(out.data() + kSector + 4 * index, 0);
        ++deleted;
    }
    if (deleted == 0)
        return 0;

    for (int i = 0; i < kChunksPerSide * kChunksPerSide; ++i) {
        const uint32_t loc = readBE32(out.constData() + 4 * i);
        if (loc == 0)
            continue;
        if (loc != readBE32(old.constData() + 4 * i)
            || readBE32(old.constData() + kSector + 4 * i) != readBE32(out.constData() + kSector + 4 * i))
            throw std::runtime_error(Tr::tr("Check failed: the table of another chunk changed. The file was not "
                                            "written.").toStdString());
        const size_t from = size_t(loc >> 8) * kSector;
        const size_t to = std::min(from + size_t(loc & 0xFF) * kSector, size_t(old.size()));
        if (from < to && std::memcmp(old.constData() + from, out.constData() + from, to - from) != 0)
            throw std::runtime_error(Tr::tr("Check failed: the data of another chunk was touched. The file was not "
                                            "written.").toStdString());
    }

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        throw std::runtime_error((Tr::tr("Cannot open the region for writing: ") + f.errorString()).toStdString());
    f.write(out);
    if (!f.commit())
        throw std::runtime_error(
            (Tr::tr("Cannot write the region (does the game or the server hold the file?): ") + f.errorString())
                .toStdString());
    for (const QString& mcc : externalFiles)
        QFile::remove(mcc);
    return deleted;
}

uint32_t RegionFile::location(int localX, int localZ) const
{
    if (data_.isEmpty() || localX < 0 || localZ < 0 || localX >= kChunksPerSide || localZ >= kChunksPerSide)
        return 0;
    return readBE32(data_.constData() + 4 * (localX + localZ * kChunksPerSide));
}

bool RegionFile::hasChunk(int localX, int localZ) const
{
    return location(localX, localZ) != 0;
}

uint32_t RegionFile::timestamp(int localX, int localZ) const
{
    if (data_.isEmpty())
        return 0;
    return readBE32(data_.constData() + kSector + 4 * (localX + localZ * kChunksPerSide));
}

std::vector<uint8_t> RegionFile::chunkData(int localX, int localZ) const
{
    const uint32_t loc = location(localX, localZ);
    if (loc == 0)
        throw std::runtime_error(Tr::tr("The chunk is not saved").toStdString());
    const size_t offset = size_t(loc >> 8) * kSector;
    if (offset < kHeader || offset + 5 > size_t(data_.size()))
        throw std::runtime_error(Tr::tr("The chunk is outside the region file").toStdString());

    const uint32_t length = readBE32(data_.constData() + offset);
    const int type = uint8_t(data_[qsizetype(offset + 4)]);
    if (length == 0 || offset + 4 + length > size_t(data_.size()))
        throw std::runtime_error(Tr::tr("The chunk data is truncated").toStdString());

    if (type & 0x80) {
        const int cx = pos_.x * kChunksPerSide + localX;
        const int cz = pos_.z * kChunksPerSide + localZ;
        QFile ext(QFileInfo(path_).absolutePath() + QStringLiteral("/c.%1.%2.mcc").arg(cx).arg(cz));
        if (!ext.open(QIODevice::ReadOnly))
            throw std::runtime_error(Tr::tr("The external chunk file is missing: ").toStdString() + ext.fileName().toStdString());
        const QByteArray bytes = ext.readAll();
        return decodeChunk({reinterpret_cast<const uint8_t*>(bytes.constData()), size_t(bytes.size())}, type & 0x7F);
    }

    const auto* payload = reinterpret_cast<const uint8_t*>(data_.constData() + offset + 5);
    return decodeChunk({payload, length - 1}, type);
}
}
