#pragma once

// ItemTablesModule — a GroupedDbcModule editing the item-reference DBCs (Item.dbc +
// ItemDisplayInfo, ItemSet, ItemExtendedCost, ItemRandomProperties/Suffix, GemProperties,
// ItemLimitCategory, ItemBagFamily, ItemPetFood). One rail entry, a dropdown per table. The
// item-side twin of SpellTablesModule; Item.dbc here is the same overlay the Item editor
// projects to on save.

#include "editors/common/GroupedDbcModule.h"

namespace we
{
const std::vector<DbcTableDef>& ItemTableDefs();  // also used by --itemtables-roundtrip

class ItemTablesModule final : public GroupedDbcModule
{
public:
    const char* Id() const override { return "itemtables"; }
    const char* DisplayName() const override { return "Item Tables"; }
    const char* RailGlyph() const override { return "i"; }

protected:
    const std::vector<DbcTableDef>& Tables() const override { return ItemTableDefs(); }
    const char* BrowserTitle() const override { return "Item Table Browser"; }
    const char* EditorTitle() const override { return "Item Table Editor"; }
};
} // namespace we
