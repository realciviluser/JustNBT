#include "core/NbtFile.h"
#include "core/Tr.h"

#include "core/Backup.h"
#include "core/BedrockDb.h"
#include "core/Region.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>

namespace justnbt {
namespace {
int32_t readLE32(const uint8_t* p)
{
    return int32_t(uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24);
}

void writeLE32(uint8_t* p, int32_t v)
{
    const auto u = uint32_t(v);
    for (int i = 0; i < 4; ++i)
        p[i] = uint8_t(u >> (8 * i));
}

std::unique_ptr<nbt::Tag> tryParse(std::span<const uint8_t> data, nbt::Endian endian)
{
    try {
        size_t used = 0;
        auto root = nbt::read(data, endian, &used);
        if (used == data.size())
            return root;
    } catch (const nbt::ParseError&) {
    }
    return nullptr;
}
}

QString keyToText(const QByteArray& key)
{
    QString s;
    for (char c : key) {
        const auto b = uint8_t(c);
        if (b >= 0x20 && b < 0x7F)
            s += QChar(b);
        else
            s += QStringLiteral("\\x%1").arg(b, 2, 16, QChar('0'));
    }
    return s;
}

QString describe(const NbtFormat& f)
{
    QString s = f.bedrockHeader ? QStringLiteral("Bedrock level.dat (v%1)").arg(f.bedrockVersion)
        : f.endian == nbt::Endian::Big ? QStringLiteral("Java")
                                       : QStringLiteral("Bedrock");
    s += f.endian == nbt::Endian::Big ? QStringLiteral(" · big-endian") : QStringLiteral(" · little-endian");
    switch (f.compression) {
    case Compression::Gzip: s += QStringLiteral(" · gzip"); break;
    case Compression::Zlib: s += QStringLiteral(" · zlib"); break;
    case Compression::Deflate: s += QStringLiteral(" · deflate"); break;
    case Compression::None: s += Tr::tr(" · no compression"); break;
    }
    return s;
}

std::unique_ptr<nbt::Tag> NbtFile::parse(std::span<const uint8_t> data, NbtFormat& format)
{
    format = {};

    const Compression c = detectCompression(data);
    if (c != Compression::None) {
        const auto raw = decompress(data, c);
        format.compression = c;
        for (auto e : {nbt::Endian::Big, nbt::Endian::Little}) {
            if (auto t = tryParse(raw, e)) {
                format.endian = e;
                return t;
            }
        }
        throw std::runtime_error(Tr::tr("The compressed file does not hold NBT").toStdString());
    }

    format.compression = Compression::None;

    if (data.size() >= 8) {
        const int32_t version = readLE32(data.data());
        const int32_t length = readLE32(data.data() + 4);
        if (length >= 0 && size_t(length) == data.size() - 8) {
            if (auto t = tryParse(data.subspan(8), nbt::Endian::Little)) {
                format.endian = nbt::Endian::Little;
                format.bedrockHeader = true;
                format.bedrockVersion = version;
                return t;
            }
        }
    }

    for (auto e : {nbt::Endian::Big, nbt::Endian::Little}) {
        if (auto t = tryParse(data, e)) {
            format.endian = e;
            return t;
        }
    }
    throw std::runtime_error(Tr::tr("The file was not recognised as NBT").toStdString());
}

std::unique_ptr<NbtFile> NbtFile::load(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        throw std::runtime_error((Tr::tr("Cannot open the file: ") + f.errorString()).toStdString());
    const QByteArray bytes = f.readAll();

    auto file = std::make_unique<NbtFile>();
    file->path = path;
    file->root = parse({reinterpret_cast<const uint8_t*>(bytes.constData()), size_t(bytes.size())}, file->format);
    return file;
}

std::unique_ptr<NbtFile> NbtFile::loadDbRecord(const QString& dbDir, const QByteArray& key)
{
    const auto db = BedrockDb::shared(dbDir);
    const auto value = db->get({key.constData(), size_t(key.size())});
    if (!value)
        throw std::runtime_error(Tr::tr("The record was not found in the world database").toStdString());

    auto file = std::make_unique<NbtFile>();
    file->storage = Storage::DbRecord;
    file->path = dbDir;
    file->dbKey = key;
    file->format = NbtFormat{nbt::Endian::Little, Compression::None, false, 0};
    file->root = tryParse({reinterpret_cast<const uint8_t*>(value->data()), value->size()}, nbt::Endian::Little);
    if (!file->root)
        throw std::runtime_error(Tr::tr("The database record is not NBT").toStdString());
    return file;
}

namespace {
int floorDiv32(int v)
{
    return v >= 0 ? v / 32 : -((-v + 31) / 32);
}

QString compressionName(int type)
{
    switch (type & 0x7F) {
    case 1: return QStringLiteral("gzip");
    case 2: return QStringLiteral("zlib");
    case 3: return Tr::tr("no compression");
    case 4: return QStringLiteral("LZ4");
    default: return QStringLiteral("?");
    }
}

constexpr char kTagBlockEntity = 49;
constexpr char kTagEntityLegacy = 50;

std::string chunkKey(int cx, int cz, int dimension)
{
    std::string k;
    for (int v : {cx, cz})
        for (int i = 0; i < 4; ++i)
            k += char(uint32_t(v) >> (8 * i));
    if (dimension != 0)
        for (int i = 0; i < 4; ++i)
            k += char(uint32_t(dimension) >> (8 * i));
    return k;
}

constexpr char kTagSubChunk = 47;

int32_t le32(const uint8_t* p)
{
    return int32_t(uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24);
}

void putLE32(std::string& s, uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        s += char(v >> (8 * i));
}

std::unique_ptr<nbt::Tag> subChunkToTag(const std::string& value, int yFromKey)
{
    const auto* p = reinterpret_cast<const uint8_t*>(value.data());
    const size_t n = value.size();
    size_t pos = 0;
    auto need = [&](size_t k) {
        if (n - pos < k)
            throw std::runtime_error(Tr::tr("The sub-chunk is truncated").toStdString());
    };
    need(1);
    const int version = p[pos++];
    int layers = 1;
    int y = yFromKey;
    if (version == 8 || version == 9) {
        need(1);
        layers = p[pos++];
        if (version == 9) {
            need(1);
            y = int8_t(p[pos++]);
        }
    } else if (version != 1) {
        return nullptr;
    }

    auto section = std::make_unique<nbt::Tag>(nbt::TagType::Compound, "");
    auto yTag = std::make_unique<nbt::Tag>(nbt::TagType::Byte, "Y");
    yTag->integer = y;
    section->append(std::move(yTag));
    auto layerList = std::make_unique<nbt::Tag>(nbt::TagType::List, "layers");
    layerList->listType = nbt::TagType::Compound;

    for (int l = 0; l < layers; ++l) {
        need(1);
        const int header = p[pos++];
        if (header & 1)
            throw std::runtime_error(Tr::tr("Sub-chunk with network block ids").toStdString());
        const int bits = header >> 1;
        const uint8_t* words = nullptr;
        int perWord = 0;
        if (bits != 0) {
            if (bits != 1 && bits != 2 && bits != 3 && bits != 4 && bits != 5 && bits != 6 && bits != 8 && bits != 16)
                throw std::runtime_error(Tr::tr("Unknown sub-chunk packing").toStdString());
            perWord = 32 / bits;
            const size_t wordCount = size_t((4096 + perWord - 1) / perWord);
            need(wordCount * 4);
            words = p + pos;
            pos += wordCount * 4;
        }
        size_t paletteSize = 1;
        if (!(bits == 0 && pos < n && p[pos] == 0x0A)) {
            need(4);
            paletteSize = size_t(uint32_t(le32(p + pos)));
            pos += 4;
        }
        if (paletteSize == 0 || paletteSize > 4096)
            throw std::runtime_error(Tr::tr("Bad sub-chunk palette size").toStdString());

        auto layer = std::make_unique<nbt::Tag>(nbt::TagType::Compound, "");
        auto palette = std::make_unique<nbt::Tag>(nbt::TagType::List, "palette");
        palette->listType = nbt::TagType::Compound;
        for (size_t i = 0; i < paletteSize; ++i) {
            size_t used = 0;
            palette->append(nbt::read({p + pos, n - pos}, nbt::Endian::Little, &used));
            pos += used;
        }
        layer->append(std::move(palette));
        if (paletteSize > 1 && words) {
            auto data = std::make_unique<nbt::Tag>(nbt::TagType::IntArray, "data");
            data->ints.resize(4096);
            const uint32_t mask = (uint32_t(1) << bits) - 1;
            for (int i = 0; i < 4096; ++i) {
                const uint32_t word = uint32_t(le32(words + 4 * (i / perWord)));
                data->ints[size_t(i)] = int32_t((word >> ((i % perWord) * bits)) & mask);
            }
            layer->append(std::move(data));
        }
        layerList->append(std::move(layer));
    }
    section->append(std::move(layerList));
    return section;
}

auto tagToSubChunk(const nbt::Tag& section) -> std::string
{
    const nbt::Tag* yTag = section.child("Y");
    const nbt::Tag* layers = section.child("layers");
    if (!yTag || !nbt::isInteger(yTag->type) || yTag->integer < -128 || yTag->integer > 127)
        throw std::runtime_error(Tr::tr("A section needs a Y between -128 and 127").toStdString());
    if (!layers || layers->type != nbt::TagType::List || layers->children.empty() || layers->children.size() > 255)
        throw std::runtime_error(Tr::tr("Section Y=").toStdString() + std::to_string(yTag->integer) + Tr::tr(" needs a non-empty layers list").toStdString());

    std::string out;
    out += char(9);
    out += char(layers->children.size());
    out += char(int8_t(yTag->integer));
    for (const auto& layer : layers->children) {
        const nbt::Tag* palette = layer->type == nbt::TagType::Compound ? layer->child("palette") : nullptr;
        const nbt::Tag* data = layer->type == nbt::TagType::Compound ? layer->child("data") : nullptr;
        const std::string where = Tr::tr("section Y=").toStdString() + std::to_string(yTag->integer) + ": ";
        if (!palette || palette->type != nbt::TagType::List || palette->children.empty()
            || palette->listType != nbt::TagType::Compound)
            throw std::runtime_error(where + Tr::tr("palette has to be a non-empty list of compound tags").toStdString());
        const size_t size = palette->children.size();
        if (size > 4096)
            throw std::runtime_error(where + Tr::tr("the palette holds more than 4096 entries").toStdString());
        if (data && (data->type != nbt::TagType::IntArray || data->ints.size() != 4096))
            throw std::runtime_error(where + Tr::tr("data has to be an IntArray of 4096 numbers").toStdString());
        if (!data && size > 1)
            throw std::runtime_error(where + Tr::tr("without data the palette may hold only one entry").toStdString());
        if (data)
            for (int32_t v : data->ints)
                if (v < 0 || size_t(v) >= size)
                    throw std::runtime_error(where + Tr::tr("index ").toStdString() + std::to_string(v) + Tr::tr(" is outside the palette").toStdString());

        int bits = 1;
        for (int b : {1, 2, 3, 4, 5, 6, 8, 16}) {
            bits = b;
            if ((size_t(1) << b) >= size)
                break;
        }
        out += char(bits << 1);
        const int perWord = 32 / bits;
        const size_t wordCount = size_t((4096 + perWord - 1) / perWord);
        std::vector<uint32_t> words(wordCount, 0);
        if (data)
            for (int i = 0; i < 4096; ++i)
                words[size_t(i / perWord)] |= uint32_t(data->ints[size_t(i)]) << ((i % perWord) * bits);
        for (uint32_t w : words)
            putLE32(out, w);
        putLE32(out, uint32_t(size));
        for (const auto& entry : palette->children) {
            std::vector<uint8_t> nbtBytes;
            nbt::write(*entry, nbt::Endian::Little, nbtBytes);
            out.append(reinterpret_cast<const char*>(nbtBytes.data()), nbtBytes.size());
        }
    }
    return out;
}
}

std::unique_ptr<nbt::Tag> decodeBedrockSubChunk(const std::string& value, int yFromKey)
{
    return subChunkToTag(value, yFromKey);
}

std::string encodeBedrockSubChunk(const nbt::Tag& section)
{
    return tagToSubChunk(section);
}

namespace {
void readConcatenated(const std::string& value, nbt::Tag& list)
{
    const auto* p = reinterpret_cast<const uint8_t*>(value.data());
    size_t pos = 0;
    while (pos < value.size()) {
        size_t used = 0;
        auto t = nbt::read({p + pos, value.size() - pos}, nbt::Endian::Little, &used);
        pos += used;
        list.append(std::move(t));
    }
}

std::string writeConcatenated(const nbt::Tag& list)
{
    std::vector<uint8_t> out;
    for (const auto& c : list.children)
        nbt::write(*c, nbt::Endian::Little, out);
    return {reinterpret_cast<const char*>(out.data()), out.size()};
}

std::string serialize(const nbt::Tag& t)
{
    std::vector<uint8_t> out;
    nbt::write(t, nbt::Endian::Little, out);
    return {reinterpret_cast<const char*>(out.data()), out.size()};
}
}

std::unique_ptr<NbtFile> NbtFile::loadRegionChunk(const QString& regionPath, int chunkX, int chunkZ)
{
    const RegionFile region(regionPath);
    const int lx = chunkX - floorDiv32(chunkX) * 32, lz = chunkZ - floorDiv32(chunkZ) * 32;
    if (floorDiv32(chunkX) != region.position().x || floorDiv32(chunkZ) != region.position().z)
        throw std::runtime_error(Tr::tr("The chunk does not belong to this region file").toStdString());
    if (!region.hasChunk(lx, lz))
        throw std::runtime_error(Tr::tr("This chunk is not stored in the region file").toStdString());
    const auto data = region.chunkData(lx, lz);

    auto file = std::make_unique<NbtFile>();
    file->storage = Storage::RegionChunk;
    file->path = regionPath;
    file->chunkX = chunkX;
    file->chunkZ = chunkZ;
    file->regionCompression = region.compressionType(lx, lz);
    file->format = NbtFormat{nbt::Endian::Big, Compression::None, false, 0};
    file->root = tryParse(data, nbt::Endian::Big);
    if (!file->root)
        throw std::runtime_error(Tr::tr("The chunk data is not NBT").toStdString());
    return file;
}

std::unique_ptr<NbtFile> NbtFile::loadBedrockChunk(const QString& dbDir, int dimension, int chunkX, int chunkZ)
{
    const auto db = BedrockDb::shared(dbDir);
    const std::string prefix = chunkKey(chunkX, chunkZ, dimension);

    auto file = std::make_unique<NbtFile>();
    file->storage = Storage::BedrockChunk;
    file->path = dbDir;
    file->dimension = dimension;
    file->chunkX = chunkX;
    file->chunkZ = chunkZ;
    file->format = NbtFormat{nbt::Endian::Little, Compression::None, false, 0};
    file->root = std::make_unique<nbt::Tag>(nbt::TagType::Compound, "");

    auto blockEntities = std::make_unique<nbt::Tag>(nbt::TagType::List, "BlockEntities");
    blockEntities->listType = nbt::TagType::Compound;
    if (auto v = db->get(prefix + kTagBlockEntity))
        readConcatenated(*v, *blockEntities);

    auto entities = std::make_unique<nbt::Tag>(nbt::TagType::List, "Entities");
    entities->listType = nbt::TagType::Compound;
    if (auto ids = db->get("digp" + prefix)) {
        file->bedrockDigpEntities_ = true;
        for (size_t i = 0; i + 8 <= ids->size(); i += 8) {
            const std::string id = ids->substr(i, 8);
            const auto actor = db->get("actorprefix" + id);
            if (!actor)
                continue;
            size_t used = 0;
            auto t = nbt::read({reinterpret_cast<const uint8_t*>(actor->data()), actor->size()}, nbt::Endian::Little,
                               &used);
            file->bedrockEntityIds_.emplace_back(entities->append(std::move(t)), id);
        }
    } else if (auto v = db->get(prefix + kTagEntityLegacy)) {
        readConcatenated(*v, *entities);
    } else {
        file->bedrockDigpEntities_ = true;
    }

    auto sections = std::make_unique<nbt::Tag>(nbt::TagType::List, "Sections");
    sections->listType = nbt::TagType::Compound;
    std::vector<std::pair<int, std::unique_ptr<nbt::Tag>>> subs;
    db->forEachWithPrefix(prefix, [&](std::string_view k, std::string_view v) {
        if (k.size() == prefix.size() + 2 && k[prefix.size()] == kTagSubChunk) {
            const int y = int8_t(k.back());
            if (auto t = subChunkToTag(std::string(v), y))
                subs.emplace_back(y, std::move(t));
        }
        return true;
    });
    std::sort(subs.begin(), subs.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (auto& [y, t] : subs) {
        file->bedrockSections_.emplace_back(y, serialize(*t));
        sections->append(std::move(t));
    }

    file->root->append(std::move(sections));
    file->root->append(std::move(blockEntities));
    file->root->append(std::move(entities));
    return file;
}

std::unique_ptr<NbtFile> NbtFile::reloaded() const
{
    switch (storage) {
    case Storage::DbRecord: return loadDbRecord(path, dbKey);
    case Storage::RegionChunk: return loadRegionChunk(path, chunkX, chunkZ);
    case Storage::BedrockChunk: return loadBedrockChunk(path, dimension, chunkX, chunkZ);
    case Storage::File: break;
    }
    return load(path);
}

QString NbtFile::chunkIdentity(const QString& path, int dimension, int chunkX, int chunkZ)
{
    return QFileInfo(path).absoluteFilePath() + QStringLiteral("#chunk:%1:%2,%3").arg(dimension).arg(chunkX).arg(chunkZ);
}

QString NbtFile::identity() const
{
    const QString p = QFileInfo(path).absoluteFilePath();
    switch (storage) {
    case Storage::DbRecord: return p + QStringLiteral("#") + keyToText(dbKey);
    case Storage::RegionChunk: return chunkIdentity(path, 0, chunkX, chunkZ);
    case Storage::BedrockChunk: return chunkIdentity(path, dimension, chunkX, chunkZ);
    case Storage::File: break;
    }
    return p;
}

QString NbtFile::displayName() const
{
    switch (storage) {
    case Storage::DbRecord: return keyToText(dbKey);
    case Storage::RegionChunk:
    case Storage::BedrockChunk: return Tr::tr("Chunk %1, %2").arg(chunkX).arg(chunkZ);
    case Storage::File: break;
    }
    return QFileInfo(path).fileName();
}

QString NbtFile::sourceText() const
{
    switch (storage) {
    case Storage::DbRecord:
        return Tr::tr("Bedrock LevelDB · record %1   ·   %2")
            .arg(keyToText(dbKey), QDir::toNativeSeparators(path));
    case Storage::RegionChunk: {
        QString s = Tr::tr("Java · chunk %1, %2 in %3 · %4")
                        .arg(chunkX)
                        .arg(chunkZ)
                        .arg(QFileInfo(path).fileName(), compressionName(regionCompression));
        if ((regionCompression & 0x7F) == 4)
            s += Tr::tr(" (will be saved as zlib)");
        return s + QStringLiteral("   ·   ") + QDir::toNativeSeparators(path);
    }
    case Storage::BedrockChunk:
        return Tr::tr("Bedrock · chunk %1, %2 (dimension %3) · in data the block index is x*256 + z*16 + y   ·   %4")
            .arg(chunkX)
            .arg(chunkZ)
            .arg(dimension)
            .arg(QDir::toNativeSeparators(path));
    case Storage::File: break;
    }
    return describe(format) + QStringLiteral("   ·   ") + QDir::toNativeSeparators(path);
}

std::vector<uint8_t> NbtFile::encode(bool compressed) const
{
    std::vector<uint8_t> body;
    if (format.bedrockHeader)
        body.resize(8);
    nbt::write(*root, format.endian, body);
    if (format.bedrockHeader) {
        writeLE32(body.data(), format.bedrockVersion);
        writeLE32(body.data() + 4, int32_t(body.size() - 8));
    }
    if (compressed && format.compression != Compression::None)
        return compress(body, format.compression);
    return body;
}

QString NbtFile::save(const QString& backupSubdir)
{
    switch (storage) {
    case Storage::DbRecord: {
        const std::vector<uint8_t> data = encode(false);
        const QString backup = backupFolderOncePerSession(path, backupSubdir);
        BedrockDb::shared(path)->put({dbKey.constData(), size_t(dbKey.size())},
                                     {reinterpret_cast<const char*>(data.data()), data.size()});
        return backup;
    }
    case Storage::RegionChunk: {
        const std::vector<uint8_t> data = encode(false);
        const QString backup = backupFile(path, backupSubdir);
        const int lx = chunkX - floorDiv32(chunkX) * 32, lz = chunkZ - floorDiv32(chunkZ) * 32;
        RegionFile::writeChunk(path, lx, lz, data, regionCompression & 0x7F);
        if ((regionCompression & 0x7F) == 4)
            regionCompression = 2;
        return backup;
    }
    case Storage::BedrockChunk:
        return saveBedrockChunk(backupSubdir);
    case Storage::File: break;
    }

    const std::vector<uint8_t> data = encode(true);
    const QString backup = QFile::exists(path) ? backupFile(path, backupSubdir) : QString();
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly))
        throw std::runtime_error((Tr::tr("Cannot open the file for writing: ") + out.errorString()).toStdString());
    out.write(reinterpret_cast<const char*>(data.data()), qint64(data.size()));
    if (!out.commit())
        throw std::runtime_error((Tr::tr("Cannot write the file: ") + out.errorString()).toStdString());
    return backup;
}

