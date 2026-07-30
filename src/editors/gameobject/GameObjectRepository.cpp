#include "editors/gameobject/GameObjectRepository.h"

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
using namespace sql;

namespace
{
bool IsAllDigits(const std::string& s)
{
    if (s.empty())
        return false;
    for (char ch : s)
        if (ch < '0' || ch > '9')
            return false;
    return true;
}

// gameobject_template: 36 columns, DDL order (world_database.sql @1341).
constexpr const char* kGameObjectTemplateCols =
    "entry, type, displayId, name, IconName, castBarCaption, unk1, size, "
    "Data0, Data1, Data2, Data3, Data4, Data5, Data6, Data7, Data8, Data9, Data10, Data11, Data12, "
    "Data13, Data14, Data15, Data16, Data17, Data18, Data19, Data20, Data21, Data22, Data23, "
    "AIName, ScriptName, StringId, VerifiedBuild";

constexpr const char* kAddonCols = "entry, faction, flags, mingold, maxgold, artkit0, artkit1, artkit2, artkit3";
constexpr const char* kLocaleCols = "entry, locale, name, castBarCaption, VerifiedBuild";
constexpr const char* kQuestItemCols = "GameObjectEntry, Idx, ItemId, VerifiedBuild";

bool ExecStep(IDatabase& db, const std::string& sql, DbError& err)
{
    db.Execute(sql, err);
    return err.ok;
}
} // namespace

// ---------------------------------------------------------------------------
DbError GameObjectRepository::ListGameObjects(IDatabase& db, const GameObjectListFilter& filter,
                                              std::vector<GameObjectListEntry>& out)
{
    out.clear();
    std::string sql = "SELECT entry, name, type, displayId FROM gameobject_template";

    std::vector<std::string> conds;
    if (filter.hasText())
    {
        const std::string esc = db.EscapeString(filter.text);
        std::string t = "(name LIKE '%" + esc + "%'";
        if (IsAllDigits(filter.text))
            t += " OR entry = " + filter.text;
        t += ")";
        conds.push_back(t);
    }
    if (filter.type >= 0)
        conds.push_back("type = " + std::to_string(filter.type));
    if (!conds.empty())
    {
        sql += " WHERE ";
        for (size_t i = 0; i < conds.size(); ++i)
            sql += (i ? " AND " : "") + conds[i];
    }

    static const char* kSortCols[] = {"entry", "name", "type"};
    const int sc = (filter.sortColumn >= 0 && filter.sortColumn < 3) ? filter.sortColumn : 0;
    sql += std::string(" ORDER BY ") + kSortCols[sc] + (filter.sortAsc ? " ASC" : " DESC");
    if (sc != 0)
        sql += ", entry ASC";
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
        GameObjectListEntry e;
        e.entry = rs->GetUInt32(0);
        e.name = rs->GetString(1);
        e.type = static_cast<uint8_t>(rs->GetUInt32(2));
        e.displayId = rs->GetUInt32(3);
        out.push_back(std::move(e));
    }
    return err;
}

