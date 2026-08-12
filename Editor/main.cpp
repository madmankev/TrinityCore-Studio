// TrinityCore Studio - application entry point.
// The GLFW/GL/ImGui loop and all editor state live in we::App (src/ui). main()
// parses flags and hands control to the App.
//   --selftest   : render a few headless frames (all panels + tabs), print
//                  "SELFTEST OK", exit 0. Validates the build/link/render chain.
//   --emit-sql [path] : run the real QuestRepository::SaveQuest path for a sample
//                  quest through SqlExportDatabase and write the generated .sql to
//                  `path` (default build/emit_test.sql). Validates SQL generation
//                  offline, with no database. Exits 0 on success.

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "app/App.h"

#include "editors/quest/QuestRepository.h"
#include "db/SqlExportDatabase.h"
#include "schema/Quest.h"

#include "clientdata/ClientData.h"
#include "clientdata/DbcStore.h"
#include "clientdata/EditableDbc.h"
#include "clientdata/DbcOverlay.h"
#include "clientdata/BlpDecoder.h"
#include "editors/achievement/AchievementSchema.h"
#include "editors/achievement/AchievementRepository.h"
#include "editors/title/CharTitlesSchema.h"
#include "editors/spell/SpellSchema.h"
#include "editors/spell/SpellRepository.h"
#include "editors/spell/SpellTablesModule.h"
#include "editors/item/ItemTablesModule.h"
#include "editors/talent/TalentSchema.h"
#include "editors/skill/SkillSchema.h"
#include "editors/zone/ZoneTablesModule.h"
#include "editors/worlddb/WorldDbTablesModule.h"
#include "editors/gameevent/GameEventModule.h"
#include "editors/conditions/ConditionsRepository.h"
#include "editors/miscdbc/MiscDbcModule.h"
#include "editors/refdbc/RefDbcModule.h"
#include "editors/generic/RuntimeSchema.h"
#include "clientdata/DbdParser.h"
#include "editors/generic/DbcDefRegistry.h"
#include "StormLib.h"
#include "editors/worlddb/WorldDbCompositeModule.h"
#include "editors/loot/LootModule.h"
#include "editors/creaturetext/CreatureTextModule.h"
#include "editors/gossip/GossipModule.h"
#include "editors/pagetext/PageTextModule.h"
#include "editors/poi/PointsOfInterestModule.h"
#include "editors/npctext/NpcTextModule.h"
#include "editors/smartai/SmartScriptRepository.h"
#include "editors/common/DbDocument.h"
#include "editors/common/DbcDocument.h"
#include "editors/common/DbTableSchema.h"
#include "editors/common/DbTableRepository.h"
#include "model/M2Loader.h"
#include "model/M2Animator.h"
#include "model/M2EffectSystem.h"
#include "model/ModelDress.h"
#include "model/ModelUploadBuild.h"
#include "wmo/WmoLoader.h"
#include "wmo/WmoUploadBuild.h"
#include "adt/AdtLoader.h"
#include "adt/AdtUploadBuild.h"
#include "adt/AdtStreamer.h"
#include "adt/AdtWriter.h"
#include "util/ByteReader.h"
#include "ui/ViewportCamera.h"

#include <chrono>
#include <thread>

#include <memory>
#include "imgui.h"
#include "platform/Window.h"
#include "gfx/VulkanRenderer.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace
{
int EmitSql(const std::string& path)
{
    we::Quest q;
    q.tmpl.id = 99999;
    q.tmpl.questType = 2;
    q.tmpl.questLevel = 70;
    q.tmpl.minLevel = 68;
    q.tmpl.logTitle = "Test Quest: O'Malley's \"Grand\" Plan";  // exercise escaping
    q.tmpl.questDescription = "Line one.\nLine two with 'quotes'.";
    q.tmpl.flags = 0x8 | 0x1000;                                 // SHARABLE | DAILY
    q.tmpl.rewardItemId[0] = 1234;
    q.tmpl.rewardAmount[0] = 2;
    q.tmpl.requiredNpcOrGo[0] = 4321;
    q.tmpl.requiredNpcOrGoCount[0] = 10;
    q.tmpl.requiredItemId[0] = 555;
    q.tmpl.requiredItemCount[0] = 5;
    q.tmpl.rewardMoney = 12345;

    q.addon.present = true;
    q.addon.allowableClasses = 0x1 | 0x4;                        // Warrior | Hunter
    q.addon.prevQuestID = 99998;
    q.addon.nextQuestID = 100000;

    q.offerReward.present = true;
    q.offerReward.rewardText = "Well done, hero!";
    q.requestItems.present = true;
    q.requestItems.completionText = "Do you have it?";
    q.details.present = true;
    q.mailSender.present = true;
    q.mailSender.rewardMailSenderEntry = 4321;

    we::QuestGreeting g;
    g.id = q.tmpl.id;
    g.type = 0;
    g.greeting = "Hello there.";
    q.greetings.push_back(g);

    q.creatureStarters.push_back(1000);
    q.creatureEnders.push_back(1001);
    q.goStarters.push_back(2000);

    we::QuestPoi poi;
    poi.questID = q.tmpl.id;
    poi.id = 0;
    we::QuestPoiPoint pt;
    pt.questID = q.tmpl.id;
    pt.idx1 = 0;
    pt.idx2 = 0;
    pt.x = -8913;
    pt.y = 554;
    poi.points.push_back(pt);
    q.pois.push_back(poi);

    we::QuestLocale loc;
    loc.locale = "deDE";
    loc.title = "Testauftrag";
    loc.templatePresent = true;
    q.locales["deDE"] = loc;

    we::SqlExportDatabase db;         // no live read source needed for pure writes
    db.SetOutputPath(path);
    we::QuestRepository repo;
    q.MarkAllDirty();                 // delta-write: emit the whole sample quest
    we::DbError e = repo.SaveQuest(db, q);
    if (!e.ok)
    {
        std::fprintf(stderr, "emit-sql FAILED: %s\n", e.message.c_str());
        return 1;
    }
    std::printf("EMIT SQL OK -> %s\n", path.c_str());
    return 0;
}
// Verify achievement_reward / achievement_criteria_data SQL generation through the real
// AchievementRepository against SqlExportDatabase (no live DB). Usage:
// --emit-ach-sql [path]
int EmitAchSql(const std::string& path)
{
    we::AchievementReward reward;
    reward.titleA = 178;   // sample CharTitles ids
    reward.titleH = 178;
    reward.itemId = 49426;
    reward.sender = 15252;
    reward.subject = "Congratulations!";
    reward.body = "You've earned it. O'Rly? Ya'Rly.";
    reward.mailTemplateId = 0;
    reward.locales.push_back({"deDE", "Glückwunsch!", "Gut gemacht."});

    std::vector<we::AchievementCriteriaData> critData = {{1u, 5u, 0u, "example_script"}};

    // Each Save is its own transaction. One export session keeps both blocks in chronological
    // order, matching a user who saves multiple editor records before reviewing one script.
    we::AchievementRepository repo;
    we::SqlExportDatabase db;
    db.SetOutputPath(path);
    we::DbError e = repo.SaveReward(db, 12345, reward);
    if (e.ok)
        e = repo.SaveCriteriaData(db, 67890, critData);
    if (!e.ok)
    {
        std::fprintf(stderr, "emit-ach-sql FAILED: %s\n", e.message.c_str());
        return 1;
    }
    std::printf("EMIT ACH SQL OK -> %s\n", path.c_str());
    return 0;
}
// Verify the GENERIC DbTableRepository::Save through SqlExportDatabase (no live DB): builds a
// representative broadcast_text record (with a locale row) and emits the upsert + locale SQL.
// Usage: --emit-broadcast-sql [path]
int EmitBroadcastSql(const std::string& path)
{
    we::DbTableSchema s;
    s.table = "broadcast_text";
    s.pk = "ID";
    s.cols = {
        {"LanguageID", we::DbColType::U32, "Language"},
        {"Text", we::DbColType::Multiline, "Text"},
        {"Text1", we::DbColType::Multiline, "Text1"},
        {"EmoteID1", we::DbColType::U32, "Emote1"},
        {"Flags", we::DbColType::U32, "Flags"},
        {"VerifiedBuild", we::DbColType::U32, "VerifiedBuild"},
    };
    s.localeTable = "broadcast_text_locale";
    s.localeKey = "ID";
    s.localeCol = "locale";
    s.localizedCols = {"Text", "Text1"};

    we::DbRecord rec;
    rec.id = 25000;
    rec.cells["LanguageID"] = "7";
    rec.cells["Text"] = "Hello, $n! Don't move.";
    rec.cells["Text1"] = "Hello, lady $n!";
    rec.cells["EmoteID1"] = "1";
    rec.cells["Flags"] = "0";
    rec.cells["VerifiedBuild"] = "12340";  // must be forced to 0 on save
    rec.locales.push_back({"deDE", {{"Text", "Hallo, $n!"}, {"Text1", ""}}});

    we::SqlExportDatabase db;
    db.SetOutputPath(path);
    we::DbTableRepository repo;
    we::DbError e = repo.Save(db, s, rec);
    if (!e.ok)
    {
        std::fprintf(stderr, "emit-broadcast-sql FAILED: %s\n", e.message.c_str());
        return 1;
    }
    std::printf("EMIT BROADCAST SQL OK -> %s\n", path.c_str());
    return 0;
}

// Verify the world-DB grab-bag schemas produce valid SQL through the generic DbTableRepository
// + SqlExportDatabase (no live DB): emits an upsert for every table, catching any column-name
// typo against the DDL. Usage: --emit-worlddb-sql [path]
int EmitWorldDbSql(const std::string& path)
{
    we::DbTableRepository repo;
    we::DbError e;
    int n = 0;
    for (const we::DbTableSchema* s : we::WorldDbTableDefs())
    {
        we::DbRecord rec;
        rec.id = 25000;
        rec.cells[s->pk] = "25000";
        for (const we::DbColumn& c : s->cols)
            rec.cells[c.name] = (c.type == we::DbColType::Text || c.type == we::DbColType::Multiline)
                                    ? "x"
                                    : "1";
        we::SqlExportDatabase db;
        db.SetOutputPath(path + "." + s->table + ".sql");
        e = repo.Save(db, *s, rec);
        if (!e.ok)
            break;
        ++n;
    }
    if (!e.ok) { std::fprintf(stderr, "emit-worlddb-sql FAILED: %s\n", e.message.c_str()); return 1; }
    std::printf("EMIT WORLDDB SQL OK (%d tables)\n", n);
    return 0;
}

// Verify the composite-PK world-DB editor's writes through SqlExportDatabase (no live DB):
// CompositeDbRepository::Save emits DELETE-by-key + INSERT for every table, catching any
// column-name typo against the DDL. Usage: --emit-worlddb-composite-sql [path]
int EmitWorldDbCompositeSql(const std::string& path)
{
    we::CompositeDbRepository repo;
    we::DbError e;
    int n = 0;
    for (const we::CompositeDbTableSchema* s : we::WorldDbCompositeTableDefs())
    {
        we::DbRecord rec;
        for (const we::DbColumn& c : s->cols)
            rec.cells[c.name] = (c.type == we::DbColType::Text || c.type == we::DbColType::Multiline)
                                    ? "x"
                                    : "1";
        std::vector<std::string> origKey(s->keyCols.size(), "1");
        we::SqlExportDatabase db;
        db.SetOutputPath(path + "." + s->table + ".sql");
        e = repo.Save(db, *s, origKey, rec);
        if (!e.ok)
            break;
        ++n;
    }
    if (!e.ok) { std::fprintf(stderr, "emit-worlddb-composite-sql FAILED: %s\n", e.message.c_str()); return 1; }
    std::printf("EMIT WORLDDB COMPOSITE SQL OK (%d tables)\n", n);
    return 0;
}

// Verify the Game Event editor's writes through SqlExportDatabase (no live DB): the primary
// game_event upsert + a ReplaceChildren for every child table, catching any column-name typo
// against the DDL. Usage: --emit-gameevent-sql [path]
int EmitGameEventSql(const std::string& path)
{
    we::DbTableRepository repo;
    we::DbError e;

    // 1) Primary game_event row.
    {
        const we::DbTableSchema& s = we::GameEventSchema();
        we::DbRecord rec;
        rec.id = 200;
        rec.cells[s.pk] = "200";
        for (const we::DbColumn& c : s.cols)
            rec.cells[c.name] = (c.type == we::DbColType::Text || c.type == we::DbColType::Multiline)
                                    ? "x"
                                    : "1";
        we::SqlExportDatabase db;
        db.SetOutputPath(path + ".game_event.sql");
        e = repo.Save(db, s, rec);
    }

    // 2) A two-row list for every child table.
    int n = 0;
    for (const we::GameEventChild& spec : we::GameEventAllChildren())
    {
        if (!e.ok) break;
        std::vector<we::DbRecord> rows(2);
        for (int i = 0; i < 2; ++i)
            for (const we::DbColumn& c : spec.cols)
                rows[i].cells[c.name] =
                    std::string(c.name) == spec.parentCol ? "200"
                    : (c.type == we::DbColType::Text || c.type == we::DbColType::Multiline) ? "x"
                                                                                            : std::to_string(i + 1);
        we::SqlExportDatabase db;
        db.SetOutputPath(path + "." + spec.table + ".sql");
        e = repo.ReplaceChildren(db, spec.table, spec.parentCol, "200", spec.cols, rows);
        ++n;
    }

    if (!e.ok) { std::fprintf(stderr, "emit-gameevent-sql FAILED: %s\n", e.message.c_str()); return 1; }
    std::printf("EMIT GAMEEVENT SQL OK (game_event + %d child tables)\n", n);
    return 0;
}

// Verify the Conditions editor's source-scoped save through SqlExportDatabase (no live DB):
// ConditionsRepository::SaveSource emits DELETE-by-source + one INSERT per row over the 15
// conditions columns, catching any column-name typo against the DDL. Usage: --emit-conditions-sql [path]
int EmitConditionsSql(const std::string& path)
{
    we::ConditionsRepository repo;
    std::vector<we::DbRecord> rows(2);
    for (int i = 0; i < 2; ++i)
    {
        for (const char* c : we::ConditionsRepository::Columns())
            rows[i].cells[c] = "0";
        rows[i].cells["ScriptName"] = "";
        rows[i].cells["Comment"] = i == 0 ? "quest 100 rewarded" : "quest 101 active";
        rows[i].cells["ConditionTypeOrReference"] = i == 0 ? "8" : "9";
        rows[i].cells["ConditionValue1"] = i == 0 ? "100" : "101";
    }
    const we::ConditionSourceKey key{19, 0, 500};
    we::SqlExportDatabase db;
    db.SetOutputPath(path);
    we::DbError e = repo.SaveSource(db, key, key, rows);
    if (!e.ok) { std::fprintf(stderr, "emit-conditions-sql FAILED: %s\n", e.message.c_str()); return 1; }
    std::printf("EMIT CONDITIONS SQL OK -> %s\n", path.c_str());
    return 0;
}

// Verify the Loot workbench's save through SqlExportDatabase (no live DB): ReplaceChildren
// (delete-by-Entry + INSERT per drop over the shared 10 loot columns) for every loot table,
// catching any column-name typo against the DDL. Usage: --emit-loot-sql [path]
int EmitLootSql(const std::string& path)
{
    we::DbTableRepository repo;
    we::DbError e;
    int n = 0;
    for (const we::LootTableDef& t : we::LootTableList())
    {
        std::vector<we::DbRecord> rows(2);
        for (int i = 0; i < 2; ++i)
        {
            for (const we::DbColumn& c : we::LootColumns())
                rows[i].cells[c.name] = (c.type == we::DbColType::Text) ? "x" : "1";
            rows[i].cells["Entry"] = "700";
            rows[i].cells["Item"] = i == 0 ? "6948" : "0";
            rows[i].cells["Reference"] = i == 0 ? "0" : "34567";
        }
        we::SqlExportDatabase db;
        db.SetOutputPath(path + "." + t.table + ".sql");
        e = repo.ReplaceChildren(db, t.table, "Entry", "700", we::LootColumns(), rows);
        if (!e.ok)
            break;
        ++n;
    }
    if (!e.ok) { std::fprintf(stderr, "emit-loot-sql FAILED: %s\n", e.message.c_str()); return 1; }
    std::printf("EMIT LOOT SQL OK (%d loot tables)\n", n);
    return 0;
}

// Verify the Creature Text editor's save through SqlExportDatabase (no live DB): ReplaceChildren
// (delete-by-CreatureID + INSERT per line) for creature_text and creature_text_locale, catching
// any column-name typo against the DDL. Usage: --emit-creaturetext-sql [path]
int EmitCreatureTextSql(const std::string& path)
{
    we::DbTableRepository repo;
    std::vector<we::DbRecord> lines(2);
    for (int i = 0; i < 2; ++i)
    {
        for (const we::DbColumn& c : we::CreatureTextColumns())
            lines[i].cells[c.name] =
                (c.type == we::DbColType::Text || c.type == we::DbColType::Multiline) ? "hi" : "0";
        lines[i].cells["CreatureID"] = "448";
        lines[i].cells["ID"] = std::to_string(i);
    }
    std::vector<we::DbRecord> locs(1);
    locs[0].cells["CreatureID"] = "448";
    locs[0].cells["GroupID"] = "0";
    locs[0].cells["ID"] = "0";
    locs[0].cells["Locale"] = "deDE";
    locs[0].cells["Text"] = "hallo";

    we::SqlExportDatabase db;
    db.SetOutputPath(path + ".creature_text.sql");
    we::DbError e = repo.ReplaceChildren(db, "creature_text", "CreatureID", "448",
                                         we::CreatureTextColumns(), lines);
    if (e.ok)
    {
        we::SqlExportDatabase db2;
        db2.SetOutputPath(path + ".creature_text_locale.sql");
        e = repo.ReplaceChildren(db2, "creature_text_locale", "CreatureID", "448",
                                 we::CreatureTextLocaleColumns(), locs);
    }
    if (!e.ok) { std::fprintf(stderr, "emit-creaturetext-sql FAILED: %s\n", e.message.c_str()); return 1; }
    std::printf("EMIT CREATURETEXT SQL OK -> %s (+ .creature_text_locale.sql)\n", path.c_str());
    return 0;
}

// Verify the Gossip editor's save through SqlExportDatabase (no live DB): ReplaceChildren
// (delete-by-MenuID + INSERT) for gossip_menu, gossip_menu_option and _locale, catching any
// column-name typo against the DDL. Usage: --emit-gossip-sql [path]
int EmitGossipSql(const std::string& path)
{
    we::DbTableRepository repo;
    auto fill = [](we::DbRecord& r, const std::vector<we::DbColumn>& cols) {
        for (const we::DbColumn& c : cols)
            r.cells[c.name] = (c.type == we::DbColType::Text || c.type == we::DbColType::Multiline) ? "x" : "1";
    };

    std::vector<we::DbRecord> links(1);
    fill(links[0], we::GossipMenuColumns());
    links[0].cells["MenuID"] = "60";

    std::vector<we::DbRecord> opts(2);
    for (int i = 0; i < 2; ++i)
    {
        fill(opts[i], we::GossipOptionColumns());
        opts[i].cells["MenuID"] = "60";
        opts[i].cells["OptionID"] = std::to_string(i);
    }
    std::vector<we::DbRecord> locs(1);
    fill(locs[0], we::GossipOptionLocaleColumns());
    locs[0].cells["MenuID"] = "60";
    locs[0].cells["OptionID"] = "0";
    locs[0].cells["Locale"] = "deDE";

    we::SqlExportDatabase db1; db1.SetOutputPath(path + ".gossip_menu.sql");
    we::DbError e = repo.ReplaceChildren(db1, "gossip_menu", "MenuID", "60", we::GossipMenuColumns(), links);
    if (e.ok) { we::SqlExportDatabase db2; db2.SetOutputPath(path + ".gossip_menu_option.sql");
        e = repo.ReplaceChildren(db2, "gossip_menu_option", "MenuID", "60", we::GossipOptionColumns(), opts); }
    if (e.ok) { we::SqlExportDatabase db3; db3.SetOutputPath(path + ".gossip_menu_option_locale.sql");
        e = repo.ReplaceChildren(db3, "gossip_menu_option_locale", "MenuID", "60", we::GossipOptionLocaleColumns(), locs); }
    if (!e.ok) { std::fprintf(stderr, "emit-gossip-sql FAILED: %s\n", e.message.c_str()); return 1; }
    std::printf("EMIT GOSSIP SQL OK -> %s (+ option, option_locale)\n", path.c_str());
    return 0;
}

// Verify the Page Text + POI editors' save (primary upsert + locale delete/insert) through
// SqlExportDatabase (no live DB), catching any column-name typo against the DDL.
// Usage: --emit-pagepoi-sql [path]
int EmitPagePoiSql(const std::string& path)
{
    we::DbTableRepository repo;
    we::DbError e;
    for (const we::DbTableSchema* s : {&we::PageTextSchema(), &we::PointsOfInterestSchema()})
    {
        we::DbRecord rec;
        rec.id = 5000;
        rec.cells[s->pk] = "5000";
        for (const we::DbColumn& c : s->cols)
            rec.cells[c.name] =
                (c.type == we::DbColType::Text || c.type == we::DbColType::Multiline) ? "x" : "1";
        we::DbLocaleRow loc;
        loc.locale = "deDE";
        for (const char* lc : s->localizedCols)
            loc.cells[lc] = "uebersetzt";
        rec.locales.push_back(loc);

        we::SqlExportDatabase db;
        db.SetOutputPath(path + "." + s->table + ".sql");
        e = repo.Save(db, *s, rec);
        if (!e.ok)
            break;
    }
    if (!e.ok) { std::fprintf(stderr, "emit-pagepoi-sql FAILED: %s\n", e.message.c_str()); return 1; }
    std::printf("EMIT PAGE/POI SQL OK (page_text + points_of_interest)\n");
    return 0;
}

// Verify the NPC Text editor's save (90-column upsert + 16-column locale delete/insert) through
// SqlExportDatabase (no live DB), catching any column-name typo against the DDL.
// Usage: --emit-npctext-sql [out.sql]
int EmitNpcTextSql(const std::string& path)
{
    const we::DbTableSchema& s = we::NpcTextSchema();
    we::DbRecord rec;
    rec.id = 60;
    rec.cells[s.pk] = "60";
    for (const we::DbColumn& c : s.cols)
        rec.cells[c.name] =
            (c.type == we::DbColType::Text || c.type == we::DbColType::Multiline) ? "greeting" : "1";
    we::DbLocaleRow loc;
    loc.locale = "deDE";
    for (const char* lc : s.localizedCols)
        loc.cells[lc] = "gruss";
    rec.locales.push_back(loc);

    we::SqlExportDatabase db;
    db.SetOutputPath(path);
    we::DbTableRepository repo;
    we::DbError e = repo.Save(db, s, rec);
    if (!e.ok) { std::fprintf(stderr, "emit-npctext-sql FAILED: %s\n", e.message.c_str()); return 1; }
    std::printf("EMIT NPCTEXT SQL OK -> %s\n", path.c_str());
    return 0;
}

// Verify the SmartAI editor's save through SqlExportDatabase (no live DB): SmartScriptRepository::
// SaveScript emits DELETE-by-scope (entryorguid, source_type) + one INSERT per row over the 30
// smart_scripts columns, catching any column-name typo against the DDL. Usage: --emit-smartai-sql [out.sql]
int EmitSmartAiSql(const std::string& path)
{
    we::SmartScriptRepository repo;
    std::vector<we::DbRecord> rows(2);
    for (int i = 0; i < 2; ++i)
    {
        for (const we::DbColumn& c : we::SmartScriptRepository::Columns())
            rows[i].cells[c.name] = (c.type == we::DbColType::Text) ? "on aggro - cast" : "0";
        rows[i].cells["id"] = std::to_string(i);
        rows[i].cells["link"] = i == 0 ? "1" : "0";      // row 0 links to row 1
        rows[i].cells["event_type"] = i == 0 ? "4" : "61";  // AGGRO / LINK
        rows[i].cells["action_type"] = "11";              // CAST
        rows[i].cells["target_x"] = "1.5";
    }
    we::SqlExportDatabase db;
    db.SetOutputPath(path);
    we::DbError e = repo.SaveScript(db, 12345, 0, rows);
    if (!e.ok) { std::fprintf(stderr, "emit-smartai-sql FAILED: %s\n", e.message.c_str()); return 1; }
    std::printf("EMIT SMARTAI SQL OK -> %s\n", path.c_str());
    return 0;
}

// Verify the Spell editor's server writes through SqlExportDatabase (no live DB): the
// DBC->spell_dbc projection of a real Spell.dbc row + a sample spell_proc + spell_required.
// Usage: --emit-spell-sql <clientRoot> [path]
int EmitSpellSql(const std::string& clientRoot, const std::string& path)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot)) { std::fprintf(stderr, "open failed\n"); return 1; }
    we::DbcDocument doc;
    doc.Init(&we::SpellSchema(), "DBFilesClient\\Spell.dbc");
    if (!doc.Load(cd)) { std::fprintf(stderr, "Spell.dbc load failed\n"); return 1; }

    // 1) DBC -> spell_dbc projection of row 0 (spell id 1).
    we::SpellRepository repo;
    we::SqlExportDatabase db1;
    db1.SetOutputPath(path);
    we::DbError e = repo.SaveSpellDbc(db1, doc, 0);

    // 2) A 1:1 augmentation row (spell_proc).
    const std::string procPath = path + ".proc.sql";
    if (e.ok)
    {
        we::DbRecord proc;
        proc.id = 25000;
        proc.cells["ProcFlags"] = "16384";
        proc.cells["Chance"] = "10.5";
        proc.cells["Charges"] = "1";
        we::SqlExportDatabase db2;
        db2.SetOutputPath(procPath);
        we::DbTableRepository dbr;
        e = dbr.Save(db2, we::SpellRepository::ProcSchema(), proc);
    }

    // 3) A child list (spell_required).
    const std::string reqPath = path + ".required.sql";
    if (e.ok)
    {
        std::vector<we::DbRecord> reqs(2);
        reqs[0].cells["spell_id"] = "25000"; reqs[0].cells["req_spell"] = "100";
        reqs[1].cells["spell_id"] = "25000"; reqs[1].cells["req_spell"] = "200";
        we::SqlExportDatabase db3;
        db3.SetOutputPath(reqPath);
        we::DbTableRepository dbr;
        const we::SpellChildSpec& s = we::SpellRepository::Required();
        e = dbr.ReplaceChildren(db3, s.table, s.parentCol, "25000", s.cols, reqs);
    }

    if (!e.ok) { std::fprintf(stderr, "emit-spell-sql FAILED: %s\n", e.message.c_str()); return 1; }
    std::printf("EMIT SPELL SQL OK -> %s (+ .proc.sql, .required.sql)\n", path.c_str());
    return 0;
}
} // namespace

