#include "core/BedrockDb.h"
#include "core/MapRender.h"
#include "core/MapRenderDetail.h"
#include "core/NbtIO.h"
#include "core/Tr.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMutex>

#include <cstring>
#include <map>
#include <stdexcept>

namespace justnbt {
namespace {
using detail::BlockTable;
using detail::ColumnResult;
using detail::ColumnWalker;
using detail::PaletteEntry;

constexpr int kTile = RenderedTile::kSize;
constexpr uint8_t kTagVersion = 44;
constexpr uint8_t kTagLegacyVersion = 118;
constexpr uint8_t kTagSubChunk = 47;
constexpr uint8_t kTagLegacyTerrain = 48;
constexpr uint8_t kTagFinalizedState = 54;

int32_t le32(const char* p)
{
    const auto* u = reinterpret_cast<const uint8_t*>(p);
    return int32_t(uint32_t(u[0]) | uint32_t(u[1]) << 8 | uint32_t(u[2]) << 16 | uint32_t(u[3]) << 24);
}

void putLE32(std::string& s, int32_t v)
{
    for (int i = 0; i < 4; ++i)
        s += char(uint32_t(v) >> (8 * i));
}

std::string chunkPrefix(int cx, int cz, int dimension)
{
    std::string k;
    putLE32(k, cx);
    putLE32(k, cz);
    if (dimension != 0)
        putLE32(k, dimension);
    return k;
}

int floorDiv32(int v)
{
    return v >= 0 ? v / 32 : -((-v + 31) / 32);
}

quint64 packPos(int x, int z)
{
    return (quint64(quint32(x)) << 32) | quint32(z);
}

std::string javaName(std::string_view name, const nbt::Tag* states)
{
    if (name.substr(0, 10) == "minecraft:")
        name.remove_prefix(10);

    static const std::unordered_map<std::string_view, std::string_view> coloured{
        {"wool", "wool"}, {"carpet", "carpet"}, {"concrete", "concrete"}, {"concrete_powder", "concrete_powder"},
        {"concretePowder", "concrete_powder"}, {"stained_glass", "stained_glass"},
        {"stained_glass_pane", "stained_glass_pane"}, {"stained_hardened_clay", "terracotta"},
        {"shulker_box", "shulker_box"},
    };
    if (auto it = coloured.find(name); it != coloured.end()) {
        std::string colour = "white";
        if (states)
            if (const nbt::Tag* c = states->child("color"); c && c->type == nbt::TagType::String)
                colour = c->string == "silver" ? "light_gray" : c->string;
        return colour + "_" + std::string(it->second);
    }

    static const std::unordered_map<std::string_view, std::string_view> renamed{
        {"grass", "grass_block"}, {"flowing_water", "water"}, {"flowing_lava", "lava"}, {"snow_layer", "snow"},
        {"tallgrass", "short_grass"}, {"waterlily", "lily_pad"}, {"reeds", "sugar_cane"}, {"red_flower", "poppy"},
        {"yellow_flower", "dandelion"}, {"double_plant", "tall_grass"}, {"hardened_clay", "terracotta"},
        {"leaves", "oak_leaves"}, {"leaves2", "acacia_leaves"}, {"log", "oak_log"}, {"log2", "acacia_log"},
        {"planks", "oak_planks"}, {"wood", "oak_wood"}, {"stonebrick", "stone_bricks"}, {"brick_block", "bricks"},
        {"red_nether_brick", "red_nether_bricks"}, {"nether_brick", "nether_bricks"}, {"lit_pumpkin", "jack_o_lantern"},
        {"melon_block", "melon"}, {"web", "cobweb"}, {"deadbush", "dead_bush"}, {"slime", "slime_block"},
        {"mob_spawner", "spawner"}, {"end_bricks", "end_stone_bricks"}, {"magma", "magma_block"},
        {"monster_egg", "stone"}, {"invisible_bedrock", "barrier"}, {"snow", "snow_block"},
        {"coral_block", "tube_coral_block"}, {"seaLantern", "sea_lantern"}, {"quartz_ore", "nether_quartz_ore"},
        {"lit_redstone_ore", "redstone_ore"}, {"sapling", "oak_sapling"}, {"stone_slab", "smooth_stone_slab"},
        {"wooden_slab", "oak_slab"}, {"double_wooden_slab", "oak_slab"}, {"red_mushroom_block", "red_mushroom_block"},
        {"brown_mushroom_block", "brown_mushroom_block"}, {"frame", "item_frame"}, {"underwater_torch", "torch"},
        {"colored_torch_rg", "torch"}, {"colored_torch_bp", "torch"}, {"unlit_redstone_torch", "redstone_torch"},
        {"standing_banner", "white_banner"}, {"wall_banner", "white_banner"}, {"standing_sign", "oak_sign"},
        {"wall_sign", "oak_sign"}, {"bed", "red_bed"}, {"skull", "skeleton_skull"}, {"golden_rail", "powered_rail"},
        {"trip_wire", "tripwire"}, {"unpowered_repeater", "repeater"}, {"powered_repeater", "repeater"},
        {"unpowered_comparator", "comparator"}, {"powered_comparator", "comparator"}, {"stone_button", "stone_button"},
        {"wooden_button", "oak_button"}, {"lit_furnace", "furnace"}, {"lit_blast_furnace", "blast_furnace"},
        {"lit_smoker", "smoker"}, {"element_0", "barrier"}, {"camera", "barrier"}, {"allow", "barrier"},
        {"deny", "barrier"}, {"border_block", "barrier"}, {"structure_block", "barrier"}, {"light_block", "light"},
    };
    if (auto it = renamed.find(name); it != renamed.end())
        return std::string(it->second);
    if (name.substr(0, 12) == "light_block_")
        return "light";
    return std::string(name);
}

struct Storage {
    std::vector<PaletteEntry> palette;
    std::vector<uint16_t> indices;
    bool allTransparent = true;
    bool anyWater = false;

