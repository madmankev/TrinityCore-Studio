#pragma once

// Shared field editors for world-DB editors: a labelled widget bound to a DbRecord cell that
// converts to/from the column's type and marks the record dirty on change. Expect an open
// BeginFieldTable (each emits FieldRow + the value widget). The DB twin of DbcEditWidgets.

#include <vector>

namespace we
{
struct DbRecord;
class LookupCache;
enum class RefKind;

bool DbU32Field(const char* label, DbRecord& rec, const char* col, const char* tip = nullptr);
bool DbI32Field(const char* label, DbRecord& rec, const char* col, const char* tip = nullptr);
bool DbFloatField(const char* label, DbRecord& rec, const char* col, const char* tip = nullptr);
bool DbTextField(const char* label, DbRecord& rec, const char* col, const char* tip = nullptr);
bool DbMultilineField(const char* label, DbRecord& rec, const char* col, float height = 60.0f);

// Id column + resolved name + "..." picker (see ui/Widgets IdNamePicker).
bool DbIdNameField(const char* label, DbRecord& rec, const char* col, LookupCache& cache,
                   RefKind kind, const char* tip = nullptr);

// A collapsible "Translations" editor for a record's locale child (the 8 non-enUS locales),
// one text field per localized column. Reads/writes rec.locales (creating rows on first edit),
// which the SimpleDbEditorModule save path persists to the schema's locale table. Draws its own
// field table; call outside BeginFieldTable. Returns true if any value changed.
bool DrawLocaleEditor(DbRecord& rec, const std::vector<const char*>& localizedCols);
} // namespace we
