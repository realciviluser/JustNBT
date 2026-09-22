#pragma once

#include <QString>

#include <climits>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace justnbt {
struct RenderOptions {
    int yLimit = INT_MAX;
};

struct RenderedTile {
    static constexpr int kSize = 512;
    static constexpr int16_t kNoHeight = INT16_MIN;
    static constexpr uint16_t kNoBlock = 0xFFFF;

    std::vector<uint32_t> pixels;
    std::vector<int16_t> heights;
    std::vector<uint16_t> blocks;
    std::vector<std::string> names;
    int chunks = 0;
    int failedChunks = 0;
    int legacyChunks = 0;
    std::string firstError;
    std::vector<std::string> unknownBlocks;
};

std::shared_ptr<RenderedTile> renderRegion(const QString& regionPath, const RenderOptions& options);

std::shared_ptr<RenderedTile> loadCachedTile(const QString& key);
void saveCachedTile(const QString& key, const RenderedTile& tile);
QString tileCacheRoot();

struct TilePos {
    int x = 0;
    int z = 0;
};

class TileSource {
public:
    virtual ~TileSource() = default;
    virtual std::vector<TilePos> listTiles() const = 0;
    virtual std::shared_ptr<RenderedTile> tile(TilePos pos, const RenderOptions& options) const = 0;
};

std::shared_ptr<TileSource> makeJavaTileSource(const QString& regionDir);
std::shared_ptr<TileSource> makeBedrockTileSource(const QString& worldPath, int dimension);
}
