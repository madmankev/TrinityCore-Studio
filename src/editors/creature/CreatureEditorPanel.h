#pragma once

// Layer E (ui) — the tabbed creature editor panel. Header (entry/name, dirty marker,
// New/Clone/Save/Revert/Delete) + creature tabs. Mirrors ItemEditorPanel.

#include <functional>

namespace we
{
struct CreatureEditorContext;

struct CreatureEditorCallbacks
{
    std::function<void()> onNew;
    std::function<void()> onClone;
    std::function<void()> onSave;
    std::function<void()> onRevert;
    std::function<void()> onDelete;
};

class CreatureEditorPanel
{
public:
    void Draw(CreatureEditorContext& ctx, bool hasCreature, bool dirty,
              const CreatureEditorCallbacks& cb);
    void SelectTab(int index) { pendingSelect = index; }

private:
    int pendingSelect = -1;
};
} // namespace we