QString NbtFile::saveBedrockChunk(const QString& backupSubdir)
{
    const nbt::Tag* sections = root->child("Sections");
    const nbt::Tag* blockEntities = root->child("BlockEntities");
    const nbt::Tag* entities = root->child("Entities");
    auto listOfCompounds = [](const nbt::Tag* t) {
        return t && t->type == nbt::TagType::List
            && (t->children.empty() || t->listType == nbt::TagType::Compound);
    };
    if (!listOfCompounds(sections) || !listOfCompounds(blockEntities) || !listOfCompounds(entities)
        || root->children.size() != 3)
        throw std::runtime_error(Tr::tr("The root of a Bedrock chunk may only hold the lists Sections, "
                                        "BlockEntities and Entities (lists of compound tags).")
                                     .toStdString());

    const std::string prefix = chunkKey(chunkX, chunkZ, dimension);
    std::vector<BedrockDb::Change> changes;

    std::vector<std::pair<int, std::string>> keptSections;
    for (const auto& s : sections->children) {
        const nbt::Tag* yTag = s->child("Y");
        if (!yTag || !nbt::isInteger(yTag->type))
            throw std::runtime_error(Tr::tr("Every section needs a numeric Y").toStdString());
        const int y = int(yTag->integer);
        if (std::any_of(keptSections.begin(), keptSections.end(), [&](const auto& k) { return k.first == y; }))
            throw std::runtime_error(Tr::tr("Two sections with the same Y=").toStdString() + std::to_string(y));
        const std::string encoded = tagToSubChunk(*s);
        const std::string nbtNow = serialize(*s);
        const auto old = std::find_if(bedrockSections_.begin(), bedrockSections_.end(),
                                      [&](const auto& o) { return o.first == y; });
        if (old == bedrockSections_.end() || old->second != nbtNow)
            changes.emplace_back(prefix + kTagSubChunk + char(int8_t(y)), encoded);
        keptSections.emplace_back(y, nbtNow);
    }
    for (const auto& [y, nbtBytes] : bedrockSections_)
        if (std::none_of(keptSections.begin(), keptSections.end(), [&](const auto& k) { return k.first == y; }))
            changes.emplace_back(prefix + kTagSubChunk + char(int8_t(y)), std::nullopt);

    const std::string be = writeConcatenated(*blockEntities);
    changes.emplace_back(prefix + kTagBlockEntity, be.empty() ? std::nullopt : std::optional<std::string>(be));

    std::vector<std::pair<const nbt::Tag*, std::string>> keptIds;
    if (bedrockDigpEntities_) {
        std::string digp;
        for (const auto& c : entities->children) {
            auto it = std::find_if(bedrockEntityIds_.begin(), bedrockEntityIds_.end(),
                                   [&](const auto& e) { return e.first == c.get(); });
            if (it == bedrockEntityIds_.end())
                throw std::runtime_error(Tr::tr("New entities cannot be added to a Bedrock chunk yet: the existing "
                                                "ones can only be changed or deleted.")
                                             .toStdString());
            changes.emplace_back("actorprefix" + it->second, serialize(*c));
            digp += it->second;
            keptIds.push_back(*it);
        }
        for (const auto& [tag, id] : bedrockEntityIds_) {
            const bool kept = std::any_of(keptIds.begin(), keptIds.end(), [&](const auto& k) { return k.second == id; });
            if (!kept)
                changes.emplace_back("actorprefix" + id, std::nullopt);
        }
        changes.emplace_back("digp" + prefix, digp.empty() ? std::nullopt : std::optional<std::string>(digp));
    } else {
        const std::string e = writeConcatenated(*entities);
        changes.emplace_back(prefix + kTagEntityLegacy, e.empty() ? std::nullopt : std::optional<std::string>(e));
    }

    const QString backup = backupFolderOncePerSession(path, backupSubdir);
    BedrockDb::shared(path)->apply(changes);
    if (bedrockDigpEntities_)
        bedrockEntityIds_ = std::move(keptIds);
    bedrockSections_ = std::move(keptSections);
    return backup;
}
}
