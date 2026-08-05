#pragma once

// Layer E (ui) — the tabbed quest editor panel. Renders a header row (ID/title,
// dirty indicator, New/Save/Revert/Delete) and the nine editor tabs. Editing
// actions are surfaced to the App via std::function callbacks; the App owns the
// Quest and dirty state.

#include <functional>

namespace we
{
struct QuestEditorContext;

// Actions the header buttons and File menu invoke on the App.
struct QuestEditorCallbacks
{
    std::function<void()> onNew;
    std::function<void()> onClone;   // duplicate current quest to a new free id
    std::function<void()> onSave;
    std::function<void()> onRevert;
    std::function<void()> onDelete;
};

class QuestEditorPanel
{
public:
    // Draws the panel. `hasQuest` gates the editor body and Save/Revert/Delete;
    // `dirty` drives the '*' indicator. Any widget edit is reflected in
    // ctx.changed, which the caller folds back into its dirty flag.
    void Draw(QuestEditorContext& ctx, bool hasQuest, bool dirty, const QuestEditorCallbacks& cb);

    // Programmatically select a tab by index (0..8) on the next Draw (used by
    // --tab demo mode). Consumed once.
    void SelectTab(int index) { pendingSelect = index; }

private:
    int pendingSelect = -1;
};
} // namespace we