namespace
{
// Dev tool: decode a BLP from client data and write it as a 32-bit BMP for eyeballing.
// Usage: --dump-blp <DataOrLooseRoot> <archivePath> <outBmp>
int DumpBlp(const std::string& data, const std::string& archivePath, const std::string& outBmp)
{
    we::ClientData cd;
    if (!cd.Open(data)) { std::fprintf(stderr, "open failed\n"); return 1; }
    we::BlpImage img = we::DecodeBlp(cd.ReadFile(archivePath));
    if (!img.valid()) { std::fprintf(stderr, "decode failed: %s\n", archivePath.c_str()); return 1; }
    {   // alpha stats: min/avg over all texels tells opaque-black vs proper-cutout textures apart
        long amin = 255, asum = 0; size_t n = img.rgba.size() / 4;
        for (size_t i = 0; i < n; ++i) { int a = img.rgba[i*4+3]; if (a < amin) amin = a; asum += a; }
        std::printf("decoded %s -> %dx%d  alpha[min=%ld avg=%ld]\n", archivePath.c_str(),
                    img.width, img.height, n ? amin : 0, n ? asum / (long)n : 0);
    }
    const int w = img.width, h = img.height;
    const uint32_t rowBytes = static_cast<uint32_t>(w) * 4, pixBytes = rowBytes * h;
    const uint32_t fileSize = 54 + pixBytes;
    uint8_t hdr[54] = {0}; hdr[0] = 'B'; hdr[1] = 'M';
    std::memcpy(hdr + 2, &fileSize, 4); uint32_t off = 54; std::memcpy(hdr + 10, &off, 4);
    uint32_t dib = 40; std::memcpy(hdr + 14, &dib, 4);
    std::memcpy(hdr + 18, &w, 4); int negH = -h; std::memcpy(hdr + 22, &negH, 4);
    uint16_t planes = 1; std::memcpy(hdr + 26, &planes, 2);
    uint16_t bpp = 32; std::memcpy(hdr + 28, &bpp, 2);
    std::memcpy(hdr + 34, &pixBytes, 4);
    FILE* f = std::fopen(outBmp.c_str(), "wb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", outBmp.c_str()); return 1; }
    std::fwrite(hdr, 1, 54, f);
    std::vector<uint8_t> row(rowBytes);
    for (int y = 0; y < h; ++y)
    {
        const uint8_t* src = img.rgba.data() + static_cast<size_t>(y) * rowBytes;
        for (int x = 0; x < w; ++x)
        { row[x*4]=src[x*4+2]; row[x*4+1]=src[x*4+1]; row[x*4+2]=src[x*4]; row[x*4+3]=src[x*4+3]; }
        std::fwrite(row.data(), 1, rowBytes, f);
    }
    std::fclose(f);
    std::printf("wrote %s\n", outBmp.c_str());
    return 0;
}

int DbcTest(const std::string& path)
{
    we::ClientData cd;
    if (!cd.Open(path))
    {
        std::fprintf(stderr, "open failed: %s\n", cd.SourceDescription().c_str());
        return 1;
    }
    std::printf("Opened: %s\n", cd.SourceDescription().c_str());
    we::DbcStore store;
    auto report = [&](const char* name, const std::unordered_map<uint32_t, std::string>& m)
    {
        std::printf("%-10s %6zu names", name, m.size());
        int shown = 0;
        for (const auto& kv : m)
        {
            if (shown++ >= 3)
                break;
            std::printf("  | %u=%s", kv.first, kv.second.c_str());
        }
        std::printf("\n");
    };
    report("factions", store.LoadFactionNames(cd));
    report("areas", store.LoadAreaNames(cd));
    report("skills", store.LoadSkillNames(cd));
    report("titles", store.LoadTitleNames(cd));
    report("spells", store.LoadSpellNames(cd));

    // Probe raw field layout of the two problem DBCs: print fieldCount and, for the
    // first record, every field index that yields a non-empty string (=name column).
    auto probe = [&](const char* file)
    {
        we::Dbc d;
        if (!d.Load(cd.ReadFile(file)))
        {
            std::printf("PROBE %s: load failed (missing?)\n", file);
            return;
        }
        std::printf("PROBE %s: records=%u fields=%u recordSize=%u\n", file, d.RecordCount(),
                    d.FieldCount(), d.RecordSize());
        for (uint32_t r = 0; r < d.RecordCount() && r < 2; ++r)
        {
            std::printf("  rec %u id=%u fields 132..144:", r, d.GetUInt(r, 0));
            for (uint32_t f = 132; f <= 144 && f < d.FieldCount(); ++f)
            {
                std::string s = d.GetString(r, f);
                std::printf(" [%u]=\"%s\"", f, s.c_str());
            }
            std::printf("\n");
        }
    };
    (void)&probe;

    // Icon chains.
    auto itemIcons = store.LoadItemDisplayIcons(cd);
    auto spellIconIds = store.LoadSpellIconIds(cd);
    auto spellIconPaths = store.LoadSpellIconPaths(cd);
    std::printf("itemDisplayIcons=%zu spellIconIds=%zu spellIconPaths=%zu\n", itemIcons.size(),
                spellIconIds.size(), spellIconPaths.size());
    for (auto& kv : itemIcons)
    {
        std::printf("  sample item displayId %u -> icon '%s'\n", kv.first, kv.second.c_str());
        break;
    }
    for (auto& kv : spellIconPaths)
    {
        std::printf("  sample spellIcon %u -> '%s'\n", kv.first, kv.second.c_str());
        break;
    }

    // BLP decode check on a known icon.
    auto tryDecode = [&](const char* p)
    {
        we::BlpImage img = we::DecodeBlp(cd.ReadFile(p));
        std::printf("BLP %s: %dx%d valid=%d\n", p, img.width, img.height, img.valid() ? 1 : 0);
    };
    tryDecode("Interface\\Icons\\INV_Misc_QuestionMark.blp");
    tryDecode("Interface\\Icons\\Spell_Fire_FlameBolt.blp");

    // WorldMapArea + a sample map tile.
    auto areas = store.LoadWorldMapAreas(cd);
    std::printf("worldMapAreas=%zu\n", areas.size());
    for (auto& kv : areas)
    {
        const auto& a = kv.second;
        if (a.dir == "Elwynn" || a.dir == "Durotar" || kv.first == 9)
        {
            std::printf("  area %u dir='%s' L=%.0f R=%.0f T=%.0f B=%.0f\n", kv.first, a.dir.c_str(),
                        a.left, a.right, a.top, a.bottom);
            tryDecode(("Interface\\WorldMap\\" + a.dir + "\\" + a.dir + "1.blp").c_str());
            break;
        }
    }
    return 0;
}

// Dev tool: parse an M2 (+ its .skin) from client data and print decoded counts, so the
// loader can be validated headless. Usage: --m2-test <clientRoot> <m2Path>
int M2Test(const std::string& clientRoot, const std::string& m2Path)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot))
    {
        std::fprintf(stderr, "open failed: %s\n", cd.SourceDescription().c_str());
        return 1;
    }
    std::printf("Opened: %s\n", cd.SourceDescription().c_str());
    std::printf("ListFiles(.m2) -> %zu entries\n", cd.ListFiles(".m2").size());

    we::m2::M2Model model;
    std::string err;
    if (!we::m2::Load(cd, m2Path, model, &err))
    {
        std::fprintf(stderr, "M2 load FAILED (%s): %s\n", m2Path.c_str(), err.c_str());
        return 1;
    }

    std::printf("M2 OK  %s\n", model.name.c_str());
    std::printf("  vertices=%zu indices=%zu batches=%zu\n", model.vertices.size(),
                model.indices.size(), model.batches.size());
    std::printf("  textures=%zu bones=%zu sequences=%zu globalSeq=%zu\n",
                model.texturePaths.size(), model.bones.size(), model.sequences.size(),
                model.globalSequenceDurations.size());
    std::printf("  particles=%zu ribbons=%zu texAnims=%zu colors=%zu transparencies=%zu\n",
                model.particleEmitters.size(), model.ribbonEmitters.size(),
                model.textureTransforms.size(), model.colors.size(), model.transparencies.size());
    std::printf("  geoParticleModels=%zu\n", model.geoParticleModels.size());
    for (size_t i = 0; i < model.geoParticleModels.size(); ++i)
        std::printf("  geoModel[%zu] verts=%zu subs=%zu textures=%zu texBase=%d\n", i,
                    model.geoParticleModels[i].verts.size(), model.geoParticleModels[i].subs.size(),
                    model.geoParticleModels[i].textures.size(), model.geoParticleModels[i].textureBase);
    std::printf("  bounds center=(%.2f,%.2f,%.2f) radius=%.2f\n", model.boundsCenter.x,
                model.boundsCenter.y, model.boundsCenter.z, model.boundsRadius);
    for (size_t i = 0; i < model.texturePaths.size(); ++i)
        std::printf("  tex[%zu] type=%u '%s'\n", i, model.textureTypes[i],
                    model.texturePaths[i].c_str());
    we::m2::M2Animator anim;
    anim.SetModel(&model, &cd, m2Path);
    std::vector<glm::mat4> bones;
    anim.Evaluate(0, 0.0f, glm::mat4(1.0f), bones);   // bind / stand pose
    for (size_t i = 0; i < model.batches.size() && i < 64; ++i)
    {
        const we::m2::RenderBatch& b = model.batches[i];
        glm::vec4 col = anim.BatchColor(b.colorIndex, b.textureWeightIndex, 0, 0.0f);
        std::printf("  batch[%zu] idx[%u..+%u] tex=%d blend=%u flags=0x%02x shader=%u layer=%u texN=%u "
                    "col=%d wgt=%d alpha=%.3f submesh=%u\n",
                    i, b.indexStart, b.indexCount, b.textureIndex, b.blendMode, b.materialFlags,
                    b.shaderId, b.materialLayer, b.textureCount, b.colorIndex, b.textureWeightIndex,
                    col.a, b.submeshId);
    }
    std::printf("  attachments=%zu bones=%zu\n", model.attachments.size(), bones.size());
    for (const we::m2::M2Attachment& at : model.attachments)
        std::printf("    attach id=%u bone=%u pos=(%.3f,%.3f,%.3f)\n", at.id, at.bone,
                    at.pos[0], at.pos[1], at.pos[2]);
    std::printf("M2TEST OK\n");
    return 0;
}

