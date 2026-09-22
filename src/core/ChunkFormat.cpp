#include "core/ChunkFormat.h"

#include <algorithm>

namespace justnbt {
namespace {
using nbt::Tag;
using nbt::TagType;

constexpr int64_t kNonSpanningDataVersion = 2527;

const Tag* childOfType(const Tag* t, const char* name, TagType type)
{
    const Tag* c = t ? t->child(name) : nullptr;
    return c && c->type == type ? c : nullptr;
}

int bitsFor(size_t n)
{
    int bits = 0;
    while ((size_t(1) << bits) < n)
        ++bits;
    return bits;
}
}

JavaChunkView viewJavaChunk(const Tag& root)
{
    JavaChunkView view;
    const Tag* dv = root.child("DataVersion");
    view.dataVersion = dv && nbt::isInteger(dv->type) ? dv->integer : 0;

    const Tag* base = &root;
    if (const Tag* level = childOfType(&root, "Level", TagType::Compound))
        base = level;

    view.blockEntities = childOfType(base, "block_entities", TagType::List);
    if (!view.blockEntities)
        view.blockEntities = childOfType(base, "TileEntities", TagType::List);
    view.entities = childOfType(base, "Entities", TagType::List);

    const Tag* sectionList = childOfType(base, "sections", TagType::List);
    if (!sectionList)
        sectionList = childOfType(base, "Sections", TagType::List);
    if (!sectionList)
        return view;
    for (const auto& st : sectionList->children) {
        if (st->type != TagType::Compound)
            continue;
        const Tag* yTag = st->child("Y");
        if (!yTag || !nbt::isInteger(yTag->type))
            continue;
        JavaSection s;
        s.y = int(yTag->integer);
        const Tag* data = nullptr;
        if (const Tag* states = childOfType(st.get(), "block_states", TagType::Compound)) {
            s.palette = childOfType(states, "palette", TagType::List);
            data = childOfType(states, "data", TagType::LongArray);
        } else {
            s.palette = childOfType(st.get(), "Palette", TagType::List);
            data = childOfType(st.get(), "BlockStates", TagType::LongArray);
        }
        if (!s.palette || s.palette->children.empty()) {
            if (st->child("Blocks"))
                view.legacy = true;
            continue;
        }
        s.data = data ? &data->longs : nullptr;
        view.sections.push_back(s);
    }
    return view;
}

void unpackBlockIndices(const std::vector<int64_t>& longs, int bits, bool spanning, size_t paletteSize,
                        std::vector<uint16_t>& out)
{
    out.assign(4096, 0);
    if (bits <= 0 || bits > 16)
        return;
    const uint64_t mask = (uint64_t(1) << bits) - 1;
    if (!spanning) {
        const int perLong = 64 / bits;
        for (int i = 0; i < 4096; ++i) {
            const size_t li = size_t(i / perLong);
            if (li >= longs.size())
                break;
            const uint64_t v = (uint64_t(longs[li]) >> ((i % perLong) * bits)) & mask;
            out[size_t(i)] = v < paletteSize ? uint16_t(v) : 0;
        }
    } else {
        for (int i = 0; i < 4096; ++i) {
            const size_t bit = size_t(i) * size_t(bits);
            const size_t li = bit >> 6;
            const int off = int(bit & 63);
            if (li >= longs.size())
                break;
            uint64_t v = uint64_t(longs[li]) >> off;
            if (off + bits > 64 && li + 1 < longs.size())
                v |= uint64_t(longs[li + 1]) << (64 - off);
            v &= mask;
            out[size_t(i)] = v < paletteSize ? uint16_t(v) : 0;
        }
    }
}

void unpackSection(const JavaSection& section, int64_t dataVersion, std::vector<uint16_t>& out)
{
    const size_t paletteSize = section.palette->children.size();
    if (paletteSize <= 1 || !section.data) {
        out.assign(4096, 0);
        return;
    }
    unpackBlockIndices(*section.data, std::max(4, bitsFor(paletteSize)), dataVersion < kNonSpanningDataVersion,
                       paletteSize, out);
}

std::string_view paletteName(const Tag& entry)
{
    for (const char* key : {"Name", "name"}) {
        const Tag* n = entry.child(key);
        if (n && n->type == TagType::String)
            return n->string;
    }
    return {};
}
}
