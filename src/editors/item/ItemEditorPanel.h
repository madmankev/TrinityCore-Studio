#pragma once

// Layer E (ui) — the tabbed item editor panel. Header row (entry id / name, dirty
// indicator, New/Clone/Save/Revert/Delete) plus the item editor tabs. Editing
// actions surface to the module via std::function callbacks; the module owns the
// Item and dirty state. Mirrors QuestEditorPanel.

#include <functional>

namespace we
{
struct ItemEditorContext;

struct ItemEditorCallbacks
{
    std::function<void()> onNew;
    std::function<void()> onClone;
    std::function<void()> onSave;
    std::function<void()> onRevert;
    std::function<void()> onDelete;
};

class ItemEditorPanel
{
public:
    // `hasItem` gates the editor body and Clone/Save/Revert/Delete; `dirty` drives
    // the '*' indicator. Any widget edit is reflected in ctx.changed.
    void Draw(ItemEditorContext& ctx, bool hasItem, bool dirty, const ItemEditorCallbacks& cb);

    // Programmatically select a tab by index on the next Draw. Consumed once.
    void SelectTab(int index) { pendingSelect = index; }

private:
    int pendingSelect = -1;
};
} // namespace we
