#pragma once

#include "creatures/creature_data.h"
#include "data/quest_data.h"
#include "data/spell_data.h"
#include "data/zone_data.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace wowedit
{
/** Thread-confined editor-facing cache. Database providers/adapters populate it from
 * TrinityCore/AzerothCore, DBC, JSON fixtures, or a plugin without leaking SQL into tools. */
class GameDatabase
{
public:
    void upsertCreature(CreatureData value);
    void upsertQuest(QuestData value);
    void upsertSpell(SpellData value);
    void upsertZone(ZoneData value);

    const CreatureData* creature(std::uint32_t id) const;
    const QuestData* quest(std::uint32_t id) const;
    const SpellData* spell(std::uint32_t id) const;
    const ZoneData* zone(std::uint32_t id) const;
    std::vector<const SpellData*> searchSpells(const std::string& query) const;

private:
    std::map<std::uint32_t, CreatureData> creatures_;
    std::map<std::uint32_t, QuestData> quests_;
    std::map<std::uint32_t, SpellData> spells_;
    std::map<std::uint32_t, ZoneData> zones_;
};
} // namespace wowedit