    const PaletteEntry& at(int x, int y, int z) const
    {
        return palette[indices.empty() ? 0 : indices[size_t((x << 8) | (z << 4) | y)]];
    }
};

struct SubChunk {
    int y = 0;
    Storage blocks;
    Storage liquid;
};

void readStorage(const char*& p, const char* end, BlockTable& table, Storage& out)
{
    auto need = [&](size_t n) {
        if (size_t(end - p) < n)
            throw std::runtime_error(Tr::tr("The sub-chunk is truncated").toStdString());
    };
    need(1);
    const int header = uint8_t(*p++);
    if (header & 1)
        throw std::runtime_error(Tr::tr("Sub-chunk with network block ids").toStdString());
    const int bits = header >> 1;

    const char* words = nullptr;
    int perWord = 0;
    if (bits != 0) {
        if (bits != 1 && bits != 2 && bits != 3 && bits != 4 && bits != 5 && bits != 6 && bits != 8 && bits != 16)
            throw std::runtime_error(Tr::tr("Unknown sub-chunk packing: ").toStdString() + std::to_string(bits) + Tr::tr(" bits").toStdString());
        perWord = 32 / bits;
        const size_t wordCount = size_t((4096 + perWord - 1) / perWord);
        need(wordCount * 4);
        words = p;
        p += wordCount * 4;
    }

    size_t paletteSize = 1;
    if (!(bits == 0 && p < end && uint8_t(*p) == 0x0A)) {
        need(4);
        paletteSize = size_t(uint32_t(le32(p)));
        p += 4;
    }
    if (paletteSize == 0 || paletteSize > 4096)
        throw std::runtime_error(Tr::tr("Bad sub-chunk palette size").toStdString());

    out.palette.reserve(paletteSize);
    for (size_t i = 0; i < paletteSize; ++i) {
        size_t used = 0;
        const auto tag = nbt::read({reinterpret_cast<const uint8_t*>(p), size_t(end - p)}, nbt::Endian::Little, &used);
        p += used;
        const nbt::Tag* name = tag->child("name");
        const std::string n = name && name->type == nbt::TagType::String ? javaName(name->string, tag->child("states"))
                                                                          : std::string("air");
        out.palette.push_back(table.entry(n, false));
        out.allTransparent = out.allTransparent && out.palette.back().style.transparent;
        out.anyWater = out.anyWater || out.palette.back().style.water;
    }

    if (words && paletteSize > 1) {
        out.indices.resize(4096);
        const uint32_t mask = (uint32_t(1) << bits) - 1;
        for (int i = 0; i < 4096; ++i) {
            const uint32_t word = uint32_t(le32(words + 4 * (i / perWord)));
            const uint32_t v = (word >> ((i % perWord) * bits)) & mask;
            out.indices[size_t(i)] = v < paletteSize ? uint16_t(v) : 0;
        }
    }
}

bool readSubChunk(std::string_view value, int yFromKey, BlockTable& table, SubChunk& out)
{
    const char* p = value.data();
    const char* end = p + value.size();
    if (p == end)
        throw std::runtime_error(Tr::tr("Empty sub-chunk").toStdString());
    const int version = uint8_t(*p++);
    int storages = 1;
    out.y = yFromKey;
    if (version == 8 || version == 9) {
        if (p == end)
            throw std::runtime_error(Tr::tr("The sub-chunk is truncated").toStdString());
        storages = uint8_t(*p++);
        if (version == 9) {
            if (p == end)
                throw std::runtime_error(Tr::tr("The sub-chunk is truncated").toStdString());
            out.y = int8_t(*p++);
        }
    } else if (version != 1) {
        return false;
    }
    for (int s = 0; s < storages; ++s) {
        Storage storage;
        readStorage(p, end, table, storage);
        if (s == 0)
            out.blocks = std::move(storage);
        else if (s == 1)
            out.liquid = std::move(storage);
    }
    return true;
}

struct BedrockIndex {
    QHash<quint64, std::vector<uint16_t>> tiles[3];
};

std::shared_ptr<BedrockIndex> buildIndex(const BedrockDb& db)
{
    auto index = std::make_shared<BedrockIndex>();
    db.forEachWithPrefix("", [&](std::string_view k, std::string_view) {
        int dim = -1;
        if (k.size() == 9 && (uint8_t(k[8]) == kTagVersion || uint8_t(k[8]) == kTagLegacyVersion))
            dim = 0;
        else if (k.size() == 13 && (uint8_t(k[12]) == kTagVersion || uint8_t(k[12]) == kTagLegacyVersion))
            dim = le32(k.data() + 8);
        if (dim < 0 || dim > 2)
            return true;
        const int cx = le32(k.data()), cz = le32(k.data() + 4);
        const int tx = floorDiv32(cx), tz = floorDiv32(cz);
        index->tiles[dim][packPos(tx, tz)].push_back(uint16_t((cz - tz * 32) * 32 + (cx - tx * 32)));
        return true;
    });
    return index;
}

QString dbSignature(const QString& dbDir)
{
    QCryptographicHash hash(QCryptographicHash::Sha1);
    for (const QFileInfo& fi : QDir(dbDir).entryInfoList(QDir::Files, QDir::Name)) {
        hash.addData(fi.fileName().toUtf8());
        hash.addData(QByteArray::number(fi.size()));
        hash.addData(QByteArray::number(fi.lastModified().toMSecsSinceEpoch()));
    }
    return QString::fromLatin1(hash.result().toHex().left(16));
}

class BedrockTileSource : public TileSource {
public:
    BedrockTileSource(const QString& worldPath, int dimension)
        : dbDir_(QDir(worldPath).absoluteFilePath(QStringLiteral("db"))), dimension_(dimension)
    {
        db_ = BedrockDb::shared(dbDir_);
        signature_ = dbSignature(dbDir_);
    }

