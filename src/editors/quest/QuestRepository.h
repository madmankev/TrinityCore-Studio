#pragma once

// Layer C (data) — QuestRepository: loads/saves a whole quest across every
// related table via the IDatabase seam. See docs/SPEC.md §5/§6.
//
// All SQL is built as escaped literal statements (no prepared statements) so
// the same code path works for both the Live and SqlExport backends, which
// only observe Query()/Execute(). String values are escaped via
// IDatabase::EscapeString and wrapped in single quotes; numerics are formatted
// directly. Load populates the aggregate Quest and clears all dirty flags;
// Save wraps every write in BeginTransaction/Commit and rolls back on error.

#include <cstdint>
#include <string>
#include <vector>

#include "db/DbTypes.h"
#include "schema/Quest.h"

namespace we
{
class IDatabase;

// A single row in the quest browser list (a projection of quest_template).
struct QuestListEntry
{
    uint32_t id = 0;
    std::string title;        // LogTitle
    int16_t questSortId = 0;  // QuestSortID (<0 zone, >0 QuestSort)
    uint16_t questInfoId = 0; // QuestInfoID
    uint8_t minLevel = 0;     // MinLevel
    int16_t questLevel = 0;   // QuestLevel
};

// Filter for ListQuests. Text is matched against the quest ID (exact) or the
// LogTitle (substring LIKE); with searchBody it also matches the description/
// objective text columns. All numeric filters are optional. Results are sorted
// server-side by `sortColumn`/`sortAsc` and paged via `offset`/`limit`.
struct QuestListFilter
{
    std::string text;
    bool searchBody = false;          // also LIKE the description/objective columns

    int questInfoId = -1;             // QuestInfoID; -1 = any
    bool hasSortId = false;           // enable the QuestSortID filter
    int questSortId = 0;              // QuestSortID exact value (when hasSortId)
    int minLevel = 0;                 // QuestLevel >= (0 = no bound)
    int maxLevel = 0;                 // QuestLevel <= (0 = no bound)

    // Sort column: 0 ID, 1 LogTitle, 2 QuestLevel, 3 QuestSortID, 4 MinLevel, 5 QuestInfoID.
    int sortColumn = 0;
    bool sortAsc = true;

    int offset = 0;                   // paging offset (rows)
    int limit = 500;                  // page size (clamped to kListLimit)

    bool hasText() const { return !text.empty(); }
};

class QuestRepository
{
public:
    // Maximum number of rows ListQuests returns in one call (keeps the browser
    // responsive on the full ~30k-row quest_template).
    static constexpr int kListLimit = 1000;

    QuestRepository() = default;

    // List quests (optionally filtered). Ordered by ID, capped to kListLimit.
    DbError ListQuests(IDatabase& db, const QuestListFilter& filter,
                       std::vector<QuestListEntry>& out);

    // Load the full quest with the given ID into `out`. The quest_template row
    // is required; if it is missing a failing DbError is returned and `out` is
    // left unspecified. Optional sub-rows set their `present` flag accordingly.
    // On success all dirty flags and isNew are cleared.
    DbError LoadQuest(IDatabase& db, uint32_t id, Quest& out);

    // Persist the entire quest across all related tables inside one transaction.
    // Single-PK tables use REPLACE (or DELETE when an optional part is absent);
    // multi-row/relationship tables are delete-then-insert. Rolls back and
    // returns the error on the first failing statement.
    DbError SaveQuest(IDatabase& db, const Quest& quest);

    // Delete a quest and all of its related rows inside one transaction.
    DbError DeleteQuest(IDatabase& db, uint32_t id);

    // Compute the next free quest_template ID (MAX(ID)+1, 1 on empty table).
    DbError NextFreeQuestId(IDatabase& db, uint32_t& out);

    // Next free ID at or above `minId` (MAX(ID)+1 among rows with ID >= minId, or
    // minId when none exist yet). Used for creating quests in a custom ID range.
    DbError NextFreeQuestIdFrom(IDatabase& db, uint32_t minId, uint32_t& out);

    // --- Reverse references ("where used") --------------------------------
    enum class ReferenceKind { Item, Creature, GameObject, Quest };
    // Find quests that reference the given entry id (as a reward/required item,
    // questgiver creature/GO, RequiredNpcOrGo, or chain link). Returns matching
    // quests as browser entries, ordered by ID, capped to kListLimit.
    DbError FindReferences(IDatabase& db, ReferenceKind kind, uint32_t id,
                           std::vector<QuestListEntry>& out);

    // --- Questgiver spawn check -------------------------------------------
    struct SpawnInfo
    {
        uint32_t count = 0;    // rows in creature/gameobject spawn table
        uint16_t map = 0;      // a sample spawn's map
        float x = 0, y = 0, z = 0;
    };
    DbError CountCreatureSpawns(IDatabase& db, uint32_t entry, SpawnInfo& out);
    DbError CountGameObjectSpawns(IDatabase& db, uint32_t entry, SpawnInfo& out);

    // --- Chain links (for the chain graph) --------------------------------
    struct ChainLinks
    {
        int32_t prevQuestId = 0;
        uint32_t nextQuestId = 0;
        uint32_t rewardNextQuest = 0;
        int32_t breadcrumbForQuestId = 0;
    };
    DbError GetChainLinks(IDatabase& db, uint32_t id, ChainLinks& out);

    // --- Batch edit -------------------------------------------------------
    enum class BatchOp { Set, Add, SetFlagBit, ClearFlagBit };
    // Apply a single-column update to many quests at once, in one transaction.
    // `column` must be a plain quest_template column name (caller-controlled, from a
    // fixed UI list — not user free-text). Returns rows affected in `affected`.
    DbError BatchUpdate(IDatabase& db, const std::vector<uint32_t>& ids, const std::string& column,
                        BatchOp op, int64_t value, uint32_t& affected);
};
} // namespace we
