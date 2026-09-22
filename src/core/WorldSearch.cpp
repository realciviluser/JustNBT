#include "core/WorldSearch.h"

#include "core/BedrockDb.h"
#include "core/ChunkFormat.h"
#include "core/ItemInfo.h"
#include "core/NbtFile.h"
#include "core/NbtIO.h"
#include "core/Region.h"
#include "core/Tr.h"

#include <QDir>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QThreadPool>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <tuple>

namespace justnbt {
namespace {
using nbt::Tag;
using nbt::TagType;

constexpr uint8_t kTagVersion = 44;
constexpr uint8_t kTagLegacyVersion = 118;

std::string_view withoutNamespace(std::string_view id)
{
    if (id.substr(0, 10) == "minecraft:")
        id.remove_prefix(10);
    return id;
}

int32_t le32(const char* p)
{
    return int32_t(uint32_t(uint8_t(p[0])) | uint32_t(uint8_t(p[1])) << 8 | uint32_t(uint8_t(p[2])) << 16
                   | uint32_t(uint8_t(p[3])) << 24);
}

const Tag* child(const Tag& t, const char* name, TagType type)
{
    const Tag* c = t.child(name);
    return c && c->type == type ? c : nullptr;
}

std::string stringOf(const Tag& t, const char* name)
{
    const Tag* c = child(t, name, TagType::String);
    return c ? c->string : std::string();
}

bool intOf(const Tag& t, const char* name, int& out)
{
    const Tag* c = t.child(name);
    if (!c || !nbt::isInteger(c->type))
        return false;
    out = int(c->integer);
    return true;
}

bool inAreas(const std::vector<ChunkRange>& areas, int cx, int cz)
{
    if (areas.empty())
        return true;
    for (const ChunkRange& a : areas)
        if (a.contains(cx, cz))
            return true;
    return false;
}

bool regionTouchesAreas(const std::vector<ChunkRange>& areas, int rx, int rz)
{
    if (areas.empty())
        return true;
    for (const ChunkRange& a : areas)
        if (a.maxX >= rx * 32 && a.minX <= rx * 32 + 31 && a.maxZ >= rz * 32 && a.minZ <= rz * 32 + 31)
            return true;
    return false;
}

struct Collector {
    const SearchQuery& query;
    std::atomic<bool>& stop;
    QMutex mutex;
    std::vector<SearchHit> hits;
    bool truncated = false;
    std::atomic<int> chunks{0};
    std::atomic<int> failed{0};
    std::atomic<int> legacy{0};

    Collector(const SearchQuery& q, std::atomic<bool>& s) : query(q), stop(s) {}