// Dev tool: scan every .m2 in the client and report models that use geometry-model
// particles (emitter.geometryModelFilename). Usage: --m2-geoscan <clientRoot> [limit]
int M2GeoScan(const std::string& clientRoot, int limit)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot))
    {
        std::fprintf(stderr, "open failed: %s\n", cd.SourceDescription().c_str());
        return 1;
    }
    std::vector<std::string> all = cd.ListFiles(".m2");
    std::printf("scanning %zu .m2 files (limit hits=%d)\n", all.size(), limit);
    int hits = 0, scanned = 0;
    for (const std::string& path : all)
    {
        we::m2::M2Model model;
        if (!we::m2::Load(cd, path, model, nullptr))
            continue;
        ++scanned;
        if (!model.geoParticleModels.empty())
        {
            ++hits;
            std::printf("HIT %s  geoModels=%zu (of %zu emitters)\n", path.c_str(),
                        model.geoParticleModels.size(), model.particleEmitters.size());
            if (limit > 0 && hits >= limit)
                break;
        }
    }
    std::printf("M2GEOSCAN done: scanned=%d hits=%d\n", scanned, hits);
    return 0;
}

// Shared: bring up the renderer headless, render `model` to a BMP through the real
// Vulkan model pipeline. `model.texturePaths` should already have any runtime skins
// filled in by the caller. `bones`/`boneCount` optional (null => bind pose).
// Write top-down RGBA8 as a 32-bit BMP (negative height), like --dump-blp.
void WriteBmp(const std::vector<uint8_t>& rgba, int rw, int rh, const std::string& outBmp)
{
    const uint32_t rowBytes = static_cast<uint32_t>(rw) * 4, pixBytes = rowBytes * rh;
    const uint32_t fileSize = 54 + pixBytes;
    uint8_t hdr[54] = {0}; hdr[0] = 'B'; hdr[1] = 'M';
    std::memcpy(hdr + 2, &fileSize, 4); uint32_t off = 54; std::memcpy(hdr + 10, &off, 4);
    uint32_t dib = 40; std::memcpy(hdr + 14, &dib, 4);
    std::memcpy(hdr + 18, &rw, 4); int negH = -rh; std::memcpy(hdr + 22, &negH, 4);
    uint16_t planes = 1; std::memcpy(hdr + 26, &planes, 2);
    uint16_t bpp = 32; std::memcpy(hdr + 28, &bpp, 2);
    std::memcpy(hdr + 34, &pixBytes, 4);
    FILE* f = std::fopen(outBmp.c_str(), "wb");
    if (!f) return;
    std::fwrite(hdr, 1, 54, f);
    std::vector<uint8_t> row(rowBytes);
    for (int y = 0; y < rh; ++y)
    {
        const uint8_t* src = rgba.data() + static_cast<size_t>(y) * rowBytes;
        for (int x = 0; x < rw; ++x)
        { row[x*4]=src[x*4+2]; row[x*4+1]=src[x*4+1]; row[x*4+2]=src[x*4]; row[x*4+3]=src[x*4+3]; }
        std::fwrite(row.data(), 1, rowBytes, f);
    }
    std::fclose(f);
}

// Bring up the renderer headless, render an already-built ModelUpload to a BMP framed on
// the given bounds. Shared by the M2 and WMO shot harnesses (WMO passes bones=nullptr).
int RenderUploadToBmp(const we::ModelUpload& up, const glm::vec3& center, float radius,
                      const std::string& outBmp, const char* okTag,
                      const float* bones = nullptr, int boneCount = 0,
                      const we::EffectFrame* effects = nullptr,
                      const we::SubmeshAnim* anims = nullptr, int animCount = 0,
                      const uint8_t* geosetVis = nullptr, int geosetCount = 0)
{
    // Minimal graphics bring-up (hidden window, no swapchain present).
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    we::Window window;
    if (!window.Create(600, 600, "shot", /*visible=*/false))
    {
        std::fprintf(stderr, "window create failed\n");
        return 1;
    }
    auto renderer = std::make_unique<we::VulkanRenderer>();
    if (!renderer->Init(window, /*headless=*/true))
    {
        std::fprintf(stderr, "renderer init failed\n");
        return 1;
    }

    we::ModelHandle h = renderer->CreateModel(up);
    if (!h)
    {
        std::fprintf(stderr, "CreateModel failed\n");
        return 1;
    }
    if (geosetVis && geosetCount > 0)
        renderer->SetSubmeshVisibility(h, geosetVis, geosetCount);

    const int W = 600, H = 600;
    const glm::vec3 c = center;
    const float r = radius;
    const glm::vec3 eye = c + glm::normalize(glm::vec3(1.0f, -1.4f, 0.55f)) * (r * 2.6f);
    glm::mat4 view = glm::lookAt(eye, c, glm::vec3(0.0f, 0.0f, 1.0f));  // WoW is Z-up
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), float(W) / H, r * 0.05f, r * 30.0f);
    proj[1][1] *= -1.0f;  // Vulkan clip space Y points down

    const float gc[3] = {c.x, c.y, c.z - r};
    renderer->SetGrid(true, gc, r * 2.5f, std::max(r * 2.5f / 20.0f, 1e-4f));
    renderer->RenderModel(h, &view[0][0], &proj[0][0], bones, boneCount, anims, animCount, effects, W, H);
    std::vector<uint8_t> rgba;
    int rw = 0, rh = 0;
    if (!renderer->CaptureModelTarget(rgba, rw, rh))
    {
        std::fprintf(stderr, "capture failed\n");
        return 1;
    }

    WriteBmp(rgba, rw, rh, outBmp);

    renderer->DestroyModel(h);
    renderer->Shutdown();
    ImGui::DestroyContext();
    std::printf("%s -> %s (%dx%d, %zu verts, %zu batches)\n", okTag, outBmp.c_str(), rw, rh,
                up.vertices.size(), up.submeshes.size());
    return 0;
}

int RenderM2ToBmp(we::ClientData& cd, const we::m2::M2Model& model, const std::string& outBmp,
                  const float* bones = nullptr, int boneCount = 0,
                  const we::EffectFrame* effects = nullptr,
                  const we::SubmeshAnim* anims = nullptr, int animCount = 0,
                  const uint8_t* geosetVis = nullptr, int geosetCount = 0)
{
    we::ModelUpload up = we::m2::BuildUpload(cd, model);
    return RenderUploadToBmp(up, model.boundsCenter, model.boundsRadius, outBmp, "M2SHOT OK",
                             bones, boneCount, effects, anims, animCount, geosetVis, geosetCount);
}

// True for a WMO group file ("Foo_000.wmo".."Foo_NNN.wmo"), which the browser hides.
bool IsWmoGroupFile(const std::string& path)
{
    if (path.size() < 8)
        return false;
    const char* p = path.c_str() + path.size() - 8;   // "_NNN.wmo"
    return p[0] == '_' && std::isdigit((unsigned char)p[1]) && std::isdigit((unsigned char)p[2]) &&
           std::isdigit((unsigned char)p[3]);
}

// Dev tool: dump LiquidType.dbc rows (id, type, and every non-empty string field) so the
// exact 3.3.5a field layout can be confirmed. Usage: --dbc-liquid <clientRoot>
int DbcLiquid(const std::string& clientRoot)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot))
    {
        std::fprintf(stderr, "open failed: %s\n", cd.SourceDescription().c_str());
        return 1;
    }
    we::Dbc dbc;
    if (!dbc.Load(cd.ReadFile("DBFilesClient\\LiquidType.dbc")))
    {
        std::fprintf(stderr, "LiquidType.dbc load failed\n");
        return 1;
    }
    std::printf("LiquidType.dbc: %u records, %u fields, recSize=%u\n", dbc.RecordCount(),
                dbc.FieldCount(), dbc.RecordSize());
    for (uint32_t r = 0; r < dbc.RecordCount() && r < 45; ++r)
    {
        std::printf("id=%u f3=%u : ", dbc.GetUInt(r, 0), dbc.GetUInt(r, 3));
        for (uint32_t f = 1; f < dbc.FieldCount(); ++f)
        {
            std::string s = dbc.GetString(r, f);
            if (s.size() > 1 && s.size() < 80)
                std::printf("[%u]=%s ", f, s.c_str());
        }
        std::printf("\n");
    }
    return 0;
}

// Dev tool: dump AnimationData.dbc id -> name. Usage: --anim-names <clientRoot>
int AnimNames(const std::string& clientRoot)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot)) { std::fprintf(stderr, "open failed\n"); return 1; }
    we::DbcStore dbc;
    auto names = dbc.LoadAnimationNames(cd);
    std::printf("AnimationData.dbc: %zu names\n", names.size());
    for (uint32_t id = 0; id < 30; ++id)
    {
        auto it = names.find(id);
        if (it != names.end()) std::printf("  %u = %s\n", id, it->second.c_str());
    }
    return 0;
}

// Dev tool: dump Map.dbc id -> directory + name. Usage: --dbc-map <clientRoot>
int DbcMap(const std::string& clientRoot)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot)) { std::fprintf(stderr, "open failed\n"); return 1; }
    we::DbcStore dbc;
    auto maps = dbc.LoadMaps(cd);
    std::printf("Map.dbc: %zu maps\n", maps.size());
    for (uint32_t id = 0; id < 800; ++id)
    {
        auto it = maps.find(id);
        if (it != maps.end())
            std::printf("  %u = %-24s (%s)\n", id, it->second.directory.c_str(), it->second.name.c_str());
    }
    return 0;
}

// Dev tool: dump any DBC's WDBC header + the first rows, each field shown as uint and
// (when it resolves to a printable string) as text — used to confirm a DBC's field layout
// before hand-writing its schema. Usage: --dbc-dump <clientRoot> <DBFilesClient\Name.dbc> [rows]
int DbcDump(const std::string& clientRoot, const std::string& dbcPath, int maxRows)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot)) { std::fprintf(stderr, "open failed\n"); return 1; }
    std::vector<uint8_t> bytes = cd.ReadFile(dbcPath);
    if (bytes.empty()) { std::fprintf(stderr, "read failed: %s\n", dbcPath.c_str()); return 1; }
    we::Dbc dbc;
    if (!dbc.Load(bytes)) { std::fprintf(stderr, "not a WDBC blob: %s\n", dbcPath.c_str()); return 1; }
    std::printf("%s\n  records=%u fields=%u recordSize=%u\n", dbcPath.c_str(),
                dbc.RecordCount(), dbc.FieldCount(), dbc.RecordSize());
    const uint32_t rows = dbc.RecordCount() < static_cast<uint32_t>(maxRows)
                              ? dbc.RecordCount()
                              : static_cast<uint32_t>(maxRows);
    for (uint32_t r = 0; r < rows; ++r)
    {
        std::printf("  [rec %u]\n", r);
        for (uint32_t f = 0; f < dbc.FieldCount(); ++f)
        {
            uint32_t u = dbc.GetUInt(r, f);
            std::string s = dbc.GetString(r, f);
            bool nameLike = !s.empty() && s.size() <= 100;
            for (unsigned char c : s)
                if (c < 0x20 && c != '\t') { nameLike = false; break; }
            if (nameLike)
                std::printf("    f%-2u = %-11u  \"%s\"\n", f, u, s.c_str());
            else
                std::printf("    f%-2u = %-11u\n", f, u);
        }
    }
    return 0;
}

// Load a DBC through EditableDbc under `schema`, Serialize() it, reload the output, and
// confirm counts + a sample of ids/strings survive. Proves the WDBC serializer is faithful.
// `strCols` are physical string columns to compare (nullptr-terminated). Returns true on OK.
bool RoundtripOne(we::ClientData& cd, const char* path, const we::DbcSchema& schema,
                  const std::vector<uint32_t>& strCols)
{
    std::vector<uint8_t> bytes = cd.ReadFile(path);
    if (bytes.empty()) { std::fprintf(stderr, "%s: not found\n", path); return false; }
    we::EditableDbc ed;
    if (!ed.Load(bytes, schema))
    {
        std::fprintf(stderr, "%s: EditableDbc.Load rejected (layout mismatch)\n", path);
        return false;
    }
    std::vector<uint8_t> out = ed.Serialize();
    we::Dbc a, b;
    if (!a.Load(bytes) || !b.Load(out)) { std::fprintf(stderr, "%s: reload failed\n", path); return false; }
    if (a.RecordCount() != b.RecordCount() || a.FieldCount() != b.FieldCount() ||
        a.RecordSize() != b.RecordSize())
    {
        std::fprintf(stderr, "%s: FAIL header mismatch\n", path);
        return false;
    }
    uint32_t mismatches = 0;
    for (uint32_t r = 0; r < a.RecordCount(); ++r)
    {
        bool diff = a.GetUInt(r, 0) != b.GetUInt(r, 0);
        for (uint32_t c : strCols)
            if (a.GetString(r, c) != b.GetString(r, c))
                diff = true;
        if (diff)
            ++mismatches;
    }
    if (mismatches) { std::fprintf(stderr, "%s: FAIL %u/%u records differ\n", path, mismatches, a.RecordCount()); return false; }
    std::printf("  %-34s OK  (%u records, %u fields, %zu bytes)\n", path, a.RecordCount(),
                a.FieldCount(), out.size());
    return true;
}

// Full round-trip of one DBC under a .dbd-derived schema, through the real edit path
// (EditableDbc). Validates the header against the schema (physical field count + byte-accurate
// record size, so packed sub-4-byte DBCs are covered), then load -> serialize -> reload and
// compares every cell (strings by value, scalars by raw). Returns "" on success, else a reason.
// Uses EditableDbc rather than the read-only Dbc, which rejects recordSize < 4 (the packed DBCs).
static std::string DbdRoundtripOne(we::ClientData& cd, const std::string& path,
                                   const we::DbcSchema& schema)
{
    std::vector<uint8_t> bytes = cd.ReadFile(path);
    if (bytes.size() < 20)
        return "not present in client";
    if (!(bytes[0] == 'W' && bytes[1] == 'D' && bytes[2] == 'B' && bytes[3] == 'C'))
        return "not a WDBC blob";
    auto rd = [&](size_t o) {
        return static_cast<uint32_t>(bytes[o]) | (static_cast<uint32_t>(bytes[o + 1]) << 8) |
               (static_cast<uint32_t>(bytes[o + 2]) << 16) |
               (static_cast<uint32_t>(bytes[o + 3]) << 24);
    };
    const uint32_t hdrFields = rd(8), hdrRecSize = rd(12);
    if (schema.FieldCount() != hdrFields)
    {
        std::ostringstream os;
        os << "field-count mismatch (def " << schema.FieldCount() << " vs dbc " << hdrFields << ")";
        return os.str();
    }
    if (schema.RecordByteSize() != hdrRecSize)
    {
        std::ostringstream os;
        os << "record-size mismatch (def " << schema.RecordByteSize() << "B vs dbc " << hdrRecSize
           << "B)";
        return os.str();
    }

    we::EditableDbc a;
    if (!a.Load(bytes, schema))
        return "EditableDbc.Load rejected";
    std::vector<uint8_t> out = a.Serialize();
    we::EditableDbc b;
    if (!b.Load(out, schema))
        return "reload rejected";
    if (a.RecordCount() != b.RecordCount())
        return "record-count mismatch after serialize";

    const uint32_t cols = schema.FieldCount();
    for (uint32_t r = 0; r < a.RecordCount(); ++r)
        for (uint32_t c = 0; c < cols; ++c)
        {
            if (a.ColumnIsString(c))
            {
                if (a.GetStr(r, c) != b.GetStr(r, c))
                {
                    std::ostringstream os;
                    os << "string diff at r" << r << " c" << c;
                    return os.str();
                }
            }
            else if (a.GetU32(r, c) != b.GetU32(r, c))
            {
                std::ostringstream os;
                os << "scalar diff at r" << r << " c" << c;
                return os.str();
            }
        }
    return {};
}

