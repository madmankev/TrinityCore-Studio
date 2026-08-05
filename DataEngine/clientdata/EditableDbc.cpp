// EditableDbc — see EditableDbc.h.

#include "clientdata/EditableDbc.h"

#include <cstring>
#include <unordered_map>

namespace we
{
const std::string EditableDbc::kEmpty_;

namespace
{
uint32_t ReadLE32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// Read `width` little-endian bytes (1/2/4) into a 32-bit value.
uint32_t ReadLE(const uint8_t* p, uint8_t width)
{
    uint32_t v = 0;
    for (uint8_t i = 0; i < width; ++i)
        v |= static_cast<uint32_t>(p[i]) << (8 * i);
    return v;
}

// Sign-extend a `width`-byte value to 32 bits.
uint32_t SignExtend(uint32_t v, uint8_t width)
{
    if (width >= 4)
        return v;
    const uint32_t bits = static_cast<uint32_t>(width) * 8;
    const uint32_t signBit = 1u << (bits - 1);
    if (v & signBit)
        v |= ~((1u << bits) - 1u);
    return v;
}

// Append the low `width` bytes of v, little-endian.
void PushLE(std::vector<uint8_t>& out, uint32_t v, uint8_t width)
{
    for (uint8_t i = 0; i < width; ++i)
        out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}
} // namespace

void EditableDbc::BuildColumnMap()
{
    colIsString_.clear();
    colByteWidth_.clear();
    colByteOffset_.clear();
    colSigned_.clear();
    const uint32_t cols = schema_.FieldCount();
    colIsString_.reserve(cols);
    colByteWidth_.reserve(cols);
    colByteOffset_.reserve(cols);
    colSigned_.reserve(cols);

    uint32_t off = 0;
    auto push = [&](bool isStr, uint8_t width, bool sign) {
        colIsString_.push_back(isStr);
        colByteWidth_.push_back(width);
        colByteOffset_.push_back(off);
        colSigned_.push_back(sign);
        off += width;
    };
    for (const auto& f : schema_.fields)
    {
        switch (f.type)
        {
        case DbcFieldType::String:
            push(true, 4, false);
            break;
        case DbcFieldType::LangString:
            for (uint32_t i = 0; i < kDbcLocaleCount; ++i)
                push(true, 4, false);  // 16 locale slots
            push(false, 4, false);     // flags word
            break;
        case DbcFieldType::Int8:   push(false, 1, true);  break;
        case DbcFieldType::UInt8:  push(false, 1, false); break;
        case DbcFieldType::Int16:  push(false, 2, true);  break;
        case DbcFieldType::UInt16: push(false, 2, false); break;
        case DbcFieldType::Int32:  push(false, 4, true);  break;
        default:                   push(false, 4, false); break;  // UInt32, Float
        }
    }
    recordByteSize_ = off;
}

bool EditableDbc::Load(const std::vector<uint8_t>& bytes, const DbcSchema& schema)
{
    loaded_ = false;
    schema_ = schema;
    rows_.clear();
    BuildColumnMap();

    if (bytes.size() < 20)
        return false;
    if (!(bytes[0] == 'W' && bytes[1] == 'D' && bytes[2] == 'B' && bytes[3] == 'C'))
        return false;

    const uint32_t recordCount = ReadLE32(&bytes[4]);
    const uint32_t fieldCount  = ReadLE32(&bytes[8]);
    const uint32_t recordSize  = ReadLE32(&bytes[12]);
    const uint32_t stringSize  = ReadLE32(&bytes[16]);

    // Fail-safe: the file must match the schema we were handed. If it doesn't, refuse
    // rather than decode garbage — a wrong/patched layout yields an empty (unusable)
    // table the caller can detect via IsLoaded(), never a corrupt save. The record-size
    // check is byte-based so packed sub-4-byte DBCs (recordSize < fieldCount*4) validate too.
    const uint32_t schemaFields = schema_.FieldCount();
    if (fieldCount != schemaFields || recordSize != recordByteSize_ || recordSize < 1)
        return false;

    const size_t recordsOffset = 20;
    const size_t stringsOffset = recordsOffset + static_cast<size_t>(recordCount) * recordSize;
    if (stringsOffset + stringSize > bytes.size())
        return false;

    auto readString = [&](uint32_t offset) -> std::string {
        if (offset == 0)
            return {};
        size_t pos = stringsOffset + offset;
        if (pos >= bytes.size())
            return {};
        const char* start = reinterpret_cast<const char*>(&bytes[pos]);
        size_t maxLen = bytes.size() - pos;
        size_t len = 0;
        while (len < maxLen && start[len] != '\0')
            ++len;
        return std::string(start, len);
    };

    rows_.resize(recordCount);
    for (uint32_t r = 0; r < recordCount; ++r)
    {
        rows_[r].resize(fieldCount);
        const size_t base = recordsOffset + static_cast<size_t>(r) * recordSize;
        for (uint32_t c = 0; c < fieldCount; ++c)
        {
            const uint32_t raw = ReadLE(&bytes[base + colByteOffset_[c]], colByteWidth_[c]);
            if (colIsString_[c])
                rows_[r][c].s = readString(raw);
            else
                rows_[r][c].u = colSigned_[c] ? SignExtend(raw, colByteWidth_[c]) : raw;
        }
    }

    loaded_ = true;
    return true;
}

void EditableDbc::InitEmpty(const DbcSchema& schema)
{
    schema_ = schema;
    rows_.clear();
    BuildColumnMap();
    loaded_ = true;
}

uint32_t EditableDbc::GetU32(uint32_t row, uint32_t col) const
{
    if (row >= rows_.size() || col >= rows_[row].size())
        return 0;
    return rows_[row][col].u;
}

int32_t EditableDbc::GetI32(uint32_t row, uint32_t col) const
{
    return static_cast<int32_t>(GetU32(row, col));
}

float EditableDbc::GetF32(uint32_t row, uint32_t col) const
{
    uint32_t bits = GetU32(row, col);
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

const std::string& EditableDbc::GetStr(uint32_t row, uint32_t col) const
{
    if (row >= rows_.size() || col >= rows_[row].size() || !ColumnIsString(col))
        return kEmpty_;
    return rows_[row][col].s;
}

void EditableDbc::SetU32(uint32_t row, uint32_t col, uint32_t v)
{
    if (row >= rows_.size() || col >= rows_[row].size() || ColumnIsString(col))
        return;
    rows_[row][col].u = v;
}

void EditableDbc::SetI32(uint32_t row, uint32_t col, int32_t v)
{
    SetU32(row, col, static_cast<uint32_t>(v));
}

void EditableDbc::SetF32(uint32_t row, uint32_t col, float v)
{
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    SetU32(row, col, bits);
}

void EditableDbc::SetStr(uint32_t row, uint32_t col, std::string v)
{
    if (row >= rows_.size() || col >= rows_[row].size() || !ColumnIsString(col))
        return;
    rows_[row][col].s = std::move(v);
}

uint32_t EditableDbc::AddRow()
{
    rows_.emplace_back(schema_.FieldCount());
    return static_cast<uint32_t>(rows_.size() - 1);
}

uint32_t EditableDbc::CloneRow(uint32_t row)
{
    if (row >= rows_.size())
        return AddRow();
    rows_.push_back(rows_[row]);
    return static_cast<uint32_t>(rows_.size() - 1);
}

void EditableDbc::DeleteRow(uint32_t row)
{
    if (row < rows_.size())
        rows_.erase(rows_.begin() + row);
}

int EditableDbc::FindById(uint32_t id) const
{
    for (size_t r = 0; r < rows_.size(); ++r)
        if (!rows_[r].empty() && rows_[r][0].u == id)
            return static_cast<int>(r);
    return -1;
}

std::vector<uint8_t> EditableDbc::Serialize() const
{
    const uint32_t fieldCount  = schema_.FieldCount();
    const uint32_t recordCount = static_cast<uint32_t>(rows_.size());
    const uint32_t recordSize  = recordByteSize_;  // byte-accurate (packed fields < 4 bytes)

    // String pool: byte 0 is a NUL so offset 0 == the empty string. Dedup identical
    // strings (matches how clients read; keeps files compact).
    std::vector<uint8_t> pool;
    pool.push_back(0);
    std::unordered_map<std::string, uint32_t> offsets;
    auto internString = [&](const std::string& s) -> uint32_t {
        if (s.empty())
            return 0;
        auto it = offsets.find(s);
        if (it != offsets.end())
            return it->second;
        uint32_t off = static_cast<uint32_t>(pool.size());
        pool.insert(pool.end(), s.begin(), s.end());
        pool.push_back(0);
        offsets.emplace(s, off);
        return off;
    };

    std::vector<uint8_t> records;
    records.reserve(static_cast<size_t>(recordCount) * recordSize);
    for (const auto& row : rows_)
    {
        for (uint32_t c = 0; c < fieldCount; ++c)
        {
            const uint8_t w = colByteWidth_[c];
            if (c < row.size() && ColumnIsString(c))
                PushLE(records, internString(row[c].s), w);
            else
                PushLE(records, c < row.size() ? row[c].u : 0u, w);
        }
    }

    std::vector<uint8_t> out;
    out.reserve(20 + records.size() + pool.size());
    out.push_back('W');
    out.push_back('D');
    out.push_back('B');
    out.push_back('C');
    PushLE(out, recordCount, 4);
    PushLE(out, fieldCount, 4);
    PushLE(out, recordSize, 4);
    PushLE(out, static_cast<uint32_t>(pool.size()), 4);
    out.insert(out.end(), records.begin(), records.end());
    out.insert(out.end(), pool.begin(), pool.end());
    return out;
}
} // namespace we