// ---------------------------------------------------------------------------
DbError GameObjectRepository::LoadGameObject(IDatabase& db, uint32_t entry, GameObject& out)
{
    out = GameObject{};
    const std::string idStr = std::to_string(entry);
    DbError err;

    // --- gameobject_template (required) ----------------------------------
    {
        std::unique_ptr<ResultSet> rs =
            db.Query("SELECT * FROM gameobject_template WHERE entry = " + idStr, err);
        if (!rs)
            return err;
        if (!rs->Next())
        {
            DbError e;
            e.ok = false;
            e.message = "gameobject_template has no row for entry " + idStr;
            return e;
        }
        ResultSet& r = *rs;
        auto U = [&](const char* n) { int c = r.ColumnIndex(n); return c >= 0 ? r.GetUInt32(c) : 0u; };
        auto I = [&](const char* n) { int c = r.ColumnIndex(n); return c >= 0 ? r.GetInt32(c) : 0; };
        auto F = [&](const char* n) { int c = r.ColumnIndex(n); return c >= 0 ? r.GetFloat(c) : 0.0f; };
        auto S = [&](const char* n) { int c = r.ColumnIndex(n); return c >= 0 ? r.GetString(c) : std::string(); };

        GameObjectTemplate& t = out.tmpl;
        t.entry = U("entry");
        t.type = static_cast<uint8_t>(U("type"));
        t.displayId = U("displayId");
        t.name = S("name");
        t.iconName = S("IconName");
        t.castBarCaption = S("castBarCaption");
        t.unk1 = S("unk1");
        t.size = F("size");
        for (int i = 0; i < 24; ++i)
            t.data[i] = I(("Data" + std::to_string(i)).c_str());
        t.aiName = S("AIName");
        t.scriptName = S("ScriptName");
        t.stringId = S("StringId");
        t.verifiedBuild = I("VerifiedBuild");
    }

    // --- gameobject_template_addon (optional) ----------------------------
    if (auto rs = db.Query("SELECT * FROM gameobject_template_addon WHERE entry = " + idStr, err))
        if (rs->Next())
        {
            ResultSet& r = *rs;
            auto U = [&](const char* n) { int c = r.ColumnIndex(n); return c >= 0 ? r.GetUInt32(c) : 0u; };
            auto I = [&](const char* n) { int c = r.ColumnIndex(n); return c >= 0 ? r.GetInt32(c) : 0; };
            GameObjectAddon& a = out.addon;
            a.present = true;
            a.faction = static_cast<uint16_t>(U("faction"));
            a.flags = U("flags");
            a.minGold = U("mingold");
            a.maxGold = U("maxgold");
            for (int i = 0; i < 4; ++i)
                a.artKit[i] = I(("artkit" + std::to_string(i)).c_str());
        }

    // --- gameobject_template_locale --------------------------------------
    if (auto rs = db.Query("SELECT locale, name, castBarCaption FROM gameobject_template_locale WHERE entry = " + idStr, err))
        while (rs->Next())
        {
            GameObjectLocale l;
            l.locale = rs->GetString(0);
            l.name = rs->GetString(1);
            l.castBarCaption = rs->GetString(2);
            if (!l.locale.empty())
                out.locales[l.locale] = std::move(l);
        }

    // --- gameobject_questitem --------------------------------------------
    if (auto rs = db.Query("SELECT ItemId FROM gameobject_questitem WHERE GameObjectEntry = " + idStr + " ORDER BY Idx", err))
        while (rs->Next())
            out.questItems.push_back(rs->GetUInt32(0));

    // --- loot + spawns ---------------------------------------------------
    LoadAssociated(db, entry, out);

    out.ClearDirty();
    out.isNew = false;
    return err;
}

