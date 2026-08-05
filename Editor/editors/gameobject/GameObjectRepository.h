#pragma once

// Layer C (data) — GameObjectRepository: loads/saves a whole gameobject across
// gameobject_template + addon + locale + questitem, plus the loot slice (by Data1)
// and world spawns. Reuses we::sql and the creature editor's save shapes.

#include <cstdint>
#include <string>
#include <vector>

#include "db/DbTypes.h"
#include "schema/GameObject.h"

namespace we
{
class IDatabase;

struct GameObjectListEntry
{
    uint32_t entry = 0;
    std::string name;
    uint8_t  type = 0;
    uint32_t displayId = 0;
};

struct GameObjectListFilter
{
    std::string text;     // name LIKE or entry exact
    int type = -1;        // gameobject_template.type; -1 = any
    int sortColumn = 0;   // 0 entry, 1 name, 2 type
    bool sortAsc = true;
    int offset = 0;
    int limit = 500;
    bool hasText() const { return !text.empty(); }
};

struct GameObjectReference
{
    std::string source;  // e.g. "Quest 1234"
    std::string detail;
};

class GameObjectRepository
{
public:
    static constexpr int kListLimit = 1000;

    GameObjectRepository() = default;

    DbError ListGameObjects(IDatabase& db, const GameObjectListFilter& filter,
                            std::vector<GameObjectListEntry>& out);
    DbError LoadGameObject(IDatabase& db, uint32_t entry, GameObject& out);
    DbError SaveGameObject(IDatabase& db, const GameObject& go);
    DbError DeleteGameObject(IDatabase& db, uint32_t entry);
    DbError NextFreeGameObjectId(IDatabase& db, uint32_t& out);
    DbError NextFreeGameObjectIdFrom(IDatabase& db, uint32_t minId, uint32_t& out);

    // Where-used: quests linked to this GO (queststarter/questender) + quest objectives
    // (RequiredNpcOrGo == -entry). Capped to kListLimit.
    DbError FindGameObjectReferences(IDatabase& db, uint32_t entry,
                                     std::vector<GameObjectReference>& out);

    enum class BatchOp { Set, Add, SetFlagBit, ClearFlagBit };
    DbError BatchUpdateGameObjects(IDatabase& db, const std::vector<uint32_t>& entries,
                                   const std::string& column, BatchOp op, int64_t value,
                                   uint32_t& affected);

private:
    // Loot (by Data1 + type) and spawns; run inside Load/SaveGameObject.
    DbError LoadAssociated(IDatabase& db, uint32_t entry, GameObject& out);
    bool SaveAssociated(IDatabase& db, const GameObject& go, DbError& err);
};
} // namespace we
