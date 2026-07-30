#include "editors/quest/QuestRepository.h"

#include <cctype>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "db/IDatabase.h"
#include "db/ResultSet.h"
#include "data/SqlBuild.h"

namespace qe
{
// Schema-adaptive SQL helpers (Row, ValueList, SplitCols, ExistingCols,
// FilteredInsert, FilteredUpsert, FmtFloat) now live in data/SqlBuild.h and are
// shared across repositories.
using namespace sql;

namespace
{
// True when every character of s is a decimal digit (and s is non-empty).
bool IsAllDigits(const std::string& s)
{
    if (s.empty())
        return false;
    for (char ch : s)
        if (ch < '0' || ch > '9')
            return false;
    return true;
}

// --- Column lists (DDL order) --------------------------------------------
// quest_template: 105 columns, exact order as world_database.sql @2757.
constexpr const char* kQuestTemplateCols =
    "ID, QuestType, QuestLevel, MinLevel, QuestSortID, QuestInfoID, SuggestedGroupNum, "
    "RequiredFactionId1, RequiredFactionId2, RequiredFactionValue1, RequiredFactionValue2, "
    "RewardNextQuest, RewardXPDifficulty, RewardMoney, RewardBonusMoney, RewardDisplaySpell, "
    "RewardSpell, RewardHonor, RewardKillHonor, StartItem, Flags, RequiredPlayerKills, "
    "RewardItem1, RewardAmount1, RewardItem2, RewardAmount2, RewardItem3, RewardAmount3, "
    "RewardItem4, RewardAmount4, ItemDrop1, ItemDropQuantity1, ItemDrop2, ItemDropQuantity2, "
    "ItemDrop3, ItemDropQuantity3, ItemDrop4, ItemDropQuantity4, "
    "RewardChoiceItemID1, RewardChoiceItemQuantity1, RewardChoiceItemID2, RewardChoiceItemQuantity2, "
    "RewardChoiceItemID3, RewardChoiceItemQuantity3, RewardChoiceItemID4, RewardChoiceItemQuantity4, "
    "RewardChoiceItemID5, RewardChoiceItemQuantity5, RewardChoiceItemID6, RewardChoiceItemQuantity6, "
    "POIContinent, POIx, POIy, POIPriority, RewardTitle, RewardTalents, RewardArenaPoints, "
    "RewardFactionID1, RewardFactionValue1, RewardFactionOverride1, "
    "RewardFactionID2, RewardFactionValue2, RewardFactionOverride2, "
    "RewardFactionID3, RewardFactionValue3, RewardFactionOverride3, "
    "RewardFactionID4, RewardFactionValue4, RewardFactionOverride4, "
    "RewardFactionID5, RewardFactionValue5, RewardFactionOverride5, "
    "TimeAllowed, AllowableRaces, LogTitle, LogDescription, QuestDescription, AreaDescription, "
    "QuestCompletionLog, RequiredNpcOrGo1, RequiredNpcOrGo2, RequiredNpcOrGo3, RequiredNpcOrGo4, "
    "RequiredNpcOrGoCount1, RequiredNpcOrGoCount2, RequiredNpcOrGoCount3, RequiredNpcOrGoCount4, "
    "RequiredItemId1, RequiredItemId2, RequiredItemId3, RequiredItemId4, RequiredItemId5, RequiredItemId6, "
    "RequiredItemCount1, RequiredItemCount2, RequiredItemCount3, RequiredItemCount4, RequiredItemCount5, "
    // Both names for the renamed column; FilteredInsert emits whichever the DB has.
    "RequiredItemCount6, RewardFactionFlags, Unknown0, "
    "ObjectiveText1, ObjectiveText2, ObjectiveText3, ObjectiveText4, "
    "VerifiedBuild";

constexpr const char* kAddonCols =
    "ID, MaxLevel, AllowableClasses, SourceSpellID, PrevQuestID, NextQuestID, ExclusiveGroup, "
    "BreadcrumbForQuestId, RewardMailTemplateID, RewardMailDelay, RequiredSkillID, RequiredSkillPoints, "
    "RequiredMinRepFaction, RequiredMaxRepFaction, RequiredMinRepValue, RequiredMaxRepValue, "
    "ProvidedItemCount, SpecialFlags";

constexpr const char* kOfferRewardCols =
    "ID, Emote1, Emote2, Emote3, Emote4, EmoteDelay1, EmoteDelay2, EmoteDelay3, EmoteDelay4, "
    "RewardText, VerifiedBuild";

constexpr const char* kRequestItemsCols =
    "ID, EmoteOnComplete, EmoteOnIncomplete, CompletionText, VerifiedBuild";

constexpr const char* kDetailsCols =
    "ID, Emote1, Emote2, Emote3, Emote4, EmoteDelay1, EmoteDelay2, EmoteDelay3, EmoteDelay4, "
    "VerifiedBuild";

constexpr const char* kMailSenderCols = "QuestId, RewardMailSenderEntry";

constexpr const char* kGreetingCols =
    "ID, Type, GreetEmoteType, GreetEmoteDelay, Greeting, VerifiedBuild";

constexpr const char* kPoiCols =
    "QuestID, id, ObjectiveIndex, MapID, WorldMapAreaId, Floor, Priority, Flags, VerifiedBuild";

constexpr const char* kPoiPointCols = "QuestID, Idx1, Idx2, X, Y, VerifiedBuild";

// Locale tables. TrinityCore (2026-07-02) renamed the quest_template_locale text
// columns to mirror quest_template (Title->LogTitle, Objectives->LogDescription,
// Details->QuestDescription, EndText->AreaDescription, CompletedText->QuestCompletionLog).
// Each renamed column appears here under BOTH names with the SAME value token, so
// FilteredInsert writes whichever the live DB has. Load reads them via aliases too.
constexpr const char* kTemplateLocaleCols =
    "ID, locale, LogTitle, Title, LogDescription, Objectives, QuestDescription, Details, "
    "AreaDescription, EndText, QuestCompletionLog, CompletedText, "
    "ObjectiveText1, ObjectiveText2, ObjectiveText3, ObjectiveText4, VerifiedBuild";

constexpr const char* kOfferRewardLocaleCols = "ID, locale, RewardText, VerifiedBuild";
constexpr const char* kRequestItemsLocaleCols = "ID, locale, CompletionText, VerifiedBuild";
constexpr const char* kGreetingLocaleCols = "ID, Type, locale, Greeting, VerifiedBuild";

constexpr const char* kConditionsCols =
    "SourceTypeOrReferenceId, SourceGroup, SourceEntry, SourceId, ElseGroup, "
    "ConditionTypeOrReference, ConditionTarget, ConditionValue1, ConditionValue2, "
    "ConditionValue3, NegativeCondition, ErrorType, ErrorTextId, ScriptName, Comment";

// Executes one write; returns false and leaves `err` populated on failure.
bool ExecStep(IDatabase& db, const std::string& sql, DbError& err)
{
    db.Execute(sql, err);
    return err.ok;
}
} // namespace

// ---------------------------------------------------------------------------
DbError QuestRepository::ListQuests(IDatabase& db, const QuestListFilter& filter,
                                    std::vector<QuestListEntry>& out)
{
    out.clear();

    std::string sql =
        "SELECT ID, LogTitle, QuestSortID, QuestInfoID, MinLevel, QuestLevel FROM quest_template";

    // Build the WHERE clause from the active filters (AND-combined).
    std::vector<std::string> conds;
    if (filter.hasText())
    {
        const std::string esc = db.EscapeString(filter.text);
        std::string like = "LogTitle LIKE '%" + esc + "%'";
        if (filter.searchBody)
        {
            // Also search the visible body text columns.
            static const char* kBodyCols[] = {"QuestDescription", "LogDescription",
                                               "AreaDescription", "QuestCompletionLog",
                                               "ObjectiveText1", "ObjectiveText2",
                                               "ObjectiveText3", "ObjectiveText4"};
            for (const char* col : kBodyCols)
                like += std::string(" OR ") + col + " LIKE '%" + esc + "%'";
        }
        std::string text = "(" + like;
        if (IsAllDigits(filter.text))
            text += " OR ID = " + filter.text;
        text += ")";
        conds.push_back(text);
    }
    if (filter.questInfoId >= 0)
        conds.push_back("QuestInfoID = " + std::to_string(filter.questInfoId));
    if (filter.hasSortId)
        conds.push_back("QuestSortID = " + std::to_string(filter.questSortId));
    if (filter.minLevel > 0)
        conds.push_back("QuestLevel >= " + std::to_string(filter.minLevel));
    if (filter.maxLevel > 0)
        conds.push_back("QuestLevel <= " + std::to_string(filter.maxLevel));

    if (!conds.empty())
    {
        sql += " WHERE ";
        for (size_t i = 0; i < conds.size(); ++i)
            sql += (i ? " AND " : "") + conds[i];
    }

    // Server-side sort (so paging is consistent across pages).
    static const char* kSortCols[] = {"ID",       "LogTitle", "QuestLevel",
                                      "QuestSortID", "MinLevel", "QuestInfoID"};
    const int sc = (filter.sortColumn >= 0 && filter.sortColumn < 6) ? filter.sortColumn : 0;
    sql += std::string(" ORDER BY ") + kSortCols[sc] + (filter.sortAsc ? " ASC" : " DESC");
    if (sc != 0)
        sql += ", ID ASC"; // stable tiebreaker

    int limit = filter.limit > 0 ? filter.limit : kListLimit;
    if (limit > kListLimit)
        limit = kListLimit;
    sql += " LIMIT " + std::to_string(limit);
    if (filter.offset > 0)
        sql += " OFFSET " + std::to_string(filter.offset);

    DbError err;
    std::unique_ptr<ResultSet> rs = db.Query(sql, err);
    if (!rs)
        return err;

    while (rs->Next())
    {
        QuestListEntry e;
        e.id = rs->GetUInt32(0);
        e.title = rs->GetString(1);
        e.questSortId = static_cast<int16_t>(rs->GetInt32(2));
        e.questInfoId = static_cast<uint16_t>(rs->GetUInt32(3));
        e.minLevel = static_cast<uint8_t>(rs->GetUInt32(4));
        e.questLevel = static_cast<int16_t>(rs->GetInt32(5));
        out.push_back(std::move(e));
    }

    return err;
}

// ---------------------------------------------------------------------------
DbError QuestRepository::LoadQuest(IDatabase& db, uint32_t id, Quest& out)
{
    out = Quest{};
    const std::string idStr = std::to_string(id);
    DbError err;

    // --- quest_template (required) ---------------------------------------
    // Read by column NAME (SELECT *) so the editor tolerates schema drift between
    // TrinityCore revisions (e.g. a removed/renamed column just reads as default)
    // instead of failing the whole load with "Unknown column".
    {
        std::unique_ptr<ResultSet> rs = db.Query("SELECT * FROM quest_template WHERE ID = " + idStr, err);
        if (!rs)
            return err;
        if (!rs->Next())
        {
            DbError e;
            e.ok = false;
            e.message = "quest_template has no row for ID " + idStr;
            return e;
        }
        ResultSet& r = *rs;
        auto U = [&](const std::string& n) { int c = r.ColumnIndex(n); return c >= 0 ? r.GetUInt32(c) : 0u; };
        auto I = [&](const std::string& n) { int c = r.ColumnIndex(n); return c >= 0 ? r.GetInt32(c) : 0; };
        auto F = [&](const std::string& n) { int c = r.ColumnIndex(n); return c >= 0 ? r.GetFloat(c) : 0.0f; };
        auto S = [&](const std::string& n) { int c = r.ColumnIndex(n); return c >= 0 ? r.GetString(c) : std::string(); };
        auto Ui = [&](const char* base, int i) { return U(base + std::to_string(i)); };
        auto Ii = [&](const char* base, int i) { return I(base + std::to_string(i)); };
        auto Si = [&](const char* base, int i) { return S(base + std::to_string(i)); };

        QuestTemplate& t = out.tmpl;
        t.id = U("ID");
        t.questType = static_cast<uint8_t>(U("QuestType"));
        t.questLevel = static_cast<int16_t>(I("QuestLevel"));
        t.minLevel = static_cast<uint8_t>(U("MinLevel"));
        t.questSortID = static_cast<int16_t>(I("QuestSortID"));
        t.questInfoID = static_cast<uint16_t>(U("QuestInfoID"));
        t.suggestedGroupNum = static_cast<uint8_t>(U("SuggestedGroupNum"));
        t.requiredFactionId1 = static_cast<uint16_t>(U("RequiredFactionId1"));
        t.requiredFactionId2 = static_cast<uint16_t>(U("RequiredFactionId2"));
        t.requiredFactionValue1 = I("RequiredFactionValue1");
        t.requiredFactionValue2 = I("RequiredFactionValue2");
        t.rewardNextQuest = U("RewardNextQuest");
        t.rewardXPDifficulty = static_cast<uint8_t>(U("RewardXPDifficulty"));
        t.rewardMoney = I("RewardMoney");
        t.rewardBonusMoney = U("RewardBonusMoney");
        t.rewardDisplaySpell = U("RewardDisplaySpell");
        t.rewardSpell = I("RewardSpell");
        t.rewardHonor = I("RewardHonor");
        t.rewardKillHonor = F("RewardKillHonor");
        t.startItem = U("StartItem");
        t.flags = U("Flags");
        t.requiredPlayerKills = static_cast<uint8_t>(U("RequiredPlayerKills"));
        for (int i = 0; i < 4; ++i)
        {
            t.rewardItemId[i] = Ui("RewardItem", i + 1);
            t.rewardAmount[i] = static_cast<uint16_t>(Ui("RewardAmount", i + 1));
            t.itemDrop[i] = Ui("ItemDrop", i + 1);
            t.itemDropQuantity[i] = static_cast<uint16_t>(Ui("ItemDropQuantity", i + 1));
        }
        for (int i = 0; i < 6; ++i)
        {
            t.rewardChoiceItemId[i] = Ui("RewardChoiceItemID", i + 1);
            t.rewardChoiceItemQuantity[i] = static_cast<uint16_t>(Ui("RewardChoiceItemQuantity", i + 1));
        }
        t.poiContinent = static_cast<uint16_t>(U("POIContinent"));
        t.poiX = F("POIx");
        t.poiY = F("POIy");
        t.poiPriority = U("POIPriority");
        t.rewardTitle = static_cast<uint8_t>(U("RewardTitle"));
        t.rewardTalents = static_cast<uint8_t>(U("RewardTalents"));
        t.rewardArenaPoints = static_cast<uint16_t>(U("RewardArenaPoints"));
        for (int i = 0; i < 5; ++i)
        {
            t.rewardFactionId[i] = static_cast<uint16_t>(Ui("RewardFactionID", i + 1));
            t.rewardFactionValue[i] = Ii("RewardFactionValue", i + 1);
            t.rewardFactionOverride[i] = Ii("RewardFactionOverride", i + 1);
        }
        t.timeAllowed = U("TimeAllowed");
        t.allowableRaces = U("AllowableRaces");
        t.logTitle = S("LogTitle");
        t.logDescription = S("LogDescription");
        t.questDescription = S("QuestDescription");
        t.areaDescription = S("AreaDescription");
        t.questCompletionLog = S("QuestCompletionLog");
        for (int i = 0; i < 4; ++i)
        {
            t.requiredNpcOrGo[i] = Ii("RequiredNpcOrGo", i + 1);
            t.requiredNpcOrGoCount[i] = static_cast<uint16_t>(Ui("RequiredNpcOrGoCount", i + 1));
        }
        for (int i = 0; i < 6; ++i)
        {
            t.requiredItemId[i] = Ui("RequiredItemId", i + 1);
            t.requiredItemCount[i] = static_cast<uint16_t>(Ui("RequiredItemCount", i + 1));
        }
        {
            // Renamed Unknown0 -> RewardFactionFlags (2026-05-19); read either.
            int c = r.ColumnIndex("RewardFactionFlags");
            if (c < 0)
                c = r.ColumnIndex("Unknown0");
            t.rewardFactionFlags = c >= 0 ? r.GetUInt32(c) : 0u;
        }
        for (int i = 0; i < 4; ++i)
            t.objectiveText[i] = Si("ObjectiveText", i + 1);
        t.verifiedBuild = I("VerifiedBuild");
    }

    // --- quest_template_addon (optional) ---------------------------------
    // All sub-tables use SELECT * + by-name reads (see Row) so a column a newer TC
    // added/removed/renamed does not break the load.
    {
        std::unique_ptr<ResultSet> rs =
            db.Query("SELECT * FROM quest_template_addon WHERE ID = " + idStr, err);
        if (!rs)
            return err;
        if (rs->Next())
        {
            Row row(*rs);
            QuestTemplateAddon& a = out.addon;
            a.id = row.U("ID");
            a.maxLevel = static_cast<uint8_t>(row.U("MaxLevel"));
            a.allowableClasses = row.U("AllowableClasses");
            a.sourceSpellID = row.U("SourceSpellID");
            a.prevQuestID = row.I("PrevQuestID");
            a.nextQuestID = row.U("NextQuestID");
            a.exclusiveGroup = row.I("ExclusiveGroup");
            a.breadcrumbForQuestId = row.I("BreadcrumbForQuestId");
            a.rewardMailTemplateID = row.U("RewardMailTemplateID");
            a.rewardMailDelay = row.U("RewardMailDelay");
            a.requiredSkillID = static_cast<uint16_t>(row.U("RequiredSkillID"));
            a.requiredSkillPoints = static_cast<uint16_t>(row.U("RequiredSkillPoints"));
            a.requiredMinRepFaction = static_cast<uint16_t>(row.U("RequiredMinRepFaction"));
            a.requiredMaxRepFaction = static_cast<uint16_t>(row.U("RequiredMaxRepFaction"));
            a.requiredMinRepValue = row.I("RequiredMinRepValue");
            a.requiredMaxRepValue = row.I("RequiredMaxRepValue");
            a.providedItemCount = static_cast<uint8_t>(row.U("ProvidedItemCount"));
            a.specialFlags = static_cast<uint8_t>(row.U("SpecialFlags"));
            a.present = true;
        }
    }

    // --- quest_offer_reward (optional) -----------------------------------
    {
        std::unique_ptr<ResultSet> rs =
            db.Query("SELECT * FROM quest_offer_reward WHERE ID = " + idStr, err);
        if (!rs)
            return err;
        if (rs->Next())
        {
            Row row(*rs);
            QuestOfferReward& o = out.offerReward;
            o.id = row.U("ID");
            for (int i = 0; i < 4; ++i)
                o.emote[i] = static_cast<uint16_t>(row.Ui("Emote", i + 1));
            for (int i = 0; i < 4; ++i)
                o.emoteDelay[i] = row.Ui("EmoteDelay", i + 1);
            o.rewardText = row.S("RewardText");
            o.verifiedBuild = row.I("VerifiedBuild");
            o.present = true;
        }
    }

    // --- quest_request_items (optional) ----------------------------------
    {
        std::unique_ptr<ResultSet> rs =
            db.Query("SELECT * FROM quest_request_items WHERE ID = " + idStr, err);
        if (!rs)
            return err;
        if (rs->Next())
        {
            Row row(*rs);
            QuestRequestItems& r = out.requestItems;
            r.id = row.U("ID");
            r.emoteOnComplete = static_cast<uint16_t>(row.U("EmoteOnComplete"));
            r.emoteOnIncomplete = static_cast<uint16_t>(row.U("EmoteOnIncomplete"));
            r.completionText = row.S("CompletionText");
            r.verifiedBuild = row.I("VerifiedBuild");
            r.present = true;
        }
    }

    // --- quest_details (optional) ----------------------------------------
    {
        std::unique_ptr<ResultSet> rs =
            db.Query("SELECT * FROM quest_details WHERE ID = " + idStr, err);
        if (!rs)
            return err;
        if (rs->Next())
        {
            Row row(*rs);
            QuestDetails& d = out.details;
            d.id = row.U("ID");
            for (int i = 0; i < 4; ++i)
                d.emote[i] = static_cast<uint16_t>(row.Ui("Emote", i + 1));
            for (int i = 0; i < 4; ++i)
                d.emoteDelay[i] = row.Ui("EmoteDelay", i + 1);
            d.verifiedBuild = row.I("VerifiedBuild");
            d.present = true;
        }
    }

    // --- quest_mail_sender (optional) ------------------------------------
    {
        std::unique_ptr<ResultSet> rs =
            db.Query("SELECT * FROM quest_mail_sender WHERE QuestId = " + idStr, err);
        if (!rs)
            return err;
        if (rs->Next())
        {
            Row row(*rs);
            QuestMailSender& m = out.mailSender;
            m.questId = row.U("QuestId");
            m.rewardMailSenderEntry = row.U("RewardMailSenderEntry");
            m.present = true;
        }
    }

    // --- quest_greeting (0..2 rows, by Type) -----------------------------
    {
        std::string sql = "SELECT * FROM quest_greeting WHERE ID = " + idStr + " ORDER BY Type";
        std::unique_ptr<ResultSet> rs = db.Query(sql, err);
        if (!rs)
            return err;
        while (rs->Next())
        {
            Row row(*rs);
            QuestGreeting g;
            g.id = row.U("ID");
            g.type = static_cast<uint8_t>(row.U("Type"));
            g.greetEmoteType = static_cast<uint16_t>(row.U("GreetEmoteType"));
            g.greetEmoteDelay = row.U("GreetEmoteDelay");
            g.greeting = row.S("Greeting");
            g.verifiedBuild = row.I("VerifiedBuild");
            out.greetings.push_back(std::move(g));
        }
    }

    // --- questgiver links -------------------------------------------------
    struct LinkQuery
    {
        const char* table;
        std::vector<uint32_t>* dest;
    };
    const LinkQuery linkQueries[] = {
        {"creature_queststarter", &out.creatureStarters},
        {"creature_questender", &out.creatureEnders},
        {"gameobject_queststarter", &out.goStarters},
        {"gameobject_questender", &out.goEnders},
    };
    for (const LinkQuery& lq : linkQueries)
    {
        std::string sql = std::string("SELECT id FROM ") + lq.table +
                          " WHERE quest = " + idStr + " ORDER BY id";
        std::unique_ptr<ResultSet> rs = db.Query(sql, err);
        if (!rs)
            return err;
        while (rs->Next())
            lq.dest->push_back(rs->GetUInt32(0));
    }

    // --- quest_poi + quest_poi_points ------------------------------------
    {
        std::string sql = "SELECT * FROM quest_poi WHERE QuestID = " + idStr + " ORDER BY id";
        std::unique_ptr<ResultSet> rs = db.Query(sql, err);
        if (!rs)
            return err;
        while (rs->Next())
        {
            Row row(*rs);
            QuestPoi p;
            p.questID = row.U("QuestID");
            p.id = row.U("id");
            p.objectiveIndex = row.I("ObjectiveIndex");
            p.mapID = row.U("MapID");
            p.worldMapAreaId = row.U("WorldMapAreaId");
            p.floor = row.U("Floor");
            p.priority = row.U("Priority");
            p.flags = row.U("Flags");
            p.verifiedBuild = row.I("VerifiedBuild");
            out.pois.push_back(std::move(p));
        }

        std::string psql =
            "SELECT * FROM quest_poi_points WHERE QuestID = " + idStr + " ORDER BY Idx1, Idx2";
        std::unique_ptr<ResultSet> prs = db.Query(psql, err);
        if (!prs)
            return err;
        while (prs->Next())
        {
            Row row(*prs);
            QuestPoiPoint pt;
            pt.questID = row.U("QuestID");
            pt.idx1 = row.U("Idx1");
            pt.idx2 = row.U("Idx2");
            pt.x = row.I("X");
            pt.y = row.I("Y");
            pt.verifiedBuild = row.I("VerifiedBuild");
            // Attach to the owning POI (Idx1 == poi id).
            for (QuestPoi& p : out.pois)
            {
                if (p.id == pt.idx1)
                {
                    p.points.push_back(pt);
                    break;
                }
            }
        }
    }

    // --- locales (merge across four *_locale tables) ---------------------
    auto localeFor = [&out](const std::string& code) -> QuestLocale& {
        auto it = out.locales.find(code);
        if (it == out.locales.end())
        {
            QuestLocale ql;
            ql.locale = code;
            it = out.locales.emplace(code, std::move(ql)).first;
        }
        return it->second;
    };

    {
        // TC 2026-07-02 renamed these columns; read new name, fall back to legacy.
        std::string sql = "SELECT * FROM quest_template_locale WHERE ID = " + idStr;
        std::unique_ptr<ResultSet> rs = db.Query(sql, err);
        if (!rs)
            return err;
        while (rs->Next())
        {
            Row row(*rs);
            QuestLocale& l = localeFor(row.S("locale"));
            l.title = row.Sa({"LogTitle", "Title"});
            l.details = row.Sa({"QuestDescription", "Details"});
            l.objectives = row.Sa({"LogDescription", "Objectives"});
            l.endText = row.Sa({"AreaDescription", "EndText"});
            l.completedText = row.Sa({"QuestCompletionLog", "CompletedText"});
            for (int i = 0; i < 4; ++i)
                l.objectiveText[i] = row.Si("ObjectiveText", i + 1);
            l.templatePresent = true;
        }
    }
    {
        std::string sql = "SELECT * FROM quest_offer_reward_locale WHERE ID = " + idStr;
        std::unique_ptr<ResultSet> rs = db.Query(sql, err);
        if (!rs)
            return err;
        while (rs->Next())
        {
            Row row(*rs);
            QuestLocale& l = localeFor(row.S("locale"));
            l.rewardText = row.S("RewardText");
            l.offerRewardPresent = true;
        }
    }
    {
        std::string sql = "SELECT * FROM quest_request_items_locale WHERE ID = " + idStr;
        std::unique_ptr<ResultSet> rs = db.Query(sql, err);
        if (!rs)
            return err;
        while (rs->Next())
        {
            Row row(*rs);
            QuestLocale& l = localeFor(row.S("locale"));
            l.completionText = row.S("CompletionText");
            l.requestItemsPresent = true;
        }
    }
    {
        std::string sql = "SELECT * FROM quest_greeting_locale WHERE ID = " + idStr;
        std::unique_ptr<ResultSet> rs = db.Query(sql, err);
        if (!rs)
            return err;
        while (rs->Next())
        {
            Row row(*rs);
            QuestLocale& l = localeFor(row.S("locale"));
            uint8_t type = static_cast<uint8_t>(row.U("Type"));
            std::string text = row.S("Greeting");
            if (type == 0)
            {
                l.greetingCreature = text;
                l.greetingCreaturePresent = true;
            }
            else
            {
                l.greetingGameObject = text;
                l.greetingGameObjectPresent = true;
            }
        }
    }

    // --- conditions (quest-available gating, SourceType 19) --------------
    {
        DbError ce;
        const std::string sql =
            "SELECT * FROM conditions WHERE SourceTypeOrReferenceId = 19 AND SourceEntry = " + idStr;
        if (auto rs = db.Query(sql, ce))
        {
            while (rs->Next())
            {
                Row row(*rs);
                QuestCondition c;
                c.sourceTypeOrReferenceId = row.I("SourceTypeOrReferenceId");
                c.sourceGroup = row.U("SourceGroup");
                c.sourceEntry = row.I("SourceEntry");
                c.sourceId = row.I("SourceId");
                c.elseGroup = row.U("ElseGroup");
                c.conditionTypeOrReference = row.I("ConditionTypeOrReference");
                c.conditionTarget = static_cast<uint8_t>(row.U("ConditionTarget"));
                c.conditionValue1 = row.U("ConditionValue1");
                c.conditionValue2 = row.U("ConditionValue2");
                c.conditionValue3 = row.U("ConditionValue3");
                c.negativeCondition = static_cast<uint8_t>(row.U("NegativeCondition"));
                c.errorType = row.U("ErrorType");
                c.errorTextId = row.U("ErrorTextId");
                c.scriptName = row.S("ScriptName");
                c.comment = row.S("Comment");
                out.conditions.push_back(std::move(c));
            }
        }
    }

    out.ClearDirty();
    out.isNew = false;
    return DbError{};
}

// ---------------------------------------------------------------------------
DbError QuestRepository::SaveQuest(IDatabase& db, const Quest& quest)
{
    const uint32_t id = quest.tmpl.id;
    const std::string idStr = std::to_string(id);

    db.BeginTransaction();
    DbError err;

    auto fail = [&](const DbError& e) -> DbError {
        db.Rollback();
        return e;
    };

    // --- quest_template (always REPLACE) ---------------------------------
    {
        const QuestTemplate& t = quest.tmpl;
        ValueList v(db);
        v.UInt(t.id);
        v.UInt(t.questType);
        v.Int(t.questLevel);
        v.UInt(t.minLevel);
        v.Int(t.questSortID);
        v.UInt(t.questInfoID);
        v.UInt(t.suggestedGroupNum);
        v.UInt(t.requiredFactionId1);
        v.UInt(t.requiredFactionId2);
        v.Int(t.requiredFactionValue1);
        v.Int(t.requiredFactionValue2);
        v.UInt(t.rewardNextQuest);
        v.UInt(t.rewardXPDifficulty);
        v.Int(t.rewardMoney);
        v.UInt(t.rewardBonusMoney);
        v.UInt(t.rewardDisplaySpell);
        v.Int(t.rewardSpell);
        v.Int(t.rewardHonor);
        v.Float(t.rewardKillHonor);
        v.UInt(t.startItem);
        v.UInt(t.flags);
        v.UInt(t.requiredPlayerKills);
        for (int i = 0; i < 4; ++i)
        {
            v.UInt(t.rewardItemId[i]);
            v.UInt(t.rewardAmount[i]);
        }
        for (int i = 0; i < 4; ++i)
        {
            v.UInt(t.itemDrop[i]);
            v.UInt(t.itemDropQuantity[i]);
        }
        for (int i = 0; i < 6; ++i)
        {
            v.UInt(t.rewardChoiceItemId[i]);
            v.UInt(t.rewardChoiceItemQuantity[i]);
        }
        v.UInt(t.poiContinent);
        v.Float(t.poiX);
        v.Float(t.poiY);
        v.UInt(t.poiPriority);
        v.UInt(t.rewardTitle);
        v.UInt(t.rewardTalents);
        v.UInt(t.rewardArenaPoints);
        for (int i = 0; i < 5; ++i)
        {
            v.UInt(t.rewardFactionId[i]);
            v.Int(t.rewardFactionValue[i]);
            v.Int(t.rewardFactionOverride[i]);
        }
        v.UInt(t.timeAllowed);
        v.UInt(t.allowableRaces);
        v.Text(t.logTitle);
        v.Text(t.logDescription);
        v.Text(t.questDescription);
        v.Text(t.areaDescription);
        v.Text(t.questCompletionLog);
        for (int i = 0; i < 4; ++i)
            v.Int(t.requiredNpcOrGo[i]);
        for (int i = 0; i < 4; ++i)
            v.UInt(t.requiredNpcOrGoCount[i]);
        for (int i = 0; i < 6; ++i)
            v.UInt(t.requiredItemId[i]);
        for (int i = 0; i < 6; ++i)
            v.UInt(t.requiredItemCount[i]);
        v.UInt(t.rewardFactionFlags); // -> RewardFactionFlags (new name)
        v.UInt(t.rewardFactionFlags); // -> Unknown0 (legacy name); only the existing one is written
        for (int i = 0; i < 4; ++i)
            v.Text(t.objectiveText[i]);
        v.Int(t.verifiedBuild);

        // Schema-adaptive: only write columns that exist in this DB (tolerates TC
        // revisions that removed/renamed a column, e.g. Unknown0/RewardFactionFlags).
        static const std::vector<std::string> qtCols = SplitCols(kQuestTemplateCols);
        static const std::set<std::string> qtLegacy = {"unknown0"};
        const std::string sql = FilteredInsert("REPLACE", "quest_template", qtCols, v.tokens,
                                               ExistingCols(db, "quest_template"), qtLegacy);
        if (!ExecStep(db, sql, err))
            return fail(err);
    }

    // --- quest_template_addon (REPLACE or DELETE) ------------------------
    if (quest.addon.present)
    {
        const QuestTemplateAddon& a = quest.addon;
        ValueList v(db);
        v.UInt(id);
        v.UInt(a.maxLevel);
        v.UInt(a.allowableClasses);
        v.UInt(a.sourceSpellID);
        v.Int(a.prevQuestID);
        v.UInt(a.nextQuestID);
        v.Int(a.exclusiveGroup);
        v.Int(a.breadcrumbForQuestId);
        v.UInt(a.rewardMailTemplateID);
        v.UInt(a.rewardMailDelay);
        v.UInt(a.requiredSkillID);
        v.UInt(a.requiredSkillPoints);
        v.UInt(a.requiredMinRepFaction);
        v.UInt(a.requiredMaxRepFaction);
        v.Int(a.requiredMinRepValue);
        v.Int(a.requiredMaxRepValue);
        v.UInt(a.providedItemCount);
        v.UInt(a.specialFlags);
        static const std::vector<std::string> cols = SplitCols(kAddonCols);
        std::string sql =
            FilteredUpsert("quest_template_addon", cols, v.tokens, ExistingCols(db, "quest_template_addon"), "ID");
        if (!ExecStep(db, sql, err))
            return fail(err);
    }
    else if (!ExecStep(db, "DELETE FROM quest_template_addon WHERE ID = " + idStr, err))
    {
        return fail(err);
    }

    // --- quest_offer_reward (REPLACE or DELETE) --------------------------
    if (quest.offerReward.present)
    {
        const QuestOfferReward& o = quest.offerReward;
        ValueList v(db);
        v.UInt(id);
        for (int i = 0; i < 4; ++i)
            v.UInt(o.emote[i]);
        for (int i = 0; i < 4; ++i)
            v.UInt(o.emoteDelay[i]);
        v.Text(o.rewardText);
        v.Int(o.verifiedBuild);
        static const std::vector<std::string> cols = SplitCols(kOfferRewardCols);
        std::string sql =
            FilteredUpsert("quest_offer_reward", cols, v.tokens, ExistingCols(db, "quest_offer_reward"), "ID");
        if (!ExecStep(db, sql, err))
            return fail(err);
    }
    else if (!ExecStep(db, "DELETE FROM quest_offer_reward WHERE ID = " + idStr, err))
    {
        return fail(err);
    }

    // --- quest_request_items (REPLACE or DELETE) -------------------------
    if (quest.requestItems.present)
    {
        const QuestRequestItems& r = quest.requestItems;
        ValueList v(db);
        v.UInt(id);
        v.UInt(r.emoteOnComplete);
        v.UInt(r.emoteOnIncomplete);
        v.Text(r.completionText);
        v.Int(r.verifiedBuild);
        // Upsert (not REPLACE) so newer columns this editor doesn't model
        // (e.g. EmoteOnCompleteDelay, added 2026-07-04) keep their stored values.
        static const std::vector<std::string> cols = SplitCols(kRequestItemsCols);
        std::string sql =
            FilteredUpsert("quest_request_items", cols, v.tokens, ExistingCols(db, "quest_request_items"), "ID");
        if (!ExecStep(db, sql, err))
            return fail(err);
    }
    else if (!ExecStep(db, "DELETE FROM quest_request_items WHERE ID = " + idStr, err))
    {
        return fail(err);
    }

    // --- quest_details (REPLACE or DELETE) -------------------------------
    if (quest.details.present)
    {
        const QuestDetails& d = quest.details;
        ValueList v(db);
        v.UInt(id);
        for (int i = 0; i < 4; ++i)
            v.UInt(d.emote[i]);
        for (int i = 0; i < 4; ++i)
            v.UInt(d.emoteDelay[i]);
        v.Int(d.verifiedBuild);
        static const std::vector<std::string> cols = SplitCols(kDetailsCols);
        std::string sql =
            FilteredUpsert("quest_details", cols, v.tokens, ExistingCols(db, "quest_details"), "ID");
        if (!ExecStep(db, sql, err))
            return fail(err);
    }
    else if (!ExecStep(db, "DELETE FROM quest_details WHERE ID = " + idStr, err))
    {
        return fail(err);
    }

    // --- quest_mail_sender (REPLACE or DELETE) ---------------------------
    if (quest.mailSender.present)
    {
        ValueList v(db);
        v.UInt(id);
        v.UInt(quest.mailSender.rewardMailSenderEntry);
        static const std::vector<std::string> cols = SplitCols(kMailSenderCols);
        std::string sql =
            FilteredUpsert("quest_mail_sender", cols, v.tokens, ExistingCols(db, "quest_mail_sender"), "QuestId");
        if (!ExecStep(db, sql, err))
            return fail(err);
    }
    else if (!ExecStep(db, "DELETE FROM quest_mail_sender WHERE QuestId = " + idStr, err))
    {
        return fail(err);
    }

    // --- quest_greeting (delete-then-insert) -----------------------------
    if (!ExecStep(db, "DELETE FROM quest_greeting WHERE ID = " + idStr, err))
        return fail(err);
    for (const QuestGreeting& g : quest.greetings)
    {
        ValueList v(db);
        v.UInt(id);
        v.UInt(g.type);
        v.UInt(g.greetEmoteType);
        v.UInt(g.greetEmoteDelay);
        v.Text(g.greeting);
        v.Int(g.verifiedBuild);
        static const std::vector<std::string> cols = SplitCols(kGreetingCols);
        std::string sql =
            FilteredInsert("INSERT", "quest_greeting", cols, v.tokens, ExistingCols(db, "quest_greeting"));
        if (!ExecStep(db, sql, err))
            return fail(err);
    }

    // --- questgiver links (delete-then-insert per table) -----------------
    struct LinkSave
    {
        const char* table;
        const std::vector<uint32_t>* src;
    };
    const LinkSave linkSaves[] = {
        {"creature_queststarter", &quest.creatureStarters},
        {"creature_questender", &quest.creatureEnders},
        {"gameobject_queststarter", &quest.goStarters},
        {"gameobject_questender", &quest.goEnders},
    };
    for (const LinkSave& ls : linkSaves)
    {
        std::string del = std::string("DELETE FROM ") + ls.table + " WHERE quest = " + idStr;
        if (!ExecStep(db, del, err))
            return fail(err);
        for (uint32_t entry : *ls.src)
        {
            std::string sql = std::string("INSERT INTO ") + ls.table + " (id, quest) VALUES (" +
                              std::to_string(entry) + ", " + idStr + ")";
            if (!ExecStep(db, sql, err))
                return fail(err);
        }
    }

    // --- quest_poi + quest_poi_points (delete-then-insert) ---------------
    if (!ExecStep(db, "DELETE FROM quest_poi_points WHERE QuestID = " + idStr, err))
        return fail(err);
    if (!ExecStep(db, "DELETE FROM quest_poi WHERE QuestID = " + idStr, err))
        return fail(err);
    static const std::vector<std::string> poiCols = SplitCols(kPoiCols);
    static const std::vector<std::string> ptCols = SplitCols(kPoiPointCols);
    const std::set<std::string> poiExisting = ExistingCols(db, "quest_poi");
    const std::set<std::string> ptExisting = ExistingCols(db, "quest_poi_points");
    for (const QuestPoi& p : quest.pois)
    {
        ValueList v(db);
        v.UInt(id);
        v.UInt(p.id);
        v.Int(p.objectiveIndex);
        v.UInt(p.mapID);
        v.UInt(p.worldMapAreaId);
        v.UInt(p.floor);
        v.UInt(p.priority);
        v.UInt(p.flags);
        v.Int(p.verifiedBuild);
        std::string sql = FilteredInsert("INSERT", "quest_poi", poiCols, v.tokens, poiExisting);
        if (!ExecStep(db, sql, err))
            return fail(err);

        for (const QuestPoiPoint& pt : p.points)
        {
            ValueList pv(db);
            pv.UInt(id);
            pv.UInt(p.id); // Idx1 == owning POI id
            pv.UInt(pt.idx2);
            pv.Int(pt.x);
            pv.Int(pt.y);
            pv.Int(pt.verifiedBuild);
            std::string psql =
                FilteredInsert("INSERT", "quest_poi_points", ptCols, pv.tokens, ptExisting);
            if (!ExecStep(db, psql, err))
                return fail(err);
        }
    }

    // --- locales (delete-then-insert across four *_locale tables) --------
    // VerifiedBuild is not tracked on QuestLocale; it is written as 0.
    if (!ExecStep(db, "DELETE FROM quest_template_locale WHERE ID = " + idStr, err))
        return fail(err);
    if (!ExecStep(db, "DELETE FROM quest_offer_reward_locale WHERE ID = " + idStr, err))
        return fail(err);
    if (!ExecStep(db, "DELETE FROM quest_request_items_locale WHERE ID = " + idStr, err))
        return fail(err);
    if (!ExecStep(db, "DELETE FROM quest_greeting_locale WHERE ID = " + idStr, err))
        return fail(err);

    // Column lists + existing-column sets (schema-adaptive; renamed template-locale
    // columns are listed under both names so only the DB's actual column is written).
    static const std::vector<std::string> tplLocCols = SplitCols(kTemplateLocaleCols);
    static const std::vector<std::string> offLocCols = SplitCols(kOfferRewardLocaleCols);
    static const std::vector<std::string> reqLocCols = SplitCols(kRequestItemsLocaleCols);
    static const std::vector<std::string> grtLocCols = SplitCols(kGreetingLocaleCols);
    const std::set<std::string> tplLocExisting = ExistingCols(db, "quest_template_locale");
    const std::set<std::string> offLocExisting = ExistingCols(db, "quest_offer_reward_locale");
    const std::set<std::string> reqLocExisting = ExistingCols(db, "quest_request_items_locale");
    const std::set<std::string> grtLocExisting = ExistingCols(db, "quest_greeting_locale");

    for (const auto& kv : quest.locales)
    {
        const QuestLocale& l = kv.second;
        const std::string loc = "'" + db.EscapeString(l.locale) + "'";

        if (l.templatePresent)
        {
            // Token order matches kTemplateLocaleCols (each renamed field twice:
            // new name then legacy name, same value).
            ValueList v(db);
            v.UInt(id);
            v.Append(loc);
            v.Text(l.title);         // LogTitle
            v.Text(l.title);         // Title (legacy)
            v.Text(l.objectives);    // LogDescription
            v.Text(l.objectives);    // Objectives (legacy)
            v.Text(l.details);       // QuestDescription
            v.Text(l.details);       // Details (legacy)
            v.Text(l.endText);       // AreaDescription
            v.Text(l.endText);       // EndText (legacy)
            v.Text(l.completedText); // QuestCompletionLog
            v.Text(l.completedText); // CompletedText (legacy)
            for (int i = 0; i < 4; ++i)
                v.Text(l.objectiveText[i]);
            v.Int(0); // VerifiedBuild
            static const std::set<std::string> tplLocLegacy = {
                "title", "objectives", "details", "endtext", "completedtext"};
            std::string sql = FilteredInsert("INSERT", "quest_template_locale", tplLocCols,
                                             v.tokens, tplLocExisting, tplLocLegacy);
            if (!ExecStep(db, sql, err))
                return fail(err);
        }
        if (l.offerRewardPresent)
        {
            ValueList v(db);
            v.UInt(id);
            v.Append(loc);
            v.Text(l.rewardText);
            v.Int(0);
            std::string sql = FilteredInsert("INSERT", "quest_offer_reward_locale", offLocCols,
                                             v.tokens, offLocExisting);
            if (!ExecStep(db, sql, err))
                return fail(err);
        }
        if (l.requestItemsPresent)
        {
            ValueList v(db);
            v.UInt(id);
            v.Append(loc);
            v.Text(l.completionText);
            v.Int(0);
            std::string sql = FilteredInsert("INSERT", "quest_request_items_locale", reqLocCols,
                                             v.tokens, reqLocExisting);
            if (!ExecStep(db, sql, err))
                return fail(err);
        }
        if (l.greetingCreaturePresent)
        {
            ValueList v(db);
            v.UInt(id);
            v.UInt(0u); // Type 0 = creature
            v.Append(loc);
            v.Text(l.greetingCreature);
            v.Int(0);
            std::string sql = FilteredInsert("INSERT", "quest_greeting_locale", grtLocCols,
                                             v.tokens, grtLocExisting);
            if (!ExecStep(db, sql, err))
                return fail(err);
        }
        if (l.greetingGameObjectPresent)
        {
            ValueList v(db);
            v.UInt(id);
            v.UInt(1u); // Type 1 = gameobject
            v.Append(loc);
            v.Text(l.greetingGameObject);
            v.Int(0);
            std::string sql = FilteredInsert("INSERT", "quest_greeting_locale", grtLocCols,
                                             v.tokens, grtLocExisting);
            if (!ExecStep(db, sql, err))
                return fail(err);
        }
    }

    // --- conditions (SourceType 19 quest-available rows) -----------------
    if (!ExecStep(db,
                  "DELETE FROM conditions WHERE SourceTypeOrReferenceId = 19 AND SourceEntry = " +
                      idStr,
                  err))
        return fail(err);
    static const std::vector<std::string> condCols = SplitCols(kConditionsCols);
    const std::set<std::string> condExisting = ExistingCols(db, "conditions");
    for (const QuestCondition& c : quest.conditions)
    {
        ValueList v(db);
        v.Int(19);                                       // SourceTypeOrReferenceId (forced)
        v.UInt(c.sourceGroup);
        v.Int(static_cast<int32_t>(quest.tmpl.id));      // SourceEntry (forced = quest id)
        v.Int(c.sourceId);
        v.UInt(c.elseGroup);
        v.Int(c.conditionTypeOrReference);
        v.UInt(c.conditionTarget);
        v.UInt(c.conditionValue1);
        v.UInt(c.conditionValue2);
        v.UInt(c.conditionValue3);
        v.UInt(c.negativeCondition);
        v.UInt(c.errorType);
        v.UInt(c.errorTextId);
        v.Text(c.scriptName);
        v.Text(c.comment);
        const std::string sql =
            FilteredInsert("INSERT", "conditions", condCols, v.tokens, condExisting);
        if (!ExecStep(db, sql, err))
            return fail(err);
    }

    return db.Commit();
}

// ---------------------------------------------------------------------------
DbError QuestRepository::DeleteQuest(IDatabase& db, uint32_t id)
{
    const std::string idStr = std::to_string(id);

    db.BeginTransaction();
    DbError err;

    // (table, key column) for every table that references this quest.
    struct DelSpec
    {
        const char* table;
        const char* keyCol;
    };
    const DelSpec specs[] = {
        {"quest_poi_points", "QuestID"},
        {"quest_poi", "QuestID"},
        {"quest_greeting_locale", "ID"},
        {"quest_greeting", "ID"},
        {"quest_request_items_locale", "ID"},
        {"quest_request_items", "ID"},
        {"quest_offer_reward_locale", "ID"},
        {"quest_offer_reward", "ID"},
        {"quest_template_locale", "ID"},
        {"quest_details", "ID"},
        {"quest_mail_sender", "QuestId"},
        {"quest_template_addon", "ID"},
        {"creature_queststarter", "quest"},
        {"creature_questender", "quest"},
        {"gameobject_queststarter", "quest"},
        {"gameobject_questender", "quest"},
        {"quest_template", "ID"},
    };

    for (const DelSpec& s : specs)
    {
        std::string sql =
            std::string("DELETE FROM ") + s.table + " WHERE " + s.keyCol + " = " + idStr;
        db.Execute(sql, err);
        if (!err.ok)
        {
            db.Rollback();
            return err;
        }
    }

    // quest-available conditions (needs the extra SourceType filter).
    db.Execute("DELETE FROM conditions WHERE SourceTypeOrReferenceId = 19 AND SourceEntry = " + idStr,
               err);
    if (!err.ok)
    {
        db.Rollback();
        return err;
    }

    return db.Commit();
}

// ---------------------------------------------------------------------------
DbError QuestRepository::NextFreeQuestId(IDatabase& db, uint32_t& out)
{
    out = 0;
    DbError err;
    std::unique_ptr<ResultSet> rs =
        db.Query("SELECT COALESCE(MAX(ID),0)+1 FROM quest_template", err);
    if (!rs)
        return err;
    if (rs->Next())
        out = rs->GetUInt32(0);
    return err;
}

// ---------------------------------------------------------------------------
DbError QuestRepository::NextFreeQuestIdFrom(IDatabase& db, uint32_t minId, uint32_t& out)
{
    out = minId;
    DbError err;
    std::unique_ptr<ResultSet> rs = db.Query(
        "SELECT COALESCE(MAX(ID),0) FROM quest_template WHERE ID >= " + std::to_string(minId), err);
    if (!rs)
        return err;
    if (rs->Next())
    {
        uint32_t maxInRange = rs->GetUInt32(0);
        out = (maxInRange >= minId) ? maxInRange + 1 : minId;
    }
    return err;
}

// ---------------------------------------------------------------------------
DbError QuestRepository::FindReferences(IDatabase& db, ReferenceKind kind, uint32_t id,
                                        std::vector<QuestListEntry>& out)
{
    out.clear();
    const std::string x = std::to_string(id);
    const std::string nx = "-" + x;  // negated (gameobjects in RequiredNpcOrGo)
    std::string where;

    switch (kind)
    {
        case ReferenceKind::Item:
            where = "StartItem=" + x +
                    " OR RewardItem1=" + x + " OR RewardItem2=" + x + " OR RewardItem3=" + x +
                    " OR RewardItem4=" + x + " OR RequiredItemId1=" + x + " OR RequiredItemId2=" + x +
                    " OR RequiredItemId3=" + x + " OR RequiredItemId4=" + x + " OR RequiredItemId5=" +
                    x + " OR RequiredItemId6=" + x + " OR ItemDrop1=" + x + " OR ItemDrop2=" + x +
                    " OR ItemDrop3=" + x + " OR ItemDrop4=" + x + " OR RewardChoiceItemID1=" + x +
                    " OR RewardChoiceItemID2=" + x + " OR RewardChoiceItemID3=" + x +
                    " OR RewardChoiceItemID4=" + x + " OR RewardChoiceItemID5=" + x +
                    " OR RewardChoiceItemID6=" + x;
            break;
        case ReferenceKind::Creature:
            where = "RequiredNpcOrGo1=" + x + " OR RequiredNpcOrGo2=" + x + " OR RequiredNpcOrGo3=" +
                    x + " OR RequiredNpcOrGo4=" + x +
                    " OR ID IN (SELECT quest FROM creature_queststarter WHERE id=" + x +
                    ") OR ID IN (SELECT quest FROM creature_questender WHERE id=" + x + ")";
            break;
        case ReferenceKind::GameObject:
            where = "RequiredNpcOrGo1=" + nx + " OR RequiredNpcOrGo2=" + nx +
                    " OR RequiredNpcOrGo3=" + nx + " OR RequiredNpcOrGo4=" + nx +
                    " OR ID IN (SELECT quest FROM gameobject_queststarter WHERE id=" + x +
                    ") OR ID IN (SELECT quest FROM gameobject_questender WHERE id=" + x + ")";
            break;
        case ReferenceKind::Quest:
            where = "RewardNextQuest=" + x +
                    " OR ID IN (SELECT ID FROM quest_template_addon WHERE PrevQuestID=" + x +
                    " OR NextQuestID=" + x + " OR BreadcrumbForQuestId=" + x + ")";
            break;
    }

    const std::string sql =
        "SELECT ID, LogTitle, QuestSortID, QuestInfoID, MinLevel, QuestLevel FROM quest_template "
        "WHERE " + where + " ORDER BY ID LIMIT " + std::to_string(kListLimit);

    DbError err;
    std::unique_ptr<ResultSet> rs = db.Query(sql, err);
    if (!rs)
        return err;
    while (rs->Next())
    {
        QuestListEntry e;
        e.id = rs->GetUInt32(0);
        e.title = rs->GetString(1);
        e.questSortId = static_cast<int16_t>(rs->GetInt32(2));
        e.questInfoId = static_cast<uint16_t>(rs->GetUInt32(3));
        e.minLevel = static_cast<uint8_t>(rs->GetUInt32(4));
        e.questLevel = static_cast<int16_t>(rs->GetInt32(5));
        out.push_back(std::move(e));
    }
    return err;
}

namespace
{
DbError CountSpawns(IDatabase& db, const char* table, uint32_t entry,
                    QuestRepository::SpawnInfo& out)
{
    out = QuestRepository::SpawnInfo{};
    const std::string e = std::to_string(entry);
    DbError err;
    if (auto rs = db.Query(std::string("SELECT COUNT(*) FROM ") + table + " WHERE id=" + e, err))
        if (rs->Next())
            out.count = rs->GetUInt32(0);
    if (out.count == 0)
        return err;
    const std::string s = std::string("SELECT map, position_x, position_y, position_z FROM ") +
                          table + " WHERE id=" + e + " LIMIT 1";
    if (auto rs = db.Query(s, err))
        if (rs->Next())
        {
            out.map = static_cast<uint16_t>(rs->GetUInt32(0));
            out.x = rs->GetFloat(1);
            out.y = rs->GetFloat(2);
            out.z = rs->GetFloat(3);
        }
    return err;
}
} // namespace

DbError QuestRepository::CountCreatureSpawns(IDatabase& db, uint32_t entry, SpawnInfo& out)
{
    return CountSpawns(db, "creature", entry, out);
}
DbError QuestRepository::CountGameObjectSpawns(IDatabase& db, uint32_t entry, SpawnInfo& out)
{
    return CountSpawns(db, "gameobject", entry, out);
}

DbError QuestRepository::GetChainLinks(IDatabase& db, uint32_t id, ChainLinks& out)
{
    out = ChainLinks{};
    const std::string idStr = std::to_string(id);
    DbError err;
    if (auto rs = db.Query("SELECT RewardNextQuest FROM quest_template WHERE ID=" + idStr, err))
        if (rs->Next())
            out.rewardNextQuest = rs->GetUInt32(0);
    if (auto rs = db.Query("SELECT PrevQuestID, NextQuestID, BreadcrumbForQuestId FROM "
                           "quest_template_addon WHERE ID=" + idStr, err))
        if (rs->Next())
        {
            out.prevQuestId = rs->GetInt32(0);
            out.nextQuestId = rs->GetUInt32(1);
            out.breadcrumbForQuestId = rs->GetInt32(2);
        }
    return err;
}

DbError QuestRepository::BatchUpdate(IDatabase& db, const std::vector<uint32_t>& ids,
                                     const std::string& column, BatchOp op, int64_t value,
                                     uint32_t& affected)
{
    affected = 0;
    if (ids.empty())
        return DbError{};

    const std::string v = std::to_string(value);
    std::string expr;
    switch (op)
    {
        case BatchOp::Set:          expr = column + " = " + v; break;
        case BatchOp::Add:          expr = column + " = " + column + " + " + v; break;
        case BatchOp::SetFlagBit:   expr = column + " = " + column + " | " + v; break;
        case BatchOp::ClearFlagBit: expr = column + " = " + column + " & ~" + v; break;
    }

    std::string inList;
    for (uint32_t id : ids)
    {
        if (!inList.empty())
            inList += ",";
        inList += std::to_string(id);
    }

    db.BeginTransaction();
    DbError err;
    db.Execute("UPDATE quest_template SET " + expr + " WHERE ID IN (" + inList + ")", err);
    if (!err.ok)
    {
        db.Rollback();
        return err;
    }
    affected = static_cast<uint32_t>(ids.size());
    return db.Commit();
}
} // namespace qe
