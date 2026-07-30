#include "data/LookupCache.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_set>

#include <json.hpp>

#include "db/IDatabase.h"
#include "db/ResultSet.h"
#include "util/StringUtil.h"

namespace qe
{
namespace
{
// Parse a purely-numeric (all-ASCII-digits, non-empty) query into a uint32_t.
// Returns false for empty, non-numeric, or out-of-range values so those fall
// through to a name search. Kept local — no dependency on <charconv> quirks.
bool ParseId(const std::string& s, uint32_t& out)
{
    if (s.empty())
        return false;
    uint64_t acc = 0;
    for (char c : s)
    {
        if (c < '0' || c > '9')
            return false;
        acc = acc * 10 + static_cast<uint64_t>(c - '0');
        if (acc > 0xFFFFFFFFull)
            return false;  // overflow -> not a valid entry id
    }
    out = static_cast<uint32_t>(acc);
    return true;
}
} // namespace

// --- Shared load ----------------------------------------------------------

DbError LookupCache::Load(IDatabase& db, const char* table, IdNameMap& byId,
                          std::vector<NameEntry>& list, bool& loaded)
{
    if (loaded)
        return DbError{};  // idempotent: already cached

    DbError err;
    std::string sql = std::string("SELECT entry, name FROM ") + table;
    std::unique_ptr<ResultSet> rs = db.Query(sql, err);
    if (!rs)
    {
        // Query failed (or unavailable). Leave the cache untouched/unloaded and
        // surface the reason. Never throw.
        if (err.ok)
        {
            err.ok = false;
            err.message = std::string("LookupCache: query failed for ") + table;
        }
        return err;
    }

    IdNameMap freshMap;
    std::vector<NameEntry> freshList;
    while (rs->Next())
    {
        if (rs->ColumnCount() < 2)
            continue;  // defensive: malformed result
        uint32_t id = rs->GetUInt32(0);
        std::string name = rs->GetString(1);
        freshMap[id] = name;
        freshList.push_back(NameEntry{ id, std::move(name) });
    }

    std::sort(freshList.begin(), freshList.end(),
              [](const NameEntry& a, const NameEntry& b) { return a.id < b.id; });

    byId = std::move(freshMap);
    list = std::move(freshList);
    loaded = true;
    return DbError{};
}

DbError LookupCache::LoadItems(IDatabase& db)
{
    DbError e = Load(db, "item_template", itemById, itemList, itemsLoaded);
    // Also capture displayid for the client-data icon chain (best-effort).
    DbError de;
    if (auto rs = db.Query("SELECT entry, displayid FROM item_template", de))
    {
        itemDisplay.clear();
        while (rs->Next())
            itemDisplay[rs->GetUInt32(0)] = rs->GetUInt32(1);
    }
    return e;
}

uint32_t LookupCache::ItemDisplayId(uint32_t entry) const
{
    auto it = itemDisplay.find(entry);
    return it == itemDisplay.end() ? 0u : it->second;
}

DbError LookupCache::LoadCreatures(IDatabase& db)
{
    DbError e = Load(db, "creature_template", creatureById, creatureList, creaturesLoaded);
    // Also capture each creature's gossip_menu_id for the Questgivers cross-reference.
    // Best-effort: a query failure here must not fail the name load.
    DbError ge;
    if (auto rs = db.Query("SELECT entry, gossip_menu_id FROM creature_template", ge))
    {
        creatureGossip.clear();
        while (rs->Next())
            creatureGossip[rs->GetUInt32(0)] = rs->GetUInt32(1);
    }
    return e;
}

uint32_t LookupCache::GossipMenuOfCreature(uint32_t id) const
{
    auto it = creatureGossip.find(id);
    return it == creatureGossip.end() ? 0u : it->second;
}

void LookupCache::Assign(IdNameMap m, IdNameMap& byId, std::vector<NameEntry>& list, bool& loaded)
{
    byId = std::move(m);
    list.clear();
    list.reserve(byId.size());
    for (const auto& kv : byId)
        list.push_back({kv.first, kv.second});
    std::sort(list.begin(), list.end(),
              [](const NameEntry& a, const NameEntry& b) { return a.id < b.id; });
    loaded = true;
}

void LookupCache::SetFactionNames(std::unordered_map<uint32_t, std::string> m)
{
    Assign(std::move(m), factionById, factionList, factionsLoaded);
}
void LookupCache::SetFactionTemplateNames(std::unordered_map<uint32_t, std::string> m)
{
    Assign(std::move(m), factionTemplateById, factionTemplateList, factionTemplatesLoaded);
}
void LookupCache::SetSpellNames(std::unordered_map<uint32_t, std::string> m)
{
    Assign(std::move(m), spellById, spellList, spellsLoaded);
}
void LookupCache::SetAreaNames(std::unordered_map<uint32_t, std::string> m)
{
    Assign(std::move(m), areaById, areaList, areasLoaded);
}
void LookupCache::SetSkillNames(std::unordered_map<uint32_t, std::string> m)
{
    Assign(std::move(m), skillById, skillList, skillsLoaded);
}
void LookupCache::SetTitleNames(std::unordered_map<uint32_t, std::string> m)
{
    Assign(std::move(m), titleById, titleList, titlesLoaded);
}

DbError LookupCache::LoadGameObjects(IDatabase& db)
{
    return Load(db, "gameobject_template", gameObjectById, gameObjectList, gameObjectsLoaded);
}

// --- Generic column load (arbitrary id/name columns) ----------------------

DbError LookupCache::LoadColumns(IDatabase& db, const char* table, const char* idCol,
                                 const char* nameCol, IdNameMap& byId,
                                 std::vector<NameEntry>& list, bool& loaded)
{
    if (loaded)
        return DbError{};  // idempotent

    DbError err;
    std::string sql = std::string("SELECT ") + idCol + ", " + nameCol + " FROM " + table;
    std::unique_ptr<ResultSet> rs = db.Query(sql, err);
    if (!rs)
    {
        if (err.ok)
        {
            err.ok = false;
            err.message = std::string("LookupCache: query failed for ") + table;
        }
        return err;
    }

    IdNameMap freshMap;
    std::vector<NameEntry> freshList;
    while (rs->Next())
    {
        if (rs->ColumnCount() < 2)
            continue;  // defensive: malformed result
        uint32_t id = rs->GetUInt32(0);
        std::string name = rs->GetString(1);
        freshMap[id] = name;
        freshList.push_back(NameEntry{ id, std::move(name) });
    }

    std::sort(freshList.begin(), freshList.end(),
              [](const NameEntry& a, const NameEntry& b) { return a.id < b.id; });

    byId = std::move(freshMap);
    list = std::move(freshList);
    loaded = true;
    return DbError{};
}

DbError LookupCache::LoadQuests(IDatabase& db)
{
    return LoadColumns(db, "quest_template", "ID", "LogTitle", questById, questList, questsLoaded);
}

// --- Best-effort named (DBC-sourced) load ---------------------------------

DbError LookupCache::LoadNamed(IDatabase& db, const std::vector<std::string>& candidateTables,
                               const std::string& jsonPath, IdNameMap& outMap,
                               std::vector<NameEntry>& outVec)
{
    IdNameMap freshMap;
    std::vector<NameEntry> freshList;
    bool haveData = false;

    // (a) Try each candidate table in turn. A missing table just fails the query
    //     and we move on — never fatal.
    for (const std::string& table : candidateTables)
    {
        DbError err;
        std::string sql = std::string("SELECT id, name FROM ") + table;
        std::unique_ptr<ResultSet> rs = db.Query(sql, err);
        if (!rs)
            continue;  // table absent / query failed — try the next candidate

        while (rs->Next())
        {
            if (rs->ColumnCount() < 2)
                continue;
            uint32_t id = rs->GetUInt32(0);
            std::string name = rs->GetString(1);
            freshMap[id] = name;
            freshList.push_back(NameEntry{ id, std::move(name) });
        }

        if (!freshList.empty())
        {
            haveData = true;
            break;  // first table that yielded rows wins
        }
    }

    // (b) No table data — fall back to an optional JSON file {"123":"Name", ...}.
    if (!haveData)
    {
        try
        {
            std::ifstream in(jsonPath, std::ios::binary);
            if (in)
            {
                nlohmann::json j;
                in >> j;
                if (j.is_object())
                {
                    for (auto it = j.begin(); it != j.end(); ++it)
                    {
                        if (!it.value().is_string())
                            continue;
                        uint32_t id = 0;
                        try
                        {
                            id = static_cast<uint32_t>(std::stoul(it.key()));
                        }
                        catch (...)
                        {
                            continue;  // non-numeric key — skip
                        }
                        std::string name = it.value().get<std::string>();
                        freshMap[id] = name;
                        freshList.push_back(NameEntry{ id, std::move(name) });
                    }
                }
            }
        }
        catch (...)
        {
            // Any JSON/IO error: treat as "no names available" — leave empty.
            freshMap.clear();
            freshList.clear();
        }
    }

    std::sort(freshList.begin(), freshList.end(),
              [](const NameEntry& a, const NameEntry& b) { return a.id < b.id; });

    outMap = std::move(freshMap);
    outVec = std::move(freshList);
    return DbError{};  // always ok — optional data
}

DbError LookupCache::LoadFaction(IDatabase& db)
{
    if (factionsLoaded)
        return DbError{};
    DbError e = LoadNamed(db, { "faction_dbc" }, "data/names/faction.json", factionById, factionList);
    factionsLoaded = true;
    return e;
}

DbError LookupCache::LoadSpell(IDatabase& db)
{
    if (spellsLoaded)
        return DbError{};
    DbError e = LoadNamed(db, { "spell_dbc", "spell" }, "data/names/spell.json", spellById, spellList);
    spellsLoaded = true;
    return e;
}

DbError LookupCache::LoadArea(IDatabase& db)
{
    if (areasLoaded)
        return DbError{};
    DbError e = LoadNamed(db, { "areatable_dbc" }, "data/names/area.json", areaById, areaList);
    areasLoaded = true;
    return e;
}

DbError LookupCache::LoadSkill(IDatabase& db)
{
    if (skillsLoaded)
        return DbError{};
    DbError e = LoadNamed(db, { "skillline_dbc" }, "data/names/skill.json", skillById, skillList);
    skillsLoaded = true;
    return e;
}

DbError LookupCache::LoadTitle(IDatabase& db)
{
    if (titlesLoaded)
        return DbError{};
    DbError e = LoadNamed(db, { "chartitles_dbc" }, "data/names/title.json", titleById, titleList);
    titlesLoaded = true;
    return e;
}

DbError LookupCache::LoadMailTemplate(IDatabase& db)
{
    if (mailTemplatesLoaded)
        return DbError{};
    DbError e = LoadNamed(db, { "mailtemplate_dbc" }, "data/names/mailtemplate.json", mailTemplateById,
                          mailTemplateList);
    mailTemplatesLoaded = true;
    return e;
}

DbError LookupCache::LoadExtras(IDatabase& db)
{
    // Best-effort: per-category failures are ignored (all names are optional).
    LoadFaction(db);
    LoadSpell(db);
    LoadArea(db);
    LoadSkill(db);
    LoadTitle(db);
    LoadMailTemplate(db);
    return DbError{};
}

DbError LookupCache::LoadAll(IDatabase& db)
{
    DbError first;
    DbError e;
    if (e = LoadItems(db); !e.ok && first.ok)
        first = e;
    if (e = LoadCreatures(db); !e.ok && first.ok)
        first = e;
    if (e = LoadGameObjects(db); !e.ok && first.ok)
        first = e;
    if (e = LoadQuests(db); !e.ok && first.ok)
        first = e;
    // DBC-sourced names are optional — never affect the overall result.
    LoadExtras(db);
    return first;
}

DbError LookupCache::ReloadItems(IDatabase& db)
{
    itemById.clear();
    itemDisplay.clear();
    itemList.clear();
    itemsLoaded = false;
    return LoadItems(db);
}

DbError LookupCache::ReloadCreatures(IDatabase& db)
{
    creatureById.clear();
    creatureList.clear();
    creaturesLoaded = false;
    return LoadCreatures(db);
}

DbError LookupCache::ReloadGameObjects(IDatabase& db)
{
    gameObjectById.clear();
    gameObjectList.clear();
    gameObjectsLoaded = false;
    return LoadGameObjects(db);
}

// --- Resolvers ------------------------------------------------------------

std::string LookupCache::NameOf(const IdNameMap& byId, uint32_t id)
{
    auto it = byId.find(id);
    return it == byId.end() ? std::string() : it->second;
}

std::string LookupCache::Label(const IdNameMap& byId, uint32_t id)
{
    auto it = byId.find(id);
    const std::string name = (it == byId.end() || it->second.empty()) ? std::string("<not found>")
                                                                      : it->second;
    return std::to_string(id) + " - " + name;
}

std::string LookupCache::LabelUnknown(const IdNameMap& byId, uint32_t id)
{
    auto it = byId.find(id);
    const std::string name = (it == byId.end() || it->second.empty()) ? std::string("<unknown>")
                                                                      : it->second;
    return std::to_string(id) + " - " + name;
}

std::string LookupCache::LabelOptional(const IdNameMap& byId, uint32_t id)
{
    auto it = byId.find(id);
    if (it == byId.end() || it->second.empty())
        return std::to_string(id);  // name optional / not present
    return std::to_string(id) + " - " + it->second;
}

std::string LookupCache::NameOfItem(uint32_t id) const { return NameOf(itemById, id); }
std::string LookupCache::NameOfCreature(uint32_t id) const { return NameOf(creatureById, id); }
std::string LookupCache::NameOfGameObject(uint32_t id) const { return NameOf(gameObjectById, id); }
std::string LookupCache::NameOfQuest(uint32_t id) const { return NameOf(questById, id); }
std::string LookupCache::NameOfFaction(uint32_t id) const { return NameOf(factionById, id); }
std::string LookupCache::NameOfFactionTemplate(uint32_t id) const { return NameOf(factionTemplateById, id); }
std::string LookupCache::NameOfSpell(uint32_t id) const { return NameOf(spellById, id); }
std::string LookupCache::NameOfArea(uint32_t id) const { return NameOf(areaById, id); }
std::string LookupCache::NameOfSkill(uint32_t id) const { return NameOf(skillById, id); }
std::string LookupCache::NameOfTitle(uint32_t id) const { return NameOf(titleById, id); }
std::string LookupCache::NameOfMailTemplate(uint32_t id) const { return NameOf(mailTemplateById, id); }

std::string LookupCache::LabelItem(uint32_t id) const { return Label(itemById, id); }
std::string LookupCache::LabelCreature(uint32_t id) const { return Label(creatureById, id); }
std::string LookupCache::LabelGameObject(uint32_t id) const { return Label(gameObjectById, id); }

std::string LookupCache::LabelQuest(uint32_t id) const { return LabelUnknown(questById, id); }
std::string LookupCache::LabelFaction(uint32_t id) const { return LabelOptional(factionById, id); }
std::string LookupCache::LabelFactionTemplate(uint32_t id) const { return LabelOptional(factionTemplateById, id); }
std::string LookupCache::LabelSpell(uint32_t id) const { return LabelOptional(spellById, id); }
std::string LookupCache::LabelArea(uint32_t id) const { return LabelOptional(areaById, id); }
std::string LookupCache::LabelSkill(uint32_t id) const { return LabelOptional(skillById, id); }
std::string LookupCache::LabelTitle(uint32_t id) const { return LabelOptional(titleById, id); }
std::string LookupCache::LabelMailTemplate(uint32_t id) const { return LabelOptional(mailTemplateById, id); }

std::string LookupCache::LabelNpcOrGo(int32_t value) const
{
    if (value == 0)
        return "(none)";
    if (value > 0)
        return LabelCreature(static_cast<uint32_t>(value));
    // Negative encodes a gameobject entry (negated). Guard INT32_MIN.
    int64_t abs = -static_cast<int64_t>(value);
    return LabelGameObject(static_cast<uint32_t>(abs));
}

// --- Search ---------------------------------------------------------------

std::vector<NameEntry> LookupCache::Search(const std::vector<NameEntry>& list,
                                           const IdNameMap& byId,
                                           const std::string& query, size_t limit)
{
    std::vector<NameEntry> out;
    if (limit == 0 || list.empty())
        return out;

    const std::string q = Trim(query);

    // Empty query -> first `limit` entries by id (list is id-sorted).
    if (q.empty())
    {
        for (const NameEntry& e : list)
        {
            out.push_back(e);
            if (out.size() >= limit)
                break;
        }
        return out;
    }

    uint32_t id = 0;
    if (ParseId(q, id))
    {
        std::unordered_set<uint32_t> seen;

        // 1) Exact id match first.
        auto exact = byId.find(id);
        if (exact != byId.end())
        {
            out.push_back(NameEntry{ id, exact->second });
            seen.insert(id);
        }

        // 2) Ids whose decimal form starts with the query digits (prefix),
        //    id-ascending (list already sorted).
        for (const NameEntry& e : list)
        {
            if (out.size() >= limit)
                return out;
            if (seen.count(e.id))
                continue;
            const std::string idStr = std::to_string(e.id);
            if (idStr.size() > q.size() && idStr.compare(0, q.size(), q) == 0)
            {
                out.push_back(e);
                seen.insert(e.id);
            }
        }

        // 3) Names that contain the digit string (e.g. "Rank 2").
        for (const NameEntry& e : list)
        {
            if (out.size() >= limit)
                return out;
            if (seen.count(e.id))
                continue;
            if (IContains(e.name, q))
            {
                out.push_back(e);
                seen.insert(e.id);
            }
        }
        return out;
    }

    // Textual query: case-insensitive substring on name, id-ascending.
    for (const NameEntry& e : list)
    {
        if (IContains(e.name, q))
        {
            out.push_back(e);
            if (out.size() >= limit)
                break;
        }
    }
    return out;
}

std::vector<NameEntry> LookupCache::SearchItems(const std::string& query, size_t limit) const
{
    return Search(itemList, itemById, query, limit);
}

std::vector<NameEntry> LookupCache::SearchCreatures(const std::string& query, size_t limit) const
{
    return Search(creatureList, creatureById, query, limit);
}

std::vector<NameEntry> LookupCache::SearchGameObjects(const std::string& query, size_t limit) const
{
    return Search(gameObjectList, gameObjectById, query, limit);
}

std::vector<NameEntry> LookupCache::SearchQuests(const std::string& query, size_t limit) const
{
    return Search(questList, questById, query, limit);
}

std::vector<NameEntry> LookupCache::SearchFactions(const std::string& query, size_t limit) const
{
    return Search(factionList, factionById, query, limit);
}
std::vector<NameEntry> LookupCache::SearchFactionTemplates(const std::string& query, size_t limit) const
{
    return Search(factionTemplateList, factionTemplateById, query, limit);
}

std::vector<NameEntry> LookupCache::SearchSpells(const std::string& query, size_t limit) const
{
    return Search(spellList, spellById, query, limit);
}

std::vector<NameEntry> LookupCache::SearchAreas(const std::string& query, size_t limit) const
{
    return Search(areaList, areaById, query, limit);
}

std::vector<NameEntry> LookupCache::SearchSkills(const std::string& query, size_t limit) const
{
    return Search(skillList, skillById, query, limit);
}

std::vector<NameEntry> LookupCache::SearchTitles(const std::string& query, size_t limit) const
{
    return Search(titleList, titleById, query, limit);
}

std::vector<NameEntry> LookupCache::SearchMailTemplates(const std::string& query, size_t limit) const
{
    return Search(mailTemplateList, mailTemplateById, query, limit);
}

// --- Cache management -----------------------------------------------------

void LookupCache::Clear()
{
    itemById.clear();
    itemDisplay.clear();
    itemList.clear();
    itemsLoaded = false;

    creatureById.clear();
    creatureList.clear();
    creatureGossip.clear();
    creaturesLoaded = false;

    gameObjectById.clear();
    gameObjectList.clear();
    gameObjectsLoaded = false;

    questById.clear();
    questList.clear();
    questsLoaded = false;

    factionById.clear();
    factionList.clear();
    factionsLoaded = false;

    factionTemplateById.clear();
    factionTemplateList.clear();
    factionTemplatesLoaded = false;

    spellById.clear();
    spellList.clear();
    spellsLoaded = false;

    areaById.clear();
    areaList.clear();
    areasLoaded = false;

    skillById.clear();
    skillList.clear();
    skillsLoaded = false;

    titleById.clear();
    titleList.clear();
    titlesLoaded = false;

    mailTemplateById.clear();
    mailTemplateList.clear();
    mailTemplatesLoaded = false;
}

void LookupCache::ClearDbSourced()
{
    // Drop only the world-DB categories (item/creature/gameobject/quest). The
    // DBC-sourced names (faction/spell/area/skill/title/mailtemplate) usually come
    // from the client and must survive a DB (re)connect/disconnect.
    itemById.clear();
    itemDisplay.clear();
    itemList.clear();
    itemsLoaded = false;

    creatureById.clear();
    creatureList.clear();
    creatureGossip.clear();
    creaturesLoaded = false;

    gameObjectById.clear();
    gameObjectList.clear();
    gameObjectsLoaded = false;

    questById.clear();
    questList.clear();
    questsLoaded = false;
}
} // namespace qe
