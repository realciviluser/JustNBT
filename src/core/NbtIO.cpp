#include "core/NbtIO.h"
#include "core/Tr.h"

#include <bit>
#include <cstring>
#include <type_traits>

namespace nbt {
namespace {
constexpr int kMaxDepth = 512;

class Reader {
public:
    Reader(std::span<const uint8_t> data, Endian endian)
        : data_(data), big_(endian == Endian::Big)
    {
    }

    size_t pos() const { return pos_; }

    template <class T>
    T readInt()
    {
        need(sizeof(T));
        using U = std::make_unsigned_t<T>;
        U v = 0;
        for (size_t i = 0; i < sizeof(T); ++i) {
            const U b = data_[pos_ + (big_ ? i : sizeof(T) - 1 - i)];
            v = U(v << 8) | b;
        }
        pos_ += sizeof(T);
        return static_cast<T>(v);
    }

    std::string readString()
    {
        const uint16_t len = readInt<uint16_t>();
        need(len);
        std::string_view raw(reinterpret_cast<const char*>(data_.data() + pos_), len);
        pos_ += len;
        return big_ ? mutf8ToUtf8(raw) : std::string(raw);
    }

    void readPayload(Tag& t, int depth)
    {
        if (depth > kMaxDepth)
            throw ParseError(justnbt::Tr::tr("The tags are nested too deeply").toStdString());

        switch (t.type) {
        case TagType::End:
            break;
        case TagType::Byte:
            t.integer = readInt<int8_t>();
            break;
        case TagType::Short:
            t.integer = readInt<int16_t>();
            break;
        case TagType::Int:
            t.integer = readInt<int32_t>();
            break;
        case TagType::Long:
            t.integer = readInt<int64_t>();
            break;
        case TagType::Float:
            t.floating = std::bit_cast<float>(readInt<uint32_t>());
            break;
        case TagType::Double:
            t.floating = std::bit_cast<double>(readInt<uint64_t>());
            break;
        case TagType::ByteArray: {
            const size_t n = readLength(1);
            t.bytes.resize(n);
            std::memcpy(t.bytes.data(), data_.data() + pos_, n);
            pos_ += n;
            break;
        }
        case TagType::IntArray: {
            const size_t n = readLength(4);
            t.ints.resize(n);
            for (auto& v : t.ints)
                v = readInt<int32_t>();
            break;
        }
        case TagType::LongArray: {
            const size_t n = readLength(8);
            t.longs.resize(n);
            for (auto& v : t.longs)
                v = readInt<int64_t>();
            break;
        }
        case TagType::String:
            t.string = readString();
            break;
        case TagType::List: {
            const uint8_t et = readInt<uint8_t>();
            if (!isValidType(et))
                throw ParseError(justnbt::Tr::tr("Unknown list element type").toStdString());
            t.listType = TagType(et);
            const size_t n = readLength(1);
            if (t.listType == TagType::End)
                break;
            t.children.reserve(n);
            for (size_t i = 0; i < n; ++i) {
                auto c = std::make_unique<Tag>(t.listType);
                readPayload(*c, depth + 1);
                t.append(std::move(c));
            }
            break;
        }
        case TagType::Compound:
            for (;;) {
                const uint8_t ct = readInt<uint8_t>();
                if (ct == 0)
                    break;
                if (!isValidType(ct))
                    throw ParseError(justnbt::Tr::tr("Unknown tag type").toStdString());
                auto c = std::make_unique<Tag>(TagType(ct), readString());
                readPayload(*c, depth + 1);
                t.append(std::move(c));
            }
            break;
        }
    }

private:
    void need(size_t n)
    {
        if (data_.size() - pos_ < n)
            throw ParseError(justnbt::Tr::tr("Unexpected end of the data").toStdString());
    }

    size_t readLength(size_t minElementSize)
    {
        const int32_t n = readInt<int32_t>();
        if (n < 0)
            throw ParseError(justnbt::Tr::tr("Negative length").toStdString());
        if (size_t(n) > (data_.size() - pos_) / minElementSize)
            throw ParseError(justnbt::Tr::tr("The length is bigger than the data that is left").toStdString());
        return size_t(n);
    }

