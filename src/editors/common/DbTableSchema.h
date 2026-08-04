#pragma once

// DbTableSchema — describes a world-DB table so a generic repository can List/Load/Save it
// and a generic editor can render typed fields, without a bespoke Repository per table. The
// world-DB twin of DbcSchema. Columns are edited by name; an optional locale child table
// (PK = pk value + a locale-code column) carries localized text columns.

#include <vector>

namespace we
{
enum class DbColType
{
    U32,
    I32,
    U16,
    U8,
    Float,
    Text,       // single-line VARCHAR/text
    Multiline,  // multi-line text/longtext
};

struct DbColumn
{
    const char* name;             // SQL column name
    DbColType   type;
    const char* label;            // UI label
    const char* tip = nullptr;
    int         refKind = -1;     // -1 = none; else static_cast<we::RefKind> for an FK picker
};

struct DbTableSchema
{
    const char* table;            // e.g. "broadcast_text"
    const char* pk;               // single integer primary key column, e.g. "ID"
    std::vector<DbColumn> cols;   // editable columns (pk excluded)

    // Columns shown in the browser list (besides the pk). Also the text columns searched.
    std::vector<const char*> browserCols;

    // Optional locale child table (PK = <localeKey> + <localeCol>). Empty table = none.
    const char* localeTable = nullptr;         // e.g. "broadcast_text_locale"
    const char* localeKey = nullptr;           // FK column = pk value, e.g. "ID"
    const char* localeCol = nullptr;           // locale-code column, e.g. "locale" / "Locale"
    std::vector<const char*> localizedCols;    // localized text columns, e.g. {"Text","Text1"}

    bool HasLocale() const { return localeTable && localeKey && localeCol; }
};
} // namespace we
