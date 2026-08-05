#pragma once

// SpellTablesModule — a GroupedDbcModule editing the ~10 small, SHARED spell-reference DBCs
// that Spell.dbc's index fields point into (SpellDuration, SpellCastTimes, SpellRange,
// SpellRadius, SpellIcon, SpellCategory, SpellMechanic, SpellDispelType, SpellFocusObject,
// SpellRuneCost). One rail entry, a dropdown per table. These are global lookup tables (not
// per-spell), so they live here rather than as tabs inside the Spell editor.

#include "editors/common/GroupedDbcModule.h"

namespace we
{
// The spell-reference table set (also used by the --spelltables-roundtrip harness).
const std::vector<DbcTableDef>& SpellTableDefs();

class SpellTablesModule final : public GroupedDbcModule
{
public:
    const char* Id() const override { return "spelltables"; }
    const char* DisplayName() const override { return "Spell Tables"; }
    const char* RailGlyph() const override { return "s"; }

protected:
    const std::vector<DbcTableDef>& Tables() const override;
    const char* BrowserTitle() const override { return "Spell Table Browser"; }
    const char* EditorTitle() const override { return "Spell Table Editor"; }
};
} // namespace we
