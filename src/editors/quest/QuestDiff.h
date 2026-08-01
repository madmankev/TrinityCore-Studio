#pragma once

// Field-by-field diff between the database version of a quest and the edited
// version, used by the "Diff vs Database" preview. Pure logic (no ImGui/SQL).

#include <string>
#include <vector>

#include "schema/Quest.h"

namespace we
{
struct FieldDiff
{
    std::string tab;       // editor tab the field belongs to
    std::string field;     // field name
    std::string oldValue;  // DB version (string-formatted)
    std::string newValue;  // edited version
};

class QuestDiff
{
public:
    // Returns only the fields that changed between `dbVersion` and `edited`,
    // ordered by tab then field.
    std::vector<FieldDiff> Compare(const Quest& dbVersion, const Quest& edited) const;
};
} // namespace we
