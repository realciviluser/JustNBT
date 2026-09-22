#pragma once

#include "core/Nbt.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace justnbt {
struct JavaSection {
    int y = 0;
    const nbt::Tag* palette = nullptr;
    const std::vector<int64_t>* data = nullptr;
};

struct JavaChunkView {
    int64_t dataVersion = 0;
    std::vector<JavaSection> sections;
    bool legacy = false;
    const nbt::Tag* blockEntities = nullptr;
    const nbt::Tag* entities = nullptr;
};

JavaChunkView viewJavaChunk(const nbt::Tag& root);

void unpackBlockIndices(const std::vector<int64_t>& longs, int bits, bool spanning, size_t paletteSize,
                        std::vector<uint16_t>& out);

void unpackSection(const JavaSection& section, int64_t dataVersion, std::vector<uint16_t>& out);

std::string_view paletteName(const nbt::Tag& entry);
}