    std::vector<TilePos> listTiles() const override
    {
        std::vector<TilePos> tiles;
        const auto idx = index();
        for (auto it = idx->tiles[dimension_].cbegin(); it != idx->tiles[dimension_].cend(); ++it)
            tiles.push_back({int(qint32(it.key() >> 32)), int(qint32(it.key() & 0xFFFFFFFFu))});
        return tiles;
    }

    std::shared_ptr<RenderedTile> tile(TilePos pos, const RenderOptions& options) const override
    {
        const QString key = QStringLiteral("bedrock|%1|%2|%3|%4|%5|%6")
                                .arg(dbDir_, signature_)
                                .arg(dimension_)
                                .arg(pos.x)
                                .arg(pos.z)
                                .arg(options.yLimit);
        if (auto cached = loadCachedTile(key))
            return cached;
        auto t = render(pos, options);
        saveCachedTile(key, *t);
        return t;
    }

private:
    std::shared_ptr<BedrockIndex> index() const
    {
        static QMutex mutex;
        static QHash<QString, std::shared_ptr<BedrockIndex>> cache;
        QMutexLocker lock(&mutex);
        const QString key = dbDir_ + '|' + signature_;
        if (auto it = cache.find(key); it != cache.end())
            return *it;
        auto idx = buildIndex(*db_);
        cache.insert(key, idx);
        return idx;
    }

