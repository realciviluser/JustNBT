#include "core/MapRender.h"

#include "core/ChunkFormat.h"
#include "core/Compression.h"
#include "core/MapRenderDetail.h"
#include "core/NbtIO.h"
#include "core/Region.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

#include <cstring>

namespace justnbt {
namespace {
constexpr quint32 kRenderVersion = 3;
constexpr int kTile = RenderedTile::kSize;

constexpr int64_t kNonSpanningDataVersion = 2527;

using detail::BlockTable;
using detail::ColumnResult;
using detail::ColumnWalker;
using detail::PaletteEntry;
using nbt::Tag;
using nbt::TagType;

struct Section {
    int y = 0;
    std::vector<PaletteEntry> palette;
    const std::vector<int64_t>* data = nullptr;
    int bits = 0;
    bool spanning = false;
    bool allTransparent = true;
    std::vector<uint16_t> indices;

    const PaletteEntry& at(int x, int ly, int z)
    {
        if (palette.size() == 1 || !data)
            return palette[0];
        if (indices.empty())
            decode();
        return palette[indices[size_t(ly * 256 + z * 16 + x)]];
    }

    void decode() { unpackBlockIndices(*data, bits, spanning, palette.size(), indices); }
};

int bitsFor(size_t n)
{
    int bits = 0;
    while ((size_t(1) << bits) < n)
        ++bits;
    return bits;
}

const Tag* childOfType(const Tag* t, const char* name, TagType type)
{
    const Tag* c = t ? t->child(name) : nullptr;
    return c && c->type == type ? c : nullptr;
}

bool drawChunk(const Tag& root, int px, int pz, BlockTable& table, const RenderOptions& options,
               std::vector<ColumnResult>& columns)
{
    const Tag* dv = root.child("DataVersion");
    const int64_t dataVersion = dv && nbt::isInteger(dv->type) ? dv->integer : 0;

    const Tag* base = &root;
    if (const Tag* level = childOfType(&root, "Level", TagType::Compound))
        base = level;

    if (const Tag* status = childOfType(base, "Status", TagType::String)) {
        std::string_view st = status->string;
        if (st.substr(0, 10) == "minecraft:")
            st.remove_prefix(10);
        if (st != "full" && st != "postprocessed" && st != "fullchunk" && st != "mobs_spawned")
            return true;
    }
    const Tag* sectionList = childOfType(base, "sections", TagType::List);
    if (!sectionList)
        sectionList = childOfType(base, "Sections", TagType::List);
    if (!sectionList)
        return true;

    std::vector<Section> sections;
    bool legacy = false;
    for (const auto& st : sectionList->children) {
        if (st->type != TagType::Compound)
            continue;
        const Tag* yTag = st->child("Y");
        if (!yTag || !nbt::isInteger(yTag->type))
            continue;

        const Tag* palette = nullptr;
        const Tag* data = nullptr;
        if (const Tag* states = childOfType(st.get(), "block_states", TagType::Compound)) {
            palette = childOfType(states, "palette", TagType::List);
            data = childOfType(states, "data", TagType::LongArray);
        } else {
            palette = childOfType(st.get(), "Palette", TagType::List);
            data = childOfType(st.get(), "BlockStates", TagType::LongArray);
        }
        if (!palette || palette->children.empty()) {
            legacy = legacy || st->child("Blocks");
            continue;
        }

        Section s;
        s.y = int(yTag->integer);
        s.palette.reserve(palette->children.size());
        for (const auto& p : palette->children) {
            const Tag* name = childOfType(p.get(), "Name", TagType::String);
            bool waterlogged = false;
            if (const Tag* props = childOfType(p.get(), "Properties", TagType::Compound))
                if (const Tag* w = childOfType(props, "waterlogged", TagType::String))
                    waterlogged = w->string == "true";
            s.palette.push_back(table.entry(name ? name->string : std::string("minecraft:air"), waterlogged));
            s.allTransparent = s.allTransparent && s.palette.back().style.transparent;
        }
        if (s.allTransparent)
            continue;
        s.data = data ? &data->longs : nullptr;
        s.bits = std::max(4, bitsFor(s.palette.size()));
        s.spanning = dataVersion < kNonSpanningDataVersion;
        sections.push_back(std::move(s));
    }
    if (sections.empty())
        return !legacy;
    std::sort(sections.begin(), sections.end(), [](const Section& a, const Section& b) { return a.y > b.y; });

    for (int z = 0; z < 16; ++z) {
        for (int x = 0; x < 16; ++x) {
            ColumnWalker walker;
            bool done = false;
            for (Section& s : sections) {
                const int baseY = s.y * 16;
                for (int ly = detail::sectionTop(baseY, options); ly >= 0 && !done; --ly)
                    done = walker.feed(s.at(x, ly, z), baseY + ly);
                if (done)
                    break;
            }
            columns[size_t((pz + z) * kTile + px + x)] = walker.result();
        }
    }
    return true;
}

uint32_t shade(uint32_t rgb, int factor256)
{
    const uint32_t r = ((rgb >> 16) & 0xFF) * uint32_t(factor256) / 256;
    const uint32_t g = ((rgb >> 8) & 0xFF) * uint32_t(factor256) / 256;
    const uint32_t b = (rgb & 0xFF) * uint32_t(factor256) / 256;
    return 0xFF000000u | r << 16 | g << 8 | b;
}

QString cacheFilePath(const QString& key)
{
    const QString full = key + '|' + QString::number(kRenderVersion);
    const QString hash =
        QString::fromLatin1(QCryptographicHash::hash(full.toUtf8(), QCryptographicHash::Sha1).toHex());
    return tileCacheRoot() + '/' + hash.left(2) + '/' + hash + QStringLiteral(".jnt");
}
}

namespace detail {
void finishTile(const std::vector<ColumnResult>& columns, RenderedTile& tile)
{
    const size_t n = size_t(kTile) * kTile;
    tile.pixels.assign(n, 0);
    tile.heights.assign(n, RenderedTile::kNoHeight);
    tile.blocks.assign(n, RenderedTile::kNoBlock);
    for (int z = 0; z < kTile; ++z) {
        for (int x = 0; x < kTile; ++x) {
            const size_t i = size_t(z * kTile + x);
            const ColumnResult& c = columns[i];
            if (c.height == RenderedTile::kNoHeight)
                continue;
            tile.heights[i] = c.height;
            tile.blocks[i] = c.block;
            int factor;
            if (c.waterDepth > 0) {
                factor = std::max(120, 256 - int(c.waterDepth) * 9);
            } else {
                const ColumnResult& north = z > 0 ? columns[i - kTile] : c;
                const int nh = north.height == RenderedTile::kNoHeight ? c.height : north.height;
                factor = c.height > nh ? 256 : c.height == nh ? 220 : 180;
            }
            tile.pixels[i] = shade(c.rgb, factor);
        }
    }
}
}

std::shared_ptr<RenderedTile> renderRegion(const QString& regionPath, const RenderOptions& options)
{
    auto tile = std::make_shared<RenderedTile>();
    std::vector<ColumnResult> columns(size_t(kTile) * kTile);

    try {
        const RegionFile region(regionPath);
        BlockTable table(tile->names, tile->unknownBlocks);
        for (int lz = 0; lz < RegionFile::kChunksPerSide; ++lz) {
            for (int lx = 0; lx < RegionFile::kChunksPerSide; ++lx) {
                if (!region.hasChunk(lx, lz))
                    continue;
                try {
                    const auto data = region.chunkData(lx, lz);
                    const auto root = nbt::read(data, nbt::Endian::Big);
                    if (drawChunk(*root, lx * 16, lz * 16, table, options, columns))
                        ++tile->chunks;
                    else
                        ++tile->legacyChunks;
                } catch (const std::exception& e) {
                    if (tile->failedChunks++ == 0)
                        tile->firstError = e.what();
                }
            }
        }
    } catch (const std::exception& e) {
        tile->firstError = e.what();
    }

    detail::finishTile(columns, *tile);
    return tile;
}

QString tileCacheRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/tiles");
}

