#pragma once

// ByteReader — a bounds-checked view over a byte buffer, plus an IFF/RIFF chunk iterator
// for WoW's chunked files (WMO, ADT, WDT). Every read is range-checked; failures return
// false / defaults rather than reading out of bounds. This lifts the parsing idiom used
// by the M2 loader (which has its own inline Reader) into a shared, format-neutral helper.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace we
{
struct ByteReader
{
    const uint8_t* data = nullptr;
    size_t size = 0;

    ByteReader() = default;
    ByteReader(const uint8_t* d, size_t n) : data(d), size(n) {}
    explicit ByteReader(const std::vector<uint8_t>& v) : data(v.data()), size(v.size()) {}

    // Is [offset, offset+bytes) fully inside the buffer? Written to avoid integer overflow.
    bool In(size_t offset, size_t bytes) const { return offset <= size && bytes <= size - offset; }

    // Copy a POD at a byte offset. Returns false (leaving out unchanged) if out of range.
    template <class T>
    bool Get(size_t offset, T& out) const
    {
        if (!In(offset, sizeof(T)))
            return false;
        std::memcpy(&out, data + offset, sizeof(T));
        return true;
    }

    // Read `count` elements of T starting at a byte offset.
    template <class T>
    bool GetArrayAt(size_t offset, size_t count, std::vector<T>& out) const
    {
        const size_t total = count * sizeof(T);
        if (!In(offset, total))
            return false;
        out.resize(count);
        if (count)
            std::memcpy(out.data(), data + offset, total);
        return true;
    }

    // NUL-terminated string starting at a byte offset (MOTX/MODN/MOGN string blobs index
    // into their chunk payload by byte offset). Clamped to the buffer end.
    std::string GetCString(size_t offset) const
    {
        if (offset >= size)
            return {};
        const char* p = reinterpret_cast<const char*>(data + offset);
        const size_t maxLen = size - offset;
        size_t len = 0;
        while (len < maxLen && p[len] != '\0')
            ++len;
        return std::string(p, len);
    }
};

// One IFF chunk: its 4-char magic plus the byte range of its payload within a ByteReader.
struct Chunk
{
    char     magic[4] = {0, 0, 0, 0};   // as stored on disk (reversed FourCC — see Is())
    size_t   offset = 0;                // byte offset of the payload (past the 8-byte header)
    uint32_t size = 0;                  // payload size in bytes

    // WoW writes chunk magics as a little-endian uint32, so on disk they appear reversed
    // ("REVM" for MVER). Callers pass the human-readable name; we compare against the
    // reversed on-disk bytes.
    bool Is(const char* human) const
    {
        return magic[0] == human[3] && magic[1] == human[2] && magic[2] == human[1] &&
               magic[3] == human[0];
    }
};

// Walk chunks sequentially from the start of a buffer. Sizes are clamped to the buffer so a
// malformed file can't drive reads out of range.
struct ChunkIter
{
    ByteReader r;
    size_t cursor = 0;

    explicit ChunkIter(ByteReader reader) : r(reader) {}

    bool Next(Chunk& out)
    {
        if (cursor + 8 > r.size)
            return false;
        std::memcpy(out.magic, r.data + cursor, 4);
        uint32_t sz = 0;
        std::memcpy(&sz, r.data + cursor + 4, 4);
        const size_t payload = cursor + 8;
        if (payload + sz > r.size)
            sz = static_cast<uint32_t>(r.size - payload);   // clamp defensively
        out.offset = payload;
        out.size = sz;
        cursor = payload + sz;
        return true;
    }
};
} // namespace we
