#include "editors/adt/MapSpawnRepository.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>
#include <initializer_list>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "data/SqlBuild.h"

namespace we
{
namespace
{
// Merge the optional spawn-gating associations (game events, pools, spawn groups) into a
// batch of spawns by guid. Each association is a separate, fail-soft query — a missing table
// (some DBs lack pool_*/spawn_group*) just leaves those attributes at their defaults, never
// breaking the core spawn load. `T` is MapSpawn or MapGameObject (both carry the fields).
template <class T>
void ApplyAssociations(IDatabase& db, const char* eventTable, const char* poolTable, int sgType,
                       std::vector<T>& spawns)
{
    if (spawns.empty())
        return;
    std::unordered_map<uint32_t, size_t> byGuid;
    byGuid.reserve(spawns.size());
    for (size_t i = 0; i < spawns.size(); ++i)
        byGuid[spawns[i].guid] = i;

    DbError err;

    // 1) Game events: guid -> signed eventEntry.
    if (auto rs = db.Query(std::string("SELECT guid, eventEntry FROM ") + eventTable, err))
        while (rs->Next())
        {
            auto it = byGuid.find(rs->GetUInt32(0));
            if (it != byGuid.end())
                spawns[it->second].eventEntry = rs->GetInt32(1);
        }

    // 2) Pooling: group members by pool, keep the top max_limit (chance desc, then guid), mark
    //    the rest hidden. Approximates the live random pick with a deterministic subset.
    {
        struct Member { float chance; uint32_t guid; size_t idx; };
        std::unordered_map<uint32_t, std::vector<Member>> pools;
        if (auto rs = db.Query(std::string("SELECT guid, pool_entry, chance FROM ") + poolTable, err))
            while (rs->Next())
            {
                auto it = byGuid.find(rs->GetUInt32(0));
                if (it == byGuid.end())
                    continue;
                pools[rs->GetUInt32(1)].push_back({rs->GetFloat(2), rs->GetUInt32(0), it->second});
            }
        if (!pools.empty())
        {
            std::unordered_map<uint32_t, uint32_t> maxLimit;
            if (auto rs = db.Query("SELECT entry, max_limit FROM pool_template", err))
                while (rs->Next())
                    maxLimit[rs->GetUInt32(0)] = rs->GetUInt32(1);
            for (auto& kv : pools)
            {
                auto lit = maxLimit.find(kv.first);
                uint32_t lim = (lit != maxLimit.end() && lit->second != 0)
                                   ? lit->second
                                   : static_cast<uint32_t>(kv.second.size());   // 0/unknown = all
                auto& mem = kv.second;
                std::sort(mem.begin(), mem.end(), [](const Member& a, const Member& b) {
                    if (a.chance != b.chance) return a.chance > b.chance;
                    return a.guid < b.guid;
                });
                for (size_t i = lim; i < mem.size(); ++i)
                    spawns[mem[i].idx].poolHidden = true;
            }
        }
    }

    // 3) Spawn groups: mark members of a MANUAL_SPAWN group (groupFlags bit 0x4).
    if (auto rs = db.Query("SELECT sg.spawnId, sgt.groupFlags FROM spawn_group sg "
                           "JOIN spawn_group_template sgt ON sgt.groupId = sg.groupId "
                           "WHERE sg.spawnType = " + std::to_string(sgType), err))
        while (rs->Next())
        {
            auto it = byGuid.find(rs->GetUInt32(0));
            if (it != byGuid.end() && (rs->GetUInt32(1) & 0x4u))
                spawns[it->second].groupManual = true;
        }
}

std::string LowerColumn(std::string value)
{
    for (char& c : value)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

bool HasColumn(const std::set<std::string>& cols, const char* name)
{
    // An empty set means schema introspection was unavailable (typically an offline export helper),
    // so use the stable TrinityCore spelling as a best-effort fallback.
    return cols.empty() || cols.count(LowerColumn(name)) != 0;
}

std::string ColumnOr(const char* alias, const std::set<std::string>& cols, const char* name,
                     const char* fallback)
{
    return HasColumn(cols, name) ? (std::string(alias) + ".`" + name + "`") : fallback;
}

std::string SpawnEntryColumn(const std::set<std::string>& cols)
{
    // Current AzerothCore uses id1/id2/id3 while TrinityCore uses id. Prefer id1 when present.
    if (!cols.empty() && cols.count("id1") != 0)
        return "id1";
    return "id";
}

// Return the first physical column matching one of a family of server/custom-schema spellings.
// ExistingCols lowercases names, and MySQL treats column names case-insensitively, so the lowercase
// spelling is safe to quote in emitted SQL. An empty metadata set means introspection was unavailable;
// callers must choose an established legacy fallback instead of guessing optional custom columns.
std::string FirstColumn(const std::set<std::string>& cols,
                        std::initializer_list<const char*> candidates)
{
    if (cols.empty())
        return {};
    for (const char* candidate : candidates)
        if (cols.count(LowerColumn(candidate)) != 0)
            return LowerColumn(candidate);
    return {};
}

std::string DisplayColumnOr(const char* alias, const std::set<std::string>& cols,
                            bool legacySpawnFallback)
{
    // A few custom server packs call this persistent server display field displayid/display_id;
    // TrinityCore calls the exact same CreatureDisplayInfo id modelid. Prefer an explicit display
    // column when it exists, then retain modelid as the documented TrinityCore fallback.
    std::string column = FirstColumn(cols, {"displayid", "display_id", "modelid"});
    if (!column.empty())
        return std::string(alias) + ".`" + column + "`";
    if (cols.empty() && legacySpawnFallback)
        return std::string(alias) + ".`modelid`";
    return "0";
}

float PositiveScale(float value)
{
    return std::isfinite(value) && value > 0.0f ? value : 1.0f;
}

// A stable hash lets the editor preview one member of a weighted server model list without model
// popping every frame/reload. A real worldserver rolls this when it creates the creature; persistent
// spawn/event overrides still win exactly and need no approximation.
uint32_t StableDisplayHash(uint32_t guid, uint32_t entry)
{
    uint32_t x = guid ? guid : entry * 0x9E3779B9u;
    x ^= entry + 0x85EBCA6Bu + (x << 6) + (x >> 2);
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

struct TemplateDisplayRow
{
    uint32_t entry = 0;
    uint32_t displayId = 0;
    uint32_t index = 0;
    float scale = 1.0f;
    float probability = 0.0f;
};

const TemplateDisplayRow* ChooseTemplateDisplay(const std::vector<TemplateDisplayRow>& rows,
                                                 uint32_t guid, uint32_t entry)
{
    if (rows.empty())
        return nullptr;
    double total = 0.0;
    for (const TemplateDisplayRow& row : rows)
        if (std::isfinite(row.probability) && row.probability > 0.0f)
            total += row.probability;
    if (total <= 0.0)
        return &rows.front();   // malformed/legacy zero-weight rows: server's first defined model

    const double roll = (static_cast<double>(StableDisplayHash(guid, entry)) /
                         4294967296.0) * total;
    double cursor = 0.0;
    for (const TemplateDisplayRow& row : rows)
    {
        if (!std::isfinite(row.probability) || row.probability <= 0.0f)
            continue;
        cursor += row.probability;
        if (roll < cursor)
            return &row;
    }
    return &rows.back();   // guards float round-off at the top of the interval
}

void ResolveBaseDisplay(MapSpawn& spawn)
{
    if (spawn.spawnDisplayId != 0)
    {
        spawn.displayId = spawn.spawnDisplayId;
        spawn.displayScale = PositiveScale(spawn.spawnDisplayScale);
        spawn.displaySource = CreatureDisplaySource::SpawnOverride;
        return;
    }
    spawn.displayId = spawn.templateDisplayId;
    spawn.displayScale = PositiveScale(spawn.templateDisplayScale);
    spawn.displaySource = spawn.templateDisplaySource;
}

// Modern AzerothCore moved template visual rows out of creature_template.modelid1..4. Load all
// rows once, pick a stable weighted preview per spawn, and preserve the selected DisplayScale so
// World Editor geometry matches the scale the server supplies to the client.
void ApplyTemplateModelDisplays(IDatabase& db, std::vector<MapSpawn>& spawns)
{
    const std::set<std::string> cols = sql::ExistingCols(db, "creature_template_model");
    const std::string entryCol = FirstColumn(cols, {"creatureid", "creature_id", "entry"});
    const std::string displayCol = FirstColumn(cols, {"creaturedisplayid", "displayid", "display_id", "modelid"});
    if (entryCol.empty() || displayCol.empty() || spawns.empty())
        return;  // old TrinityCore or a custom schema that does not expose server model rows
    const std::string indexCol = FirstColumn(cols, {"idx", "index", "modelindex"});
    const std::string scaleCol = FirstColumn(cols, {"displayscale", "display_scale", "scale"});
    const std::string probabilityCol = FirstColumn(cols, {"probability", "chance"});

    // Do not read every model row in a large AzerothCore world DB on every map switch. The map
    // may have many spawn GUIDs but usually far fewer distinct template entries, so a compact IN
    // list gives the database an indexed, one-pass lookup without duplicating model rows per spawn.
    std::set<uint32_t> requestedEntries;
    for (const MapSpawn& spawn : spawns)
        if (spawn.entry != 0)
            requestedEntries.insert(spawn.entry);
    if (requestedEntries.empty())
        return;
    std::string inList;
    for (uint32_t entry : requestedEntries)
    {
        if (!inList.empty())
            inList += ',';
        inList += std::to_string(entry);
    }
    const std::string query = "SELECT `" + entryCol + "`, " +
        (indexCol.empty() ? "0" : ("`" + indexCol + "`")) + ", `" + displayCol + "`, " +
        (scaleCol.empty() ? "1" : ("`" + scaleCol + "`")) + ", " +
        (probabilityCol.empty() ? "1" : ("`" + probabilityCol + "`")) +
        " FROM creature_template_model WHERE `" + entryCol + "` IN (" + inList + ")";
    DbError err;
    auto rs = db.Query(query, err);
    if (!rs)
        return;  // optional table; never prevent the rest of the map from opening

    std::unordered_map<uint32_t, std::vector<TemplateDisplayRow>> rowsByEntry;
    while (rs->Next())
    {
        TemplateDisplayRow row;
        row.entry = rs->GetUInt32(0);
        row.index = rs->GetUInt32(1);
        row.displayId = rs->GetUInt32(2);
        row.scale = PositiveScale(rs->GetFloat(3));
        row.probability = rs->GetFloat(4);
        if (row.entry != 0 && row.displayId != 0)
            rowsByEntry[row.entry].push_back(std::move(row));
    }
    for (auto& pair : rowsByEntry)
        std::sort(pair.second.begin(), pair.second.end(), [](const TemplateDisplayRow& a,
                                                              const TemplateDisplayRow& b) {
            if (a.index != b.index)
                return a.index < b.index;
            return a.displayId < b.displayId;
        });

    for (MapSpawn& spawn : spawns)
    {
        const auto found = rowsByEntry.find(spawn.entry);
        if (found == rowsByEntry.end())
            continue;
        const std::vector<TemplateDisplayRow>& rows = found->second;
        const TemplateDisplayRow* selected = ChooseTemplateDisplay(rows, spawn.guid, spawn.entry);
        if (!selected)
            continue;
        // AzerothCore honors a creature.displayid only when it names one of this template's
        // CreatureDisplayID rows. When it does, that row's DisplayScale is the one delivered to
        // the client—not the scale of whichever random fallback row we chose for the preview.
        for (const TemplateDisplayRow& row : rows)
            if (spawn.spawnDisplayId != 0 && row.displayId == spawn.spawnDisplayId)
            {
                spawn.spawnDisplayScale = row.scale;
                break;
            }
        spawn.templateDisplayId = selected->displayId;
        spawn.templateDisplayScale = selected->scale;
        spawn.templateDisplaySource = CreatureDisplaySource::TemplateModel;
        spawn.templateDisplayIndex = static_cast<uint16_t>(std::min<uint32_t>(selected->index, 0xFFFFu));
        spawn.templateDisplayCount = static_cast<uint16_t>(std::min<size_t>(rows.size(), 0xFFFFu));
        ResolveBaseDisplay(spawn);
    }
}

// game_event_model_equip is the DB-backed server path that changes a loaded NPC's display id at
// runtime. Keep all event alternatives on the spawn so changing the World Editor's Events filter
// immediately switches the same M2/display skin without a map reload.
void ApplyEventModelDisplays(IDatabase& db, uint32_t mapId, std::vector<MapSpawn>& spawns)
{
    const std::set<std::string> cols = sql::ExistingCols(db, "game_event_model_equip");
    const std::string guidCol = FirstColumn(cols, {"guid", "spawnid"});
    const std::string eventCol = FirstColumn(cols, {"evententry", "event_entry"});
    const std::string displayCol = FirstColumn(cols, {"modelid", "displayid", "display_id"});
    if (guidCol.empty() || eventCol.empty() || displayCol.empty() || spawns.empty())
        return;

    std::unordered_map<uint32_t, MapSpawn*> byGuid;
    byGuid.reserve(spawns.size());
    for (MapSpawn& spawn : spawns)
        byGuid[spawn.guid] = &spawn;

    const std::string query = "SELECT e.`" + guidCol + "`, e.`" + eventCol + "`, e.`" +
        displayCol + "` FROM game_event_model_equip e JOIN creature c ON c.guid=e.`" + guidCol +
        "` WHERE c.map=" + std::to_string(mapId);
    DbError err;
    auto rs = db.Query(query, err);
    if (!rs)
        return;
    while (rs->Next())
    {
        const auto it = byGuid.find(rs->GetUInt32(0));
        const int32_t eventEntry = rs->GetInt32(1);
        const uint32_t displayId = rs->GetUInt32(2);
        if (it == byGuid.end() || eventEntry == 0 || displayId == 0)
            continue;
        it->second->eventDisplayOverrides.push_back({eventEntry, displayId});
    }
    for (MapSpawn& spawn : spawns)
        std::sort(spawn.eventDisplayOverrides.begin(), spawn.eventDisplayOverrides.end(),
                  [](const GameEventCreatureDisplayOverride& a,
                     const GameEventCreatureDisplayOverride& b) {
                      if (a.eventEntry != b.eventEntry)
                          return a.eventEntry < b.eventEntry;
                      return a.displayId < b.displayId;
                  });
}

// Resolve server-backed combat behavior fields without assuming one core's schema. AzerothCore
// currently exposes creature_template.detection_range; custom/older packs commonly use aggro_radius
// and a few add a per-template/per-spawn leash_distance. Missing columns are normal — Studio keeps a
// clearly-labelled preview fallback rather than inventing writes to unrelated core fields.
void ApplyAiBehaviorFields(IDatabase& db, uint32_t mapId, const std::string& entryCol,
                           const std::set<std::string>& creatureCols,
                           const std::set<std::string>& templateCols,
                           std::vector<MapSpawn>& spawns)
{
    if (spawns.empty())
        return;
    const std::string spawnAggro = FirstColumn(creatureCols,
        {"aggro_radius", "aggro_range", "detection_range", "detectionrange", "sight_distance"});
    const std::string templateAggro = FirstColumn(templateCols,
        {"detection_range", "aggro_radius", "aggro_range", "detectionrange", "sight_distance"});
    const std::string spawnLeash = FirstColumn(creatureCols,
        {"leash_distance", "leash_range", "chase_distance"});
    const std::string templateLeash = FirstColumn(templateCols,
        {"leash_distance", "leash_range", "chase_distance"});
    if (spawnAggro.empty() && templateAggro.empty() && spawnLeash.empty() && templateLeash.empty())
        return;

    auto col = [](const char* alias, const std::string& name) {
        return name.empty() ? std::string("NULL")
                            : (std::string(alias) + ".`" + name + "`");
    };
    const std::string query =
        "SELECT c.guid, " + col("c", spawnAggro) + ", " + col("ct", templateAggro) + ", " +
        col("c", spawnLeash) + ", " + col("ct", templateLeash) +
        " FROM creature c JOIN creature_template ct ON ct.entry=c.`" + entryCol + "` WHERE c.map=" +
        std::to_string(mapId);
    DbError err;
    auto rs = db.Query(query, err);
    if (!rs)
        return;

    std::unordered_map<uint32_t, MapSpawn*> byGuid;
    byGuid.reserve(spawns.size());
    for (MapSpawn& spawn : spawns)
        byGuid[spawn.guid] = &spawn;
    auto saneDistance = [](float value, float fallback) {
        return std::isfinite(value) && value >= 0.0f ? value : fallback;
    };
    while (rs->Next())
    {
        const auto found = byGuid.find(rs->GetUInt32(0));
        if (found == byGuid.end())
            continue;
        MapSpawn& spawn = *found->second;
        spawn.hasAggroRadiusColumn = !spawnAggro.empty() || !templateAggro.empty();
        spawn.hasLeashDistanceColumn = !spawnLeash.empty() || !templateLeash.empty();

        // A positive spawn override wins. A zero-valued non-null spawn column conventionally means
        // "inherit template" on supported custom packs, while an explicitly nullable value also
        // falls through safely to its template default.
        const float cAggro = rs->GetFloat(1), tAggro = rs->GetFloat(2);
        if (!spawnAggro.empty() && !rs->IsNull(1) && cAggro > 0.0f)
        {
            spawn.aiBehavior.aggroRadius = saneDistance(cAggro, spawn.aiBehavior.aggroRadius);
            spawn.aggroRadiusFromSpawn = true;
        }
        else if (!templateAggro.empty() && !rs->IsNull(2))
            spawn.aiBehavior.aggroRadius = saneDistance(tAggro, spawn.aiBehavior.aggroRadius);
        else if (!spawnAggro.empty() && !rs->IsNull(1))
            spawn.aiBehavior.aggroRadius = saneDistance(cAggro, spawn.aiBehavior.aggroRadius);

        const float cLeash = rs->GetFloat(3), tLeash = rs->GetFloat(4);
        if (!spawnLeash.empty() && !rs->IsNull(3) && cLeash > 0.0f)
        {
            spawn.aiBehavior.leashDistance = saneDistance(cLeash, spawn.aiBehavior.leashDistance);
            spawn.leashDistanceFromSpawn = true;
        }
        else if (!templateLeash.empty() && !rs->IsNull(4))
            spawn.aiBehavior.leashDistance = saneDistance(tLeash, spawn.aiBehavior.leashDistance);
        else if (!spawnLeash.empty() && !rs->IsNull(3))
            spawn.aiBehavior.leashDistance = saneDistance(cLeash, spawn.aiBehavior.leashDistance);
    }
}
} // namespace

DbError MapSpawnRepository::LoadSpawnsForMap(IDatabase& db, uint32_t mapId,
                                            std::vector<MapSpawn>& out) const
{
    out.clear();

    const std::set<std::string> creatureCols = sql::ExistingCols(db, "creature");
    const std::set<std::string> templateCols = sql::ExistingCols(db, "creature_template");
    const std::set<std::string> addonCols = sql::ExistingCols(db, "creature_addon");
    const std::set<std::string> templateAddonCols = sql::ExistingCols(db, "creature_template_addon");
    const std::string entryCol = SpawnEntryColumn(creatureCols);

    // Alias every projected field into a stable position. This accommodates TrinityCore's id/modelid
    // spawn shape and AzerothCore's id1/id2/id3 shape without making the renderer/UI care which core
    // supplied the map. In addition to the legacy names, accept custom server-side displayid fields:
    // they already contain a CreatureDisplayInfo id sent to clients and therefore must beat a template
    // fallback. Modern AzerothCore template rows are resolved in a second pass below so their 1:N
    // model list never duplicates map spawns in this primary query.
    const std::string spawnDisplay = DisplayColumnOr("c", creatureCols, true);
    const bool hasSpawnDisplayColumn = creatureCols.empty() ||
        !FirstColumn(creatureCols, {"displayid", "display_id", "modelid"}).empty();
    const std::string templateDirectDisplay = DisplayColumnOr("ct", templateCols, false);
    const std::string sql =
        "SELECT c.guid, c.`" + entryCol + "`, c.position_x, c.position_y, c.position_z, c.orientation, " +
        ColumnOr("c", creatureCols, "MovementType", "0") + ", " +
        ColumnOr("c", creatureCols, "wander_distance", "0") + ", " +
        spawnDisplay + ", " + templateDirectDisplay + ", " +
        ColumnOr("ct", templateCols, "modelid1", "0") + ", " +
        ColumnOr("ct", templateCols, "modelid2", "0") + ", " +
        ColumnOr("ct", templateCols, "modelid3", "0") + ", " +
        ColumnOr("ct", templateCols, "modelid4", "0") + ", " +
        ColumnOr("ct", templateCols, "scale", "1") + ", " +
        ColumnOr("ct", templateCols, "speed_walk", "1") + ", " +
        ColumnOr("ct", templateCols, "speed_run", "1.14286") + ", " +
        ColumnOr("cta", templateAddonCols, "path_id", "0") + ", " +
        (HasColumn(addonCols, "guid") ? "ca.guid" : "NULL") + ", " +
        ColumnOr("ca", addonCols, "path_id", "0") + ", " +
        ColumnOr("c", creatureCols, "phaseMask", "1") + ", " +
        ColumnOr("c", creatureCols, "spawnMask", "1") +
        " FROM creature c JOIN creature_template ct ON ct.entry = c.`" + entryCol + "` " +
        "LEFT JOIN creature_template_addon cta ON cta.entry = c.`" + entryCol + "` " +
        "LEFT JOIN creature_addon ca ON ca.guid = c.guid WHERE c.map = " + std::to_string(mapId);

    DbError err;
    auto rs = db.Query(sql, err);
    if (!rs)
        return err;

    while (rs->Next())
    {
        MapSpawn s;
        s.guid = rs->GetUInt32(0);
        s.entry = rs->GetUInt32(1);
        s.x = rs->GetFloat(2);
        s.y = rs->GetFloat(3);
        s.z = rs->GetFloat(4);
        s.o = rs->GetFloat(5);
        s.movementType = static_cast<uint8_t>(rs->GetUInt32(6));
        s.wanderDistance = rs->GetFloat(7);

        // Resolve the legacy/default template display now. A nonzero persistent server-side spawn
        // override wins over it; ApplyTemplateModelDisplays below then replaces only the template
        // fallback with the authoritative AzerothCore creature_template_model row when available.
        s.spawnDisplayId = rs->GetUInt32(8);
        s.hasSpawnDisplayOverrideColumn = hasSpawnDisplayColumn;
        s.templateDisplayId = rs->GetUInt32(9);   // custom creature_template.displayid/modelid, if any
        if (s.templateDisplayId == 0)
            for (int col = 10; col <= 13; ++col)
            {
                const uint32_t model = rs->GetUInt32(col);
                if (model != 0) { s.templateDisplayId = model; break; }
            }
        s.templateDisplayScale = 1.0f;
        s.templateDisplaySource = s.templateDisplayId != 0
                                      ? CreatureDisplaySource::LegacyTemplate
                                      : CreatureDisplaySource::None;
        ResolveBaseDisplay(s);

        s.scale = PositiveScale(rs->GetFloat(14));
        s.speedWalk = rs->GetFloat(15);
        if (!std::isfinite(s.speedWalk) || s.speedWalk <= 0.0f)
            s.speedWalk = 1.0f;
        s.speedRun = rs->GetFloat(16);
        if (!std::isfinite(s.speedRun) || s.speedRun <= 0.0f)
            s.speedRun = 1.14286f;
        s.templatePathId = rs->GetUInt32(17);
        s.hasSpawnAddon = !rs->IsNull(18);
        s.spawnPathId = rs->GetUInt32(19);
        s.pathId = s.hasSpawnAddon ? s.spawnPathId : s.templatePathId;
        s.phaseMask = rs->GetUInt32(20);
        if (s.phaseMask == 0)
            s.phaseMask = 1;
        s.spawnMask = rs->GetUInt32(21);
        if (s.spawnMask == 0)
            s.spawnMask = 1;
        out.push_back(std::move(s));
    }

    // Resolve server-owned model sources after the one-row-per-spawn query. This is essential for
    // AzerothCore: its current schema stores visual display ids in creature_template_model rather
    // than the legacy creature_template.modelid1..4 fields. Event replacements stay attached to
    // each spawn and are selected live by SpawnFilter in NpcLayer.
    ApplyTemplateModelDisplays(db, out);
    ApplyEventModelDisplays(db, mapId, out);
    ApplyAiBehaviorFields(db, mapId, entryCol, creatureCols, templateCols, out);

    // Merge game-event / pool / spawn-group gating (spawn_group spawnType 0 = creature).
    ApplyAssociations(db, "game_event_creature", "pool_creature", 0, out);

    // Resolve equipped weapons -> ItemDisplayInfo ids, keyed by guid. The chosen equip set is
    // creature.equipment_id when > 0, else the lowest-numbered set for that creature. Fail-soft:
    // a missing creature_equip_template / item_template must not break the spawn load.
    {
        std::unordered_map<uint32_t, MapSpawn*> byGuid;
        byGuid.reserve(out.size());
        for (MapSpawn& s : out)
            byGuid[s.guid] = &s;
        const std::string equipId = ColumnOr("c", creatureCols, "equipment_id", "0");
        const std::string eq =
            "SELECT c.guid, it1.displayid, it2.displayid, it3.displayid "
            "FROM creature c "
            "JOIN creature_equip_template cet ON cet.CreatureID = c.`" + entryCol + "` AND cet.ID = "
            "  (CASE WHEN " + equipId + " > 0 THEN " + equipId +
            "        ELSE (SELECT MIN(e2.ID) FROM creature_equip_template e2 WHERE e2.CreatureID = c.`" +
                         entryCol + "`) END) "
            "LEFT JOIN item_template it1 ON it1.entry = cet.ItemID1 "
            "LEFT JOIN item_template it2 ON it2.entry = cet.ItemID2 "
            "LEFT JOIN item_template it3 ON it3.entry = cet.ItemID3 "
            "WHERE c.map = " + std::to_string(mapId);
        DbError eqErr;
        if (auto ers = db.Query(eq, eqErr))
        {
            while (ers->Next())
            {
                auto it = byGuid.find(ers->GetUInt32(0));
                if (it == byGuid.end())
                    continue;
                it->second->weaponDisplay[0] = ers->GetUInt32(1);
                it->second->weaponDisplay[1] = ers->GetUInt32(2);
                it->second->weaponDisplay[2] = ers->GetUInt32(3);
            }
        }
    }
    return DbError{};
}

DbError MapSpawnRepository::LoadCreatureFormationsForMap(
    IDatabase& db, uint32_t mapId, std::vector<CreatureFormationMember>& out) const
{
    out.clear();
    DbError err;
    // SELECT cf.* keeps this loader schema-adaptive: older databases simply lack point_1/point_2,
    // while newer revisions expose them through sql::Row by name.
    auto rs = db.Query("SELECT cf.* FROM creature_formations cf "
                       "JOIN creature c ON c.guid = cf.memberGUID WHERE c.map=" +
                       std::to_string(mapId) + " ORDER BY cf.leaderGUID, cf.memberGUID", err);
    if (!rs)
        return err;
    while (rs->Next())
    {
        sql::Row row(*rs);
        CreatureFormationMember f;
        f.memberGuid = row.U("memberGUID");
        f.leaderGuid = row.U("leaderGUID");
        f.distance = row.F("dist");
        f.angle = row.F("angle");
        f.groupAi = static_cast<uint8_t>(row.U("groupAI"));
        f.point1 = row.Ua({"point_1", "point1"});
        f.point2 = row.Ua({"point_2", "point2"});
        if (f.memberGuid != 0)
            out.push_back(std::move(f));
    }
    return DbError{};
}

DbError MapSpawnRepository::SaveCreatureFormation(IDatabase& db,
                                                   const CreatureFormationMember& formation) const
{
    if (formation.memberGuid == 0 || formation.leaderGuid == 0)
        return DbError{false, "Formation member and leader guids are required."};
    if (!std::isfinite(formation.distance) || formation.distance < 0.0f ||
        !std::isfinite(formation.angle) || formation.angle < 0.0f || formation.angle > 360.0f)
        return DbError{false, "Formation distance must be non-negative and angle must be between 0 and 360 degrees."};

    static const std::vector<std::string> cols = sql::SplitCols(
        "memberGUID, leaderGUID, dist, angle, groupAI, point_1, point_2");
    sql::ValueList values(db);
    values.UInt(formation.memberGuid);
    values.UInt(formation.leaderGuid);
    values.Float(formation.distance);
    values.Float(formation.angle);
    values.UInt(formation.groupAi);
    values.UInt(formation.point1);
    values.UInt(formation.point2);
    const std::set<std::string> existing = sql::ExistingCols(db, "creature_formations");

    db.BeginTransaction();
    DbError err;
    db.Execute(sql::FilteredUpsert("creature_formations", cols, values.tokens, existing, "memberGUID"), err);
    if (!err.ok)
    {
        db.Rollback();
        return err;
    }
    return db.Commit();
}

DbError MapSpawnRepository::DeleteCreatureFormation(IDatabase& db, uint32_t memberGuid) const
{
    if (memberGuid == 0)
        return DbError{false, "A formation member guid is required."};
    db.BeginTransaction();
    DbError err;
    db.Execute("DELETE FROM creature_formations WHERE memberGUID=" + std::to_string(memberGuid), err);
    if (!err.ok)
    {
        db.Rollback();
        return err;
    }
    return db.Commit();
}

DbError MapSpawnRepository::LoadWaypointPath(IDatabase& db, uint32_t pathId, WaypointPath& out) const
{
    out.id = pathId;
    out.points.clear();
    if (pathId == 0)
        return DbError{};

    // AzerothCore's base waypoint_data may omit the TrinityCore event/action extension columns.
    // SELECT * + Row-by-name preserves the common path fields and simply defaults unavailable
    // optional fields, while the schema-adaptive writer below only emits columns that exist.
    const std::string sql = "SELECT * FROM waypoint_data WHERE id = " + std::to_string(pathId) +
                            " ORDER BY point";

    DbError err;
    auto rs = db.Query(sql, err);
    if (!rs)
        return err;

    while (rs->Next())
    {
        sql::Row row(*rs);
        WaypointPoint p;
        p.point = row.U("point");
        p.x = row.F("position_x");
        p.y = row.F("position_y");
        p.z = row.F("position_z");
        p.o = row.F("orientation");
        p.delay = row.U("delay");
        p.moveType = static_cast<uint8_t>(row.U("move_type"));
        p.moveEvent = static_cast<uint8_t>(row.U("move_event"));
        p.action = row.U("action");
        const int chanceColumn = rs->ColumnIndex("action_chance");
        p.actionChance = chanceColumn >= 0 ? static_cast<uint8_t>(rs->GetUInt32(chanceColumn)) : 100;
        p.waypointGuid = row.U("wpguid");
        p.sourcePoint = p.point;
        p.sourceExists = true;
        out.points.push_back(std::move(p));
    }
    return DbError{};
}

namespace
{
// Standard `waypoint_data` column list for 3.3.5a. Existing rows are never REPLACEd: the
// visual editor moves them through temporary point numbers and updates them in place, retaining
// any project-specific columns that this build does not know about.
const std::vector<std::string>& WaypointCols()
{
    static const std::vector<std::string> cols = sql::SplitCols(
        "id, point, position_x, position_y, position_z, orientation, delay, move_type, "
        "move_event, action, action_chance, wpguid");
    return cols;
}

bool HasWaypointColumn(const std::set<std::string>& cols, const char* col)
{
    return cols.empty() || cols.count(col) != 0;
}

void SetWaypointSaveError(DbError& err, const std::string& message)
{
    err.ok = false;
    err.message = message;
}

bool ExecuteWaypointStep(IDatabase& db, const std::string& sqlText, DbError& err)
{
    db.Execute(sqlText, err);
    return err.ok;
}

// Emit a targeted update for a row that was temporarily moved from `temporaryPoint`. All modeled
// standard fields are written, but custom fields are intentionally left untouched.
bool UpdateWaypointRow(IDatabase& db, uint32_t pathId, uint32_t temporaryPoint,
                       const WaypointPoint& p, const std::set<std::string>& existingCols,
                       DbError& err)
{
    std::string set = "point=" + std::to_string(p.point);
    auto add = [&](const char* col, const std::string& value) {
        if (!HasWaypointColumn(existingCols, col))
            return;
        set += ", ";
        set += col;
        set += "=";
        set += value;
    };
    add("position_x", sql::FmtFloat(p.x));
    add("position_y", sql::FmtFloat(p.y));
    add("position_z", sql::FmtFloat(p.z));
    add("orientation", sql::FmtFloat(p.o));
    add("delay", std::to_string(p.delay));
    add("move_type", std::to_string(p.moveType));
    add("move_event", std::to_string(p.moveEvent));
    add("action", std::to_string(p.action));
    add("action_chance", std::to_string(p.actionChance));
    add("wpguid", std::to_string(p.waypointGuid));
    return ExecuteWaypointStep(db, "UPDATE waypoint_data SET " + set + " WHERE id=" +
                                   std::to_string(pathId) + " AND point=" +
                                   std::to_string(temporaryPoint), err);
}

bool InsertWaypointRow(IDatabase& db, uint32_t pathId, const WaypointPoint& p,
                       const std::set<std::string>& existingCols, DbError& err)
{
    sql::ValueList values(db);
    values.UInt(pathId);
    values.UInt(p.point);
    values.Float(p.x);
    values.Float(p.y);
    values.Float(p.z);
    values.Float(p.o);
    values.UInt(p.delay);
    values.UInt(p.moveType);
    values.UInt(p.moveEvent);
    values.UInt(p.action);
    values.UInt(p.actionChance);
    values.UInt(p.waypointGuid);
    return ExecuteWaypointStep(db, sql::FilteredInsert("INSERT", "waypoint_data", WaypointCols(),
                                                        values.tokens, existingCols), err);
}

// Called inside a transaction by SaveWaypointPath and SaveWaypointPathAndBindCreature.
bool SaveWaypointPathInTransaction(IDatabase& db, const WaypointPath& path, DbError& err)
{
    if (path.id == 0)
    {
        SetWaypointSaveError(err, "Waypoint path id must be non-zero.");
        return false;
    }
    // An empty route leaves no waypoint_data row, which makes MAX(id)+1 allocation ambiguous and
    // is not a useful WaypointMotionGenerator route anyway. Users can clear a spawn override instead.
    if (path.points.empty())
    {
        SetWaypointSaveError(err, "A waypoint path must contain at least one point.");
        return false;
    }

    // Validate final primary keys before changing anything. Point order in the vector is the
    // desired route order, but `point` remains explicit so imports and advanced workflows round-trip.
    std::unordered_set<uint32_t> finalPoints;
    std::unordered_set<uint32_t> sourcePoints;
    uint64_t highPoint = 0;
    for (const WaypointPoint& p : path.points)
    {
        if (!finalPoints.insert(p.point).second)
        {
            SetWaypointSaveError(err, "Waypoint path contains duplicate point #" +
                                       std::to_string(p.point) + ". Renumber it before saving.");
            return false;
        }
        if (p.actionChance > 100)
        {
            SetWaypointSaveError(err, "Waypoint point #" + std::to_string(p.point) +
                                       " has an action chance above 100%.");
            return false;
        }
        if (p.sourceExists && !sourcePoints.insert(p.sourcePoint).second)
        {
            SetWaypointSaveError(err, "Waypoint path has two edits for source point #" +
                                       std::to_string(p.sourcePoint) + ". Duplicate points must be new rows.");
            return false;
        }
        highPoint = std::max<uint64_t>(highPoint, p.point);
    }

    // Read just the current primary keys. This gives saves an up-to-date picture even if a path was
    // changed after it was loaded, and lets unknown/custom row columns survive any reordering.
    std::vector<uint32_t> existingPoints;
    std::unordered_set<uint32_t> existingSet;
    DbError readErr;
    auto rows = db.Query("SELECT point FROM waypoint_data WHERE id=" + std::to_string(path.id) +
                         " ORDER BY point", readErr);
    if (!rows)
    {
        err = readErr;
        if (err.ok)
            SetWaypointSaveError(err, "Could not read waypoint_data for path " + std::to_string(path.id) + ".");
        return false;
    }
    while (rows->Next())
    {
        const uint32_t point = rows->GetUInt32(0);
        existingPoints.push_back(point);
        existingSet.insert(point);
        highPoint = std::max<uint64_t>(highPoint, point);
    }
    // A loaded row disappearing underneath an edit is a concurrency conflict, not a new point.
    // Failing safely avoids accidentally deleting/replacing a route another author just changed.
    for (const WaypointPoint& p : path.points)
        if (p.sourceExists && existingSet.count(p.sourcePoint) == 0)
        {
            SetWaypointSaveError(err, "Waypoint source point #" + std::to_string(p.sourcePoint) +
                                       " changed outside the editor. Reload the path before saving.");
            return false;
        }

    // Pick a block above every old and desired point. Each old row is moved there first, preventing
    // (id, point) primary-key collisions while routes are reordered or renumbered.
    const uint64_t temporaryFirst = highPoint + 1u;
    if (temporaryFirst + existingPoints.size() > std::numeric_limits<uint32_t>::max())
    {
        SetWaypointSaveError(err, "Waypoint point values are too large to renumber safely.");
        return false;
    }

    std::unordered_map<uint32_t, uint32_t> temporaryBySource;
    temporaryBySource.reserve(existingPoints.size());
    for (size_t i = 0; i < existingPoints.size(); ++i)
    {
        const uint32_t oldPoint = existingPoints[i];
        const uint32_t temporaryPoint = static_cast<uint32_t>(temporaryFirst + i);
        temporaryBySource.emplace(oldPoint, temporaryPoint);
        if (!ExecuteWaypointStep(db, "UPDATE waypoint_data SET point=" + std::to_string(temporaryPoint) +
                                     " WHERE id=" + std::to_string(path.id) + " AND point=" +
                                     std::to_string(oldPoint), err))
            return false;
    }

    const std::set<std::string> existingCols = sql::ExistingCols(db, "waypoint_data");
    std::unordered_set<uint32_t> consumedSources;
    for (const WaypointPoint& p : path.points)
    {
        const auto source = p.sourceExists ? temporaryBySource.find(p.sourcePoint)
                                            : temporaryBySource.end();
        if (source != temporaryBySource.end())
        {
            consumedSources.insert(p.sourcePoint);
            if (!UpdateWaypointRow(db, path.id, source->second, p, existingCols, err))
                return false;
        }
        else if (!InsertWaypointRow(db, path.id, p, existingCols, err))
            return false;
    }

    // Any original row not represented by a current point was deleted in the visual editor.
    for (const auto& source : temporaryBySource)
        if (consumedSources.count(source.first) == 0 &&
            !ExecuteWaypointStep(db, "DELETE FROM waypoint_data WHERE id=" + std::to_string(path.id) +
                                     " AND point=" + std::to_string(source.second), err))
            return false;
    return true;
}

bool EnableWaypointMotionInTransaction(IDatabase& db, uint32_t guid, DbError& err)
{
    const std::set<std::string> creatureCols = sql::ExistingCols(db, "creature");
    std::string set = "MovementType=2";
    if (HasColumn(creatureCols, "wander_distance"))
        set += ", wander_distance=0";
    return ExecuteWaypointStep(db, "UPDATE creature SET " + set + " WHERE guid=" +
                                   std::to_string(guid), err);
}

bool BindCreatureWaypointPathInTransaction(IDatabase& db, uint32_t guid, uint32_t entry,
                                            uint32_t pathId, bool enableWaypointMotion, DbError& err)
{
    if (guid == 0 || entry == 0 || pathId == 0)
    {
        SetWaypointSaveError(err, "A creature guid, entry, and non-zero waypoint path id are required.");
        return false;
    }

    const std::set<std::string> addonCols = sql::ExistingCols(db, "creature_addon");
    const std::set<std::string> templateAddonCols = sql::ExistingCols(db, "creature_template_addon");
    struct AddonField { const char* name; const char* fallback; };
    // TrinityCore's extended appearance fields and AzerothCore's bytes/anim-kit fields are both
    // represented. Only fields physically present in creature_addon are inserted; fields absent
    // from the template addon receive their documented/default literal instead of breaking INSERT.
    static const AddonField kFields[] = {
        {"mount", "0"}, {"bytes1", "0"}, {"bytes2", "1"}, {"emote", "0"},
        {"aiAnimKit", "0"}, {"movementAnimKit", "0"}, {"meleeAnimKit", "0"},
        {"MountCreatureID", "0"}, {"StandState", "0"}, {"AnimTier", "0"},
        {"VisFlags", "0"}, {"SheathState", "1"}, {"PvPFlags", "0"},
        {"visibilityDistanceType", "0"}, {"auras", "''"},
    };

    std::vector<std::string> columns = {"`guid`", "`path_id`"};
    std::vector<std::string> values = {std::to_string(guid), std::to_string(pathId)};
    for (const AddonField& field : kFields)
    {
        // When schema metadata is unavailable (offline export with no read source), retain a
        // conservative guid/path_id-only insert instead of guessing one core's required fields.
        if (addonCols.empty() || !HasColumn(addonCols, field.name))
            continue;
        columns.emplace_back(std::string("`") + field.name + "`");
        if (!templateAddonCols.empty() && HasColumn(templateAddonCols, field.name))
            values.emplace_back(std::string("COALESCE(cta.`") + field.name + "`," + field.fallback + ")");
        else
            values.emplace_back(field.fallback);
    }

    std::string columnSql, valueSql;
    for (size_t i = 0; i < columns.size(); ++i)
    {
        if (i) { columnSql += ", "; valueSql += ", "; }
        columnSql += columns[i];
        valueSql += values[i];
    }
    const std::string sql =
        "INSERT INTO creature_addon (" + columnSql + ") SELECT " + valueSql +
        " FROM (SELECT 1) AS singleton LEFT JOIN creature_template_addon cta ON cta.entry=" +
        std::to_string(entry) + " ON DUPLICATE KEY UPDATE path_id=VALUES(path_id)";
    if (!ExecuteWaypointStep(db, sql, err))
        return false;
    if (enableWaypointMotion && !EnableWaypointMotionInTransaction(db, guid, err))
        return false;
    return true;
}
} // namespace

DbError MapSpawnRepository::SaveWaypointPath(IDatabase& db, const WaypointPath& path) const
{
    db.BeginTransaction();
    DbError err;
    if (!SaveWaypointPathInTransaction(db, path, err))
    {
        db.Rollback();
        return err;
    }
    return db.Commit();
}

DbError MapSpawnRepository::NextWaypointPathId(IDatabase& db, uint32_t& out) const
{
    out = 0;
    DbError err;
    // A route id can legitimately be referenced by an addon before any waypoint rows are authored.
    // Include both addon sources so a newly allocated id cannot collide with such a reserved route.
    auto rows = db.Query("SELECT GREATEST(COALESCE((SELECT MAX(id) FROM waypoint_data),0), "
                         "COALESCE((SELECT MAX(path_id) FROM creature_addon),0), "
                         "COALESCE((SELECT MAX(path_id) FROM creature_template_addon),0))+1", err);
    if (!rows)
        return err.ok ? DbError{false, "Could not allocate a waypoint path id."} : err;
    if (!rows->Next())
        return DbError{false, "Could not allocate a waypoint path id."};
    out = rows->GetUInt32(0);
    if (out == 0)
        return DbError{false, "Waypoint path id range is exhausted."};
    return DbError{};
}

DbError MapSpawnRepository::BindCreatureWaypointPath(IDatabase& db, uint32_t guid, uint32_t entry,
                                                      uint32_t pathId, bool enableWaypointMotion) const
{
    db.BeginTransaction();
    DbError err;
    if (!BindCreatureWaypointPathInTransaction(db, guid, entry, pathId, enableWaypointMotion, err))
    {
        db.Rollback();
        return err;
    }
    return db.Commit();
}

DbError MapSpawnRepository::EnableCreatureWaypointMotion(IDatabase& db, uint32_t guid) const
{
    if (guid == 0)
        return DbError{false, "A creature guid is required."};
    db.BeginTransaction();
    DbError err;
    if (!EnableWaypointMotionInTransaction(db, guid, err))
    {
        db.Rollback();
        return err;
    }
    return db.Commit();
}

DbError MapSpawnRepository::ClearCreatureWaypointPath(IDatabase& db, uint32_t guid) const
{
    if (guid == 0)
        return DbError{false, "A creature guid is required."};
    db.BeginTransaction();
    DbError err;
    // Do not DELETE the addon row: it may carry appearance/auras/emotes unrelated to movement.
    db.Execute("UPDATE creature_addon SET path_id=0 WHERE guid=" + std::to_string(guid), err);
    if (!err.ok)
    {
        db.Rollback();
        return err;
    }
    return db.Commit();
}

DbError MapSpawnRepository::SaveWaypointPathAndBindCreature(IDatabase& db, const WaypointPath& path,
                                                             uint32_t guid, uint32_t entry,
                                                             bool enableWaypointMotion) const
{
    db.BeginTransaction();
    DbError err;
    if (!SaveWaypointPathInTransaction(db, path, err) ||
        !BindCreatureWaypointPathInTransaction(db, guid, entry, path.id, enableWaypointMotion, err))
    {
        db.Rollback();
        return err;
    }
    return db.Commit();
}

DbError MapSpawnRepository::LoadGameObjectsForMap(IDatabase& db, uint32_t mapId,
                                                 std::vector<MapGameObject>& out) const
{
    out.clear();
    const std::set<std::string> spawnCols = sql::ExistingCols(db, "gameobject");
    const std::set<std::string> templateCols = sql::ExistingCols(db, "gameobject_template");
    const std::string entryCol = SpawnEntryColumn(spawnCols);
    const std::string sql =
        "SELECT g.guid, g.`" + entryCol + "`, g.position_x, g.position_y, g.position_z, g.orientation, " +
        ColumnOr("g", spawnCols, "rotation0", "0") + ", " +
        ColumnOr("g", spawnCols, "rotation1", "0") + ", " +
        ColumnOr("g", spawnCols, "rotation2", "0") + ", " +
        ColumnOr("g", spawnCols, "rotation3", "0") + ", " +
        ColumnOr("g", spawnCols, "state", "1") + ", " +
        ColumnOr("gt", templateCols, "displayId", "0") + ", " +
        ColumnOr("gt", templateCols, "type", "0") + ", " +
        ColumnOr("gt", templateCols, "size", "1") + ", " +
        ColumnOr("g", spawnCols, "phaseMask", "1") + ", " +
        ColumnOr("g", spawnCols, "spawnMask", "1") +
        " FROM gameobject g JOIN gameobject_template gt ON gt.entry = g.`" + entryCol + "` " +
        "WHERE g.map = " + std::to_string(mapId);

    DbError err;
    auto rs = db.Query(sql, err);
    if (!rs)
        return err;

    while (rs->Next())
    {
        MapGameObject g;
        g.guid = rs->GetUInt32(0);
        g.entry = rs->GetUInt32(1);
        g.x = rs->GetFloat(2);
        g.y = rs->GetFloat(3);
        g.z = rs->GetFloat(4);
        g.o = rs->GetFloat(5);
        g.rot[0] = rs->GetFloat(6);
        g.rot[1] = rs->GetFloat(7);
        g.rot[2] = rs->GetFloat(8);
        g.rot[3] = rs->GetFloat(9);
        g.state = static_cast<uint8_t>(rs->GetUInt32(10));
        g.displayId = rs->GetUInt32(11);
        g.type = static_cast<uint8_t>(rs->GetUInt32(12));
        g.size = rs->GetFloat(13);
        if (g.size <= 0.0f)
            g.size = 1.0f;
        g.phaseMask = rs->GetUInt32(14);
        if (g.phaseMask == 0)
            g.phaseMask = 1;
        g.spawnMask = rs->GetUInt32(15);
        if (g.spawnMask == 0)
            g.spawnMask = 1;
        out.push_back(std::move(g));
    }

    // Merge game-event / pool / spawn-group gating (spawn_group spawnType 1 = gameobject).
    ApplyAssociations(db, "game_event_gameobject", "pool_gameobject", 1, out);
    return DbError{};
}

DbError MapSpawnRepository::LoadMoTransports(IDatabase& db, std::vector<MoTransportDef>& out) const
{
    out.clear();

    // The `transports` table lists MO_TRANSPORT entries to spawn; the template's Data0 is the
    // taxi path id and Data1 the move speed.
    const std::string sql =
        "SELECT t.entry, gt.displayId, gt.Data0, gt.Data1, gt.size "
        "FROM transports t JOIN gameobject_template gt ON gt.entry = t.entry";

    DbError err;
    auto rs = db.Query(sql, err);
    if (!rs)
        return err;

    while (rs->Next())
    {
        MoTransportDef d;
        d.entry = rs->GetUInt32(0);
        d.displayId = rs->GetUInt32(1);
        d.taxiPathId = rs->GetUInt32(2);
        d.moveSpeed = rs->GetFloat(3);
        d.size = rs->GetFloat(4);
        if (d.size <= 0.0f)
            d.size = 1.0f;
        out.push_back(std::move(d));
    }
    return DbError{};
}

DbError MapSpawnRepository::LoadGameEvents(IDatabase& db, std::vector<GameEventInfo>& out) const
{
    out.clear();
    DbError err;
    auto rs = db.Query("SELECT eventEntry, description FROM game_event ORDER BY eventEntry", err);
    if (!rs)
        return err;   // missing table -> caller treats events as unavailable
    while (rs->Next())
    {
        GameEventInfo e;
        e.id = rs->GetInt32(0);
        e.description = rs->GetString(1);
        out.push_back(std::move(e));
    }
    return DbError{};
}

namespace
{
// Format a float with enough precision for a spawn coordinate (columns are 32-bit floats).
std::string Num(float v)
{
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.6f", v);
    return buf;
}
} // namespace

DbError MapSpawnRepository::UpdateGameObjectTransform(IDatabase& db, uint32_t guid, float x, float y,
                                                      float z, float o, const float rot[4]) const
{
    db.BeginTransaction();
    DbError e;
    std::string sql = "UPDATE gameobject SET position_x=" + Num(x) + ", position_y=" + Num(y) +
                      ", position_z=" + Num(z) + ", orientation=" + Num(o) +
                      ", rotation0=" + Num(rot[0]) + ", rotation1=" + Num(rot[1]) +
                      ", rotation2=" + Num(rot[2]) + ", rotation3=" + Num(rot[3]) +
                      " WHERE guid=" + std::to_string(guid);
    db.Execute(sql, e);
    if (!e.ok)
    {
        db.Rollback();
        return e;
    }
    return db.Commit();
}

DbError MapSpawnRepository::UpdateCreatureTransform(IDatabase& db, uint32_t guid, float x, float y,
                                                    float z, float o) const
{
    db.BeginTransaction();
    DbError e;
    std::string sql = "UPDATE creature SET position_x=" + Num(x) + ", position_y=" + Num(y) +
                      ", position_z=" + Num(z) + ", orientation=" + Num(o) +
                      " WHERE guid=" + std::to_string(guid);
    db.Execute(sql, e);
    if (!e.ok)
    {
        db.Rollback();
        return e;
    }
    return db.Commit();
}

DbError MapSpawnRepository::UpdateCreatureAiBehavior(IDatabase& db, uint32_t guid, uint32_t entry,
                                                       bool templateScope, const NpcAiBehavior& behavior,
                                                       bool& outAggroPersisted, bool& outLeashPersisted) const
{
    outAggroPersisted = false;
    outLeashPersisted = false;
    if (guid == 0 || entry == 0)
        return DbError{false, "A creature guid and template entry are required."};
    if (!std::isfinite(behavior.aggroRadius) || behavior.aggroRadius < 0.0f ||
        !std::isfinite(behavior.leashDistance) || behavior.leashDistance < 0.0f)
        return DbError{false, "Aggro and leash distances must be finite non-negative yard values."};

    const char* table = templateScope ? "creature_template" : "creature";
    const std::set<std::string> cols = sql::ExistingCols(db, table);
    // Do not guess optional behavior columns in SQL-export/no-introspection sessions. A guessed
    // column makes a reviewable export fail on import; Studio preview persistence remains available.
    if (cols.empty())
        return DbError{false, "Live schema metadata is required to save aggro/leash behavior columns."};
    const std::string aggroCol = FirstColumn(cols,
        {"detection_range", "aggro_radius", "aggro_range", "detectionrange", "sight_distance"});
    const std::string leashCol = FirstColumn(cols,
        {"leash_distance", "leash_range", "chase_distance"});
    if (aggroCol.empty() && leashCol.empty())
        return DbError{false, std::string(table) + " has no recognized aggro or leash distance column."};

    std::string set;
    if (!aggroCol.empty())
    {
        set += "`" + aggroCol + "`=" + Num(behavior.aggroRadius);
        outAggroPersisted = true;
    }
    if (!leashCol.empty())
    {
        if (!set.empty()) set += ", ";
        set += "`" + leashCol + "`=" + Num(behavior.leashDistance);
        outLeashPersisted = true;
    }
    db.BeginTransaction();
    DbError e;
    const std::string key = templateScope ? ("entry=" + std::to_string(entry))
                                          : ("guid=" + std::to_string(guid));
    db.Execute(std::string("UPDATE ") + table + " SET " + set + " WHERE " + key, e);
    if (!e.ok)
    {
        db.Rollback();
        outAggroPersisted = outLeashPersisted = false;
        return e;
    }
    e = db.Commit();
    if (!e.ok)
    {
        outAggroPersisted = outLeashPersisted = false;
        return e;
    }
    if (!outAggroPersisted || !outLeashPersisted)
        e.message = std::string("Saved available behavior column(s); ") +
                    (!outAggroPersisted ? "aggro uses Studio preview fallback. " : "") +
                    (!outLeashPersisted ? "leash uses Studio preview fallback." : "");
    return e;
}

DbError MapSpawnRepository::LoadCreatureSpawn(IDatabase& db, uint32_t guid, CreatureSpawn& out) const
{
    out = CreatureSpawn{};
    DbError e;
    auto rs = db.Query("SELECT * FROM creature WHERE guid = " + std::to_string(guid), e);
    if (!rs)
        return e.ok ? DbError{false, "creature spawn query failed"} : e;
    if (!rs->Next())
        return DbError{false, "creature spawn " + std::to_string(guid) + " not found"};

    // id/modelid/StringId differ across supported cores, so read the canonical spawn fields by name.
    // sql::Row returns a harmless default for omitted columns (for example AzerothCore's modelid).
    sql::Row row(*rs);
    out.guid = guid;
    out.map = static_cast<uint16_t>(row.U("map"));
    out.zoneId = static_cast<uint16_t>(row.U("zoneId"));
    out.areaId = static_cast<uint16_t>(row.U("areaId"));
    out.spawnMask = static_cast<uint8_t>(row.U("spawnMask"));
    out.phaseMask = row.U("phaseMask");
    out.modelId = row.Ua({"displayid", "display_id", "modelid"});
    out.equipmentId = static_cast<int8_t>(row.I("equipment_id"));
    out.x = row.F("position_x");
    out.y = row.F("position_y");
    out.z = row.F("position_z");
    out.o = row.F("orientation");
    out.spawnTimeSecs = row.U("spawntimesecs");
    out.wanderDistance = row.F("wander_distance");
    out.currentWaypoint = row.U("currentwaypoint");
    out.curHealth = row.U("curhealth");
    out.curMana = row.U("curmana");
    out.movementType = static_cast<uint8_t>(row.U("MovementType"));
    out.npcflag = row.U("npcflag");
    out.unitFlags = row.U("unit_flags");
    out.dynamicFlags = row.U("dynamicflags");
    out.scriptName = row.S("ScriptName");
    out.stringId = row.S("StringId");
    out.verifiedBuild = row.I("VerifiedBuild");
    return DbError{};
}

DbError MapSpawnRepository::UpdateCreatureSpawn(IDatabase& db, const CreatureSpawn& s) const
{
    DbError e;
    // Only write columns the live DB actually has (ExistingCols is empty in SqlExport mode -> write
    // all). Names compare case-insensitively since ExistingCols lowercases.
    const std::set<std::string> existing = sql::ExistingCols(db, "creature");
    auto has = [&](const char* c) {
        if (existing.empty())
            return true;
        std::string lc(c);
        for (auto& ch : lc)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return existing.count(lc) != 0;
    };
    std::string set;
    auto add = [&](const char* col, const std::string& tok) {
        if (!has(col))
            return;
        if (!set.empty())
            set += ", ";
        set += col;
        set += "=";
        set += tok;
    };
    add("map", std::to_string(s.map));
    add("zoneId", std::to_string(s.zoneId));
    add("areaId", std::to_string(s.areaId));
    add("spawnMask", std::to_string(s.spawnMask));
    add("phaseMask", std::to_string(s.phaseMask));
    // Prefer an explicit server displayid column over the legacy modelid spelling, matching the
    // reader and World Editor render resolver. In offline/export mode use modelid as TrinityCore's
    // stable documented column rather than guessing a project-specific alias.
    std::string displayColumn = FirstColumn(existing, {"displayid", "display_id", "modelid"});
    if (displayColumn.empty())
        displayColumn = "modelid";
    add(displayColumn.c_str(), std::to_string(s.modelId));
    add("equipment_id", std::to_string(static_cast<int>(s.equipmentId)));
    add("position_x", Num(s.x));
    add("position_y", Num(s.y));
    add("position_z", Num(s.z));
    add("orientation", Num(s.o));
    add("spawntimesecs", std::to_string(s.spawnTimeSecs));
    add("wander_distance", Num(s.wanderDistance));
    add("currentwaypoint", std::to_string(s.currentWaypoint));
    add("curhealth", std::to_string(s.curHealth));
    add("curmana", std::to_string(s.curMana));
    add("MovementType", std::to_string(static_cast<unsigned>(s.movementType)));
    add("npcflag", std::to_string(s.npcflag));
    add("unit_flags", std::to_string(s.unitFlags));
    add("dynamicflags", std::to_string(s.dynamicFlags));
    add("ScriptName", "'" + db.EscapeString(s.scriptName) + "'");
    add("StringId", "'" + db.EscapeString(s.stringId) + "'");
    add("VerifiedBuild", "0");   // convention: tool-written rows carry VerifiedBuild 0

    if (set.empty())
        return DbError{};   // nothing to write (no matching columns)
    db.BeginTransaction();
    db.Execute("UPDATE creature SET " + set + " WHERE guid=" + std::to_string(s.guid), e);
    if (!e.ok)
    {
        db.Rollback();
        return e;
    }
    return db.Commit();
}

DbError MapSpawnRepository::LoadGameObjectSpawn(IDatabase& db, uint32_t guid,
                                               GameObjectSpawn& out) const
{
    out = GameObjectSpawn{};
    DbError e;
    auto rs = db.Query("SELECT * FROM gameobject WHERE guid = " + std::to_string(guid), e);
    if (!rs)
        return e.ok ? DbError{false, "gameobject spawn query failed"} : e;
    if (!rs->Next())
        return DbError{false, "gameobject spawn " + std::to_string(guid) + " not found"};
    sql::Row row(*rs);
    out.guid = guid;
    out.map = static_cast<uint16_t>(row.U("map"));
    out.zoneId = static_cast<uint16_t>(row.U("zoneId"));
    out.areaId = static_cast<uint16_t>(row.U("areaId"));
    out.spawnMask = static_cast<uint8_t>(row.U("spawnMask"));
    out.phaseMask = row.U("phaseMask");
    out.x = row.F("position_x");
    out.y = row.F("position_y");
    out.z = row.F("position_z");
    out.o = row.F("orientation");
    out.rotation[0] = row.F("rotation0");
    out.rotation[1] = row.F("rotation1");
    out.rotation[2] = row.F("rotation2");
    out.rotation[3] = row.F("rotation3");
    out.spawnTimeSecs = row.I("spawntimesecs");
    out.animProgress = static_cast<uint8_t>(row.U("animprogress"));
    out.state = static_cast<uint8_t>(row.U("state"));
    out.scriptName = row.S("ScriptName");
    out.stringId = row.S("StringId");
    out.verifiedBuild = row.I("VerifiedBuild");
    return DbError{};
}

DbError MapSpawnRepository::UpdateGameObjectSpawn(IDatabase& db, const GameObjectSpawn& s) const
{
    DbError e;
    const std::set<std::string> existing = sql::ExistingCols(db, "gameobject");
    auto has = [&](const char* c) {
        if (existing.empty())
            return true;
        std::string lc(c);
        for (auto& ch : lc)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return existing.count(lc) != 0;
    };
    std::string set;
    auto add = [&](const char* col, const std::string& tok) {
        if (!has(col))
            return;
        if (!set.empty())
            set += ", ";
        set += col;
        set += "=";
        set += tok;
    };
    add("map", std::to_string(s.map));
    add("zoneId", std::to_string(s.zoneId));
    add("areaId", std::to_string(s.areaId));
    add("spawnMask", std::to_string(s.spawnMask));
    add("phaseMask", std::to_string(s.phaseMask));
    add("position_x", Num(s.x));
    add("position_y", Num(s.y));
    add("position_z", Num(s.z));
    add("orientation", Num(s.o));
    add("rotation0", Num(s.rotation[0]));
    add("rotation1", Num(s.rotation[1]));
    add("rotation2", Num(s.rotation[2]));
    add("rotation3", Num(s.rotation[3]));
    add("spawntimesecs", std::to_string(s.spawnTimeSecs));
    add("animprogress", std::to_string(s.animProgress));
    add("state", std::to_string(s.state));
    add("ScriptName", "'" + db.EscapeString(s.scriptName) + "'");
    add("StringId", "'" + db.EscapeString(s.stringId) + "'");
    add("VerifiedBuild", "0");   // convention: tool-written rows carry VerifiedBuild 0

    if (set.empty())
        return DbError{};
    db.BeginTransaction();
    db.Execute("UPDATE gameobject SET " + set + " WHERE guid=" + std::to_string(s.guid), e);
    if (!e.ok)
    {
        db.Rollback();
        return e;
    }
    return db.Commit();
}

namespace
{
// SELECT one uint32 (a resolved id / allocated guid); returns 0 if the query fails or is empty.
uint32_t QueryU32(IDatabase& db, const std::string& sql, DbError& e)
{
    auto rs = db.Query(sql, e);
    return (rs && rs->Next()) ? rs->GetUInt32(0) : 0u;
}

ResolvedCreatureDisplay ResolveTemplateDisplayForNewSpawn(IDatabase& db, uint32_t entry, uint32_t guid)
{
    // Reuse the exact map-load resolver for a just-added actor so a new AzerothCore NPC is visible
    // immediately, rather than waiting for the next map reload to discover creature_template_model.
    MapSpawn preview;
    preview.entry = entry;
    preview.guid = guid;
    const std::set<std::string> templateCols = sql::ExistingCols(db, "creature_template");
    const std::string query = "SELECT " + DisplayColumnOr("ct", templateCols, false) + ", " +
        ColumnOr("ct", templateCols, "modelid1", "0") + ", " +
        ColumnOr("ct", templateCols, "modelid2", "0") + ", " +
        ColumnOr("ct", templateCols, "modelid3", "0") + ", " +
        ColumnOr("ct", templateCols, "modelid4", "0") +
        " FROM creature_template ct WHERE ct.entry=" + std::to_string(entry);
    DbError lookup;
    if (auto rs = db.Query(query, lookup))
        if (rs->Next())
        {
            preview.templateDisplayId = rs->GetUInt32(0);
            if (preview.templateDisplayId == 0)
                for (int col = 1; col <= 4; ++col)
                {
                    const uint32_t display = rs->GetUInt32(col);
                    if (display != 0) { preview.templateDisplayId = display; break; }
                }
        }
    preview.templateDisplayScale = 1.0f;
    preview.templateDisplaySource = preview.templateDisplayId != 0
                                        ? CreatureDisplaySource::LegacyTemplate
                                        : CreatureDisplaySource::None;
    ResolveBaseDisplay(preview);
    std::vector<MapSpawn> singleton;
    singleton.push_back(std::move(preview));
    ApplyTemplateModelDisplays(db, singleton);
    return singleton.front().ResolveDisplay(SpawnFilter{});
}
} // namespace

DbError MapSpawnRepository::InsertCreatureSpawn(IDatabase& db, uint32_t mapId, uint32_t entry,
                                                float x, float y, float z, float o,
                                                uint32_t& guid, uint32_t& outDisplayId,
                                                float* outServerDisplayScale,
                                                CreatureDisplaySource* outDisplaySource) const
{
    outDisplayId = 0;
    if (outServerDisplayScale)
        *outServerDisplayScale = 1.0f;
    if (outDisplaySource)
        *outDisplaySource = CreatureDisplaySource::None;
    DbError e;

    if (guid == 0)   // 0 = allocate; a specific guid comes from undo/redo re-insert
        guid = QueryU32(db, "SELECT COALESCE(MAX(guid),0)+1 FROM creature", e);
    if (guid == 0)
        return e.ok ? DbError{false, "creature guid allocation failed"} : e;
    const uint32_t outGuid = guid;

    const ResolvedCreatureDisplay preview = ResolveTemplateDisplayForNewSpawn(db, entry, guid);
    outDisplayId = preview.displayId;
    if (outServerDisplayScale)
        *outServerDisplayScale = preview.serverScale;
    if (outDisplaySource)
        *outDisplaySource = preview.source;

    const std::set<std::string> existing = sql::ExistingCols(db, "creature");
    const std::string entryCol = SpawnEntryColumn(existing);
    std::string spawnDisplayColumn = FirstColumn(existing, {"displayid", "display_id", "modelid"});
    if (spawnDisplayColumn.empty())
        spawnDisplayColumn = "modelid";  // offline TrinityCore-compatible export fallback
    std::vector<std::string> cols = {"guid", entryCol};
    sql::ValueList v(db);
    v.UInt(outGuid); v.UInt(entry);
    // AzerothCore's current id1/id2/id3 spawn layout needs its secondary IDs explicitly set to 0
    // on schemas where they are non-null/no-default. TrinityCore simply filters these columns out.
    if (!existing.empty() && existing.count("id2") != 0) { cols.push_back("id2"); v.UInt(0); }
    if (!existing.empty() && existing.count("id3") != 0) { cols.push_back("id3"); v.UInt(0); }
    const std::vector<std::string> tail = {
        "map", "zoneId", "areaId", "spawnMask", "phaseMask", spawnDisplayColumn, "equipment_id",
        "position_x", "position_y", "position_z", "orientation", "spawntimesecs", "wander_distance",
        "currentwaypoint", "curhealth", "curmana", "MovementType", "npcflag", "unit_flags",
        "dynamicflags", "ScriptName", "StringId", "VerifiedBuild"};
    cols.insert(cols.end(), tail.begin(), tail.end());
    v.UInt(mapId); v.UInt(0); v.UInt(0); v.UInt(1); v.UInt(1);
    v.UInt(0); v.UInt(0); v.Float(x); v.Float(y); v.Float(z); v.Float(o);
    v.UInt(120); v.Float(0.0f); v.UInt(0); v.UInt(1); v.UInt(0); v.UInt(0); v.UInt(0); v.UInt(0);
    v.UInt(0); v.Text(""); v.Text(""); v.Int(0);
    db.BeginTransaction();
    db.Execute(sql::FilteredInsert("INSERT", "creature", cols, v.tokens, existing), e);
    if (!e.ok)
    {
        db.Rollback();
        guid = 0;
        return e;
    }
    return db.Commit();
}

DbError MapSpawnRepository::InsertGameObjectSpawn(IDatabase& db, uint32_t mapId, uint32_t entry,
                                                  float x, float y, float z, float o,
                                                  const float rot[4], uint32_t& guid,
                                                  uint32_t& outDisplayId) const
{
    outDisplayId = 0;
    DbError e;
    outDisplayId = QueryU32(db, "SELECT displayId FROM gameobject_template WHERE entry = " +
                                    std::to_string(entry), e);

    if (guid == 0)   // 0 = allocate; a specific guid comes from undo/redo re-insert
        guid = QueryU32(db, "SELECT COALESCE(MAX(guid),0)+1 FROM gameobject", e);
    if (guid == 0)
        return e.ok ? DbError{false, "gameobject guid allocation failed"} : e;
    const uint32_t outGuid = guid;

    const std::set<std::string> existing = sql::ExistingCols(db, "gameobject");
    const std::string entryCol = SpawnEntryColumn(existing);
    std::vector<std::string> cols = {"guid", entryCol};
    sql::ValueList v(db);
    v.UInt(outGuid); v.UInt(entry);
    if (!existing.empty() && existing.count("id2") != 0) { cols.push_back("id2"); v.UInt(0); }
    if (!existing.empty() && existing.count("id3") != 0) { cols.push_back("id3"); v.UInt(0); }
    const std::vector<std::string> tail = {
        "map", "zoneId", "areaId", "spawnMask", "phaseMask", "position_x", "position_y",
        "position_z", "orientation", "rotation0", "rotation1", "rotation2", "rotation3",
        "spawntimesecs", "animprogress", "state", "ScriptName", "StringId", "VerifiedBuild"};
    cols.insert(cols.end(), tail.begin(), tail.end());
    v.UInt(mapId); v.UInt(0); v.UInt(0); v.UInt(1); v.UInt(1);
    v.Float(x); v.Float(y); v.Float(z); v.Float(o);
    v.Float(rot[0]); v.Float(rot[1]); v.Float(rot[2]); v.Float(rot[3]);
    v.UInt(120); v.UInt(100); v.UInt(1); v.Text(""); v.Text(""); v.Int(0);
    db.BeginTransaction();
    db.Execute(sql::FilteredInsert("INSERT", "gameobject", cols, v.tokens, existing), e);
    if (!e.ok)
    {
        db.Rollback();
        guid = 0;
        return e;
    }
    return db.Commit();
}

DbError MapSpawnRepository::DeleteCreatureSpawn(IDatabase& db, uint32_t guid) const
{
    db.BeginTransaction();
    DbError e;
    db.Execute("DELETE FROM creature WHERE guid=" + std::to_string(guid), e);
    if (!e.ok) { db.Rollback(); return e; }
    return db.Commit();
}

DbError MapSpawnRepository::DeleteGameObjectSpawn(IDatabase& db, uint32_t guid) const
{
    db.BeginTransaction();
    DbError e;
    db.Execute("DELETE FROM gameobject WHERE guid=" + std::to_string(guid), e);
    if (!e.ok) { db.Rollback(); return e; }
    return db.Commit();
}
} // namespace we