// Exercise the generic DBC editor's open logic (registry lookup -> validate vs header -> raw
// fallback / unsupported) against real client files, without the GUI. Usage:
// --dbc-registry-test <clientRoot>
int DbcRegistryTest(const std::string& clientRoot)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot)) { std::fprintf(stderr, "open failed\n"); return 1; }
    we::DbcDefRegistry reg;
    std::printf("definitions dir: %s\n", reg.DefinitionsDir().c_str());

    // Representative mix: normal .dbd, packed .dbd, delete-marker file, and a made-up custom name.
    const char* names[] = {"Item",          "Map",          "PowerDisplay", "CharVariations",
                           "SpellItemEnchantmentCondition", "MyCustomTable"};
    for (const char* name : names)
    {
        const std::string archivePath = std::string("DBFilesClient\\") + name + ".dbc";
        std::vector<uint8_t> bytes = cd.ReadFile(archivePath);
        if (bytes.size() < 20 || bytes[0] != 'W' || bytes[1] != 'D' || bytes[2] != 'B' ||
            bytes[3] != 'C')
        {
            std::printf("  %-30s not present\n", name);
            continue;
        }
        auto rd = [&](size_t o) {
            return (uint32_t)bytes[o] | ((uint32_t)bytes[o + 1] << 8) |
                   ((uint32_t)bytes[o + 2] << 16) | ((uint32_t)bytes[o + 3] << 24);
        };
        const uint32_t hdrFields = rd(8), hdrRecSize = rd(12);
        const we::DbcSchema* schema = reg.Lookup(name);
        const char* source = "";
        if (schema && schema->FieldCount() == hdrFields && schema->RecordByteSize() == hdrRecSize)
            source = reg.Source(name);
        else if (hdrRecSize == hdrFields * 4)
            source = "raw";
        else
            source = "unsupported";

        std::string loaded = "-";
        if (schema && std::string(source) != "unsupported")
        {
            we::EditableDbc ed;
            loaded = ed.Load(bytes, *schema) ? std::to_string(ed.RecordCount()) + " rows"
                                             : "LOAD FAIL";
        }
        else if (std::string(source) == "raw")
        {
            loaded = "(raw grid)";
        }
        std::printf("  %-30s source=%-11s fields=%u recBytes=%u  %s\n", name, source, hdrFields,
                    hdrRecSize, loaded.c_str());
    }
    return 0;
}

// Dev/verify tool: parse every vendored .dbd, build the 3.3.5.12340 schema, and round-trip it
// against the matching client DBC (all columns, bit-exact for scalars, by-value for strings).
// This is the strongest proof that every generated layout is correct.
// Usage: --dbd-check <clientRoot> [defsDir]  (defsDir default: third_party/wowdbdefs/definitions)
int DbdCheck(const std::string& clientRoot, const std::string& defsDirArg)
{
    namespace fs = std::filesystem;
    std::string defsDir = defsDirArg.empty() ? "third_party/wowdbdefs/definitions" : defsDirArg;
    if (!fs::is_directory(defsDir))
    {
        std::fprintf(stderr, "defs dir not found: %s\n", defsDir.c_str());
        return 1;
    }
    we::ClientData cd;
    if (!cd.Open(clientRoot))
    {
        std::fprintf(stderr, "open failed: %s\n", clientRoot.c_str());
        return 1;
    }

    std::vector<std::string> dbds;
    for (const auto& e : fs::directory_iterator(defsDir))
        if (e.is_regular_file() && e.path().extension() == ".dbd")
            dbds.push_back(e.path().string());
    std::sort(dbds.begin(), dbds.end());

    uint32_t okCount = 0, failCount = 0, absentCount = 0, noDefCount = 0;
    std::vector<std::string> failures;
    for (const std::string& dbdPath : dbds)
    {
        std::string name = fs::path(dbdPath).stem().string();  // e.g. "Item"
        std::string dbcPath = "DBFilesClient\\" + name + ".dbc";
        if (cd.ReadFile(dbcPath).empty())
        {
            ++absentCount;
            continue;
        }
        std::ifstream f(dbdPath, std::ios::binary);
        std::ostringstream ss;
        ss << f.rdbuf();
        auto parsed = we::ParseDbdFor12340(ss.str());
        if (!parsed || !parsed->ok)
        {
            ++noDefCount;
            std::printf("  %-34s no 3.3.5.12340 layout\n", name.c_str());
            continue;
        }
        std::string err = DbdRoundtripOne(cd, dbcPath, parsed->schema);
        if (err.empty())
        {
            ++okCount;
            std::printf("  %-34s OK  (%u fields)\n", name.c_str(), parsed->schema.FieldCount());
        }
        else
        {
            ++failCount;
            std::printf("  %-34s FAIL: %s\n", name.c_str(), err.c_str());
            failures.push_back(name + ": " + err);
        }
    }

    std::printf("\nDBD-CHECK: %u OK, %u FAIL, %u no-def, %u absent-in-client (of %zu defs)\n",
                okCount, failCount, noDefCount, absentCount, dbds.size());
    if (!failures.empty())
    {
        std::printf("Failures:\n");
        for (const auto& s : failures)
            std::printf("  - %s\n", s.c_str());
    }
    return failCount == 0 ? 0 : 1;
}

// Dev/verify tool: round-trip Achievement.dbc + Achievement_Criteria.dbc +
// Achievement_Category.dbc through EditableDbc. Usage: --ach-roundtrip <clientRoot>
int AchRoundtrip(const std::string& clientRoot)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot)) { std::fprintf(stderr, "open failed\n"); return 1; }
    bool ok = true;
    ok &= RoundtripOne(cd, "DBFilesClient\\Achievement.dbc", we::AchievementSchema(),
                       {we::ach::TitleLoc0, we::ach::DescLoc0, we::ach::RewardLoc0});
    ok &= RoundtripOne(cd, "DBFilesClient\\Achievement_Criteria.dbc",
                       we::AchievementCriteriaSchema(), {we::ach::crit::DescLoc0});
    ok &= RoundtripOne(cd, "DBFilesClient\\Achievement_Category.dbc",
                       we::AchievementCategorySchema(), {we::ach::cat::NameLoc0});
    if (!ok)
        return 1;
    std::printf("ROUNDTRIP OK (all achievement DBCs)\n");
    return 0;
}

// Open a SINGLE .MPQ standalone (no patch chain) and report whether it directly contains a file
// and that file's stored size. Distinguishes "file removed by a patch delete-marker in the chain"
// from "file genuinely absent". Usage: --mpq-probe <mpqPath> <internalFile>
int MpqProbe(const std::string& mpqPath, const std::string& internalFile);

// Probe whether the client exposes a given archive file, and (for a .dbc) its header.
// Usage: --has-file <clientRoot> <archivePath>
int HasFileProbe(const std::string& clientRoot, const std::string& archivePath)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot)) { std::fprintf(stderr, "open failed\n"); return 1; }
    bool has = cd.HasFile(archivePath);
    std::vector<uint8_t> bytes = cd.ReadFile(archivePath);
    std::printf("HasFile(%s) = %s ; ReadFile -> %zu bytes\n", archivePath.c_str(),
                has ? "true" : "false", bytes.size());
    if (bytes.size() >= 20 && bytes[0] == 'W' && bytes[1] == 'D' && bytes[2] == 'B' &&
        bytes[3] == 'C')
    {
        auto rd = [&](size_t o) {
            return static_cast<uint32_t>(bytes[o]) | (static_cast<uint32_t>(bytes[o + 1]) << 8) |
                   (static_cast<uint32_t>(bytes[o + 2]) << 16) |
                   (static_cast<uint32_t>(bytes[o + 3]) << 24);
        };
        std::printf("  WDBC: records=%u fields=%u recordSize=%u stringSize=%u\n", rd(4), rd(8),
                    rd(12), rd(16));
    }
    return has ? 0 : 2;
}

int MpqProbe(const std::string& mpqPath, const std::string& internalFile)
{
    std::wstring wpath(mpqPath.begin(), mpqPath.end());
    HANDLE mpq = nullptr;
    if (!SFileOpenArchive(wpath.c_str(), 0, MPQ_OPEN_READ_ONLY, &mpq) || !mpq)
    {
        std::printf("open failed: %s\n", mpqPath.c_str());
        return 1;
    }
    bool has = SFileHasFile(mpq, internalFile.c_str()) != 0;
    std::printf("%-28s HasFile(%s) = %s", mpqPath.c_str(), internalFile.c_str(),
                has ? "true" : "false");
    if (has)
    {
        HANDLE f = nullptr;
        if (SFileOpenFileEx(mpq, internalFile.c_str(), 0, &f) && f)
        {
            DWORD hi = 0, sz = SFileGetFileSize(f, &hi);
            std::printf("  size=%lu", static_cast<unsigned long>(sz));
            SFileCloseFile(f);
        }
    }
    std::printf("\n");
    SFileCloseArchive(mpq);
    return has ? 0 : 2;
}

// Round-trip CharTitles.dbc through EditableDbc. Usage: --title-roundtrip <clientRoot>
int TitleRoundtrip(const std::string& clientRoot)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot)) { std::fprintf(stderr, "open failed\n"); return 1; }
    if (!RoundtripOne(cd, "DBFilesClient\\CharTitles.dbc", we::CharTitlesSchema(),
                      {we::title::NameMaleLoc0, we::title::NameFemaleLoc0}))
        return 1;
    std::printf("ROUNDTRIP OK (CharTitles.dbc)\n");
    return 0;
}

// Round-trip Spell.dbc (234 fields, 49839 rows) — validates the SpellSchema field count and
// the four LangString blocks. Usage: --spell-roundtrip <clientRoot>
int SpellRoundtrip(const std::string& clientRoot)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot)) { std::fprintf(stderr, "open failed\n"); return 1; }
    if (!RoundtripOne(cd, "DBFilesClient\\Spell.dbc", we::SpellSchema(),
                      {we::spell::SpellName, we::spell::Rank, we::spell::Description, we::spell::ToolTip}))
        return 1;
    std::printf("ROUNDTRIP OK (Spell.dbc)\n");
    return 0;
}

// Round-trip all 10 spell-reference DBCs — validates each SpellTablesModule schema's field
// count. Usage: --spelltables-roundtrip <clientRoot>
int SpellTablesRoundtrip(const std::string& clientRoot)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot)) { std::fprintf(stderr, "open failed\n"); return 1; }
    bool ok = true;
    for (const we::DbcTableDef& d : we::SpellTableDefs())
        ok &= RoundtripOne(cd, d.archivePath, *d.schema, {});
    if (!ok)
        return 1;
    std::printf("ROUNDTRIP OK (all spell-reference DBCs)\n");
    return 0;
}

// Round-trip all 10 item DBCs — validates every ItemTablesModule schema's field count.
// Usage: --itemtables-roundtrip <clientRoot>
int ItemTablesRoundtrip(const std::string& clientRoot)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot)) { std::fprintf(stderr, "open failed\n"); return 1; }
    bool ok = true;
    for (const we::DbcTableDef& d : we::ItemTableDefs())
        ok &= RoundtripOne(cd, d.archivePath, *d.schema, {});
    if (!ok)
        return 1;
    std::printf("ROUNDTRIP OK (all item-reference DBCs)\n");
    return 0;
}

// Round-trip Talent.dbc + TalentTab.dbc — validates the TalentSchema field counts.
// Usage: --talent-roundtrip <clientRoot>
int TalentRoundtrip(const std::string& clientRoot)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot)) { std::fprintf(stderr, "open failed\n"); return 1; }
    bool ok = RoundtripOne(cd, "DBFilesClient\\Talent.dbc", we::TalentSchema(), {});
    ok &= RoundtripOne(cd, "DBFilesClient\\TalentTab.dbc", we::TalentTabSchema(),
                       {we::talenttab::Name});
    if (!ok)
        return 1;
    std::printf("ROUNDTRIP OK (Talent.dbc + TalentTab.dbc)\n");
    return 0;
}

// Round-trip SkillLineAbility.dbc + SkillLine.dbc. Usage: --skill-roundtrip <clientRoot>
int SkillRoundtrip(const std::string& clientRoot)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot)) { std::fprintf(stderr, "open failed\n"); return 1; }
    bool ok = RoundtripOne(cd, "DBFilesClient\\SkillLineAbility.dbc", we::SkillLineAbilitySchema(), {});
    ok &= RoundtripOne(cd, "DBFilesClient\\SkillLine.dbc", we::SkillLineSchema(),
                       {we::skillline::Name, we::skillline::Description});
    if (!ok)
        return 1;
    std::printf("ROUNDTRIP OK (SkillLineAbility.dbc + SkillLine.dbc)\n");
    return 0;
}

// Round-trip all zone/world DBCs — validates every ZoneTablesModule schema. Usage:
// --zonetables-roundtrip <clientRoot>
int ZoneTablesRoundtrip(const std::string& clientRoot)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot)) { std::fprintf(stderr, "open failed\n"); return 1; }
    bool ok = true;
    for (const we::DbcTableDef& d : we::ZoneTableDefs())
        ok &= RoundtripOne(cd, d.archivePath, *d.schema, {});
    if (!ok)
        return 1;
    std::printf("ROUNDTRIP OK (all zone/world DBCs)\n");
    return 0;
}

// Round-trip all misc grab-bag DBCs — validates every MiscDbcModule schema (EditableDbc::Load
// refuses a wrong field count). Usage: --miscdbc-roundtrip <clientRoot>
int MiscDbcRoundtrip(const std::string& clientRoot)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot)) { std::fprintf(stderr, "open failed\n"); return 1; }
    bool ok = true;
    for (const we::DbcTableDef& d : we::MiscDbcTableDefs())
        ok &= RoundtripOne(cd, d.archivePath, *d.schema, {});
    if (!ok)
        return 1;
    std::printf("ROUNDTRIP OK (all misc DBCs)\n");
    return 0;
}

// Round-trip the reference DBCs — validates every RefDbcModule schema (field count + string
// positions; EditableDbc::Load refuses a wrong field count). Usage: --refdbc-roundtrip <clientRoot>
int RefDbcRoundtrip(const std::string& clientRoot)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot)) { std::fprintf(stderr, "open failed\n"); return 1; }
    bool ok = true;
    for (const we::DbcTableDef& d : we::RefDbcTableDefs())
        ok &= RoundtripOne(cd, d.archivePath, *d.schema, {});
    if (!ok)
        return 1;
    std::printf("ROUNDTRIP OK (all reference DBCs)\n");
    return 0;
}

// Headless check of the generic DB editor's runtime-schema logic (no live DB): SQL-type mapping +
// single-PK vs composite pathing from fake introspected columns. Usage: --runtime-schema-test
int RuntimeSchemaTest()
{
    const char* types[] = {"int(10) unsigned", "int(11)", "smallint(5) unsigned", "smallint(6)",
                           "tinyint(3) unsigned", "tinyint(4)", "float", "double", "varchar(255)",
                           "char(64)", "mediumtext", "longtext", "bigint(20) unsigned"};
    std::printf("Type map (0=U32 1=I32 2=U16 3=U8 4=Float 5=Text 6=Multiline):\n");
    for (const char* t : types)
        std::printf("  %-22s -> %d\n", t, static_cast<int>(we::SqlTypeToDbColType(t)));

    auto show = [](const char* name, std::vector<we::IntrospectedColumn> cols) {
        we::RuntimeTableSchema rt;
        rt.Build(name, cols);
        if (!rt.ok()) { std::printf("\n%s: BUILD FAILED\n", name); return; }
        if (!rt.composite())
            std::printf("\n%s: SINGLE-PK  pk=%s  cols=%zu  browser=%zu\n", name, rt.single().pk,
                        rt.single().cols.size(), rt.single().browserCols.size());
        else
            std::printf("\n%s: COMPOSITE  keyCols=%zu  cols=%zu  browser=%zu\n", name,
                        rt.compositeSchema().keyCols.size(), rt.compositeSchema().cols.size(),
                        rt.compositeSchema().browserCols.size());
    };
    show("broadcast_text", {{"ID", "int(10) unsigned", true}, {"LanguageID", "int(10) unsigned", false},
                            {"Text", "longtext", false}});
    show("graveyard_zone", {{"ID", "int(10) unsigned", true}, {"GhostZone", "int(10) unsigned", true},
                            {"Faction", "smallint(5) unsigned", false}, {"Comment", "mediumtext", false}});
    show("event_scripts", {{"id", "int(10) unsigned", false}, {"delay", "int(10) unsigned", false},
                           {"command", "int(10) unsigned", false}});
    std::printf("\nRUNTIME SCHEMA TEST OK\n");
    return 0;
}

// Verify the edit-overlay round-trip: read Achievement.dbc (from MPQ), edit a title,
// write it as a loose file under <editRoot>, then confirm ClientData reads the edited copy
// back in preference to the MPQ (the "custom wins" overlay). Usage:
// --overlay-test <clientRoot> <editRoot>
int OverlayTest(const std::string& clientRoot, const std::string& editRoot)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot)) { std::fprintf(stderr, "open failed\n"); return 1; }

    const char* path = "DBFilesClient\\Achievement.dbc";
    we::EditableDbc ed;
    if (!ed.Load(cd.ReadFile(path), we::AchievementSchema()))
    {
        std::fprintf(stderr, "load failed\n");
        return 1;
    }
    std::string before = ed.GetStr(0, we::ach::TitleLoc0);
    const std::string edited = before + " [EDITED]";
    ed.SetStr(0, we::ach::TitleLoc0, edited);

    std::string err;
    if (!we::WriteLooseFile(editRoot, path, ed.Serialize(), err))
    {
        std::fprintf(stderr, "write failed: %s\n", err.c_str());
        return 1;
    }

    // Point the reader at the edit folder; the loose file must now shadow the MPQ.
    cd.SetEditOverlay(editRoot);
    we::EditableDbc reread;
    if (!reread.Load(cd.ReadFile(path), we::AchievementSchema()))
    {
        std::fprintf(stderr, "reread failed\n");
        return 1;
    }
    std::string after = reread.GetStr(0, we::ach::TitleLoc0);
    std::printf("before overlay: \"%s\"\nafter  overlay: \"%s\"\n", before.c_str(), after.c_str());
    if (after != edited)
    {
        std::fprintf(stderr, "FAIL: overlay did not win\n");
        return 1;
    }
    std::printf("OVERLAY OK (edited loose file shadows the MPQ copy)\n");
    return 0;
}

