// JustNBT: see mojang_zlib.h

#include "util/mojang_zlib.h"

#include <libdeflate.h>

namespace leveldb {
namespace mojang {

bool Compress(const char* input, size_t length, bool raw, std::string* output) {
  // Level 6 matches zlib's default, which is what Bedrock uses.
  libdeflate_compressor* c = libdeflate_alloc_compressor(6);
  if (c == nullptr) return false;
  const size_t bound = raw ? libdeflate_deflate_compress_bound(c, length)
                           : libdeflate_zlib_compress_bound(c, length);
  output->resize(bound);
  const size_t size =
      raw ? libdeflate_deflate_compress(c, input, length, &(*output)[0], bound)
          : libdeflate_zlib_compress(c, input, length, &(*output)[0], bound);
  libdeflate_free_compressor(c);
  if (size == 0) return false;
  output->resize(size);
  return true;
}

bool Uncompress(const char* input, size_t length, bool raw,
                std::string* output) {
  libdeflate_decompressor* d = libdeflate_alloc_decompressor();
  if (d == nullptr) return false;
  // Blocks are small (a few hundred KB at most); grow the buffer until it fits.
  size_t capacity = length * 4 + 4096;
  bool ok = false;
  for (int attempt = 0; attempt < 16; ++attempt) {
    output->resize(capacity);
    size_t actual = 0;
    const libdeflate_result r =
        raw ? libdeflate_deflate_decompress(d, input, length, &(*output)[0],
                                            capacity, &actual)
            : libdeflate_zlib_decompress(d, input, length, &(*output)[0],
                                         capacity, &actual);
    if (r == LIBDEFLATE_SUCCESS) {
      output->resize(actual);
      ok = true;
      break;
    }
    if (r != LIBDEFLATE_INSUFFICIENT_SPACE) break;
    capacity *= 2;
  }
  libdeflate_free_decompressor(d);
  return ok;
}

}  // namespace mojang
}  // namespace leveldb
