#include "data/game_database.h"

#include "utils/string_utils.h"

namespace wowedit
{
void GameDatabase::upsertCreature(CreatureData value) { creatures_[value.templateId] = std::move(value); }
void GameDatabase::upsertQuest(QuestData value) { quests_[value.id] = std::move(value); }
void GameDatabase::upsertSpell(SpellData value) { spells_[value.id] = std::move(value); }
void GameDatabase::upsertZone(ZoneData value) { zones_[value.id] = std::move(value); }
const CreatureData* GameDatabase::creature(std::uint32_t id) const { const auto f = creatures_.find(id); return f == creatures_.end() ? nullptr : &f->second; }
const QuestData* GameDatabase::quest(std::uint32_t id) const { const auto f = quests_.find(id); return f == quests_.end() ? nullptr : &f->second; }
const SpellData* GameDatabase::spell(std::uint32_t id) const { const auto f = spells_.find(id); return f == spells_.end() ? nullptr : &f->second; }
const ZoneData* GameDatabase::zone(std::uint32_t id) const { const auto f = zones_.find(id); return f == zones_.end() ? nullptr : &f->second; }

std::vector<const SpellData*> GameDatabase::searchSpells(const std::string& query) const
{
    std::vector<const SpellData*> result;
    for (const auto& entry : spells_)
        if (strings::ContainsInsensitive(entry.second.name, query) || std::to_string(entry.first).find(query) != std::string::npos)
            result.push_back(&entry.second);
    return result;
}
} // namespace wowedit
