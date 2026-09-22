// JustNBT: zlib / raw deflate block compression for Minecraft Bedrock worlds,
// implemented with libdeflate.

#ifndef STORAGE_LEVELDB_UTIL_MOJANG_ZLIB_H_
#define STORAGE_LEVELDB_UTIL_MOJANG_ZLIB_H_

#include <cstddef>
#include <string>

namespace leveldb {
namespace mojang {

// raw == true: raw deflate (type 4), otherwise zlib with header (type 2).
bool Compress(const char* input, size_t length, bool raw, std::string* output);
bool Uncompress(const char* input, size_t length, bool raw, std::string* output);

}  // namespace mojang
}  // namespace leveldb

#endif  // STORAGE_LEVELDB_UTIL_MOJANG_ZLIB_H_
