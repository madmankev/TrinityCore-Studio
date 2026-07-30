#pragma once

// Layer E (ui) — creature editor tab entry points. Each is a free function taking the
// per-frame CreatureEditorContext by reference. Bodies live in editors/creature/tabs/*.

namespace qe
{
struct CreatureEditorContext;

// Core record tabs (Stage 6).
void DrawCreatureGeneralTab(CreatureEditorContext&);
void DrawCreatureStatsTab(CreatureEditorContext&);
void DrawCreatureCombatTab(CreatureEditorContext&);
void DrawCreatureFlagsTab(CreatureEditorContext&);
void DrawCreatureTypeLootTab(CreatureEditorContext&);
void DrawCreatureMovementTab(CreatureEditorContext&);
void DrawCreatureScriptingTab(CreatureEditorContext&);
void DrawCreatureAddonTab(CreatureEditorContext&);
void DrawCreatureEquipmentTab(CreatureEditorContext&);
void DrawCreatureLocalesTab(CreatureEditorContext&);

// Associated-system tabs (Stage 7).
void DrawCreatureVendorTab(CreatureEditorContext&);
void DrawCreatureTrainerTab(CreatureEditorContext&);
void DrawCreatureLootTab(CreatureEditorContext&);
void DrawCreatureSpawnsTab(CreatureEditorContext&);
} // namespace qe
