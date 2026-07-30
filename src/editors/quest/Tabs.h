#pragma once

// Layer E (ui) — the tab contract Wave 4 fills in. QuestEditorPanel calls these
// per frame with the current QuestEditorContext. Signatures MUST stay stable; only
// the bodies (in ui/tabs/*.cpp) change.

namespace qe
{
struct QuestEditorContext;

void DrawGeneralTab(QuestEditorContext&);
void DrawRequirementsTab(QuestEditorContext&);
void DrawObjectivesTab(QuestEditorContext&);
void DrawRewardsTab(QuestEditorContext&);
void DrawTextTab(QuestEditorContext&);
void DrawChainTab(QuestEditorContext&);
void DrawQuestgiversTab(QuestEditorContext&);
void DrawPoiTab(QuestEditorContext&);
void DrawLocalesTab(QuestEditorContext&);
void DrawConditionsTab(QuestEditorContext&);
} // namespace qe