// Usage: --wmo-test <clientRoot> <wmoPath>
int WmoTest(const std::string& clientRoot, const std::string& wmoPath)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot))
    {
        std::fprintf(stderr, "open failed: %s\n", cd.SourceDescription().c_str());
        return 1;
    }
    we::DbcStore dbc;
    we::wmo::LiquidTypeTable liquids = dbc.LoadLiquidTypes(cd);
    we::wmo::WmoModel model;
    std::string err;
    if (!we::wmo::Load(cd, wmoPath, model, {}, &liquids, &err))
    {
        std::fprintf(stderr, "WMO load FAILED (%s): %s\n", wmoPath.c_str(), err.c_str());
        return 1;
    }
    std::printf("WMO OK  %s\n", model.name.c_str());
    std::printf("  groups=%d materials=%d vertices=%zu indices=%zu submeshes=%zu\n",
                model.groupCount, model.materialCount, model.vertices.size(),
                model.indices.size(), model.submeshes.size());
    std::printf("  doodadSets=%zu doodadDefs=%d liquidTiles=%d textures=%zu\n",
                model.doodadSets.size(), model.doodadDefCount, model.liquidTileCount,
                model.texturePaths.size());
    std::printf("  bounds center=(%.1f,%.1f,%.1f) radius=%.1f\n", model.boundsCenter.x,
                model.boundsCenter.y, model.boundsCenter.z, model.boundsRadius);
    for (size_t i = 0; i < model.texturePaths.size() && i < 8; ++i)
        std::printf("  tex[%zu] '%s'\n", i, model.texturePaths[i].c_str());
    for (size_t i = 0; i < model.doodadSets.size(); ++i)
        std::printf("  set[%zu] '%s' first=%u count=%u\n", i, model.doodadSets[i].name.c_str(),
                    model.doodadSets[i].first, model.doodadSets[i].count);
    std::printf("WMOTEST OK\n");
    return 0;
}

// Usage: --adt-test <clientRoot> <adtPath>
int AdtTest(const std::string& clientRoot, const std::string& adtPath)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot))
    {
        std::fprintf(stderr, "open failed: %s\n", cd.SourceDescription().c_str());
        return 1;
    }
    we::DbcStore dbc;
    we::adt::LiquidTypeTable liquids = dbc.LoadLiquidTypes(cd);
    we::adt::AdtTile tile;
    std::string err;
    if (!we::adt::Load(cd, adtPath, tile, {}, &liquids, &err))
    {
        std::fprintf(stderr, "ADT load FAILED (%s): %s\n", adtPath.c_str(), err.c_str());
        return 1;
    }
    std::printf("ADT OK  %s\n", tile.name.c_str());
    std::printf("  chunks=%d rendered=%d vertices=%zu indices=%zu submeshes=%zu\n",
                tile.chunkCount, tile.renderedChunks, tile.vertices.size(),
                tile.indices.size(), tile.submeshes.size());
    std::printf("  textures=%d doodadDefs=%d wmoDefs=%d liquidTiles=%d mh2o=%d mclqChunks=%d noLayerChunks=%d\n",
                tile.textureCount, tile.doodadDefCount, tile.wmoDefCount,
                tile.liquidTileCount, tile.hasMh2o ? 1 : 0, tile.mclqChunks, tile.chunksNoLayer);
    std::printf("  worldOffset=(%.1f,%.1f) bounds center=(%.1f,%.1f,%.1f) radius=%.1f\n",
                tile.worldOffset.x, tile.worldOffset.y, tile.boundsCenter.x,
                tile.boundsCenter.y, tile.boundsCenter.z, tile.boundsRadius);
    int m2p = 0, wmop = 0;
    for (const auto& p : tile.placements) (p.isWmo ? wmop : m2p)++;
    std::printf("  placements=%zu (%d M2, %d WMO)\n", tile.placements.size(), m2p, wmop);
    // Raw top-level chunk list (diagnostic): magic + size, MCNK collapsed to a count.
    {
        std::vector<uint8_t> bytes = cd.ReadFile(adtPath);
        we::ByteReader rr(bytes);
        we::ChunkIter it(rr);
        we::Chunk c;
        int mcnk = 0;
        std::string line = "  chunks:";
        while (it.Next(c))
        {
            if (c.Is("MCNK")) { ++mcnk; continue; }
            char m[5] = {c.magic[3], c.magic[2], c.magic[1], c.magic[0], 0};
            line += " " + std::string(m) + "(" + std::to_string(c.size) + ")";
        }
        line += " MCNKx" + std::to_string(mcnk);
        std::printf("%s\n", line.c_str());
    }
    std::printf("ADTTEST OK\n");
    return 0;
}

// Usage: --adt-patch-test <clientRoot> <adtPath>
// Validates AdtWriter::PlacementToRaw (the inverse of the loader's PlacementMatrix) against every
// real MDDF doodad record in the tile, plus PatchTilePlacement's byte round-trip. Writes nothing.
int AdtPatchTest(const std::string& clientRoot, const std::string& adtPath)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot))
    {
        std::fprintf(stderr, "open failed: %s\n", cd.SourceDescription().c_str());
        return 1;
    }
    we::DbcStore dbc;
    we::adt::LiquidTypeTable liquids = dbc.LoadLiquidTypes(cd);
    we::adt::AdtTile tile;
    std::string err;
    if (!we::adt::Load(cd, adtPath, tile, {}, &liquids, &err))
    {
        std::fprintf(stderr, "ADT load FAILED: %s\n", err.c_str());
        return 1;
    }

    // Ground-truth MDDF records straight from the file, keyed by uniqueId.
    std::vector<uint8_t> bytes = cd.ReadFile(adtPath);
    std::unordered_map<uint32_t, we::adt::AdtDoodadDef> fileRecs;
    {
        we::ByteReader r(bytes);
        we::ChunkIter it(r);
        we::Chunk c;
        while (it.Next(c))
        {
            if (!c.Is("MDDF")) continue;
            const size_t n = c.size / sizeof(we::adt::AdtDoodadDef);
            for (size_t i = 0; i < n; ++i)
            {
                we::adt::AdtDoodadDef d;
                std::memcpy(&d, bytes.data() + c.offset + i * sizeof(d), sizeof(d));
                fileRecs[d.uniqueId] = d;
            }
        }
    }

    // Replicate the loader's rotation build so rotations are compared as MATRICES (Euler triples can
    // differ yet describe the same rotation).
    auto rotMat = [](const float rot[3]) {
        glm::mat4 M(1.0f);
        M = glm::rotate(M, glm::radians(rot[1] + 180.0f), glm::vec3(0, 0, 1));
        M = glm::rotate(M, glm::radians(rot[0]), glm::vec3(0, 1, 0));
        M = glm::rotate(M, glm::radians(rot[2]), glm::vec3(1, 0, 0));
        return glm::mat3(M);
    };

    const glm::vec3 origin(tile.worldOffset.x, tile.worldOffset.y, 0.0f);
    int checked = 0, failed = 0;
    for (const auto& p : tile.placements)
    {
        if (p.isWmo) continue;
        auto fit = fileRecs.find(p.uniqueId);
        if (fit == fileRecs.end()) continue;
        const we::adt::AdtDoodadDef& d = fit->second;
        const glm::mat4 T = glm::make_mat4(p.transform);
        const we::adt::RawPlacement raw = we::adt::PlacementToRaw(T, origin);
        ++checked;

        bool ok = true;
        for (int k = 0; k < 3; ++k)
            if (std::fabs(raw.position[k] - d.position[k]) > 0.5f) ok = false;
        if (std::fabs(raw.scale * 1024.0f - static_cast<float>(d.scale)) > 2.0f) ok = false;
        const glm::mat3 Rf = rotMat(d.rotation), Rm = rotMat(raw.rotation);
        float maxd = 0.0f;
        for (int a = 0; a < 3; ++a) for (int b = 0; b < 3; ++b) maxd = std::max(maxd, std::fabs(Rf[a][b] - Rm[a][b]));
        if (maxd > 1e-3f) ok = false;

        std::vector<uint8_t> copy = bytes;   // byte round-trip: the record must be found + patched
        if (!we::adt::PatchTilePlacement(copy, p.uniqueId, false, raw)) ok = false;

        if (!ok && failed < 5)
            std::printf("  FAIL uid=%u  posdiff=(%.2f,%.2f,%.2f) rotdiff=%.4f scale(file=%u mine=%.3f)\n",
                        p.uniqueId, raw.position[0] - d.position[0], raw.position[1] - d.position[1],
                        raw.position[2] - d.position[2], maxd, d.scale, raw.scale);
        if (!ok) ++failed;
    }
    std::printf("checked %d doodad placements, %d failed\n", checked, failed);
    std::printf(failed == 0 ? "ADTPATCHTEST OK\n" : "ADTPATCHTEST FAIL\n");
    return failed == 0 ? 0 : 1;
}

// Usage: --adt-add-test <clientRoot> <adtPath>
// Adds a doodad + a WMO to a tile in memory via AdtWriter::AddPlacement, then re-parses to assert:
// the record counts grew by 1, the new records are present, and — the load-bearing check for the
// client — every MCIN offset + the MHDR offset table point at the actual re-emitted chunks.
int AdtAddTest(const std::string& clientRoot, const std::string& adtPath)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot))
    {
        std::fprintf(stderr, "open failed: %s\n", cd.SourceDescription().c_str());
        return 1;
    }

    auto countRecs = [](const std::vector<uint8_t>& bytes, const char* magic, size_t recSize) {
        we::ByteReader r(bytes);
        we::ChunkIter it(r);
        we::Chunk c;
        size_t n = 0;
        while (it.Next(c))
            if (c.Is(magic)) n += c.size / recSize;
        return n;
    };
    auto hasUid = [](const std::vector<uint8_t>& bytes, const char* magic, size_t recSize, uint32_t uid) {
        we::ByteReader r(bytes);
        we::ChunkIter it(r);
        we::Chunk c;
        while (it.Next(c))
            if (c.Is(magic))
                for (size_t i = 0; i < c.size / recSize; ++i)
                {
                    uint32_t u = 0;
                    std::memcpy(&u, bytes.data() + c.offset + i * recSize + 4, 4);
                    if (u == uid) return true;
                }
        return false;
    };

    int fails = 0;
    std::vector<uint8_t> bytes = cd.ReadFile(adtPath);
    if (bytes.empty()) { std::fprintf(stderr, "read failed\n"); return 1; }

    const size_t mddf0 = countRecs(bytes, "MDDF", sizeof(we::adt::AdtDoodadDef));
    const size_t modf0 = countRecs(bytes, "MODF", sizeof(we::adt::AdtMapObjDef));

    we::adt::RawPlacement d{};
    d.position[0] = 100.0f; d.position[1] = 200.0f; d.position[2] = 300.0f;
    d.rotation[1] = 45.0f;  d.scale = 1.5f;
    if (!we::adt::AddPlacement(bytes, 0xF0000001u, false, "World\\Test\\Doodad.m2", d))
    { std::printf("  FAIL: AddPlacement(MDDF) returned false\n"); ++fails; }

    we::adt::RawPlacement wpl{};
    wpl.position[0] = 400.0f; wpl.position[1] = 500.0f; wpl.position[2] = 600.0f;
    if (!we::adt::AddPlacement(bytes, 0xF0000002u, true, "World\\Test\\Building.wmo", wpl))
    { std::printf("  FAIL: AddPlacement(MODF) returned false\n"); ++fails; }

    if (countRecs(bytes, "MDDF", sizeof(we::adt::AdtDoodadDef)) != mddf0 + 1) { std::printf("  FAIL: MDDF count not +1\n"); ++fails; }
    if (countRecs(bytes, "MODF", sizeof(we::adt::AdtMapObjDef)) != modf0 + 1) { std::printf("  FAIL: MODF count not +1\n"); ++fails; }
    if (!hasUid(bytes, "MDDF", sizeof(we::adt::AdtDoodadDef), 0xF0000001u)) { std::printf("  FAIL: new doodad uid missing\n"); ++fails; }
    if (!hasUid(bytes, "MODF", sizeof(we::adt::AdtMapObjDef), 0xF0000002u)) { std::printf("  FAIL: new WMO uid missing\n"); ++fails; }

    // MCIN + MHDR consistency: every MCIN offset must equal the actual MCNK magic offset; MHDR's
    // offset table (relative to MHDR data) must resolve to the right chunk magics.
    {
        we::ByteReader r(bytes);
        we::ChunkIter it(r);
        we::Chunk c;
        std::vector<size_t> mcnkMagic;
        size_t mcinOff = 0, mcinSize = 0, mhdrPayload = 0, mddfMagic = 0, modfMagic = 0;
        while (it.Next(c))
        {
            if (c.Is("MCNK")) mcnkMagic.push_back(c.offset - 8);
            else if (c.Is("MCIN")) { mcinOff = c.offset; mcinSize = c.size; }
            else if (c.Is("MHDR")) mhdrPayload = c.offset;
            else if (c.Is("MDDF")) mddfMagic = c.offset - 8;
            else if (c.Is("MODF")) modfMagic = c.offset - 8;
        }
        if (mcnkMagic.size() != 256) { std::printf("  FAIL: expected 256 MCNK, got %zu\n", mcnkMagic.size()); ++fails; }
        if (mcinSize >= 256 * 16)
        {
            int bad = 0;
            for (size_t i = 0; i < mcnkMagic.size() && i < 256; ++i)
            {
                uint32_t off = 0;
                std::memcpy(&off, bytes.data() + mcinOff + i * 16, 4);
                if (off != mcnkMagic[i]) ++bad;
            }
            if (bad) { std::printf("  FAIL: %d MCIN offsets don't match MCNK positions\n", bad); ++fails; }
        }
        else { std::printf("  FAIL: MCIN missing/short\n"); ++fails; }
        if (mhdrPayload && bytes.size() >= mhdrPayload + sizeof(we::adt::AdtHeader))
        {
            we::adt::AdtHeader h;
            std::memcpy(&h, bytes.data() + mhdrPayload, sizeof(h));
            if (mhdrPayload + h.mddf != mddfMagic) { std::printf("  FAIL: MHDR.mddf mismatch\n"); ++fails; }
            if (mhdrPayload + h.modf != modfMagic) { std::printf("  FAIL: MHDR.modf mismatch\n"); ++fails; }
        }
    }

    // Remove the two we just added; counts must return to baseline and MCIN stay consistent.
    if (!we::adt::RemovePlacement(bytes, 0xF0000001u, false)) { std::printf("  FAIL: RemovePlacement(MDDF) false\n"); ++fails; }
    if (!we::adt::RemovePlacement(bytes, 0xF0000002u, true)) { std::printf("  FAIL: RemovePlacement(MODF) false\n"); ++fails; }
    if (countRecs(bytes, "MDDF", sizeof(we::adt::AdtDoodadDef)) != mddf0) { std::printf("  FAIL: MDDF count not back to baseline\n"); ++fails; }
    if (countRecs(bytes, "MODF", sizeof(we::adt::AdtMapObjDef)) != modf0) { std::printf("  FAIL: MODF count not back to baseline\n"); ++fails; }
    if (hasUid(bytes, "MDDF", sizeof(we::adt::AdtDoodadDef), 0xF0000001u)) { std::printf("  FAIL: removed doodad still present\n"); ++fails; }
    {
        we::ByteReader r(bytes);
        we::ChunkIter it(r);
        we::Chunk c;
        std::vector<size_t> mcnkMagic;
        size_t mcinOff = 0, mcinSize = 0;
        while (it.Next(c))
        {
            if (c.Is("MCNK")) mcnkMagic.push_back(c.offset - 8);
            else if (c.Is("MCIN")) { mcinOff = c.offset; mcinSize = c.size; }
        }
        int bad = 0;
        if (mcinSize >= 256 * 16)
            for (size_t i = 0; i < mcnkMagic.size() && i < 256; ++i)
            {
                uint32_t off = 0;
                std::memcpy(&off, bytes.data() + mcinOff + i * 16, 4);
                if (off != mcnkMagic[i]) ++bad;
            }
        if (bad) { std::printf("  FAIL: %d MCIN offsets wrong after remove\n", bad); ++fails; }
    }

    std::printf(fails == 0 ? "ADTADDTEST OK\n" : "ADTADDTEST FAIL (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}

// Usage: --adt-shot <clientRoot> <adtPath> <outBmp>  (terrain via the dedicated terrain pipeline)
int AdtShot(const std::string& clientRoot, const std::string& adtPath, const std::string& outBmp,
            int radius)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot))
    {
        std::fprintf(stderr, "open failed: %s\n", cd.SourceDescription().c_str());
        return 1;
    }
    we::DbcStore dbc;
    we::adt::LiquidTypeTable liquids = dbc.LoadLiquidTypes(cd);

    // Parse "World\Maps\<dir>\<dir>_<x>_<y>.adt" into (dir, x, y) for LoadNeighborhood.
    std::string norm = adtPath;
    for (char& ch : norm) if (ch == '/') ch = '\\';
    size_t lastSlash = norm.find_last_of('\\');
    std::string file = lastSlash == std::string::npos ? norm : norm.substr(lastSlash + 1);
    std::string before = lastSlash == std::string::npos ? std::string() : norm.substr(0, lastSlash);
    size_t s2 = before.find_last_of('\\');
    std::string dir = s2 == std::string::npos ? before : before.substr(s2 + 1);
    std::string stem = (file.size() > 4) ? file.substr(0, file.size() - 4) : file;   // drop .adt
    size_t u2 = stem.find_last_of('_');
    size_t u1 = (u2 != std::string::npos && u2 > 0) ? stem.find_last_of('_', u2 - 1) : std::string::npos;
    int tx = 0, ty = 0;
    if (u1 != std::string::npos && u2 != std::string::npos)
    {
        tx = std::atoi(stem.substr(u1 + 1, u2 - u1 - 1).c_str());
        ty = std::atoi(stem.substr(u2 + 1).c_str());
    }

    we::adt::AdtTile tile;
    std::string err;
    if (!we::adt::LoadNeighborhood(cd, dir, tx, ty, radius, tile, {}, &liquids, &err))
    {
        std::fprintf(stderr, "ADT load FAILED (%s): %s\n", adtPath.c_str(), err.c_str());
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    we::Window window;
    if (!window.Create(600, 600, "adt-shot", /*visible=*/false)) { std::fprintf(stderr, "window failed\n"); return 1; }
    auto renderer = std::make_unique<we::VulkanRenderer>();
    if (!renderer->Init(window, /*headless=*/true)) { std::fprintf(stderr, "renderer init failed\n"); return 1; }

    we::TerrainUpload up = we::adt::BuildTerrainUpload(cd, tile);
    we::TerrainHandle th = renderer->CreateTerrain(up);
    if (!th) { std::fprintf(stderr, "CreateTerrain failed\n"); return 1; }

    we::ModelUpload liqUp = we::adt::BuildLiquidUpload(cd, tile);
    we::ModelHandle liqH = liqUp.vertices.empty() ? 0 : renderer->CreateModel(liqUp);

    const int W = 600, H = 600;
    const glm::vec3 c = tile.boundsCenter;
    const float r = tile.boundsRadius;
    const glm::vec3 eye = c + glm::normalize(glm::vec3(1.0f, -1.4f, 0.85f)) * (r * 2.2f);
    glm::mat4 view = glm::lookAt(eye, c, glm::vec3(0.0f, 0.0f, 1.0f));
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), float(W) / H, r * 0.02f, r * 6.0f);
    proj[1][1] *= -1.0f;
    const glm::vec3 camRight(view[0][0], view[1][0], view[2][0]);
    const glm::vec3 camUp(view[0][1], view[1][1], view[2][1]);

    // Load unique M2/WMO models referenced by placements; instance them into the scene.
    struct UM { bool isWmo = false; we::ModelHandle h = 0;
                we::m2::M2Model m2; we::m2::M2Animator anim; bool emit = false;
                std::vector<glm::mat4> local; std::vector<we::SubmeshAnim> sub;
                glm::vec3 bcenter{0.0f}; float bradius = 1.0f; };
    struct PI { int u = -1; glm::mat4 xform{1.0f}; glm::vec3 origin{0.0f};
                std::unique_ptr<we::m2::M2EffectSystem> fx; std::vector<glm::mat4> pal; };
    std::vector<std::unique_ptr<UM>> uniq;
    std::vector<PI> insts;
    std::unordered_map<std::string, int> cache;
    auto lower = [](std::string s) { for (char& ch : s) ch = (char)std::tolower((unsigned char)ch); return s; };

    for (const we::adt::AdtPlacement& p : tile.placements)
    {
        std::string key = lower(p.path);
        int mi;
        auto cit = cache.find(key);
        if (cit == cache.end())
        {
            auto um = std::make_unique<UM>();
            um->isWmo = p.isWmo;
            if (p.isWmo)
            {
                we::wmo::WmoModel wm;
                if (!we::wmo::Load(cd, p.path, wm, {}, &liquids, nullptr)) { cache[key] = -1; continue; }
                we::ModelUpload wup = we::wmo::BuildUpload(cd, wm);
                for (we::ModelVertexGpu& v : wup.vertices) { v.boneIndices[0] = 0; v.boneWeights[0] = 1.0f; }
                wup.boneCount = 1;   // shell rides bone 0 = the placement matrix
                um->h = renderer->CreateModel(wup);
                if (!um->h) { cache[key] = -1; continue; }
                um->bcenter = wm.boundsCenter; um->bradius = wm.boundsRadius;
            }
            else
            {
                if (!we::m2::Load(cd, p.path, um->m2, nullptr)) { cache[key] = -1; continue; }
                um->anim.SetModel(&um->m2, &cd, p.path);
                um->emit = !um->m2.particleEmitters.empty() || !um->m2.ribbonEmitters.empty();
                we::ModelUpload mup = we::m2::BuildUpload(cd, um->m2);
                um->h = renderer->CreateModel(mup);
                if (!um->h) { cache[key] = -1; continue; }
                um->anim.Evaluate(0, 0.0f, view, um->local);   // bind / stand pose
                um->sub.resize(um->m2.batches.size());
                for (size_t bi = 0; bi < um->m2.batches.size(); ++bi)
                {
                    const we::m2::RenderBatch& b = um->m2.batches[bi];
                    glm::mat4 tmx = um->anim.TextureMatrix(b.textureTransformIndex, 0, 0.0f);
                    std::memcpy(um->sub[bi].texMatrix, &tmx[0][0], sizeof(um->sub[bi].texMatrix));
                    glm::vec4 col = um->anim.BatchColor(b.colorIndex, b.textureWeightIndex, 0, 0.0f);
                    um->sub[bi].color[0] = col.r; um->sub[bi].color[1] = col.g;
                    um->sub[bi].color[2] = col.b; um->sub[bi].color[3] = col.a;
                }
                um->bcenter = um->m2.boundsCenter; um->bradius = um->m2.boundsRadius;
            }
            mi = (int)uniq.size(); cache[key] = mi; uniq.push_back(std::move(um));
        }
        else { mi = cit->second; if (mi < 0) continue; }

        PI pi; pi.u = mi;
        std::memcpy(&pi.xform[0][0], p.transform, sizeof(p.transform));
        pi.origin = glm::vec3(p.origin[0], p.origin[1], p.origin[2]);
        if (uniq[mi]->emit) { pi.fx = std::make_unique<we::m2::M2EffectSystem>(); pi.fx->SetModel(&uniq[mi]->m2, &uniq[mi]->anim); }
        insts.push_back(std::move(pi));
    }

    for (PI& pi : insts)   // fold placement transform into each palette
    {
        UM* um = uniq[pi.u].get();
        if (um->isWmo) { pi.pal.assign(1, pi.xform); }
        else
        {
            pi.pal.resize(um->local.size());
            for (size_t j = 0; j < um->local.size(); ++j) pi.pal[j] = pi.xform * um->local[j];
        }
    }
    for (int f = 0; f < 30; ++f)   // warm up particle sims
        for (PI& pi : insts)
            if (pi.fx) pi.fx->Step(0, 0.0f, 33.0f, pi.pal, camRight, camUp, eye);

    const glm::mat4 vpCull = proj * view;
    int culled = 0;
    std::vector<we::SceneInstanceGpu> scene;
    scene.reserve(insts.size() + 1);
    if (liqH) { we::SceneInstanceGpu si{}; si.handle = liqH; scene.push_back(si); }   // liquid surface
    for (PI& pi : insts)
    {
        UM* um = uniq[pi.u].get();
        glm::vec3 cc = glm::vec3(pi.xform * glm::vec4(um->bcenter, 1.0f));
        float cr = um->bradius * glm::length(glm::vec3(pi.xform[0])) + 0.01f;
        if (!we::SphereInFrustum(vpCull, cc, cr)) { ++culled; continue; }
        we::SceneInstanceGpu si{};
        si.handle = um->h;
        si.boneMatrices = pi.pal.empty() ? nullptr : reinterpret_cast<const float*>(pi.pal.data());
        si.boneCount = (int)pi.pal.size();
        si.submeshAnims = um->sub.empty() ? nullptr : um->sub.data();
        si.submeshAnimCount = (int)um->sub.size();
        if (pi.fx)
        {
            const we::m2::EffectGeometry& g = pi.fx->Geometry();
            si.effectVerts = g.verts.data(); si.effectVertCount = (int)g.verts.size();
            si.effectDraws = g.draws.data(); si.effectDrawCount = (int)g.draws.size();
        }
        si.worldOrigin[0] = pi.origin.x; si.worldOrigin[1] = pi.origin.y; si.worldOrigin[2] = pi.origin.z;
        scene.push_back(si);
    }

    const float gc[3] = {c.x, c.y, c.z - r};
    renderer->SetGrid(true, gc, r * 2.2f, std::max(r * 2.2f / 20.0f, 1e-4f));
    renderer->RenderWorld(&th, 1, nullptr, 0, scene.data(), (int)scene.size(), &view[0][0], &proj[0][0], W, H);
    std::vector<uint8_t> rgba;
    int rw = 0, rh = 0;
    if (!renderer->CaptureModelTarget(rgba, rw, rh)) { std::fprintf(stderr, "capture failed\n"); return 1; }
    WriteBmp(rgba, rw, rh, outBmp);
    if (liqH) renderer->DestroyModel(liqH);
    renderer->DestroyTerrain(th);
    renderer->Shutdown();
    ImGui::DestroyContext();
    std::printf("ADTSHOT OK -> %s (%dx%d, %zu chunks, %zu placements / %zu models, %d culled, %zu liquidTiles)\n",
                outBmp.c_str(), rw, rh, up.submeshes.size(), insts.size(), uniq.size(), culled,
                (size_t)tile.liquidTileCount);
    return 0;
}

