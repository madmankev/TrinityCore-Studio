#pragma once

// Layer E (ui) — item editor tab entry points. Each tab is a free function taking
// the per-frame ItemEditorContext by reference (signatures stay stable; only bodies
// change). Bodies live in editors/item/tabs/*.cpp.

namespace qe
{
struct ItemEditorContext;

void DrawItemGeneralTab(ItemEditorContext&);
void DrawItemFlagsTab(ItemEditorContext&);
void DrawItemRequirementsTab(ItemEditorContext&);
void DrawItemStatsTab(ItemEditorContext&);
void DrawItemWeaponArmorTab(ItemEditorContext&);
void DrawItemSpellsTab(ItemEditorContext&);
void DrawItemSocketsTab(ItemEditorContext&);
void DrawItemTextSetTab(ItemEditorContext&);
void DrawItemLocalesTab(ItemEditorContext&);
} // namespace qe
