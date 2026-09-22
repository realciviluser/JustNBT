#pragma once

#include "core/Nbt.h"

#include <span>
#include <stdexcept>

namespace nbt {
enum class Endian { Big, Little };

struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

std::unique_ptr<Tag> read(std::span<const uint8_t> data, Endian endian, size_t* consumed = nullptr);
void write(const Tag& root, Endian endian, std::vector<uint8_t>& out);

std::string mutf8ToUtf8(std::string_view in);
std::string utf8ToMutf8(std::string_view in);
}