std::shared_ptr<RenderedTile> loadCachedTile(const QString& key)
{
    QFile f(cacheFilePath(key));
    if (!f.open(QIODevice::ReadOnly))
        return nullptr;
    QDataStream in(&f);
    in.setVersion(QDataStream::Qt_6_0);

    quint32 magic = 0, version = 0;
    in >> magic >> version;
    if (magic != 0x4A4E544C || version != kRenderVersion)
        return nullptr;

    auto tile = std::make_shared<RenderedTile>();
    qint32 chunks = 0, failed = 0, legacy = 0;
    QStringList names;
    QString error;
    QByteArray packed;
    in >> chunks >> failed >> legacy >> names >> error >> packed;
    if (in.status() != QDataStream::Ok)
        return nullptr;

    const size_t n = size_t(kTile) * kTile;
    std::vector<uint8_t> raw;
    try {
        raw = decompress({reinterpret_cast<const uint8_t*>(packed.constData()), size_t(packed.size())},
                         Compression::Zlib);
    } catch (const std::exception&) {
        return nullptr;
    }
    if (raw.size() != n * (4 + 2 + 2))
        return nullptr;

    tile->chunks = chunks;
    tile->failedChunks = failed;
    tile->legacyChunks = legacy;
    tile->firstError = error.toStdString();
    for (const QString& s : names)
        tile->names.push_back(s.toStdString());
    tile->pixels.resize(n);
    tile->heights.resize(n);
    tile->blocks.resize(n);
    std::memcpy(tile->pixels.data(), raw.data(), n * 4);
    std::memcpy(tile->heights.data(), raw.data() + n * 4, n * 2);
    std::memcpy(tile->blocks.data(), raw.data() + n * 6, n * 2);
    return tile;
}

