#pragma once

// DbRecord — one loaded world-DB row plus its locale child rows, held as raw text cells
// (column name -> value string, as read from ResultSet::GetString). Widgets convert per the
// column's DbColType; the repository writes them back via ValueList. `dirty` gates saves.

#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace we
{
struct DbLocaleRow
{
    std::string locale;                          // 'koKR', 'frFR', ...
    std::map<std::string, std::string> cells;    // localized column -> value
};

struct DbRecord
{
    uint32_t id = 0;
    bool     present = false;  // exists in the DB (vs a new, unsaved record)
    bool     dirty = false;
    std::map<std::string, std::string> cells;  // column name -> raw text value
    std::vector<DbLocaleRow> locales;

    const std::string& Get(const std::string& col) const
    {
        static const std::string kEmpty;
        auto it = cells.find(col);
        return it != cells.end() ? it->second : kEmpty;
    }
    void Set(const std::string& col, std::string v)
    {
        cells[col] = std::move(v);
        dirty = true;
    }

    uint32_t GetU32(const std::string& col) const
    {
        const std::string& s = Get(col);
        return s.empty() ? 0u : static_cast<uint32_t>(std::strtoul(s.c_str(), nullptr, 10));
    }
    int32_t GetI32(const std::string& col) const
    {
        const std::string& s = Get(col);
        return s.empty() ? 0 : static_cast<int32_t>(std::strtol(s.c_str(), nullptr, 10));
    }
    float GetF32(const std::string& col) const
    {
        const std::string& s = Get(col);
        return s.empty() ? 0.0f : std::strtof(s.c_str(), nullptr);
    }
};
} // namespace we
