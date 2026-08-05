#pragma once

// MiscDbcModule — a GroupedDbcModule editing a grab-bag of small standalone client DBCs from
// one rail entry (a dropdown per table): CreatureType, CreatureFamily, Languages, GameTips,
// QuestSort, QuestFactionReward, CurrencyTypes, CurrencyCategory, MapDifficulty, LoadingScreens,
// Emotes, BankBagSlotPrices, DurabilityQuality, DurabilityCosts. Each is too trivial to warrant
// its own editor; the generic field dump + loose-overlay save of the base covers them all.

#include "editors/common/GroupedDbcModule.h"

namespace we
{
const std::vector<DbcTableDef>& MiscDbcTableDefs();  // also used by --miscdbc-roundtrip

class MiscDbcModule final : public GroupedDbcModule
{
public:
    const char* Id() const override { return "miscdbc"; }
    const char* DisplayName() const override { return "Misc DBCs"; }
    const char* RailGlyph() const override { return "M"; }

protected:
    const std::vector<DbcTableDef>& Tables() const override { return MiscDbcTableDefs(); }
    const char* BrowserTitle() const override { return "Misc DBC Browser"; }
    const char* EditorTitle() const override { return "Misc DBC Editor"; }
};
} // namespace we