    void add(std::vector<SearchHit>& found)
    {
        if (found.empty())
            return;
        QMutexLocker lock(&mutex);
        for (SearchHit& h : found) {
            if (int(hits.size()) >= query.maxHits) {
                truncated = true;
                stop = true;
                break;
            }
            hits.push_back(std::move(h));
        }
        found.clear();
    }
};

std::string entityId(const Tag& e)
{
    std::string id = stringOf(e, "id");
    return id.empty() ? stringOf(e, "identifier") : id;
}

QString customName(const Tag& t)
{
    const Tag* name = t.child("CustomName");
    return name ? textComponentText(*name) : QString();
}

std::string spawnerMob(const Tag& be)
{
    if (const Tag* data = child(be, "SpawnData", TagType::Compound)) {
        if (const Tag* e = child(*data, "entity", TagType::Compound))
            if (std::string id = stringOf(*e, "id"); !id.empty())
                return id;
        if (std::string id = stringOf(*data, "id"); !id.empty())
            return id;
    }
    return stringOf(be, "EntityIdentifier");
}

bool position(const Tag& e, int& x, int& y, int& z)
{
    const Tag* pos = child(e, "Pos", TagType::List);
    if (!pos || pos->children.size() != 3)
        return false;
    auto coord = [](const Tag& t) {
        return int(std::floor(nbt::isFloating(t.type) ? t.floating : double(t.integer)));
    };
    x = coord(*pos->children[0]);
    y = coord(*pos->children[1]);
    z = coord(*pos->children[2]);
    return true;
}

int countItems(const Tag& t, const QStringList& patterns, std::string& firstId)
{
    int total = 0;
    if (t.type == TagType::Compound) {
        if (const auto item = readItemStack(t)) {
            const std::string id = item->id.toStdString();
            if (idMatches(id, patterns)) {
                if (firstId.empty())
                    firstId = id;
                total += std::max(1, item->count);
            }
        }
    }
    for (const auto& c : t.children) {
        if (c->name == "Offers" || c->name == "SpawnData" || c->name == "SpawnPotentials")
            continue;
        total += countItems(*c, patterns, firstId);
    }
    return total;
}

void findItemsInBlockEntities(const Tag* list, const SearchQuery& q, SearchSource source,
                              std::vector<SearchHit>& out)
{
    if (!list)
        return;
    for (const auto& be : list->children) {
        if (be->type != TagType::Compound)
            continue;
        std::string first;
        const int n = countItems(*be, q.patterns, first);
        SearchHit h;
        if (n == 0 || !intOf(*be, "x", h.x) || !intOf(*be, "y", h.y) || !intOf(*be, "z", h.z))
            continue;
        h.id = stringOf(*be, "id");
        h.subject = first;
        h.count = n;
        h.name = customName(*be);
        h.source = source;
        out.push_back(std::move(h));
    }
}

void findInEntities(const Tag* list, const SearchQuery& q, SearchSource source, std::vector<SearchHit>& out)
{
    if (!list)
        return;
    for (const auto& e : list->children) {
        if (e->type != TagType::Compound)
            continue;
        SearchHit h;
        if (position(*e, h.x, h.y, h.z)) {
            h.id = entityId(*e);
            h.name = customName(*e);
            h.source = source;
            if (q.kind == SearchKind::Item) {
                std::string first;
                if (const int n = countItems(*e, q.patterns, first); n > 0) {
                    h.subject = first;
                    h.count = n;
                    out.push_back(h);
                }
            } else if (idMatches(h.id, q.patterns)) {
                if (const Tag* data = child(*e, "VillagerData", TagType::Compound))
                    h.subject = stringOf(*data, "profession");
                else if (const Tag* item = child(*e, "Item", TagType::Compound))
                    h.subject = entityId(*item).empty() ? stringOf(*item, "Name") : entityId(*item);
                out.push_back(h);
            }
        }
        if (q.kind == SearchKind::Entity)
            findInEntities(child(*e, "Passengers", TagType::List), q, source, out);
    }
}

void addBlockEntityDetails(const Tag* blockEntities, std::vector<SearchHit>& hits, size_t from)
{
    if (!blockEntities || from == hits.size())
        return;
    std::map<std::tuple<int, int, int>, const Tag*> at;
    for (const auto& be : blockEntities->children) {
        int x, y, z;
        if (be->type == TagType::Compound && intOf(*be, "x", x) && intOf(*be, "y", y) && intOf(*be, "z", z))
            at[{x, y, z}] = be.get();
    }
    for (size_t i = from; i < hits.size(); ++i) {
        const auto it = at.find({hits[i].x, hits[i].y, hits[i].z});
        if (it == at.end())
            continue;
        hits[i].subject = spawnerMob(*it->second);
        hits[i].name = customName(*it->second);
    }
}

void scanJavaChunk(const Tag& root, int cx, int cz, SearchSource source, Collector& c, std::vector<SearchHit>& out)
{
    const SearchQuery& q = c.query;
    if (source == SearchSource::Poi) {
        const Tag* sections = child(root, "Sections", TagType::Compound);
        if (!sections)
            return;
        for (const auto& section : sections->children) {
            const Tag* records = section->type == TagType::Compound ? child(*section, "Records", TagType::List)
                                                                     : nullptr;
            if (!records)
                continue;
            for (const auto& r : records->children) {
                if (r->type != TagType::Compound)
                    continue;
                const std::string type = stringOf(*r, "type");
                const Tag* pos = child(*r, "pos", TagType::IntArray);
                if (!pos || pos->ints.size() != 3 || !idMatches(type, q.patterns))
                    continue;
                SearchHit h;
                h.x = pos->ints[0];
                h.y = pos->ints[1];
                h.z = pos->ints[2];
                h.id = type;
                intOf(*r, "free_tickets", h.count);
                h.source = source;
                out.push_back(std::move(h));
            }
        }
        return;
    }
    if (source == SearchSource::Entities) {
        findInEntities(child(root, "Entities", TagType::List), q, source, out);
        return;
    }

    const JavaChunkView view = viewJavaChunk(root);
    if (q.kind == SearchKind::Item) {
        findItemsInBlockEntities(view.blockEntities, q, source, out);
        findInEntities(view.entities, q, source, out);
        return;
    }
    if (q.kind == SearchKind::Entity) {
        findInEntities(view.entities, q, source, out);
        return;
    }

    if (view.legacy && view.sections.empty())
        ++c.legacy;
    const size_t first = out.size();
    std::vector<uint16_t> indices;
    std::vector<char> wanted;
    for (const JavaSection& s : view.sections) {
        const auto& palette = s.palette->children;
        wanted.assign(palette.size(), 0);
        bool any = false;
        for (size_t i = 0; i < palette.size(); ++i) {
            if (palette[i]->type == TagType::Compound && idMatches(paletteName(*palette[i]), q.patterns)) {
                wanted[i] = 1;
                any = true;
            }
        }
        if (!any)
            continue;
        unpackSection(s, view.dataVersion, indices);
        for (int i = 0; i < 4096; ++i) {
            if (!wanted[indices[size_t(i)]])
                continue;
            SearchHit h;
            h.x = cx * 16 + (i & 15);
            h.z = cz * 16 + ((i >> 4) & 15);
            h.y = s.y * 16 + (i >> 8);
            h.id = std::string(paletteName(*palette[indices[size_t(i)]]));
            h.source = source;
            out.push_back(std::move(h));
            if (int(out.size()) >= q.maxHits)
                break;
        }
        if (int(out.size()) >= q.maxHits)
            break;
    }
    addBlockEntityDetails(view.blockEntities, out, first);
}

void scanRegion(const QString& path, SearchSource source, Collector& c)
{
    const auto pos = parseRegionFileName(QFileInfo(path).fileName());
    if (!pos)
        return;
    std::unique_ptr<RegionFile> file;
    try {
        file = std::make_unique<RegionFile>(path);
    } catch (const std::exception&) {
        ++c.failed;
        return;
    }
    std::vector<SearchHit> found;
    for (int lz = 0; lz < RegionFile::kChunksPerSide && !c.stop; ++lz) {
        for (int lx = 0; lx < RegionFile::kChunksPerSide && !c.stop; ++lx) {
            const int cx = pos->x * 32 + lx, cz = pos->z * 32 + lz;
            if (!file->hasChunk(lx, lz) || !inAreas(c.query.areas, cx, cz))
                continue;
            ++c.chunks;
            try {
                const auto data = file->chunkData(lx, lz);
                const auto root = nbt::read(data, nbt::Endian::Big);
                scanJavaChunk(*root, cx, cz, source, c, found);
            } catch (const std::exception&) {
                ++c.failed;
            }
            c.add(found);
        }
    }
}

QStringList regionFiles(const QString& folder, const std::vector<ChunkRange>& areas)
{
    QStringList files;
    const QDir dir(folder);
    for (const QString& name : dir.entryList({QStringLiteral("r.*.*.mca")}, QDir::Files)) {
        const auto pos = parseRegionFileName(name);
        if (pos && regionTouchesAreas(areas, pos->x, pos->z))
            files << dir.filePath(name);
    }
    return files;
}

void scanBedrockChunk(const QString& dbDir, int dimension, int cx, int cz, Collector& c,
                      std::vector<SearchHit>& out)
{
    const SearchQuery& q = c.query;
    const auto file = NbtFile::loadBedrockChunk(dbDir, dimension, cx, cz);
    const Tag& root = *file->root;
    const Tag* blockEntities = child(root, "BlockEntities", TagType::List);
    if (q.kind == SearchKind::Item) {
        findItemsInBlockEntities(blockEntities, q, SearchSource::Bedrock, out);
        findInEntities(child(root, "Entities", TagType::List), q, SearchSource::Bedrock, out);
        return;
    }
    if (q.kind == SearchKind::Entity) {
        findInEntities(child(root, "Entities", TagType::List), q, SearchSource::Bedrock, out);
        return;
    }

    const size_t first = out.size();
    const Tag* sections = child(root, "Sections", TagType::List);
    if (!sections)
        return;
    for (const auto& section : sections->children) {
        int sy = 0;
        const Tag* layers = child(*section, "layers", TagType::List);
        if (!intOf(*section, "Y", sy) || !layers)
            continue;
        std::vector<char> seen(4096, 0);
        for (const auto& layer : layers->children) {
            const Tag* palette = child(*layer, "palette", TagType::List);
            if (!palette)
                continue;
            std::vector<char> wanted(palette->children.size(), 0);
            bool any = false;
            for (size_t i = 0; i < palette->children.size(); ++i) {
                if (idMatches(paletteName(*palette->children[i]), q.patterns)) {
                    wanted[i] = 1;
                    any = true;
                }
            }
            if (!any)
                continue;
            const Tag* data = child(*layer, "data", TagType::IntArray);
            for (int i = 0; i < 4096; ++i) {
                const int index = data && size_t(i) < data->ints.size() ? data->ints[size_t(i)] : 0;
                if (index < 0 || size_t(index) >= wanted.size() || !wanted[size_t(index)] || seen[size_t(i)])
                    continue;
                seen[size_t(i)] = 1;
                SearchHit h;
                h.x = cx * 16 + (i >> 8);
                h.z = cz * 16 + ((i >> 4) & 15);
                h.y = sy * 16 + (i & 15);
                h.id = std::string(paletteName(*palette->children[size_t(index)]));
                h.source = SearchSource::Bedrock;
                out.push_back(std::move(h));
                if (int(out.size()) >= q.maxHits)
                    break;
            }
        }
        if (int(out.size()) >= q.maxHits)
            break;
    }
    addBlockEntityDetails(blockEntities, out, first);
}

std::vector<std::pair<int, int>> bedrockChunks(const BedrockDb& db, int dimension, const std::vector<ChunkRange>& areas)
{
    std::vector<std::pair<int, int>> chunks;
    db.forEachWithPrefix("", [&](std::string_view k, std::string_view) {
        int dim = -1;
        if (k.size() == 9 && (uint8_t(k[8]) == kTagVersion || uint8_t(k[8]) == kTagLegacyVersion))
            dim = 0;
        else if (k.size() == 13 && (uint8_t(k[12]) == kTagVersion || uint8_t(k[12]) == kTagLegacyVersion))
            dim = le32(k.data() + 8);
        if (dim != dimension)
            return true;
        const int cx = le32(k.data()), cz = le32(k.data() + 4);
        if (inAreas(areas, cx, cz))
            chunks.emplace_back(cx, cz);
        return true;
    });
    return chunks;
}

bool runJobs(const std::vector<std::function<void()>>& jobs, std::atomic<bool>& stop, std::atomic<int>& done,
             int total, const std::function<bool(int, int)>& progress)
{
    QThreadPool pool;
    pool.setMaxThreadCount(std::max(1, QThread::idealThreadCount()));
    for (const auto& job : jobs)
        pool.start([&job, &stop] {
            if (!stop)
                job();
        });
    bool cancelled = false;
    do {
        if (progress && !cancelled && !progress(done, total)) {
            cancelled = true;
            stop = true;
        }
    } while (!pool.waitForDone(100));
    if (progress && !cancelled)
        progress(total, total);
    return cancelled;
}
}

bool idMatches(std::string_view id, const QStringList& patterns)
{
    if (id.empty())
        return false;
    const std::string_view path = withoutNamespace(id);
    for (const QString& pattern : patterns) {
        const std::string p = pattern.trimmed().toLower().toStdString();
        if (p.empty())
            continue;
        if (p.find(':') != std::string::npos) {
            if (withoutNamespace(p) == path && (p.substr(0, 10) == "minecraft:" || p == id))
                return true;
        } else if (path.find(p) != std::string_view::npos) {
            return true;
        }
    }
    return false;
}

bool searchSupported(const WorldInfo& world, SearchKind kind)
{
    return !(world.edition == Edition::Bedrock && kind == SearchKind::Poi);
}

SearchResult searchWorld(const WorldInfo& world, const Dimension& dimension, const SearchQuery& query,
                         const std::function<bool(int, int)>& progress)
{
    SearchResult result;
    if (query.patterns.isEmpty())
        return result;
    if (!searchSupported(world, query.kind)) {
        result.error = Tr::tr("Bedrock worlds keep no points of interest.");
        return result;
    }

    std::atomic<bool> stop{false};
    std::atomic<int> done{0};
    Collector c(query, stop);
    std::vector<std::function<void()>> jobs;
    int total = 0;

    if (world.edition == Edition::Java) {
        std::vector<std::pair<QString, SearchSource>> folders;
        const QString entities = dimension.path + QStringLiteral("/entities");
        const bool entityFiles = !regionFiles(entities, {}).isEmpty();
        switch (query.kind) {
        case SearchKind::Block: folders = {{QStringLiteral("region"), SearchSource::Region}}; break;
        case SearchKind::Poi: folders = {{QStringLiteral("poi"), SearchSource::Poi}}; break;
        case SearchKind::Item:
            folders = {{QStringLiteral("region"), SearchSource::Region}};
            if (entityFiles)
                folders.push_back({QStringLiteral("entities"), SearchSource::Entities});
            break;
        case SearchKind::Entity:
            folders = {entityFiles ? std::pair{QStringLiteral("entities"), SearchSource::Entities}
                                   : std::pair{QStringLiteral("region"), SearchSource::Region}};
            break;
        }
        for (const auto& [folder, source] : folders) {
            for (const QString& path : regionFiles(dimension.path + QLatin1Char('/') + folder, query.areas)) {
                jobs.push_back([path, source = source, &c, &done] {
                    scanRegion(path, source, c);
                    ++done;
                });
            }
        }
        total = int(jobs.size());
    } else {
        const QString dbDir = world.path + QStringLiteral("/db");
        std::vector<std::pair<int, int>> chunks;
        try {
            chunks = bedrockChunks(*BedrockDb::shared(dbDir), dimension.bedrockId, query.areas);
        } catch (const std::exception& e) {
            result.error = QString::fromUtf8(e.what());
            return result;
        }
        constexpr size_t kBatch = 64;
        for (size_t from = 0; from < chunks.size(); from += kBatch) {
            std::vector<std::pair<int, int>> batch(chunks.begin() + ptrdiff_t(from),
                                                   chunks.begin() + ptrdiff_t(std::min(chunks.size(), from + kBatch)));
            const int dim = dimension.bedrockId;
            jobs.push_back([batch = std::move(batch), dbDir, dim, &c, &done] {
                std::vector<SearchHit> found;
                for (const auto& [cx, cz] : batch) {
                    if (c.stop)
                        break;
                    ++c.chunks;
                    try {
                        scanBedrockChunk(dbDir, dim, cx, cz, c, found);
                    } catch (const std::exception&) {
                        ++c.failed;
                    }
                    c.add(found);
                    ++done;
                }
            });
        }
        total = int(chunks.size());
    }

    result.cancelled = runJobs(jobs, stop, done, total, progress);
    result.hits = std::move(c.hits);
    result.truncated = c.truncated;
    result.chunks = c.chunks;
    result.failedChunks = c.failed;
    result.legacyChunks = c.legacy;
    std::sort(result.hits.begin(), result.hits.end(), [](const SearchHit& a, const SearchHit& b) {
        return std::tie(a.x, a.z, a.y) < std::tie(b.x, b.z, b.y);
    });
    return result;
}
}
