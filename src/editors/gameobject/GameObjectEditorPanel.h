#pragma once

// Layer E (ui) — the tabbed gameobject editor panel. Mirrors CreatureEditorPanel.

#include <functional>

namespace we
{
struct GameObjectEditorContext;

struct GameObjectEditorCallbacks
{
    std::function<void()> onNew;
    std::function<void()> onClone;
    std::function<void()> onSave;
    std::function<void()> onRevert;
    std::function<void()> onDelete;
};

class GameObjectEditorPanel
{
public:
    void Draw(GameObjectEditorContext& ctx, bool hasGo, bool dirty,
              const GameObjectEditorCallbacks& cb);
    void SelectTab(int index) { pendingSelect = index; }

private:
    int pendingSelect = -1;
};
} // namespace we
