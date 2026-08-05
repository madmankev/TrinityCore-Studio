#pragma once

// Describes a WDBC file's columns so a table (EditableDbc) can decode/encode typed
// cells and an editor can label/type its fields. 3.3.5a DBCs are a flat array of 4-byte
// columns; this schema names each logical field and gives its type. A localized string
// (LangString) is physically 17 columns: 16 locale string-offset slots (enUS = index 0)
// followed by one flags word.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace we
{
enum class DbcFieldType
{
    UInt32,
    Int32,
    Float,
    String,      // one string-offset column
    LangString,  // 16 locale string-offset columns + 1 flags word = 17 columns
    // Sub-4-byte packed integers (a handful of 3.3.5a DBCs pack u8/u16 fields to shave record
    // size — e.g. PowerDisplay's RGB, SpellItemEnchantmentCondition's operand arrays). Each is
    // still ONE physical column for addressing/rendering; only its byte width in the record
    // differs. See FieldByteWidth / DbcSchema::RecordByteSize.
    UInt8,
    Int8,
    UInt16,
    Int16,
};

// enUS is locale index 0; the flags word follows the 16 locale slots.
constexpr uint32_t kDbcLocaleCount = 16;
constexpr uint32_t kDbcLangFlagsOffset = 16;

// Number of physical columns a logical field occupies (for cell addressing / rendering). A packed
// integer is still one column; only its byte width shrinks.
inline uint32_t FieldWidth(DbcFieldType t)
{
    return t == DbcFieldType::LangString ? (kDbcLocaleCount + 1) : 1u;
}

// Byte width one logical field occupies in the record (a LangString is 17 four-byte columns).
inline uint32_t FieldByteWidth(DbcFieldType t)
{
    switch (t)
    {
    case DbcFieldType::UInt8:
    case DbcFieldType::Int8:       return 1;
    case DbcFieldType::UInt16:
    case DbcFieldType::Int16:      return 2;
    case DbcFieldType::LangString: return (kDbcLocaleCount + 1) * 4;  // 17 columns * 4
    default:                       return 4;  // UInt32, Int32, Float, String
    }
}

// A packed field whose raw value should sign-extend to 32 bits when read.
inline bool FieldIsSignedNarrow(DbcFieldType t)
{
    return t == DbcFieldType::Int8 || t == DbcFieldType::Int16;
}

struct DbcFieldDef
{
    const char*  name;  // display/debug label
    DbcFieldType type;
};

struct DbcSchema
{
    std::vector<DbcFieldDef> fields;  // logical fields, in column order

    // Total physical columns (a LangString counts as 17). Matches a WDBC header's fieldCount even
    // for packed DBCs, whose header counts each sub-4-byte field as one column.
    uint32_t FieldCount() const
    {
        uint32_t n = 0;
        for (const auto& f : fields)
            n += FieldWidth(f.type);
        return n;
    }

    // Total record size in BYTES (packed sub-4-byte fields at their real width). Equals
    // FieldCount()*4 for the common all-4-byte DBCs; smaller when packed fields are present.
    uint32_t RecordByteSize() const
    {
        uint32_t n = 0;
        for (const auto& f : fields)
            n += FieldByteWidth(f.type);
        return n;
    }

    // Physical column index where logical field `i` begins (end/out-of-range clamps to
    // FieldCount()).
    uint32_t ColumnStart(size_t i) const
    {
        uint32_t c = 0;
        for (size_t k = 0; k < i && k < fields.size(); ++k)
            c += FieldWidth(fields[k].type);
        return c;
    }
};

// Human labels for the 16 locale columns (enUS = 0). Slots 9..15 are unused in 3.3.5a
// enUS clients but are preserved byte-for-byte on save. Shared by every DBC editor.
inline const char* DbcLocaleName(uint32_t i)
{
    static const char* kNames[16] = {"enUS", "koKR", "frFR", "deDE", "zhCN", "zhTW",
                                     "esES", "esMX", "ruRU", "loc9", "loc10", "loc11",
                                     "loc12", "loc13", "loc14", "loc15"};
    return i < 16 ? kNames[i] : "?";
}
} // namespace we
