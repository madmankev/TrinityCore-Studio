#pragma once

// Layer C (data) — id -> name resolution and name/id search for the three world
// tables referenced by quests: item_template, creature_template,
// gameobject_template. See docs/SPEC.md §6 (Layer C — LookupCache).
//
// Each category is bulk-loaded once (tens of thousands of rows is fine) into an
// id->name map (fast resolve) plus an id-sorted vector (iteration/search). Loads
// go through the IDatabase seam; resolvers and search are const and read only the
// cache — they never touch the DB. Nothing here throws across the API; DB failures
// surface as a DbError.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "db/DbTypes.h"

namespace we
{
class IDatabase;

// One (id, name) pair as returned by search / iteration.
struct NameEntry
{
    uint32_t id = 0;
    std::string name;
};

class LookupCache
{
public:
    // --- Explicit loads (idempotent) --------------------------------------
    // Each runs `SELECT entry, name FROM <table>` and fills the category cache.
    // Guarded by the per-category Loaded flag: a second call is a no-op that
    // returns ok, so it is safe to call before every access. Use Clear() (or the
    // Reload variants) to force a refresh. A Query failure leaves the cache
    // unloaded and returns the translated DbError.
    DbError LoadItems(IDatabase& db);
    DbError LoadCreatures(IDatabase& db);
    DbError LoadGameObjects(IDatabase& db);

    // Quest titles from the world DB: `SELECT ID, LogTitle FROM quest_template`.
    // Same idempotent/error semantics as the loaders above.
    DbError LoadQuests(IDatabase& db);

    // DBC-sourced categories (Faction/Spell/Area/Skill/Title/MailTemplate). These
    // names do not live in the world DB, so each loads best-effort from an optional
    // helper table or an optional JSON file (see .cpp). A missing table/file is
    // normal — the category is simply left empty (id-only), never an error.
    DbError LoadFaction(IDatabase& db);
    DbError LoadSpell(IDatabase& db);
    DbError LoadArea(IDatabase& db);
    DbError LoadSkill(IDatabase& db);
    DbError LoadTitle(IDatabase& db);
    DbError LoadMailTemplate(IDatabase& db);

    // Load all six DBC-sourced categories, best-effort (per-category failures are
    // ignored — these are all optional). Returns ok unless something unexpected
    // happened; individual empty categories are not failures.
    DbError LoadExtras(IDatabase& db);

    // Load every category. Returns the first failure encountered (later
    // categories are still attempted so as much as possible is cached).
    DbError LoadAll(IDatabase& db);

    // Force a fresh load even if already cached (drops the category first).
    DbError ReloadItems(IDatabase& db);
    DbError ReloadCreatures(IDatabase& db);
    DbError ReloadGameObjects(IDatabase& db);

    // --- Resolvers (cache-only; never hit the DB) -------------------------
    // NameOfX: the name for id, or "" if not cached / unknown.
    std::string NameOfItem(uint32_t id) const;
    std::string NameOfCreature(uint32_t id) const;
    std::string NameOfGameObject(uint32_t id) const;
    // creature_template.gossip_menu_id for a creature (0 = default gossip / none).
    uint32_t GossipMenuOfCreature(uint32_t id) const;
    // item_template.displayid for an item (0 = unknown); used for the icon chain.
    uint32_t ItemDisplayId(uint32_t entry) const;
    std::string NameOfQuest(uint32_t id) const;
    std::string NameOfFaction(uint32_t id) const;
    // creature_template.faction is a FactionTemplate.dbc id; resolves to the name of
    // the Faction.dbc faction it references (via LoadFactionTemplateNames).
    std::string NameOfFactionTemplate(uint32_t id) const;
    std::string NameOfSpell(uint32_t id) const;
    std::string NameOfArea(uint32_t id) const;
    std::string NameOfSkill(uint32_t id) const;
    std::string NameOfTitle(uint32_t id) const;
    std::string NameOfMailTemplate(uint32_t id) const;

    // LabelX: UI display string "<id> - <name>", or "<id> - <not found>" when the
    // id is unknown (e.g. cache not loaded or dangling reference).
    std::string LabelItem(uint32_t id) const;
    std::string LabelCreature(uint32_t id) const;
    std::string LabelGameObject(uint32_t id) const;

    // Quest label: "<id> - <title>", or "<id> - <unknown>" when unknown.
    std::string LabelQuest(uint32_t id) const;

