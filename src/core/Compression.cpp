#include "core/Compression.h"
#include "core/Tr.h"

#include <libdeflate.h>

#include <memory>
#include <stdexcept>

namespace justnbt {
namespace {
struct DecompressorDeleter {
    void operator()(libdeflate_decompressor* d) const { libdeflate_free_decompressor(d); }
};
struct CompressorDeleter {
    void operator()(libdeflate_compressor* c) const { libdeflate_free_compressor(c); }
};

constexpr size_t kMaxOutput = size_t(1) << 31;
}

Compression detectCompression(std::span<const uint8_t> data)
{
    if (data.size() >= 2 && data[0] == 0x1F && data[1] == 0x8B)
        return Compression::Gzip;
    if (data.size() >= 2 && (data[0] & 0x0F) == 8 && ((data[0] << 8) | data[1]) % 31 == 0)
        return Compression::Zlib;
    return Compression::None;
}

std::vector<uint8_t> decompress(std::span<const uint8_t> data, Compression c)
{
    if (c == Compression::None)
        return {data.begin(), data.end()};

    std::unique_ptr<libdeflate_decompressor, DecompressorDeleter> d(libdeflate_alloc_decompressor());
    if (!d)
        throw std::bad_alloc();

    size_t capacity = data.size() * 4 + 1024;
    if (c == Compression::Gzip && data.size() >= 18) {
        const size_t n = data.size();
        const uint32_t isize = uint32_t(data[n - 4]) | uint32_t(data[n - 3]) << 8
            | uint32_t(data[n - 2]) << 16 | uint32_t(data[n - 1]) << 24;
        if (isize > 0)
            capacity = isize;
    }

    std::vector<uint8_t> out;
    for (;;) {
        out.resize(capacity);
        size_t actual = 0;
        libdeflate_result r;
        switch (c) {
        case Compression::Gzip:
            r = libdeflate_gzip_decompress(d.get(), data.data(), data.size(), out.data(), capacity, &actual);
            break;
        case Compression::Zlib:
            r = libdeflate_zlib_decompress(d.get(), data.data(), data.size(), out.data(), capacity, &actual);
            break;
        default:
            r = libdeflate_deflate_decompress(d.get(), data.data(), data.size(), out.data(), capacity, &actual);
            break;
        }
        if (r == LIBDEFLATE_SUCCESS) {
            out.resize(actual);
            return out;
        }
        if (r == LIBDEFLATE_INSUFFICIENT_SPACE && capacity < kMaxOutput) {
            capacity *= 2;
            continue;
        }
        throw std::runtime_error(Tr::tr("Cannot unpack the data (is the file damaged?)").toStdString());
    }
}

std::vector<uint8_t> compress(std::span<const uint8_t> data, Compression c, int level)
{
    if (c == Compression::None)
        return {data.begin(), data.end()};

    std::unique_ptr<libdeflate_compressor, CompressorDeleter> z(libdeflate_alloc_compressor(level));
    if (!z)
        throw std::bad_alloc();

    std::vector<uint8_t> out;
    size_t size = 0;
    switch (c) {
    case Compression::Gzip:
        out.resize(libdeflate_gzip_compress_bound(z.get(), data.size()));
        size = libdeflate_gzip_compress(z.get(), data.data(), data.size(), out.data(), out.size());
        break;
    case Compression::Zlib:
        out.resize(libdeflate_zlib_compress_bound(z.get(), data.size()));
        size = libdeflate_zlib_compress(z.get(), data.data(), data.size(), out.data(), out.size());
        break;
    default:
        out.resize(libdeflate_deflate_compress_bound(z.get(), data.size()));
        size = libdeflate_deflate_compress(z.get(), data.data(), data.size(), out.data(), out.size());
        break;
    }
    if (size == 0)
        throw std::runtime_error(Tr::tr("Cannot compress the data").toStdString());
    out.resize(size);
    return out;
}
}