    std::span<const uint8_t> data_;
    size_t pos_ = 0;
    bool big_;
};

class Writer {
public:
    Writer(std::vector<uint8_t>& out, Endian endian) : out_(out), big_(endian == Endian::Big) {}

    template <class T>
    void writeInt(T v)
    {
        using U = std::make_unsigned_t<T>;
        const U u = U(v);
        for (size_t i = 0; i < sizeof(T); ++i) {
            const size_t shift = big_ ? (sizeof(T) - 1 - i) * 8 : i * 8;
            out_.push_back(uint8_t(u >> shift));
        }
    }

    void writeString(const std::string& s)
    {
        const std::string encoded = big_ ? utf8ToMutf8(s) : s;
        if (encoded.size() > 0xFFFF)
            throw ParseError(justnbt::Tr::tr("The string is longer than 65535 bytes").toStdString());
        writeInt<uint16_t>(uint16_t(encoded.size()));
        out_.insert(out_.end(), encoded.begin(), encoded.end());
    }

    void writePayload(const Tag& t)
    {
        switch (t.type) {
        case TagType::End:
            break;
        case TagType::Byte:
            writeInt<int8_t>(int8_t(t.integer));
            break;
        case TagType::Short:
            writeInt<int16_t>(int16_t(t.integer));
            break;
        case TagType::Int:
            writeInt<int32_t>(int32_t(t.integer));
            break;
        case TagType::Long:
            writeInt<int64_t>(t.integer);
            break;
        case TagType::Float:
            writeInt<uint32_t>(std::bit_cast<uint32_t>(float(t.floating)));
            break;
        case TagType::Double:
            writeInt<uint64_t>(std::bit_cast<uint64_t>(t.floating));
            break;
        case TagType::ByteArray:
            writeInt<int32_t>(int32_t(t.bytes.size()));
            out_.insert(out_.end(), t.bytes.begin(), t.bytes.end());
            break;
        case TagType::IntArray:
            writeInt<int32_t>(int32_t(t.ints.size()));
            for (int32_t v : t.ints)
                writeInt<int32_t>(v);
            break;
        case TagType::LongArray:
            writeInt<int32_t>(int32_t(t.longs.size()));
            for (int64_t v : t.longs)
                writeInt<int64_t>(v);
            break;
        case TagType::String:
            writeString(t.string);
            break;
        case TagType::List:
            writeInt<uint8_t>(uint8_t(t.listType));
            writeInt<int32_t>(int32_t(t.children.size()));
            for (const auto& c : t.children)
                writePayload(*c);
            break;
        case TagType::Compound:
            for (const auto& c : t.children) {
                writeInt<uint8_t>(uint8_t(c->type));
                writeString(c->name);
                writePayload(*c);
            }
            writeInt<uint8_t>(0);
            break;
        }
    }

private:
    std::vector<uint8_t>& out_;
    bool big_;
};

void appendUtf8(std::string& out, uint32_t cp)
{
    if (cp < 0x80) {
        out += char(cp);
    } else if (cp < 0x800) {
        out += char(0xC0 | (cp >> 6));
        out += char(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += char(0xE0 | (cp >> 12));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    } else {
        out += char(0xF0 | (cp >> 18));
        out += char(0x80 | ((cp >> 12) & 0x3F));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    }
}

bool isPlainAscii(std::string_view s)
{
    for (char c : s)
        if (uint8_t(c) >= 0x80 || c == 0)
            return false;
    return true;
}
}

std::unique_ptr<Tag> read(std::span<const uint8_t> data, Endian endian, size_t* consumed)
{
    Reader r(data, endian);
    const uint8_t type = r.readInt<uint8_t>();
    if (type == 0 || !isValidType(type))
        throw ParseError(justnbt::Tr::tr("Bad root tag").toStdString());
    auto root = std::make_unique<Tag>(TagType(type), r.readString());
    r.readPayload(*root, 0);
    if (consumed)
        *consumed = r.pos();
    return root;
}

void write(const Tag& root, Endian endian, std::vector<uint8_t>& out)
{
    Writer w(out, endian);
    w.writeInt<uint8_t>(uint8_t(root.type));
    w.writeString(root.name);
    w.writePayload(root);
}

std::string mutf8ToUtf8(std::string_view in)
{
    if (isPlainAscii(in))
        return std::string(in);

    const size_t n = in.size();
    auto byteAt = [&](size_t i) { return uint8_t(in[i]); };
    auto isCont = [&](size_t i) { return i < n && (byteAt(i) & 0xC0) == 0x80; };
    auto nextUnit = [&](size_t& i) -> uint32_t {
        const uint8_t a = byteAt(i);
        if (a < 0x80) {
            i += 1;
            return a;
        }
        if ((a & 0xE0) == 0xC0 && isCont(i + 1)) {
            const uint32_t v = ((a & 0x1Fu) << 6) | (byteAt(i + 1) & 0x3Fu);
            i += 2;
            return v;
        }
        if ((a & 0xF0) == 0xE0 && isCont(i + 1) && isCont(i + 2)) {
            const uint32_t v = ((a & 0x0Fu) << 12) | ((byteAt(i + 1) & 0x3Fu) << 6) | (byteAt(i + 2) & 0x3Fu);
            i += 3;
            return v;
        }
        i += 1;
        return 0xFFFD;
    };

    std::string out;
    out.reserve(n);
    size_t i = 0;
    while (i < n) {
        const uint32_t u = nextUnit(i);
        if (u >= 0xD800 && u <= 0xDBFF && i < n) {
            size_t j = i;
            const uint32_t lo = nextUnit(j);
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                appendUtf8(out, 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00));
                i = j;
                continue;
            }
        }
        appendUtf8(out, (u >= 0xD800 && u <= 0xDFFF) ? 0xFFFD : u);
    }
    return out;
}