    // DBC-category labels: names are optional, so a present entry renders
    // "<id> - <name>" while a missing entry renders just "<id>" (never a
    // "<not found>" placeholder).
    std::string LabelFaction(uint32_t id) const;
    std::string LabelFactionTemplate(uint32_t id) const;
    std::string LabelSpell(uint32_t id) const;
    std::string LabelArea(uint32_t id) const;
    std::string LabelSkill(uint32_t id) const;
    std::string LabelTitle(uint32_t id) const;
    std::string LabelMailTemplate(uint32_t id) const;

    // quest_template.RequiredNpcOrGo encoding: > 0 creature entry, < 0 negated
    // gameobject entry, 0 == none. Resolves to the matching label.
    std::string LabelNpcOrGo(int32_t value) const;

    // --- Search (cache-only) ----------------------------------------------
    // If `query` parses as a number, matches ids (exact first, then id-prefix,
    // then names containing the digits). Otherwise a case-insensitive substring
    // match on name (via we::IContains). Returns up to `limit` entries, best
    // match first. Empty query returns the first `limit` entries by id.
    std::vector<NameEntry> SearchItems(const std::string& query, size_t limit = 50) const;
    std::vector<NameEntry> SearchCreatures(const std::string& query, size_t limit = 50) const;
    std::vector<NameEntry> SearchGameObjects(const std::string& query, size_t limit = 50) const;
    std::vector<NameEntry> SearchQuests(const std::string& query, size_t limit = 50) const;
    std::vector<NameEntry> SearchFactions(const std::string& query, size_t limit = 50) const;
    std::vector<NameEntry> SearchFactionTemplates(const std::string& query, size_t limit = 50) const;
    std::vector<NameEntry> SearchSpells(const std::string& query, size_t limit = 50) const;
    std::vector<NameEntry> SearchAreas(const std::string& query, size_t limit = 50) const;
    std::vector<NameEntry> SearchSkills(const std::string& query, size_t limit = 50) const;
    std::vector<NameEntry> SearchTitles(const std::string& query, size_t limit = 50) const;
    std::vector<NameEntry> SearchMailTemplates(const std::string& query, size_t limit = 50) const;

    // --- Cache management --------------------------------------------------
    void Clear();          // drop all caches
    void ClearDbSourced(); // drop only world-DB categories, keep client-DBC names

    bool ItemsLoaded() const { return itemsLoaded; }
    bool CreaturesLoaded() const { return creaturesLoaded; }
    bool GameObjectsLoaded() const { return gameObjectsLoaded; }
    bool QuestsLoaded() const { return questsLoaded; }
    bool FactionsLoaded() const { return factionsLoaded; }
    bool FactionTemplatesLoaded() const { return factionTemplatesLoaded; }
    bool SpellsLoaded() const { return spellsLoaded; }
    bool AreasLoaded() const { return areasLoaded; }
    bool SkillsLoaded() const { return skillsLoaded; }
    bool TitlesLoaded() const { return titlesLoaded; }
    bool MailTemplatesLoaded() const { return mailTemplatesLoaded; }

    // Row counts (0 when not loaded), handy for status display.
    size_t ItemCount() const { return itemList.size(); }
    size_t CreatureCount() const { return creatureList.size(); }
    size_t GameObjectCount() const { return gameObjectList.size(); }
    size_t QuestCount() const { return questList.size(); }
    size_t FactionCount() const { return factionList.size(); }
    size_t FactionTemplateCount() const { return factionTemplateList.size(); }
    size_t SpellCount() const { return spellList.size(); }
    size_t AreaCount() const { return areaList.size(); }
    size_t SkillCount() const { return skillList.size(); }
    size_t TitleCount() const { return titleList.size(); }
    size_t MailTemplateCount() const { return mailTemplateList.size(); }

    // Populate a DBC-sourced category directly (e.g. from client DBC files) — takes
    // an id->name map, rebuilds the sorted search list, and marks the category loaded.
    void SetFactionNames(std::unordered_map<uint32_t, std::string> m);
    void SetFactionTemplateNames(std::unordered_map<uint32_t, std::string> m);
    void SetSpellNames(std::unordered_map<uint32_t, std::string> m);
    void SetAreaNames(std::unordered_map<uint32_t, std::string> m);
    void SetSkillNames(std::unordered_map<uint32_t, std::string> m);
    void SetTitleNames(std::unordered_map<uint32_t, std::string> m);

private:
    using IdNameMap = std::unordered_map<uint32_t, std::string>;

    // Move `m` into (byId,list) and set loaded=true, rebuilding the id-sorted list.
    static void Assign(IdNameMap m, IdNameMap& byId, std::vector<NameEntry>& list, bool& loaded);

