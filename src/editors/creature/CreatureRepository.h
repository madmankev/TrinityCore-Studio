#pragma once

// Layer C (data) — CreatureRepository: loads/saves a whole creature across
// creature_template + its child tables (addon, movement, resistance, spell, equip,
// locale) AND the four associated systems keyed by the entry (vendor, trainer, loot,
// spawns), via the IDatabase seam. Reuses the schema-adaptive we::sql helpers and the
// Quest editor's save shapes (REPLACE / upsert-or-delete / delete-then-insert).

#include <cstdint>
#include <string>
#include <vector>

#include "db/DbTypes.h"
#include "schema/Creature.h"

namespace we
{
class IDatabase;

// A row in the creature browser list (projection of creature_template).
struct CreatureListEntry
{
    uint32_t entry = 0;
    std::string name;
    std::string subname;
    uint8_t  type = 0;
    uint8_t  rank = 0;
    uint8_t  minLevel = 0;
    uint8_t  maxLevel = 0;
};

struct CreatureListFilter
{
    std::string text;         // name LIKE or entry exact
    int type = -1;            // creature_template.type; -1 = any
    int rank = -1;            // creature_template.rank; -1 = any
    int minLevel = 0;         // minlevel >= (0 = no bound)
    int maxLevel = 0;         // maxlevel <= (0 = no bound)

    // Sort: 0 entry, 1 name, 2 type, 3 rank, 4 minlevel.
    int sortColumn = 0;
    bool sortAsc = true;
    int offset = 0;
    int limit = 500;

    bool hasText() const { return !text.empty(); }
};

// One "where-used" hit for a creature entry.
struct CreatureReference
{
    std::string source;  // e.g. "Quest 1234", "creature_queststarter"
    std::string detail;  // quest title / description
};

class CreatureRepository
{
public:
    static constexpr int kListLimit = 1000;

    CreatureRepository() = default;

    DbError ListCreatures(IDatabase& db, const CreatureListFilter& filter,
                          std::vector<CreatureListEntry>& out);

    // Loads creature_template + all child tables + all associated systems.
    DbError LoadCreature(IDatabase& db, uint32_t entry, Creature& out);

    // Persists the whole creature (record + children + associated) in one transaction.
    DbError SaveCreature(IDatabase& db, const Creature& c);

    DbError DeleteCreature(IDatabase& db, uint32_t entry);

    DbError NextFreeCreatureId(IDatabase& db, uint32_t& out);
    DbError NextFreeCreatureIdFrom(IDatabase& db, uint32_t minId, uint32_t& out);

    // Where-used: quests that reference this creature (RequiredNpcOrGo, creature_
    // queststarter/questender, KillCredit). Capped to kListLimit.
    DbError FindCreatureReferences(IDatabase& db, uint32_t entry,
                                   std::vector<CreatureReference>& out);

    enum class BatchOp { Set, Add, SetFlagBit, ClearFlagBit };
    DbError BatchUpdateCreatures(IDatabase& db, const std::vector<uint32_t>& entries,
                                 const std::string& column, BatchOp op, int64_t value,
                                 uint32_t& affected);

private:
    // Load/save the four associated systems (called by Load/SaveCreature). `db` write
    // helpers run inside SaveCreature's transaction.
    DbError LoadAssociated(IDatabase& db, uint32_t entry, Creature& out);
    bool SaveAssociated(IDatabase& db, const Creature& c, DbError& err);
};
} // namespace we
