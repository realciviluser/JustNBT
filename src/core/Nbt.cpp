#include "core/Nbt.h"

namespace nbt {
const char* typeName(TagType t)
{
    switch (t) {
    case TagType::End: return "End";
    case TagType::Byte: return "Byte";
    case TagType::Short: return "Short";
    case TagType::Int: return "Int";
    case TagType::Long: return "Long";
    case TagType::Float: return "Float";
    case TagType::Double: return "Double";
    case TagType::ByteArray: return "ByteArray";
    case TagType::String: return "String";
    case TagType::List: return "List";
    case TagType::Compound: return "Compound";
    case TagType::IntArray: return "IntArray";
    case TagType::LongArray: return "LongArray";
    }
    return "?";
}

size_t Tag::arraySize() const
{
    switch (type) {
    case TagType::ByteArray: return bytes.size();
    case TagType::IntArray: return ints.size();
    case TagType::LongArray: return longs.size();
    default: return 0;
    }
}

Tag* Tag::child(std::string_view childName) const
{
    if (type != TagType::Compound)
        return nullptr;
    for (const auto& c : children)
        if (c->name == childName)
            return c.get();
    return nullptr;
}

int Tag::indexOf(const Tag* c) const
{
    for (size_t i = 0; i < children.size(); ++i)
        if (children[i].get() == c)
            return int(i);
    return -1;
}

Tag* Tag::insert(size_t index, std::unique_ptr<Tag> c)
{
    c->parent = this;
    if (type == TagType::List)
        c->name.clear();
    Tag* raw = c.get();
    children.insert(children.begin() + ptrdiff_t(index), std::move(c));
    return raw;
}

std::unique_ptr<Tag> Tag::take(size_t index)
{
    auto c = std::move(children[index]);
    children.erase(children.begin() + ptrdiff_t(index));
    c->parent = nullptr;
    return c;
}

std::unique_ptr<Tag> Tag::clone() const
{
    auto t = std::make_unique<Tag>(type, name);
    t->integer = integer;
    t->floating = floating;
    t->string = string;
    t->bytes = bytes;
    t->ints = ints;
    t->longs = longs;
    t->listType = listType;
    t->children.reserve(children.size());
    for (const auto& c : children)
        t->append(c->clone());
    return t;
}
}