    // Shared load: SELECT entry, name FROM `table` into the map + id-sorted list,
    // then set `loaded`. A no-op returning ok when `loaded` is already true.
    static DbError Load(IDatabase& db, const char* table, IdNameMap& byId,
                        std::vector<NameEntry>& list, bool& loaded);

    // Generic (id, name) load from an explicit SELECT. Used by the quest loader,
    // which reads `SELECT <idCol>, <nameCol> FROM <table>` instead of the fixed
    // (entry, name) shape of Load().
    static DbError LoadColumns(IDatabase& db, const char* table, const char* idCol,
                               const char* nameCol, IdNameMap& byId,
                               std::vector<NameEntry>& list, bool& loaded);

    // Best-effort load for a DBC-sourced category. Tries each candidate table in
    // turn (`SELECT id, name FROM <table>`); a missing table is skipped, not an
    // error. If no table yielded rows, tries to read+parse `jsonPath`
    // ({"123":"Name", ...}); any parse/IO error is swallowed. On no data the
    // category is left empty (id-only). Never throws.
    static DbError LoadNamed(IDatabase& db, const std::vector<std::string>& candidateTables,
                             const std::string& jsonPath, std::unordered_map<uint32_t, std::string>& outMap,
                             std::vector<NameEntry>& outVec);

    static std::string NameOf(const IdNameMap& byId, uint32_t id);
    static std::string Label(const IdNameMap& byId, uint32_t id);
    // Like Label but uses "<unknown>" for a missing id (quest titles).
    static std::string LabelUnknown(const IdNameMap& byId, uint32_t id);
    // Optional-name label: "id - name" when present, otherwise just "id".
    static std::string LabelOptional(const IdNameMap& byId, uint32_t id);
    static std::vector<NameEntry> Search(const std::vector<NameEntry>& list,
                                         const IdNameMap& byId,
                                         const std::string& query, size_t limit);

    // item_template
    std::unordered_map<uint32_t, std::string> itemById;
    std::unordered_map<uint32_t, uint32_t> itemDisplay;  // entry -> displayid (for icons)
    std::vector<NameEntry> itemList;
    bool itemsLoaded = false;

    // creature_template
    std::unordered_map<uint32_t, std::string> creatureById;
    std::unordered_map<uint32_t, uint32_t> creatureGossip;
    std::vector<NameEntry> creatureList;
    bool creaturesLoaded = false;

    // gameobject_template
    std::unordered_map<uint32_t, std::string> gameObjectById;
    std::vector<NameEntry> gameObjectList;
    bool gameObjectsLoaded = false;

    // quest_template (ID -> LogTitle)
    std::unordered_map<uint32_t, std::string> questById;
    std::vector<NameEntry> questList;
    bool questsLoaded = false;

    // DBC-sourced (optional names). Each has a `<cat>ClientSourced` flag: true once the
    // names were set from client DBC data (SetXNames). ClearDbSourced keeps client-sourced
    // categories (they must survive a DB reconnect) but drops DB/JSON-fallback ones so the
    // next connect reloads them from the possibly-different DB. See LookupCache.cpp.
    std::unordered_map<uint32_t, std::string> factionById;
    std::vector<NameEntry> factionList;
    bool factionsLoaded = false;
    bool factionsClientSourced = false;

    std::unordered_map<uint32_t, std::string> factionTemplateById;
    std::vector<NameEntry> factionTemplateList;
    bool factionTemplatesLoaded = false;
    bool factionTemplatesClientSourced = false;

    std::unordered_map<uint32_t, std::string> spellById;
    std::vector<NameEntry> spellList;
    bool spellsLoaded = false;
    bool spellsClientSourced = false;

    std::unordered_map<uint32_t, std::string> areaById;
    std::vector<NameEntry> areaList;
    bool areasLoaded = false;
    bool areasClientSourced = false;

    std::unordered_map<uint32_t, std::string> skillById;
    std::vector<NameEntry> skillList;
    bool skillsLoaded = false;
    bool skillsClientSourced = false;

    std::unordered_map<uint32_t, std::string> titleById;
    std::vector<NameEntry> titleList;
    bool titlesLoaded = false;
    bool titlesClientSourced = false;

    std::unordered_map<uint32_t, std::string> mailTemplateById;
    std::vector<NameEntry> mailTemplateList;
    bool mailTemplatesLoaded = false;
    bool mailTemplatesClientSourced = false;
};
} // namespace we