// Usage: --adt-region <clientRoot> <mapDir> <cx> <cy> <radius> <out.bmp>
// Streams a (2r+1)^2 tile region through the async streamer, drains to completion, and renders —
// verifies the worker pipeline, multi-terrain rendering, and uniqueId object dedup.
int AdtRegion(const std::string& clientRoot, const std::string& mapDir, int cx, int cy, int radius,
              const std::string& outBmp)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot)) { std::fprintf(stderr, "open failed: %s\n", cd.SourceDescription().c_str()); return 1; }
    we::DbcStore dbc;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    we::Window window;
    if (!window.Create(700, 700, "adt-region", false)) { std::fprintf(stderr, "window failed\n"); return 1; }
    auto renderer = std::make_unique<we::VulkanRenderer>();
    if (!renderer->Init(window, true)) { std::fprintf(stderr, "renderer init failed\n"); return 1; }

    we::AdtStreamer streamer;
    streamer.Init(&cd, &dbc, renderer.get(), 3);
    if (!streamer.OpenMap(mapDir)) { std::fprintf(stderr, "OpenMap failed: %s\n", mapDir.c_str()); return 1; }

    if (streamer.wmoOnly())
        std::printf("map is WMO-only (global WMO)\n");

    // Camera at tile (cx,cy) in the shared frame, then pump Update until the region is loaded.
    const glm::vec2 cw = {(31.5f - cy) * we::adt::kTileSize, (31.5f - cx) * we::adt::kTileSize};
    glm::vec3 camPos(cw.x - streamer.origin().x, cw.y - streamer.origin().y, 200.0f);

    int desired = 0;
    for (const auto& t : streamer.world().tiles)
        if (std::max(std::abs(t.first - cx), std::abs(t.second - cy)) <= radius) ++desired;

    we::adt::AdtLoadOptions opt;
    if (!streamer.wmoOnly())
        for (int iter = 0; iter < 4000; ++iter)
        {
            streamer.Update(camPos, radius, opt);
            if (streamer.loadedTiles() >= desired && streamer.pendingTiles() == 0)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }

    std::printf("region loaded=%d/%d tiles, objects=%d, models=%d, uniqueGroundTextures=%d\n",
                streamer.loadedTiles(), desired, streamer.objectCount(), streamer.modelCount(),
                streamer.terrainTextureCount());

    const int W = 700, H = 700;
    const glm::vec3 c = streamer.wmoOnly() ? streamer.startPos() : camPos;
    const float r = streamer.wmoOnly() ? streamer.startRadius() : (radius + 1) * we::adt::kTileSize;
    const glm::vec3 eye = c + glm::normalize(glm::vec3(0.4f, -0.7f, 0.9f)) * (r * 1.6f);
    glm::mat4 view = glm::lookAt(eye, c, glm::vec3(0.0f, 0.0f, 1.0f));
    glm::mat4 proj = glm::perspective(glm::radians(50.0f), float(W) / H, r * 0.01f, r * 8.0f);
    proj[1][1] *= -1.0f;

    std::vector<we::TerrainHandle> terrains;
    std::vector<we::InstancedGroup> groups;
    std::vector<we::SceneInstanceGpu> scene;
    streamer.BuildFrame(view, proj, eye, 16.0f, terrains, groups, scene);
    size_t instTotal = 0;
    for (const auto& g : groups) instTotal += g.instanceCount;
    std::printf("instanced: %zu groups covering %zu objects; %zu non-instanced (liquid + effects)\n",
                groups.size(), instTotal, scene.size());
    const float gc[3] = {c.x, c.y, c.z - r};
    renderer->SetGrid(true, gc, r * 2.0f, std::max(r * 2.0f / 20.0f, 1e-4f));
    renderer->RenderWorld(terrains.data(), (int)terrains.size(), groups.data(), (int)groups.size(),
                          scene.data(), (int)scene.size(), &view[0][0], &proj[0][0], W, H);
    std::vector<uint8_t> rgba;
    int rw = 0, rh = 0;
    if (!renderer->CaptureModelTarget(rgba, rw, rh)) { std::fprintf(stderr, "capture failed\n"); return 1; }
    WriteBmp(rgba, rw, rh, outBmp);
    const we::RenderStats& rstats = renderer->renderStats();
    std::printf("ADTREGION OK -> %s (%dx%d, %zu terrains, %zu instances)\n", outBmp.c_str(), rw, rh,
                terrains.size(), scene.size());
    std::printf("stats: drawCalls=%d gpuMs=%.2f (terrainTiles=%d terrainChunks=%d culledChunks=%d groups=%d instances=%d nonInst=%d)\n",
                rstats.drawCalls, rstats.gpuMs, rstats.terrainTiles, rstats.terrainChunks,
                rstats.terrainChunksCulled, rstats.instancedGroups, rstats.instances,
                rstats.nonInstanced);
    std::printf("wdl low-detail tiles loaded=%d\n", streamer.lowTileCount());
    streamer.Shutdown();
    renderer->Shutdown();
    ImGui::DestroyContext();
    return 0;
}

// Usage: --wmo-list <clientRoot> [max]
int WmoList(const std::string& clientRoot, int maxPrint)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot))
    {
        std::fprintf(stderr, "open failed: %s\n", cd.SourceDescription().c_str());
        return 1;
    }
    std::vector<std::string> all = cd.ListFiles(".wmo");
    int roots = 0;
    for (const std::string& p : all)
    {
        if (IsWmoGroupFile(p))
            continue;
        if (roots < maxPrint)
            std::printf("%s\n", p.c_str());
        ++roots;
    }
    std::printf("WMOLIST done: %zu entries, %d root WMOs\n", all.size(), roots);
    return 0;
}

// Usage: --wmo-shot <clientRoot> <wmoPath> <outBmp>
// Renders the WMO shell plus its doodads as live instances (bind pose; effects warmed up a
// few frames so torches/braziers show fire) through the multi-instance scene renderer.
int WmoShot(const std::string& clientRoot, const std::string& wmoPath, const std::string& outBmp)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot))
    {
        std::fprintf(stderr, "open failed: %s\n", cd.SourceDescription().c_str());
        return 1;
    }
    we::DbcStore dbc;
    we::wmo::LiquidTypeTable liquids = dbc.LoadLiquidTypes(cd);
    we::wmo::WmoModel model;
    std::string err;
    if (!we::wmo::Load(cd, wmoPath, model, {}, &liquids, &err))
    {
        std::fprintf(stderr, "WMO load FAILED (%s): %s\n", wmoPath.c_str(), err.c_str());
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    we::Window window;
    if (!window.Create(600, 600, "wmoshot", false)) { std::fprintf(stderr, "window failed\n"); return 1; }
    auto renderer = std::make_unique<we::VulkanRenderer>();
    if (!renderer->Init(window, true)) { std::fprintf(stderr, "renderer init failed\n"); return 1; }

    we::ModelUpload shellUp = we::wmo::BuildUpload(cd, model);
    we::ModelHandle shellH = renderer->CreateModel(shellUp);
    if (!shellH) { std::fprintf(stderr, "CreateModel failed\n"); return 1; }

    const int W = 600, H = 600;
    const glm::vec3 c = model.boundsCenter;
    const float r = model.boundsRadius;
    const glm::vec3 eye = c + glm::normalize(glm::vec3(1.0f, -1.4f, 0.55f)) * (r * 2.6f);
    glm::mat4 view = glm::lookAt(eye, c, glm::vec3(0.0f, 0.0f, 1.0f));
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), float(W) / H, r * 0.05f, r * 30.0f);
    proj[1][1] *= -1.0f;
    const glm::vec3 camRight(view[0][0], view[1][0], view[2][0]);
    const glm::vec3 camUp(view[0][1], view[1][1], view[2][1]);

    struct UD { we::ModelHandle h = 0; we::m2::M2Model model; we::m2::M2Animator anim;
                bool emit = false; std::vector<glm::mat4> local; std::vector<we::SubmeshAnim> sub; };
    struct DI { int u = -1; glm::mat4 xform{1.0f}; glm::vec3 origin{0.0f};
                std::unique_ptr<we::m2::M2EffectSystem> fx; std::vector<glm::mat4> pal; };
    std::vector<std::unique_ptr<UD>> uniq;
    std::vector<DI> insts;
    std::unordered_map<std::string, int> cache;
    auto lower = [](std::string s) { for (char& ch : s) ch = (char)std::tolower((unsigned char)ch); return s; };

    for (const we::wmo::WmoDoodadInstance& inst : model.doodadInstances)
    {
        std::string key = lower(inst.m2Path);
        int mi;
        auto it = cache.find(key);
        if (it == cache.end())
        {
            auto ud = std::make_unique<UD>();
            if (!we::m2::Load(cd, inst.m2Path, ud->model, nullptr)) { cache[key] = -1; continue; }
            ud->anim.SetModel(&ud->model, &cd, inst.m2Path);
            ud->emit = !ud->model.particleEmitters.empty() || !ud->model.ribbonEmitters.empty();
            we::ModelUpload up = we::m2::BuildUpload(cd, ud->model);
            ud->h = renderer->CreateModel(up);
            if (!ud->h) { cache[key] = -1; continue; }
            ud->anim.Evaluate(0, 0.0f, view, ud->local);   // bind / stand pose
            ud->sub.resize(ud->model.batches.size());
            for (size_t bi = 0; bi < ud->model.batches.size(); ++bi)
            {
                const we::m2::RenderBatch& b = ud->model.batches[bi];
                glm::mat4 tmx = ud->anim.TextureMatrix(b.textureTransformIndex, 0, 0.0f);
                std::memcpy(ud->sub[bi].texMatrix, &tmx[0][0], sizeof(ud->sub[bi].texMatrix));
                glm::vec4 col = ud->anim.BatchColor(b.colorIndex, b.textureWeightIndex, 0, 0.0f);
                ud->sub[bi].color[0] = col.r; ud->sub[bi].color[1] = col.g;
                ud->sub[bi].color[2] = col.b; ud->sub[bi].color[3] = col.a;
            }
            mi = (int)uniq.size(); cache[key] = mi; uniq.push_back(std::move(ud));
        }
        else { mi = it->second; if (mi < 0) continue; }

        DI di; di.u = mi;
        std::memcpy(&di.xform[0][0], inst.transform, sizeof(inst.transform));
        di.origin = glm::vec3(inst.origin[0], inst.origin[1], inst.origin[2]);
        if (uniq[mi]->emit) { di.fx = std::make_unique<we::m2::M2EffectSystem>(); di.fx->SetModel(&uniq[mi]->model, &uniq[mi]->anim); }
        insts.push_back(std::move(di));
    }

    for (DI& di : insts)   // fold instance transform into each palette
    {
        UD* ud = uniq[di.u].get();
        di.pal.resize(ud->local.size());
        for (size_t j = 0; j < ud->local.size(); ++j) di.pal[j] = di.xform * ud->local[j];
    }
    for (int f = 0; f < 45; ++f)   // warm up particle sims so fire/glow is visible
        for (DI& di : insts)
            if (di.fx) di.fx->Step(0, 0.0f, 33.0f, di.pal, camRight, camUp, eye);

    // Frustum-cull (sanity: framed on the whole WMO, nearly all instances pass).
    const glm::mat4 vpCull = proj * view;
    int culled = 0;

    std::vector<we::SceneInstanceGpu> scene;
    scene.reserve(insts.size() + 1);
    { we::SceneInstanceGpu shell{}; shell.handle = shellH; scene.push_back(shell); }
    for (DI& di : insts)
    {
        UD* ud = uniq[di.u].get();
        glm::vec3 cc = glm::vec3(di.xform * glm::vec4(ud->model.boundsCenter, 1.0f));
        float cr = ud->model.boundsRadius * glm::length(glm::vec3(di.xform[0])) + 0.01f;
        if (!we::SphereInFrustum(vpCull, cc, cr)) { ++culled; continue; }
        we::SceneInstanceGpu si{};
        si.handle = ud->h;
        si.boneMatrices = di.pal.empty() ? nullptr : reinterpret_cast<const float*>(di.pal.data());
        si.boneCount = (int)di.pal.size();
        si.submeshAnims = ud->sub.empty() ? nullptr : ud->sub.data();
        si.submeshAnimCount = (int)ud->sub.size();
        if (di.fx)
        {
            const we::m2::EffectGeometry& g = di.fx->Geometry();
            si.effectVerts = g.verts.data(); si.effectVertCount = (int)g.verts.size();
            si.effectDraws = g.draws.data(); si.effectDrawCount = (int)g.draws.size();
        }
        si.worldOrigin[0] = di.origin.x; si.worldOrigin[1] = di.origin.y; si.worldOrigin[2] = di.origin.z;
        scene.push_back(si);
    }

    const float gc[3] = {c.x, c.y, c.z - r};
    renderer->SetGrid(true, gc, r * 2.5f, std::max(r * 2.5f / 20.0f, 1e-4f));
    renderer->RenderScene(scene.data(), (int)scene.size(), &view[0][0], &proj[0][0], W, H);
    std::vector<uint8_t> rgba; int rw = 0, rh = 0;
    if (!renderer->CaptureModelTarget(rgba, rw, rh)) { std::fprintf(stderr, "capture failed\n"); return 1; }
    WriteBmp(rgba, rw, rh, outBmp);

    insts.clear();   // release effect systems before destroying models
    for (auto& ud : uniq) renderer->DestroyModel(ud->h);
    renderer->DestroyModel(shellH);
    renderer->Shutdown();
    ImGui::DestroyContext();
    std::printf("WMOSHOT OK -> %s (%dx%d, %zu doodad instances / %zu models, %d culled)\n", outBmp.c_str(),
                rw, rh, model.doodadInstances.size(), uniq.size(), culled);
    return 0;
}