    std::shared_ptr<RenderedTile> render(TilePos pos, const RenderOptions& options) const
    {
        auto tile = std::make_shared<RenderedTile>();
        std::vector<ColumnResult> columns(size_t(kTile) * kTile);
        BlockTable table(tile->names, tile->unknownBlocks);

        const auto idx = index();
        const auto chunks = idx->tiles[dimension_].value(packPos(pos.x, pos.z));
        for (uint16_t local : chunks) {
            const int lx = local % 32, lz = local / 32;
            try {
                if (drawChunk(pos.x * 32 + lx, pos.z * 32 + lz, lx * 16, lz * 16, table, options, columns))
                    ++tile->chunks;
                else
                    ++tile->legacyChunks;
            } catch (const std::exception& e) {
                if (tile->failedChunks++ == 0)
                    tile->firstError = e.what();
            }
        }
        detail::finishTile(columns, *tile);
        return tile;
    }

    bool drawChunk(int cx, int cz, int px, int pz, BlockTable& table, const RenderOptions& options,
                   std::vector<ColumnResult>& columns) const
    {
        const std::string prefix = chunkPrefix(cx, cz, dimension_);
        std::vector<SubChunk> subs;
        bool legacy = false;
        int finalized = 2;
        db_->forEachWithPrefix(prefix, [&](std::string_view k, std::string_view v) {
            if (k.size() == prefix.size() + 2 && uint8_t(k[prefix.size()]) == kTagSubChunk) {
                SubChunk sub;
                if (readSubChunk(v, int8_t(k.back()), table, sub)) {
                    if (!sub.blocks.allTransparent || sub.liquid.anyWater)
                        subs.push_back(std::move(sub));
                } else {
                    legacy = true;
                }
            } else if (k.size() == prefix.size() + 1) {
                const uint8_t tag = uint8_t(k[prefix.size()]);
                if (tag == kTagLegacyTerrain)
                    legacy = true;
                else if (tag == kTagFinalizedState && v.size() >= 4)
                    finalized = le32(v.data());
            }
            return true;
        });
        if (finalized != 2)
            return true;
        if (subs.empty())
            return !legacy;
        std::sort(subs.begin(), subs.end(), [](const SubChunk& a, const SubChunk& b) { return a.y > b.y; });

        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                ColumnWalker walker;
                bool done = false;
                for (const SubChunk& s : subs) {
                    const int baseY = s.y * 16;
                    const bool hasLiquid = s.liquid.anyWater;
                    for (int ly = detail::sectionTop(baseY, options); ly >= 0 && !done; --ly) {
                        if (hasLiquid) {
                            const PaletteEntry& l = s.liquid.at(x, ly, z);
                            if (l.style.water) {
                                done = walker.feed(l, baseY + ly);
                                continue;
                            }
                        }
                        done = walker.feed(s.blocks.at(x, ly, z), baseY + ly);
                    }
                    if (done)
                        break;
                }
                columns[size_t((pz + z) * kTile + px + x)] = walker.result();
            }
        }
        return true;
    }

    QString dbDir_;
    int dimension_;
    std::shared_ptr<BedrockDb> db_;
    QString signature_;
};
}

std::shared_ptr<TileSource> makeBedrockTileSource(const QString& worldPath, int dimension)
{
    if (dimension < 0 || dimension > 2)
        throw std::runtime_error(Tr::tr("Unknown Bedrock dimension").toStdString());
    return std::make_shared<BedrockTileSource>(worldPath, dimension);
}
}