void saveCachedTile(const QString& key, const RenderedTile& tile)
{
    const size_t n = size_t(kTile) * kTile;
    if (tile.pixels.size() != n)
        return;
    std::vector<uint8_t> raw(n * 8);
    std::memcpy(raw.data(), tile.pixels.data(), n * 4);
    std::memcpy(raw.data() + n * 4, tile.heights.data(), n * 2);
    std::memcpy(raw.data() + n * 6, tile.blocks.data(), n * 2);
    const auto packed = compress(raw, Compression::Zlib, 3);

    const QString path = cacheFilePath(key);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return;
    QDataStream out(&f);
    out.setVersion(QDataStream::Qt_6_0);
    QStringList names;
    for (const auto& s : tile.names)
        names << QString::fromStdString(s);
    out << quint32(0x4A4E544C) << kRenderVersion << qint32(tile.chunks) << qint32(tile.failedChunks)
        << qint32(tile.legacyChunks) << names << QString::fromStdString(tile.firstError)
        << QByteArray(reinterpret_cast<const char*>(packed.data()), qsizetype(packed.size()));
    f.commit();
}

namespace {
class JavaTileSource : public TileSource {
public:
    explicit JavaTileSource(QString regionDir) : dir_(std::move(regionDir)) {}

    std::vector<TilePos> listTiles() const override
    {
        std::vector<TilePos> tiles;
        for (const QFileInfo& fi : QDir(dir_).entryInfoList({QStringLiteral("r.*.mca")}, QDir::Files)) {
            if (fi.size() == 0)
                continue;
            if (auto pos = parseRegionFileName(fi.fileName()))
                tiles.push_back({pos->x, pos->z});
        }
        return tiles;
    }

    std::shared_ptr<RenderedTile> tile(TilePos pos, const RenderOptions& options) const override
    {
        const QFileInfo fi(dir_ + QStringLiteral("/r.%1.%2.mca").arg(pos.x).arg(pos.z));
        const QString key = QStringLiteral("java|%1|%2|%3|%4")
                                .arg(fi.absoluteFilePath())
                                .arg(fi.size())
                                .arg(fi.lastModified().toMSecsSinceEpoch())
                                .arg(options.yLimit);
        if (auto cached = loadCachedTile(key))
            return cached;
        auto tile = renderRegion(fi.absoluteFilePath(), options);
        saveCachedTile(key, *tile);
        return tile;
    }

private:
    QString dir_;
};
}

std::shared_ptr<TileSource> makeJavaTileSource(const QString& regionDir)
{
    return std::make_shared<JavaTileSource>(regionDir);
}
}