// Usage: --m2-shot <clientRoot> <m2Path> <outBmp>
int M2Shot(const std::string& clientRoot, const std::string& m2Path, const std::string& outBmp)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot))
    {
        std::fprintf(stderr, "open failed: %s\n", cd.SourceDescription().c_str());
        return 1;
    }
    we::m2::M2Model model;
    std::string err;
    if (!we::m2::Load(cd, m2Path, model, &err))
    {
        std::fprintf(stderr, "M2 load FAILED (%s): %s\n", m2Path.c_str(), err.c_str());
        return 1;
    }
    // Per-batch material animation at bind pose (UV transform + color/transparency), so blend and
    // transparency (e.g. the faded-translucent hourglass glass) render as they do in the viewer.
    we::m2::M2Animator anim;
    anim.SetModel(&model, &cd, m2Path);
    std::vector<we::SubmeshAnim> subs(model.batches.size());
    for (size_t bi = 0; bi < model.batches.size(); ++bi)
    {
        const we::m2::RenderBatch& b = model.batches[bi];
        glm::mat4 tmx = anim.TextureMatrix(b.textureTransformIndex, 0, 0.0f);
        std::memcpy(subs[bi].texMatrix, &tmx[0][0], sizeof(subs[bi].texMatrix));
        glm::vec4 col = anim.BatchColor(b.colorIndex, b.textureWeightIndex, 0, 0.0f);
        subs[bi].color[0] = col.r; subs[bi].color[1] = col.g; subs[bi].color[2] = col.b; subs[bi].color[3] = col.a;
    }
    return RenderM2ToBmp(cd, model, outBmp, nullptr, 0, nullptr, subs.data(), (int)subs.size());
}

// Dev tool: load an M2, resolve its RUNTIME textures (creature/item/character skins) via
// ModelDresser as the interactive viewer does when browsing by path, then render. Verifies the
// flat-white fix for models whose textures come from a display record.
// Usage: --m2-dress <clientRoot> <m2Path> <outBmp> [skinColor face hair hairColor facial [equipIds...]]
int M2Dress(const std::string& clientRoot, const std::string& m2Path, const std::string& outBmp,
            const we::CharCustomize& custom = {}, const std::vector<uint32_t>& equipIds = {})
{
    we::ClientData cd;
    if (!cd.Open(clientRoot))
    {
        std::fprintf(stderr, "open failed: %s\n", cd.SourceDescription().c_str());
        return 1;
    }
    we::m2::M2Model model;
    std::string err;
    if (!we::m2::Load(cd, m2Path, model, &err))
    {
        std::fprintf(stderr, "M2 load FAILED (%s): %s\n", m2Path.c_str(), err.c_str());
        return 1;
    }
    we::DbcStore store;
    we::ModelDresser dresser(cd, store);
    int filled = dresser.ResolveDefaultTextures(m2Path, model);
    std::printf("dressed %s: class=%d, %d runtime texture slot(s) resolved\n", m2Path.c_str(),
                (int)we::ClassifyModel(m2Path), filled);
    if (we::ClassifyModel(m2Path) == we::ModelClass::Creature)
    {
        auto sk = dresser.CreatureSkinsFor(m2Path);
        std::printf("  [debug] creature skin triples found: %zu\n", sk.size());
        for (size_t j = 0; j < sk.size() && j < 4; ++j)
            std::printf("    skins[%zu] = '%s' | '%s' | '%s'\n", j, sk[j][0].c_str(), sk[j][1].c_str(),
                        sk[j][2].c_str());
    }
    for (size_t i = 0; i < model.texturePaths.size(); ++i)
        std::printf("  tex[%zu] type=%u '%s'\n", i, i < model.textureTypes.size() ? model.textureTypes[i] : 0,
                    model.texturePaths[i].c_str());

    we::m2::M2Animator anim;
    anim.SetModel(&model, &cd, m2Path);
    std::vector<we::SubmeshAnim> subs(model.batches.size());
    for (size_t bi = 0; bi < model.batches.size(); ++bi)
    {
        const we::m2::RenderBatch& b = model.batches[bi];
        glm::mat4 tmx = anim.TextureMatrix(b.textureTransformIndex, 0, 0.0f);
        std::memcpy(subs[bi].texMatrix, &tmx[0][0], sizeof(subs[bi].texMatrix));
        glm::vec4 col = anim.BatchColor(b.colorIndex, b.textureWeightIndex, 0, 0.0f);
        subs[bi].color[0] = col.r; subs[bi].color[1] = col.g; subs[bi].color[2] = col.b; subs[bi].color[3] = col.a;
    }
    // Character: composite the body texture (base skin + face + underwear) and select hair/facial
    // geosets for the DEFAULT customization; otherwise fall back to the unresolved-texture default.
    std::vector<uint8_t> geoVis;
    we::BlpImage bodyComposite;
    int bodySlot = -1;
    we::CharacterOptions co = dresser.CharacterInfoFor(m2Path);
    if (co.valid)
    {
        std::vector<we::EquippedItem> equipped;
        for (uint32_t id : equipIds)
        {
            we::DbcStore::ItemDisplay it;
            if (!dresser.ItemDisplayById(id, it)) continue;
            // Harness slot inference (the viewer uses explicit slots): held model -> main hand,
            // else by which armour region the item fills.
            we::EquipSlot slot = we::EquipSlot::Chest;
            if (!it.modelName[0].empty()) slot = we::EquipSlot::MainHand;
            else if (!it.texture[2].empty()) slot = we::EquipSlot::Hands;
            else if (!it.texture[7].empty()) slot = we::EquipSlot::Feet;
            else if (it.geosetGroup[2] > 0) slot = we::EquipSlot::Legs;
            equipped.push_back({slot, it});
        }
        bodyComposite = dresser.ComposeCharacterBody(co.race, co.sex, custom, equipped);
        std::string hairTex = dresser.CharacterHairTexture(co.race, co.sex, custom);
        for (size_t i = 0; i < model.texturePaths.size(); ++i)
        {
            uint32_t t = i < model.textureTypes.size() ? model.textureTypes[i] : 0;
            if (t == 6 && !hairTex.empty()) model.texturePaths[i] = hairTex;
            if (t == 1) bodySlot = (int)i;
        }
        dresser.CharacterGeosets(co.race, co.sex, custom, model, geoVis, equipped);
        std::printf("  [char] race=%u sex=%u skinColors=%u faces=%u hairs=%u hairColors=%u facial=%u\n",
                    co.race, co.sex, co.skinColors, co.faceVariations, co.hairVariations, co.hairColors,
                    co.facialVariations);
    }
    else
    {
        geoVis.assign(model.batches.size(), 1);
        for (size_t bi = 0; bi < model.batches.size(); ++bi)
        {
            int ti = model.batches[bi].textureIndex;
            uint32_t type = (ti >= 0 && ti < (int)model.textureTypes.size()) ? model.textureTypes[ti] : 0;
            const bool unresolved = ti < 0 || ti >= (int)model.texturePaths.size() || model.texturePaths[ti].empty();
            if (type != 0 && unresolved) geoVis[bi] = 0;
        }
    }

    we::ModelUpload up = we::m2::BuildUpload(cd, model);
    if (bodySlot >= 0 && bodyComposite.valid() && bodySlot < (int)up.textures.size())
    {
        up.textures[bodySlot].rgba = std::move(bodyComposite.rgba);
        up.textures[bodySlot].w = bodyComposite.width;
        up.textures[bodySlot].h = bodyComposite.height;
    }
    return RenderUploadToBmp(up, model.boundsCenter, model.boundsRadius, outBmp, "M2SHOT OK",
                             nullptr, 0, nullptr, subs.data(), (int)subs.size(), geoVis.data(), (int)geoVis.size());
}

// Resolve a creature display id -> model + skin variations, apply the skins, and render.
// Validates the CreatureDisplayInfo/CreatureModelData path (textures creature bodies).
// Usage: --m2-shot-display <clientRoot> <displayId> <outBmp>
int M2ShotDisplay(const std::string& clientRoot, uint32_t displayId, const std::string& outBmp)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot))
    {
        std::fprintf(stderr, "open failed\n");
        return 1;
    }
    we::DbcStore store;
    auto displays = store.LoadCreatureDisplays(cd);
    auto modelPaths = store.LoadCreatureModelPaths(cd);
    std::printf("displays=%zu creatureModels=%zu\n", displays.size(), modelPaths.size());

    auto dit = displays.find(displayId);
    if (dit == displays.end())
    {
        std::fprintf(stderr, "display %u not found; sample ids:", displayId);
        int n = 0;
        for (auto& kv : displays) { std::fprintf(stderr, " %u", kv.first); if (++n >= 10) break; }
        std::fprintf(stderr, "\n");
        return 1;
    }
    auto mit = modelPaths.find(dit->second.modelId);
    if (mit == modelPaths.end())
    {
        std::fprintf(stderr, "model %u for display %u not found\n", dit->second.modelId, displayId);
        return 1;
    }
    const std::string m2Path = mit->second;
    std::printf("display %u -> model %u '%s' skins ['%s','%s','%s']\n", displayId, dit->second.modelId,
                m2Path.c_str(), dit->second.skins[0].c_str(), dit->second.skins[1].c_str(),
                dit->second.skins[2].c_str());

    we::m2::M2Model model;
    std::string err;
    if (!we::m2::Load(cd, m2Path, model, &err))
    {
        std::fprintf(stderr, "M2 load FAILED (%s): %s\n", m2Path.c_str(), err.c_str());
        return 1;
    }
    // Apply skins to runtime slots (types 11/12/13), combined with the model directory.
    std::string dir;
    { size_t s = m2Path.find_last_of("\\/"); dir = (s == std::string::npos) ? "" : m2Path.substr(0, s); }
    for (size_t i = 0; i < model.texturePaths.size(); ++i)
    {
        uint32_t t = i < model.textureTypes.size() ? model.textureTypes[i] : 0;
        if (t >= 11 && t <= 13 && !dit->second.skins[t - 11].empty())
            model.texturePaths[i] = dir + "\\" + dit->second.skins[t - 11] + ".blp";
    }
    return RenderM2ToBmp(cd, model, outBmp);
}

// List the distinct texture-variation skins available for a model, mirroring the model
// viewer's BuildSkinOptions. Fast (no rendering).
// Usage: --m2-skins <clientRoot> <m2Path>
int M2Skins(const std::string& clientRoot, const std::string& m2Path)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot)) { std::fprintf(stderr, "open failed\n"); return 1; }
    we::DbcStore store;
    auto modelPaths = store.LoadCreatureModelPaths(cd);
    auto displays = store.LoadCreatureDisplays(cd);

    std::string low = m2Path;
    for (char& c : low) c = (char)std::tolower((unsigned char)c);
    uint32_t modelId = 0;
    for (const auto& kv : modelPaths)
    {
        std::string p = kv.second;
        for (char& c : p) c = (char)std::tolower((unsigned char)c);
        if (p == low) { modelId = kv.first; break; }
    }
    std::printf("model '%s' -> modelId %u\n", m2Path.c_str(), modelId);

    std::unordered_map<std::string, uint32_t> seen;
    for (const auto& kv : displays)
    {
        if (kv.second.modelId != modelId)
            continue;
        const auto& s = kv.second.skins;
        std::string key = s[0] + "|" + s[1] + "|" + s[2];
        if (seen.emplace(key, kv.first).second)
            std::printf("  skin: '%s' | '%s' | '%s'  (display %u)\n", s[0].c_str(), s[1].c_str(),
                        s[2].c_str(), kv.first);
    }
    std::printf("distinct skins=%zu\n", seen.size());
    return 0;
}

// Step the particle/ribbon simulation for `steps` frames (~30fps) then render one frame
// with the effects, to validate the effect system headlessly. Optional display id textures
// the body. Usage: --m2-fx <clientRoot> <m2Path> <animIndex> <steps> <outBmp> [displayId]
int M2Fx(const std::string& clientRoot, const std::string& m2Path, int animIndex, int steps,
         const std::string& outBmp, uint32_t displayId)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot)) { std::fprintf(stderr, "open failed\n"); return 1; }
    we::m2::M2Model model;
    std::string err;
    if (!we::m2::Load(cd, m2Path, model, &err)) { std::fprintf(stderr, "load FAILED: %s\n", err.c_str()); return 1; }

    // Optional creature skin so the body is textured.
    if (displayId)
    {
        auto displays = we::DbcStore{}.LoadCreatureDisplays(cd);
        auto it = displays.find(displayId);
        if (it != displays.end())
        {
            std::string dir; size_t sl = m2Path.find_last_of("\\/"); dir = (sl==std::string::npos)?"":m2Path.substr(0,sl);
            for (size_t i = 0; i < model.texturePaths.size(); ++i)
            {
                uint32_t t = i < model.textureTypes.size() ? model.textureTypes[i] : 0;
                if (t >= 11 && t <= 13 && !it->second.skins[t-11].empty())
                    model.texturePaths[i] = dir + "\\" + it->second.skins[t-11] + ".blp";
            }
        }
    }

    // Optional ParticleColor.dbc tint from the display's ParticleColorID.
    we::m2::M2EffectSystem::ParticleColorRamps pcRamps;
    if (displayId)
    {
        we::DbcStore store;
        auto displays = store.LoadCreatureDisplays(cd);
        auto it = displays.find(displayId);
        if (it != displays.end() && it->second.particleColorId)
        {
            auto colors = store.LoadParticleColors(cd);
            auto cit = colors.find(it->second.particleColorId);
            if (cit != colors.end())
            {
                auto toRgb = [](uint32_t v) {
                    return glm::vec3(((v >> 16) & 0xFF), ((v >> 8) & 0xFF), (v & 0xFF)) * (1.0f / 255.0f);
                };
                for (int k = 0; k < 3; ++k)
                    for (int p = 0; p < 3; ++p)
                        pcRamps.c[k][p] = toRgb(cit->second.bgra[k][p]);
                pcRamps.present = true;
                std::printf("particleColor %u applied\n", it->second.particleColorId);
            }
        }
    }

    we::m2::M2Animator anim;
    anim.SetModel(&model, &cd, m2Path);
    if (anim.SequenceCount() == 0) { std::fprintf(stderr, "no sequences\n"); return 1; }
    animIndex = std::max(0, std::min(animIndex, anim.SequenceCount() - 1));
    const uint32_t dur = anim.Duration(animIndex);

    // Camera matching RenderM2ToBmp.
    const glm::vec3 c = model.boundsCenter;
    const float r = model.boundsRadius;
    const glm::vec3 eye = c + glm::normalize(glm::vec3(1.0f, -1.4f, 0.55f)) * (r * 2.6f);
    const glm::mat4 view = glm::lookAt(eye, c, glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::vec3 camRight(view[0][0], view[1][0], view[2][0]);
    const glm::vec3 camUp(view[0][1], view[1][1], view[2][1]);

    we::m2::M2EffectSystem fx;
    fx.SetModel(&model, &anim);
    if (pcRamps.present) fx.SetParticleColorOverride(pcRamps);
    std::vector<glm::mat4> bones;
    const float dt = 33.0f;
    float t = 0.0f;
    for (int i = 0; i < steps; ++i)
    {
        t = dur > 0 ? std::fmod(i * dt, (float)dur) : 0.0f;
        anim.Evaluate(animIndex, t, view, bones);
        fx.Step(animIndex, t, dt, bones, camRight, camUp, eye);
    }
    std::printf("fx: live particles=%zu effect verts=%zu draws=%zu\n", fx.LiveParticleCount(),
                fx.Geometry().verts.size(), fx.Geometry().draws.size());

    we::EffectFrame ef;
    ef.verts = fx.Geometry().verts.data();
    ef.vertCount = (int)fx.Geometry().verts.size();
    ef.draws = fx.Geometry().draws.data();
    ef.drawCount = (int)fx.Geometry().draws.size();

    const float* bp = bones.empty() ? nullptr : reinterpret_cast<const float*>(bones.data());
    return RenderM2ToBmp(cd, model, outBmp, bp, (int)bones.size(), &ef);
}

