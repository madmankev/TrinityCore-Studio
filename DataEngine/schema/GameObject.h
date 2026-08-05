#pragma once

// Layer A aggregate: everything the editor knows about a single gameobject entry —
// the gameobject_template row, its 1:1 addon, per-locale strings, quest items, the
// loot slice (keyed by Data1), and world spawns.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "GameObjectTemplate.h"
#include "LootItem.h"

namespace we
{
// gameobject_template_locale (PK entry+locale).
struct GameObjectLocale
{
    std::string locale;          // `locale` varchar(4)
    std::string name;            // `name` mediumtext
    std::string castBarCaption;  // `castBarCaption` mediumtext
};

struct GameObject
{
    GameObjectTemplate tmpl;                        // gameobject_template (always present)
    GameObjectAddon    addon;                       // gameobject_template_addon (.present)
    std::map<std::string, GameObjectLocale> locales; // gameobject_template_locale
    std::vector<uint32_t> questItems;               // gameobject_questitem (ItemId, Idx = order)
    std::vector<LootItem> loot;                     // gameobject_/fishing_loot_template (by tmpl.data[1])
    std::vector<GameObjectSpawn> spawns;            // gameobject (world spawns, by gameobject.id)

    // --- Dirty tracking ---------------------------------------------------
    bool tmplDirty = false;
    bool addonDirty = false;
    bool localesDirty = false;
    bool questItemsDirty = false;
    bool lootDirty = false;
    bool spawnsDirty = false;

    bool isNew = false;

    bool AnyDirty() const
    {
        return tmplDirty || addonDirty || localesDirty || questItemsDirty || lootDirty || spawnsDirty;
    }
    void ClearDirty()
    {
        tmplDirty = addonDirty = localesDirty = questItemsDirty = lootDirty = spawnsDirty = false;
    }

    // Force every part dirty (whole-record write when no per-part flags were set).
    void MarkAllDirty()
    {
        tmplDirty = addonDirty = localesDirty = questItemsDirty = lootDirty = spawnsDirty = true;
    }
};
} // namespace we
