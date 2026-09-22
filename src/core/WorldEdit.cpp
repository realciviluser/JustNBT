#include "core/WorldEdit.h"

#include "core/Backup.h"
#include "core/BedrockDb.h"
#include "core/Region.h"

#include <QDir>
#include <QFileInfo>

#include <map>
#include <set>

namespace justnbt {
namespace {
int floorDiv32(int v)
{
    return v >= 0 ? v / 32 : -((-v + 31) / 32);
}

const char* const kJavaFolders[] = {"region", "entities", "poi"};

DeleteChunksResult deleteJavaChunks(const Dimension& dimension, const std::set<std::pair<int, int>>& chunks,
                                    const QString& backupSubdir, const std::function<bool(int, int)>& progress)
{
    std::map<std::pair<int, int>, std::vector<std::pair<int, int>>> byRegion;
    for (const auto& [cx, cz] : chunks)
        byRegion[{floorDiv32(cx), floorDiv32(cz)}].push_back({cx - floorDiv32(cx) * 32, cz - floorDiv32(cz) * 32});

    DeleteChunksResult result;
    const int total = int(byRegion.size()) * int(std::size(kJavaFolders));
    int done = 0;
    for (const auto& [region, localChunks] : byRegion) {
        for (const char* folder : kJavaFolders) {
            const QString path = dimension.path + QStringLiteral("/%1/r.%2.%3.mca").arg(folder).arg(region.first).arg(region.second);
            ++done;
            if (progress && !progress(done, total)) {
                result.cancelled = true;
                return result;
            }
            if (!QFileInfo::exists(path))
                continue;
            try {
                backupFile(path, backupSubdir + '/' + QString::fromLatin1(folder));
                const int deleted = RegionFile::deleteChunks(path, localChunks);
                if (deleted > 0) {
                    ++result.files;
                    if (QLatin1String(folder) == QLatin1String("region"))
                        result.chunks += deleted;
                }
            } catch (const std::exception& e) {
                result.error = QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), QString::fromUtf8(e.what()));
                return result;
            }
        }
    }
    return result;
}

DeleteChunksResult deleteBedrockChunks(const WorldInfo& world, int dimension,
                                       const std::set<std::pair<int, int>>& chunks, const QString& backupSubdir,
                                       const std::function<bool(int, int)>& progress)
{
    DeleteChunksResult result;
    const QString dbDir = world.path + QStringLiteral("/db");
    try {
        backupFolderOncePerSession(dbDir, backupSubdir);
        const auto db = BedrockDb::shared(dbDir);

        const int total = int(chunks.size());
        int done = 0;
        std::vector<BedrockDb::Change> changes;
        for (const auto& [cx, cz] : chunks) {
            {
                ++done;
                if (progress && !progress(done, total)) {
                    result.cancelled = true;
                    break;
                }
                std::string prefix;
                for (int v : {cx, cz})
                    for (int i = 0; i < 4; ++i)
                        prefix += char(uint32_t(v) >> (8 * i));
                if (dimension != 0)
                    for (int i = 0; i < 4; ++i)
                        prefix += char(uint32_t(dimension) >> (8 * i));

                bool existed = false;
                db->forEachWithPrefix(prefix, [&](std::string_view k, std::string_view) {
                    if (k.size() == prefix.size() + 1 || k.size() == prefix.size() + 2) {
                        changes.emplace_back(std::string(k), std::nullopt);
                        existed = true;
                    }
                    return true;
                });
                if (auto ids = db->get("digp" + prefix)) {
                    for (size_t i = 0; i + 8 <= ids->size(); i += 8)
                        changes.emplace_back("actorprefix" + ids->substr(i, 8), std::nullopt);
                    changes.emplace_back("digp" + prefix, std::nullopt);
                    existed = true;
                }
                if (existed)
                    ++result.chunks;
                if (changes.size() > 4096) {
                    db->apply(changes);
                    result.records += int(changes.size());
                    changes.clear();
                }
            }
            if (result.cancelled)
                break;
        }
        if (!changes.empty()) {
            db->apply(changes);
            result.records += int(changes.size());
        }
    } catch (const std::exception& e) {
        result.error = QString::fromUtf8(e.what());
    }
    return result;
}
}

namespace {
std::set<std::pair<int, int>> chunksOf(const std::vector<ChunkRange>& areas)
{
    std::set<std::pair<int, int>> chunks;
    for (const ChunkRange& r : areas)
        for (int cz = r.minZ; cz <= r.maxZ; ++cz)
            for (int cx = r.minX; cx <= r.maxX; ++cx)
                chunks.emplace(cx, cz);
    return chunks;
}
}

qint64 chunkCount(const std::vector<ChunkRange>& areas)
{
    return qint64(chunksOf(areas).size());
}

DeleteChunksResult deleteChunks(const WorldInfo& world, const Dimension& dimension,
                                const std::vector<ChunkRange>& areas, const QString& backupSubdir,
                                const std::function<bool(int, int)>& progress)
{
    const auto chunks = chunksOf(areas);
    if (world.edition == Edition::Bedrock)
        return deleteBedrockChunks(world, dimension.bedrockId, chunks, backupSubdir, progress);
    return deleteJavaChunks(dimension, chunks, backupSubdir, progress);
}
}
