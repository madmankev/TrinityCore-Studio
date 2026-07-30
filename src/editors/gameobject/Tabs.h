#pragma once

// Layer E (ui) — gameobject editor tab entry points. Bodies in editors/gameobject/tabs/*.

namespace qe
{
struct GameObjectEditorContext;

void DrawGameObjectGeneralTab(GameObjectEditorContext&);
void DrawGameObjectDataTab(GameObjectEditorContext&);
void DrawGameObjectAddonTab(GameObjectEditorContext&);
void DrawGameObjectLocalesTab(GameObjectEditorContext&);
void DrawGameObjectQuestItemsTab(GameObjectEditorContext&);
void DrawGameObjectLootTab(GameObjectEditorContext&);
void DrawGameObjectSpawnsTab(GameObjectEditorContext&);
} // namespace qe
