#pragma once

// Shared field editors for DBC editors: draw a labelled widget bound to a DbcDocument cell
// and mark the document dirty on change. The scalar helpers expect an open BeginFieldTable
// (they emit a FieldRow + the value widget); DbcLangEditor draws its own section + table.

#include <cstdint>

namespace we
{
class DbcDocument;
class LookupCache;
enum class RefKind;

// FieldRow(label,tip) + value widget bound to (row, col); returns true if changed.
bool DbcU32Field(const char* label, DbcDocument& doc, uint32_t row, uint32_t col,
                 const char* tooltip = nullptr);
bool DbcI32Field(const char* label, DbcDocument& doc, uint32_t row, uint32_t col,
                 const char* tooltip = nullptr);
bool DbcF32Field(const char* label, DbcDocument& doc, uint32_t row, uint32_t col,
                 const char* tooltip = nullptr);
bool DbcStrField(const char* label, DbcDocument& doc, uint32_t row, uint32_t col,
                 const char* tooltip = nullptr);

// Id column + resolved name + "..." picker (ui/Widgets IdNamePicker), bound to a cell.
bool DbcIdNameField(const char* label, DbcDocument& doc, uint32_t row, uint32_t col,
                    LookupCache& cache, RefKind kind, const char* tooltip = nullptr);

// A full 16-locale editor for a LangString beginning at physical column `loc0Col`:
// SeparatorText(label) + one string input per locale (labelled enUS/koKR/...).
void DbcLangEditor(const char* label, DbcDocument& doc, uint32_t row, uint32_t loc0Col);
} // namespace we
