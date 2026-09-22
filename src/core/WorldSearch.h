#pragma once

#include "core/WorldEdit.h"
#include "core/Worlds.h"

#include <QString>
#include <QStringList>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace justnbt {
enum class SearchKind {
    Block,
    Item,
    Entity,
    Poi,
};

struct SearchQuery {
    SearchKind kind = SearchKind::Block;
    QStringList patterns;
    std::vector<ChunkRange> areas;
    int maxHits = 10000;
};

enum class SearchSource { Region, Entities, Poi, Bedrock };

struct SearchHit {
    int x = 0, y = 0, z = 0;
    std::string id;
    std::string subject;
    int count = 0;
    QString name;
    SearchSource source = SearchSource::Region;
};

struct SearchResult {
    std::vector<SearchHit> hits;
    bool truncated = false;
    bool cancelled = false;
    int chunks = 0;
    int failedChunks = 0;
    int legacyChunks = 0;
    QString error;
};

SearchResult searchWorld(const WorldInfo& world, const Dimension& dimension, const SearchQuery& query,
                         const std::function<bool(int done, int total)>& progress = {});

bool idMatches(std::string_view id, const QStringList& patterns);

bool searchSupported(const WorldInfo& world, SearchKind kind);
}
