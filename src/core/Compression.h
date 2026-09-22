#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace justnbt {
enum class Compression { None, Gzip, Zlib, Deflate };

Compression detectCompression(std::span<const uint8_t> data);

std::vector<uint8_t> decompress(std::span<const uint8_t> data, Compression c);
std::vector<uint8_t> compress(std::span<const uint8_t> data, Compression c, int level = 6);
}
