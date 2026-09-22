#pragma once

#include "core/BlockColors.h"
#include "core/MapRender.h"
#include "core/Tr.h"

#include <algorithm>
#include <climits>
#include <string>
#include <unordered_map>
#include <vector>

namespace justnbt::detail {
struct PaletteEntry {
    BlockStyle style;
    uint16_t nameId = 0;
};

class BlockTable {
public:
    BlockTable(std::vector<std::string>& names, std::vector<std::string>& unknown) : names_(names), unknown_(unknown)
    {
    }

    PaletteEntry entry(const std::string& name, bool waterlogged)
    {
        std::string key = name;
        if (waterlogged)
            key += "#w";
        if (auto it = cache_.find(key); it != cache_.end())
            return it->second;

        PaletteEntry e;
        e.style = blockStyle(name, waterlogged);
        if (!e.style.known)
            unknown_.push_back(name);
        const std::string shown = e.style.water && waterlogged ? name + Tr::tr(" (under water)").toStdString() : name;
        auto id = ids_.find(shown);
        if (id == ids_.end()) {
            id = ids_.emplace(shown, uint16_t(std::min<size_t>(names_.size(), RenderedTile::kNoBlock - 1))).first;
            if (names_.size() < RenderedTile::kNoBlock - 1)
                names_.push_back(shown);
        }
        e.nameId = id->second;
        cache_.emplace(std::move(key), e);
        return e;
    }

private:
    std::vector<std::string>& names_;
    std::vector<std::string>& unknown_;
    std::unordered_map<std::string, uint16_t> ids_;
    std::unordered_map<std::string, PaletteEntry> cache_;
};

struct ColumnResult {
    uint32_t rgb = 0;
    int16_t height = RenderedTile::kNoHeight;
    uint16_t block = RenderedTile::kNoBlock;
    uint8_t waterDepth = 0;
};

class ColumnWalker {
public:
    bool feed(const PaletteEntry& e, int y)
    {
        if (e.style.transparent)
            return false;
        if (e.style.water) {
            if (waterTop_ == INT_MIN) {
                waterTop_ = y;
                r_.block = e.nameId;
                r_.rgb = e.style.rgb;
            }
            return ++depth_ >= 64;
        }
        if (waterTop_ == INT_MIN) {
            r_.rgb = e.style.rgb;
            r_.block = e.nameId;
            r_.height = int16_t(y);
        }
        return true;
    }

    ColumnResult result() const
    {
        ColumnResult r = r_;
        if (waterTop_ != INT_MIN) {
            r.height = int16_t(waterTop_);
            r.waterDepth = uint8_t(std::min(depth_, 255));
        }
        return r;
    }

private:
    ColumnResult r_;
    int waterTop_ = INT_MIN;
    int depth_ = 0;
};

inline int sectionTop(int baseY, const RenderOptions& options)
{
    if (baseY > options.yLimit)
        return -1;
    return int(std::min<int64_t>(15, int64_t(options.yLimit) - baseY));
}

void finishTile(const std::vector<ColumnResult>& columns, RenderedTile& tile);
}
