#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace nbt {
enum class TagType : uint8_t {
    End = 0,
    Byte = 1,
    Short = 2,
    Int = 3,
    Long = 4,
    Float = 5,
    Double = 6,
    ByteArray = 7,
    String = 8,
    List = 9,
    Compound = 10,
    IntArray = 11,
    LongArray = 12,
};

constexpr bool isValidType(uint8_t t) { return t <= 12; }
constexpr bool isInteger(TagType t) { return t >= TagType::Byte && t <= TagType::Long; }
constexpr bool isFloating(TagType t) { return t == TagType::Float || t == TagType::Double; }
constexpr bool isArray(TagType t)
{
    return t == TagType::ByteArray || t == TagType::IntArray || t == TagType::LongArray;
}
constexpr bool isContainer(TagType t) { return t == TagType::List || t == TagType::Compound; }

const char* typeName(TagType t);

struct Tag {
    TagType type = TagType::End;
    std::string name;
    Tag* parent = nullptr;

    int64_t integer = 0;
    double floating = 0.0;
    std::string string;
    std::vector<int8_t> bytes;
    std::vector<int32_t> ints;
    std::vector<int64_t> longs;
    TagType listType = TagType::End;
    std::vector<std::unique_ptr<Tag>> children;

    explicit Tag(TagType t = TagType::End, std::string n = {}) : type(t), name(std::move(n)) {}

    size_t arraySize() const;
    Tag* child(std::string_view childName) const;
    int indexOf(const Tag* c) const;
    Tag* insert(size_t index, std::unique_ptr<Tag> c);
    Tag* append(std::unique_ptr<Tag> c) { return insert(children.size(), std::move(c)); }
    std::unique_ptr<Tag> take(size_t index);
    std::unique_ptr<Tag> clone() const;
};
}