// Render one animation frame to a BMP through the real skinning pipeline, to validate
// the animator + GPU skinning headlessly.
// Usage: --m2-anim <clientRoot> <m2Path> <animIndex> <timeMs> <outBmp>
int M2Anim(const std::string& clientRoot, const std::string& m2Path, int animIndex, float timeMs,
           const std::string& outBmp)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot))
    {
        std::fprintf(stderr, "open failed\n");
        return 1;
    }
    we::m2::M2Model model;
    std::string err;
    if (!we::m2::Load(cd, m2Path, model, &err))
    {
        std::fprintf(stderr, "M2 load FAILED: %s\n", err.c_str());
        return 1;
    }
    we::m2::M2Animator anim;
    anim.SetModel(&model, &cd, m2Path);
    const int seqCount = anim.SequenceCount();
    std::printf("sequences=%d bones=%zu\n", seqCount, model.bones.size());
    if (seqCount == 0)
    {
        std::fprintf(stderr, "no sequences\n");
        return 1;
    }
    // Diagnostic: for each sequence, show inline flag + whether its external .anim file
    // exists under our constructed name (validates the .anim filename format).
    {
        std::string base = m2Path.substr(0, m2Path.find_last_of('.'));
        int ext = 0, extFound = 0;
        for (int i = 0; i < seqCount; ++i)
        {
            const we::m2::M2Sequence& s = model.sequences[i];
            bool inl = (s.flags & 0x20) != 0;
            if (inl) continue;
            ++ext;
            char suf[24];
            std::snprintf(suf, sizeof(suf), "%04u-%02u.anim", s.animationId, s.subAnimationId);
            if (cd.HasFile(base + suf)) ++extFound;
        }
        std::printf("external sequences=%d, of which .anim found=%d\n", ext, extFound);
    }

    animIndex = std::max(0, std::min(animIndex, seqCount - 1));
    const uint32_t dur = anim.Duration(animIndex);
    const bool inl = (model.sequences[animIndex].flags & 0x20) != 0;
    std::printf("anim %d id=%u var=%u dur=%ums inline=%d\n", animIndex,
                model.sequences[animIndex].animationId, model.sequences[animIndex].subAnimationId,
                dur, inl ? 1 : 0);

    // Match RenderM2ToBmp's camera so billboard bones orient correctly.
    const glm::vec3 c = model.boundsCenter;
    const float r = model.boundsRadius;
    const glm::vec3 eye = c + glm::normalize(glm::vec3(1.0f, -1.4f, 0.55f)) * (r * 2.6f);
    const glm::mat4 view = glm::lookAt(eye, c, glm::vec3(0.0f, 0.0f, 1.0f));

    std::vector<glm::mat4> palette;
    anim.Evaluate(animIndex, timeMs, view, palette);
    const float* bones = palette.empty() ? nullptr : reinterpret_cast<const float*>(palette.data());
    return RenderM2ToBmp(cd, model, outBmp, bones, (int)palette.size());
}
} // namespace

int main(int argc, char** argv)
{
    bool selftest = false;
    bool demo = false;
    int demoTab = -1;
    bool emitSql = false;
    std::string emitPath = "build/emit_test.sql";
    std::string shotPath;
    int shotTab = 7;   // POI tab by default
    std::string clientPath;
    std::string editorId;   // --editor <id> selects the startup editor module
    std::string loadProject;   // --load-project <folder>: auto-open a project at startup

    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--selftest") == 0)
            selftest = true;
        else if (std::strcmp(argv[i], "--demo") == 0)
            demo = true;
        else if (std::strcmp(argv[i], "--tab") == 0)
        {
            demo = true;
            if (i + 1 < argc)
                demoTab = std::atoi(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--emit-sql") == 0)
        {
            emitSql = true;
            if (i + 1 < argc && argv[i + 1][0] != '-')
                emitPath = argv[++i];
        }
        else if (std::strcmp(argv[i], "--dbc-test") == 0 && i + 1 < argc)
        {
            return DbcTest(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--dump-blp") == 0 && i + 3 < argc)
        {
            return DumpBlp(argv[i + 1], argv[i + 2], argv[i + 3]);
        }
        else if (std::strcmp(argv[i], "--m2-test") == 0 && i + 2 < argc)
        {
            return M2Test(argv[i + 1], argv[i + 2]);
        }
        else if (std::strcmp(argv[i], "--m2-geoscan") == 0 && i + 1 < argc)
        {
            int lim = (i + 2 < argc) ? std::atoi(argv[i + 2]) : 20;
            return M2GeoScan(argv[i + 1], lim);
        }
        else if (std::strcmp(argv[i], "--wmo-test") == 0 && i + 2 < argc)
        {
            return WmoTest(argv[i + 1], argv[i + 2]);
        }
        else if (std::strcmp(argv[i], "--dbc-liquid") == 0 && i + 1 < argc)
        {
            return DbcLiquid(argv[i + 1]);
        }
        else if (std::strcmp(argv[i], "--anim-names") == 0 && i + 1 < argc)
        {
            return AnimNames(argv[i + 1]);
        }
        else if (std::strcmp(argv[i], "--dbc-map") == 0 && i + 1 < argc)
        {
            return DbcMap(argv[i + 1]);
        }
        else if (std::strcmp(argv[i], "--dbc-dump") == 0 && i + 2 < argc)
        {
            int rows = (i + 3 < argc && argv[i + 3][0] != '-') ? std::atoi(argv[i + 3]) : 3;
            return DbcDump(argv[i + 1], argv[i + 2], rows);
        }
        else if (std::strcmp(argv[i], "--ach-roundtrip") == 0 && i + 1 < argc)
        {
            return AchRoundtrip(argv[i + 1]);
        }
        else if (std::strcmp(argv[i], "--title-roundtrip") == 0 && i + 1 < argc)
        {
            return TitleRoundtrip(argv[i + 1]);
        }
        else if (std::strcmp(argv[i], "--spell-roundtrip") == 0 && i + 1 < argc)
        {
            return SpellRoundtrip(argv[i + 1]);
        }
        else if (std::strcmp(argv[i], "--spelltables-roundtrip") == 0 && i + 1 < argc)
        {
            return SpellTablesRoundtrip(argv[i + 1]);
        }
        else if (std::strcmp(argv[i], "--itemtables-roundtrip") == 0 && i + 1 < argc)
        {
            return ItemTablesRoundtrip(argv[i + 1]);
        }
        else if (std::strcmp(argv[i], "--talent-roundtrip") == 0 && i + 1 < argc)
        {
            return TalentRoundtrip(argv[i + 1]);
        }
        else if (std::strcmp(argv[i], "--skill-roundtrip") == 0 && i + 1 < argc)
        {
            return SkillRoundtrip(argv[i + 1]);
        }
        else if (std::strcmp(argv[i], "--miscdbc-roundtrip") == 0 && i + 1 < argc)
        {
            return MiscDbcRoundtrip(argv[i + 1]);
        }
        else if (std::strcmp(argv[i], "--refdbc-roundtrip") == 0 && i + 1 < argc)
        {
            return RefDbcRoundtrip(argv[i + 1]);
        }
        else if (std::strcmp(argv[i], "--runtime-schema-test") == 0)
        {
            return RuntimeSchemaTest();
        }
        else if (std::strcmp(argv[i], "--dbd-check") == 0 && i + 1 < argc)
        {
            return DbdCheck(argv[i + 1], i + 2 < argc ? argv[i + 2] : "");
        }
        else if (std::strcmp(argv[i], "--has-file") == 0 && i + 2 < argc)
        {
            return HasFileProbe(argv[i + 1], argv[i + 2]);
        }
        else if (std::strcmp(argv[i], "--mpq-probe") == 0 && i + 2 < argc)
        {
            return MpqProbe(argv[i + 1], argv[i + 2]);
        }
        else if (std::strcmp(argv[i], "--dbc-registry-test") == 0 && i + 1 < argc)
        {
            return DbcRegistryTest(argv[i + 1]);
        }
        else if (std::strcmp(argv[i], "--zonetables-roundtrip") == 0 && i + 1 < argc)
        {
            return ZoneTablesRoundtrip(argv[i + 1]);
        }
        else if (std::strcmp(argv[i], "--emit-spell-sql") == 0 && i + 1 < argc)
        {
            std::string p = (i + 2 < argc && argv[i + 2][0] != '-') ? argv[i + 2] : "build\\spell_emit.sql";
            return EmitSpellSql(argv[i + 1], p);
        }
        else if (std::strcmp(argv[i], "--overlay-test") == 0 && i + 2 < argc)
        {
            return OverlayTest(argv[i + 1], argv[i + 2]);
        }
        else if (std::strcmp(argv[i], "--emit-ach-sql") == 0)
        {
            std::string p = (i + 1 < argc && argv[i + 1][0] != '-') ? argv[++i] : "build\\ach_emit.sql";
            return EmitAchSql(p);
        }
        else if (std::strcmp(argv[i], "--emit-broadcast-sql") == 0)
        {
            std::string p = (i + 1 < argc && argv[i + 1][0] != '-') ? argv[++i] : "build\\bt_emit.sql";
            return EmitBroadcastSql(p);
        }
        else if (std::strcmp(argv[i], "--emit-gameevent-sql") == 0)
        {
            std::string p = (i + 1 < argc && argv[i + 1][0] != '-') ? argv[++i] : "build\\gameevent_emit";
            return EmitGameEventSql(p);
        }
        else if (std::strcmp(argv[i], "--emit-conditions-sql") == 0)
        {
            std::string p = (i + 1 < argc && argv[i + 1][0] != '-') ? argv[++i] : "build\\conditions_emit.sql";
            return EmitConditionsSql(p);
        }
        else if (std::strcmp(argv[i], "--emit-loot-sql") == 0)
        {
            std::string p = (i + 1 < argc && argv[i + 1][0] != '-') ? argv[++i] : "build\\loot_emit";
            return EmitLootSql(p);
        }
        else if (std::strcmp(argv[i], "--emit-creaturetext-sql") == 0)
        {
            std::string p = (i + 1 < argc && argv[i + 1][0] != '-') ? argv[++i] : "build\\creaturetext_emit";
            return EmitCreatureTextSql(p);
        }
        else if (std::strcmp(argv[i], "--emit-gossip-sql") == 0)
        {
            std::string p = (i + 1 < argc && argv[i + 1][0] != '-') ? argv[++i] : "build\\gossip_emit";
            return EmitGossipSql(p);
        }
        else if (std::strcmp(argv[i], "--emit-pagepoi-sql") == 0)
        {
            std::string p = (i + 1 < argc && argv[i + 1][0] != '-') ? argv[++i] : "build\\pagepoi_emit";
            return EmitPagePoiSql(p);
        }
        else if (std::strcmp(argv[i], "--emit-npctext-sql") == 0)
        {
            std::string p = (i + 1 < argc && argv[i + 1][0] != '-') ? argv[++i] : "build\\npctext_emit.sql";
            return EmitNpcTextSql(p);
        }
        else if (std::strcmp(argv[i], "--emit-smartai-sql") == 0)
        {
            std::string p = (i + 1 < argc && argv[i + 1][0] != '-') ? argv[++i] : "build\\smartai_emit.sql";
            return EmitSmartAiSql(p);
        }
        else if (std::strcmp(argv[i], "--emit-worlddb-composite-sql") == 0)
        {
            std::string p = (i + 1 < argc && argv[i + 1][0] != '-') ? argv[++i] : "build\\worlddb_composite_emit";
            return EmitWorldDbCompositeSql(p);
        }
        else if (std::strcmp(argv[i], "--emit-worlddb-sql") == 0)
        {
            std::string p = (i + 1 < argc && argv[i + 1][0] != '-') ? argv[++i] : "build\\worlddb_emit";
            return EmitWorldDbSql(p);
        }
        else if (std::strcmp(argv[i], "--wmo-list") == 0 && i + 1 < argc)
        {
            int max = (i + 2 < argc) ? std::atoi(argv[i + 2]) : 40;
            return WmoList(argv[i + 1], max);
        }
        else if (std::strcmp(argv[i], "--wmo-shot") == 0 && i + 3 < argc)
        {
            return WmoShot(argv[i + 1], argv[i + 2], argv[i + 3]);
        }
        else if (std::strcmp(argv[i], "--adt-test") == 0 && i + 2 < argc)
        {
            return AdtTest(argv[i + 1], argv[i + 2]);
        }
        else if (std::strcmp(argv[i], "--adt-patch-test") == 0 && i + 2 < argc)
        {
            return AdtPatchTest(argv[i + 1], argv[i + 2]);
        }
        else if (std::strcmp(argv[i], "--adt-add-test") == 0 && i + 2 < argc)
        {
            return AdtAddTest(argv[i + 1], argv[i + 2]);
        }
        else if (std::strcmp(argv[i], "--adt-shot") == 0 && i + 3 < argc)
        {
            int radius = (i + 4 < argc) ? std::atoi(argv[i + 4]) : 0;
            return AdtShot(argv[i + 1], argv[i + 2], argv[i + 3], radius);
        }
        else if (std::strcmp(argv[i], "--adt-region") == 0 && i + 6 < argc)
        {
            return AdtRegion(argv[i + 1], argv[i + 2], std::atoi(argv[i + 3]), std::atoi(argv[i + 4]),
                             std::atoi(argv[i + 5]), argv[i + 6]);
        }
        else if (std::strcmp(argv[i], "--m2-shot") == 0 && i + 3 < argc)
        {
            return M2Shot(argv[i + 1], argv[i + 2], argv[i + 3]);
        }
        else if (std::strcmp(argv[i], "--m2-dress") == 0 && i + 3 < argc)
        {
            we::CharCustomize cc;
            if (i + 4 < argc) cc.skinColor = (uint32_t)std::atoi(argv[i + 4]);
            if (i + 5 < argc) cc.faceVariation = (uint32_t)std::atoi(argv[i + 5]);
            if (i + 6 < argc) cc.hairVariation = (uint32_t)std::atoi(argv[i + 6]);
            if (i + 7 < argc) cc.hairColor = (uint32_t)std::atoi(argv[i + 7]);
            if (i + 8 < argc) cc.facialVariation = (uint32_t)std::atoi(argv[i + 8]);
            std::vector<uint32_t> equip;
            for (int j = i + 9; j < argc; ++j) equip.push_back((uint32_t)std::atoi(argv[j]));
            return M2Dress(argv[i + 1], argv[i + 2], argv[i + 3], cc, equip);
        }
        else if (std::strcmp(argv[i], "--m2-shot-display") == 0 && i + 3 < argc)
        {
            return M2ShotDisplay(argv[i + 1], (uint32_t)std::atoi(argv[i + 2]), argv[i + 3]);
        }
        else if (std::strcmp(argv[i], "--m2-skins") == 0 && i + 2 < argc)
        {
            return M2Skins(argv[i + 1], argv[i + 2]);
        }
        else if (std::strcmp(argv[i], "--m2-anim") == 0 && i + 5 < argc)
        {
            return M2Anim(argv[i + 1], argv[i + 2], std::atoi(argv[i + 3]), (float)std::atof(argv[i + 4]),
                          argv[i + 5]);
        }
        else if (std::strcmp(argv[i], "--m2-fx") == 0 && i + 5 < argc)
        {
            uint32_t did = (i + 6 < argc && argv[i + 6][0] != '-') ? (uint32_t)std::atoi(argv[i + 6]) : 0;
            return M2Fx(argv[i + 1], argv[i + 2], std::atoi(argv[i + 3]), std::atoi(argv[i + 4]),
                        argv[i + 5], did);
        }
        else if (std::strcmp(argv[i], "--shot") == 0 && i + 1 < argc)
        {
            shotPath = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-')
                shotTab = std::atoi(argv[++i]);
            demo = true;
        }
        else if (std::strcmp(argv[i], "--client") == 0 && i + 1 < argc)
        {
            clientPath = argv[++i];
        }
        else if (std::strcmp(argv[i], "--editor") == 0 && i + 1 < argc)
        {
            editorId = argv[++i];
        }
        else if (std::strcmp(argv[i], "--load-project") == 0 && i + 1 < argc)
        {
            loadProject = argv[++i];
        }
    }

    if (emitSql)
        return EmitSql(emitPath);

    we::App app;
    if (demoTab >= 0)
        app.SetDemoTab(demoTab);
    if (!shotPath.empty())
        app.SetScreenshot(shotPath, shotTab);
    if (!clientPath.empty())
        app.SetForcedClientPath(clientPath);
    if (!editorId.empty())
        app.SetEditor(editorId);
    if (!loadProject.empty())
        app.SetStartupProject(loadProject);
    return app.Run(selftest, demo);
}
