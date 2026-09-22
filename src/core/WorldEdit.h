#pragma once

#include "core/Worlds.h"

#include <functional>
#include <vector>

namespace justnbt {
struct ChunkRange {
    int minX = 0, minZ = 0, maxX = 0, maxZ = 0;

    qint64 chunkCount() const { return qint64(maxX - minX + 1) * (maxZ - minZ + 1); }
    bool contains(int x, int z) const { return x >= minX && x <= maxX && z >= minZ && z <= maxZ; }
};

struct DeleteChunksResult {
    int chunks = 0;
    int files = 0;
    int records = 0;
    bool cancelled = false;
    QString error;
};

DeleteChunksResult deleteChunks(const WorldInfo& world, const Dimension& dimension,
                                const std::vector<ChunkRange>& areas, const QString& backupSubdir,
                                const std::function<bool(int done, int total)>& progress = {});

qint64 chunkCount(const std::vector<ChunkRange>& areas);
}