std::string utf8ToMutf8(std::string_view in)
{
    if (isPlainAscii(in))
        return std::string(in);

    const size_t n = in.size();
    auto byteAt = [&](size_t i) { return uint8_t(in[i]); };
    auto isCont = [&](size_t i) { return i < n && (byteAt(i) & 0xC0) == 0x80; };

    std::string out;
    out.reserve(n + 8);
    size_t i = 0;
    while (i < n) {
        const uint8_t a = byteAt(i);
        uint32_t cp = 0xFFFD;
        if (a < 0x80) {
            cp = a;
            i += 1;
        } else if ((a & 0xE0) == 0xC0 && isCont(i + 1)) {
            cp = ((a & 0x1Fu) << 6) | (byteAt(i + 1) & 0x3Fu);
            i += 2;
        } else if ((a & 0xF0) == 0xE0 && isCont(i + 1) && isCont(i + 2)) {
            cp = ((a & 0x0Fu) << 12) | ((byteAt(i + 1) & 0x3Fu) << 6) | (byteAt(i + 2) & 0x3Fu);
            i += 3;
        } else if ((a & 0xF8) == 0xF0 && isCont(i + 1) && isCont(i + 2) && isCont(i + 3)) {
            cp = ((a & 0x07u) << 18) | ((byteAt(i + 1) & 0x3Fu) << 12) | ((byteAt(i + 2) & 0x3Fu) << 6)
                | (byteAt(i + 3) & 0x3Fu);
            i += 4;
        } else {
            i += 1;
        }

        if (cp == 0) {
            out += char(0xC0);
            out += char(0x80);
        } else if (cp >= 0x10000) {
            const uint32_t v = cp - 0x10000;
            appendUtf8(out, 0xD800 + (v >> 10));
            appendUtf8(out, 0xDC00 + (v & 0x3FF));
        } else {
            appendUtf8(out, cp);
        }
    }
    return out;
}
}
