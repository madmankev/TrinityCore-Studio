#pragma once

// Layer C (data) — ItemRepository: loads/saves a whole item across item_template
// (+ item_template_locale) via the IDatabase seam. Mirrors QuestRepository but is
// simpler: item_template is a single-PK table, so the main row is a REPLACE and the
// only child is the locale table (delete-then-insert). Reuses the schema-adaptive
// qe::sql helpers (Row/ValueList/SplitCols/ExistingCols/FilteredInsert).

#include <cstdint>
#include <string>
#include <vector>

#include "db/DbTypes.h"
#include "schema/Item.h"

namespace qe
{
class IDatabase;

// A single row in the item browser list (a projection of item_template).
struct ItemListEntry
{
    uint32_t entry = 0;
    std::string name;
    uint8_t  cls = 0;           // class
    uint8_t  subclass = 0;
    uint8_t  quality = 0;       // Quality
    uint8_t  inventoryType = 0; // InventoryType
    uint16_t itemLevel = 0;     // ItemLevel
    uint8_t  requiredLevel = 0; // RequiredLevel
};

// Filter for ListItems. Text matches the entry (exact) or the name (substring LIKE).
// Numeric/enum filters are optional (-1/0 = any). Results are sorted server-side by
// `sortColumn`/`sortAsc` and paged via `offset`/`limit`.
struct ItemListFilter
{
    std::string text;

    int itemClass = -1;      // class; -1 = any
    int subclass = -1;       // subclass; -1 = any (only meaningful together with itemClass)
    int quality = -1;        // Quality; -1 = any
    int inventoryType = -1;  // InventoryType; -1 = any
    int minItemLevel = 0;    // ItemLevel >= (0 = no bound)
    int maxItemLevel = 0;    // ItemLevel <= (0 = no bound)

    // Sort column: 0 entry, 1 name, 2 class, 3 subclass, 4 Quality, 5 InventoryType,
    // 6 ItemLevel, 7 RequiredLevel.
    int sortColumn = 0;
    bool sortAsc = true;

    int offset = 0;
    int limit = 500;

    bool hasText() const { return !text.empty(); }
};

// One "where-used" hit for an item (a row in some table that references the entry).
struct ItemReference
{
    std::string source;  // e.g. "Quest 1234", "npc_vendor", "creature_loot_template"
    std::string detail;  // e.g. the quest title, or "creature 567", "entry 123"
};

class ItemRepository
{
public:
    static constexpr int kListLimit = 1000;

    ItemRepository() = default;

    // List items (optionally filtered). Ordered server-side, capped to kListLimit.
    DbError ListItems(IDatabase& db, const ItemListFilter& filter,
                      std::vector<ItemListEntry>& out);

    // Load the full item with the given entry into `out`. The item_template row is
    // required; a missing row returns a failing DbError. On success dirty/isNew clear.
    DbError LoadItem(IDatabase& db, uint32_t entry, Item& out);

    // Persist the whole item inside one transaction: REPLACE item_template, then
    // delete-then-insert item_template_locale. Rolls back on the first failure.
    DbError SaveItem(IDatabase& db, const Item& item);

    // Delete an item and its locale rows inside one transaction.
    DbError DeleteItem(IDatabase& db, uint32_t entry);

    // Next free item_template entry (MAX(entry)+1, 1 on empty table).
    DbError NextFreeItemId(IDatabase& db, uint32_t& out);

    // Next free entry at or above `minId` (for creating items in a custom id range).
    DbError NextFreeItemIdFrom(IDatabase& db, uint32_t minId, uint32_t& out);

    // --- Where-used -------------------------------------------------------
    // Find rows that reference the given item entry: quests (reward/choice/required/
    // drop/start), npc_vendor, and the common *_loot_template tables. Missing tables
    // are skipped silently. Capped to kListLimit total.
    DbError FindItemReferences(IDatabase& db, uint32_t entry, std::vector<ItemReference>& out);

    // --- Batch edit -------------------------------------------------------
    enum class BatchOp { Set, Add, SetFlagBit, ClearFlagBit };
    // Apply a single-column update to many items at once, in one transaction.
    // `column` must be a plain item_template column name (from a fixed UI list, not
    // user free-text). Returns rows affected in `affected`.
    DbError BatchUpdateItems(IDatabase& db, const std::vector<uint32_t>& entries,
                             const std::string& column, BatchOp op, int64_t value,
                             uint32_t& affected);
};
} // namespace qe
