#pragma once

#include "core/command.h"
#include "core/event_system.h"
#include "creatures/creature_spawner.h"
#include "data/game_database.h"
#include "editing/world_chunk_clipboard.h"
#include "objects/doodad_manager.h"
#include "io/map_serializer.h"
#include "scripting/script_engine.h"
#include "terrain/terrain_editor.h"
#include "ui/chunk_operations_panel.h"
#include "ui/content_browser.h"
#include "ui/creature_editor_panel.h"
#include "ui/debug_panel.h"
#include "ui/menu_bar.h"
#include "ui/property_panel.h"
#include "ui/spell_effect_previewer.h"
#include "ui/terrain_tools_panel.h"
#include "ui/theme_manager.h"
#include "ui/toolbar.h"
#include "ui/ui_types.h"
#include "ui/viewport_panel.h"
#include "utils/profiler.h"

#include <string>

namespace wowedit
{
/** MVC composition root. A Dear ImGui/Qt frontend reads these controllers and
 * calls invokeMenu(); document services remain testable without a graphics device. */
class MainWindow
{
public:
    MainWindow();

    bool newMap(const std::string& name, std::uint32_t resolution = 256);
    bool openMap(const std::string& path, std::string& error);
    bool saveMap(std::string& error);
    bool saveMapAs(const std::string& path, std::string& error);
    bool invokeMenu(const std::string& actionId);
    void update(float deltaSeconds);

    WorldProject& project() { return project_; }
    const WorldProject& project() const { return project_; }
    CommandManager& commands() { return commands_; }
    EventBus& events() { return events_; }
    MenuBar& menuBar() { return menuBar_; }
    ViewportPanel& viewport() { return viewport_; }
    Toolbar& toolbar() { return toolbar_; }
    PanelVisibility& panels() { return panels_; }
    SpellEffectPreviewer& spellPreviewer() { return spellPreviewer_; }
    std::string lastAction() const { return lastAction_; }

private:
    TerrainChunk* activeChunk();
    void bindMenuActions();
    void togglePanel(bool PanelVisibility::*member);

    CommandManager commands_;
    EventBus events_;
    GameDatabase database_;
    ScriptEngine scripts_;
    WorldProject project_;
    std::string projectPath_;
    MenuBar menuBar_;
    ViewportPanel viewport_;
    Toolbar toolbar_;
    PanelVisibility panels_;
    TerrainEditor terrainEditor_;
    TerrainToolsPanel terrainTools_;
    WorldChunkClipboard chunkClipboard_;
    ChunkOperationsPanel chunkOperations_;
    ContentBrowser contentBrowser_;
    DoodadManager doodads_;
    CreatureSpawnerRegistry creatures_;
    CreatureEditorPanel creatureEditor_;
    PropertyPanel propertyPanel_;
    SpellEffectPreviewer spellPreviewer_;
    Profiler profiler_;
    DebugPanel debugPanel_;
    ThemeManager themes_;
    std::string lastAction_;
};
} // namespace wowedit
