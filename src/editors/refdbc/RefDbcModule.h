#pragma once

// RefDbcModule — a GroupedDbcModule editing the reference DBCs that other editors point at via FK
// fields but which had no editor of their own: SpellItemEnchantment, Lock, SoundEntries, Vehicle,
// VehicleSeat, MailTemplate, EmotesText, Holidays, and the display/model DBCs CreatureDisplayInfo,
// CreatureModelData, GameObjectDisplayInfo (creature_template.modelid / gameobject_template.displayId
// point here — editing them closes the custom-model gap). One rail entry, a dropdown per table;
// loose-overlay save. Layouts verified vs --dbc-dump (field counts + string positions) + DBCStructure.h.

#include "editors/common/GroupedDbcModule.h"

namespace we
{
const std::vector<DbcTableDef>& RefDbcTableDefs();  // also used by --refdbc-roundtrip

class RefDbcModule final : public GroupedDbcModule
{
public:
    const char* Id() const override { return "refdbc"; }
    const char* DisplayName() const override { return "Ref DBCs"; }
    const char* RailGlyph() const override { return "J"; }

protected:
    const std::vector<DbcTableDef>& Tables() const override { return RefDbcTableDefs(); }
    const char* BrowserTitle() const override { return "Ref DBC Browser"; }
    const char* EditorTitle() const override { return "Ref DBC Editor"; }
};
} // namespace we