// ---------------------------------------------------------------------------
DbError GameObjectRepository::SaveGameObject(IDatabase& db, const GameObject& go)
{
    const uint32_t id = go.tmpl.entry;
    const std::string idStr = std::to_string(id);

    db.BeginTransaction();
    DbError err;
    auto fail = [&](const DbError& e) -> DbError {
        db.Rollback();
        return e;
    };

    // --- gameobject_template (REPLACE) -----------------------------------
    {
        const GameObjectTemplate& t = go.tmpl;
        ValueList v(db);
        v.UInt(t.entry);
        v.UInt(t.type);
        v.UInt(t.displayId);
        v.Text(t.name);
        v.Text(t.iconName);
        v.Text(t.castBarCaption);
        v.Text(t.unk1);
        v.Float(t.size);
        for (int i = 0; i < 24; ++i)
            v.Int(t.data[i]);
        v.Text(t.aiName);
        v.Text(t.scriptName);
        v.Text(t.stringId);
        v.Int(t.verifiedBuild);

        static const std::vector<std::string> cols = SplitCols(kGameObjectTemplateCols);
        if (cols.size() != v.tokens.size())
        {
            DbError e;
            e.ok = false;
            e.message = "GameObjectRepository: gameobject_template column/value count mismatch (cols=" +
                        std::to_string(cols.size()) + ", vals=" + std::to_string(v.tokens.size()) + ")";
            return fail(e);
        }
        const std::string sql = FilteredInsert("REPLACE", "gameobject_template", cols, v.tokens,
                                               ExistingCols(db, "gameobject_template"));
        if (!ExecStep(db, sql, err))
            return fail(err);
    }

    // --- gameobject_template_addon (upsert-or-delete) --------------------
    if (go.addon.present)
    {
        const GameObjectAddon& a = go.addon;
        ValueList v(db);
        v.UInt(id);
        v.UInt(a.faction);
        v.UInt(a.flags);
        v.UInt(a.minGold);
        v.UInt(a.maxGold);
        for (int i = 0; i < 4; ++i)
            v.Int(a.artKit[i]);
        static const std::vector<std::string> cols = SplitCols(kAddonCols);
        const std::string sql = FilteredUpsert("gameobject_template_addon", cols, v.tokens,
                                               ExistingCols(db, "gameobject_template_addon"), "entry");
        if (!ExecStep(db, sql, err))
            return fail(err);
    }
    else if (!ExecStep(db, "DELETE FROM gameobject_template_addon WHERE entry = " + idStr, err))
        return fail(err);

    // --- gameobject_template_locale (delete-then-insert) -----------------
    if (!ExecStep(db, "DELETE FROM gameobject_template_locale WHERE entry = " + idStr, err))
        return fail(err);
    {
        static const std::vector<std::string> cols = SplitCols(kLocaleCols);
        const std::set<std::string> existing = ExistingCols(db, "gameobject_template_locale");
        for (const auto& kv : go.locales)
        {
            const GameObjectLocale& l = kv.second;
            if (l.name.empty() && l.castBarCaption.empty())
                continue;
            ValueList v(db);
            v.UInt(id);
            v.Text(kv.first);
            v.Text(l.name);
            v.Text(l.castBarCaption);
            v.Int(0);
            if (!ExecStep(db, FilteredInsert("INSERT", "gameobject_template_locale", cols, v.tokens, existing), err))
                return fail(err);
        }
    }

    // --- gameobject_questitem (delete-then-insert) -----------------------
    if (!ExecStep(db, "DELETE FROM gameobject_questitem WHERE GameObjectEntry = " + idStr, err))
        return fail(err);
    {
        static const std::vector<std::string> cols = SplitCols(kQuestItemCols);
        const std::set<std::string> existing = ExistingCols(db, "gameobject_questitem");
        for (int i = 0; i < static_cast<int>(go.questItems.size()); ++i)
        {
            if (go.questItems[i] == 0)
                continue;
            ValueList v(db);
            v.UInt(id);
            v.UInt(i);              // Idx
            v.UInt(go.questItems[i]);
            v.Int(0);
            if (!ExecStep(db, FilteredInsert("INSERT", "gameobject_questitem", cols, v.tokens, existing), err))
                return fail(err);
        }
    }

    // --- loot + spawns ---------------------------------------------------
    if (!SaveAssociated(db, go, err))
        return fail(err);

    return db.Commit();
}

// ---------------------------------------------------------------------------
DbError GameObjectRepository::DeleteGameObject(IDatabase& db, uint32_t entry)
{
    const std::string idStr = std::to_string(entry);
    db.BeginTransaction();
    DbError err;

    struct DelSpec { const char* table; const char* keyCol; };
    const DelSpec specs[] = {
        {"gameobject_template_locale", "entry"},
        {"gameobject_questitem", "GameObjectEntry"},
        {"gameobject_queststarter", "id"},
        {"gameobject_questender", "id"},
        {"gameobject_template_addon", "entry"},
        {"gameobject", "id"},   // world spawns of this entry
        {"gameobject_template", "entry"},
    };
    for (const DelSpec& s : specs)
    {
        db.Execute(std::string("DELETE FROM ") + s.table + " WHERE " + s.keyCol + " = " + idStr, err);
        if (!err.ok)
        {
            db.Rollback();
            return err;
        }
    }
    return db.Commit();
}

// ---------------------------------------------------------------------------
DbError GameObjectRepository::NextFreeGameObjectId(IDatabase& db, uint32_t& out)
{
    out = 0;
    DbError err;
    std::unique_ptr<ResultSet> rs =
        db.Query("SELECT COALESCE(MAX(entry),0)+1 FROM gameobject_template", err);
    if (!rs)
        return err;
    if (rs->Next())
        out = rs->GetUInt32(0);
    return err;
}

DbError GameObjectRepository::NextFreeGameObjectIdFrom(IDatabase& db, uint32_t minId, uint32_t& out)
{
    out = minId;
    DbError err;
    std::unique_ptr<ResultSet> rs = db.Query(
        "SELECT COALESCE(MAX(entry),0) FROM gameobject_template WHERE entry >= " + std::to_string(minId), err);
    if (!rs)
        return err;
    if (rs->Next())
    {
        uint32_t maxInRange = rs->GetUInt32(0);
        out = (maxInRange >= minId) ? maxInRange + 1 : minId;
    }
    return err;
}

// Associated load/save + where-used + batch live in GameObjectRepositoryAssociated.cpp.
} // namespace qe
