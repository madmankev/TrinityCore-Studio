#include "ui/main_window.h"

#include "core/commands.h"
#include "scripting/api_bindings.h"
#include "utils/logger.h"

namespace wowedit
{
MainWindow::MainWindow()
    : terrainEditor_(commands_, &events_), terrainTools_(terrainEditor_), roadPathTool_(commands_, &events_),
      roadPathPanel_(roadPathTool_), chunkClipboard_(&commands_), chunkOperations_(chunkClipboard_),
      doodads_(&commands_, &events_), creatures_(&commands_, &events_),
      creatureEditor_(creatures_), propertyPanel_(commands_), debugPanel_(Logger::instance(), profiler_)
{
    RegisterCoreApiBindings(scripts_, database_, commands_);
    toolbar_.setChangedCallback([this](ActiveTool) { events_.publish({EventType::ToolActivated, 0, "Toolbar", {}}); });
    bindMenuActions();
    newMap("Untitled World");
}

bool MainWindow::newMap(const std::string& name, std::uint32_t resolution)
{
    if (resolution < 2) return false;
    project_ = {};
    project_.name = name.empty() ? "Untitled World" : name;
    project_.tiles.emplace_back(0, project_.name, 0, 0, resolution);
    project_.settings = {{"activeTool", "Select"}, {"visiblePanels", {"contentBrowser", "properties", "terrainTools"}}};
    commands_.clearHistory(); projectPath_.clear();
    chunkClipboard_.setSource(activeChunk()); chunkClipboard_.setTarget(activeChunk());
    events_.publish({EventType::MapOpened, 0, "MainWindow", project_.name});
    return true;
}

bool MainWindow::openMap(const std::string& path, std::string& error)
{
    WorldProject loaded; MapSerializer serializer;
    if (!serializer.load(path, loaded, error)) return false;
    project_ = std::move(loaded); projectPath_ = path; commands_.clearHistory();
    chunkClipboard_.setSource(activeChunk()); chunkClipboard_.setTarget(activeChunk());
    events_.publish({EventType::MapOpened, 0, "MainWindow", project_.name}); return true;
}

bool MainWindow::saveMap(std::string& error)
{
    if (projectPath_.empty()) { error = "Choose a .wowedit path with Save As"; return false; }
    return saveMapAs(projectPath_, error);
}

bool MainWindow::saveMapAs(const std::string& path, std::string& error)
{
    MapSerializer serializer; if (!serializer.save(project_, path, error)) return false;
    projectPath_ = path; for (MapTile& tile : project_.tiles) tile.dirty = false;
    events_.publish({EventType::MapSaved, 0, "MainWindow", path}); return true;
}

bool MainWindow::invokeMenu(const std::string& actionId)
{
    lastAction_ = actionId;
    if (menuBar_.trigger(actionId)) return true;
    if (!menuBar_.contains(actionId)) return false;
    // Unbound actions are still observable by dialogs/plugins. This makes every
    // command addressable now rather than silently dropping menu selections.
    events_.publish({EventType::ToolActivated, 0, "MenuBar", actionId});
    return true;
}

void MainWindow::update(float deltaSeconds)
{
    ProfileScope profile(profiler_, "MainWindow::update");
    spellPreviewer_.update(deltaSeconds);
    events_.dispatchQueued();
}

TerrainChunk* MainWindow::activeChunk() { return project_.tiles.empty() ? nullptr : &project_.tiles.front().terrain; }

void MainWindow::bindMenuActions()
{
    menuBar_.bind("edit.undo", [this] { commands_.undo(); events_.publish({EventType::UndoPerformed, 0, "MenuBar", {}}); });
    menuBar_.bind("edit.redo", [this] { commands_.redo(); events_.publish({EventType::RedoPerformed, 0, "MenuBar", {}}); });
    menuBar_.bind("window.content_browser", [this] { togglePanel(&PanelVisibility::contentBrowser); });
    menuBar_.bind("window.properties", [this] { togglePanel(&PanelVisibility::properties); });
    menuBar_.bind("window.hierarchy", [this] { togglePanel(&PanelVisibility::hierarchy); });
    menuBar_.bind("window.terrain_tools", [this] { togglePanel(&PanelVisibility::terrainTools); });
    menuBar_.bind("window.road_path", [this] { togglePanel(&PanelVisibility::roadPath); });
    menuBar_.bind("terrain.road", [this] { panels_.roadPath = true; });
    menuBar_.bind("window.texture_palette", [this] { togglePanel(&PanelVisibility::texturePalette); });
    menuBar_.bind("window.doodad_browser", [this] { togglePanel(&PanelVisibility::doodadBrowser); });
    menuBar_.bind("window.creature_editor", [this] { togglePanel(&PanelVisibility::creatureEditor); });
    menuBar_.bind("window.quest", [this] { togglePanel(&PanelVisibility::questIntegration); });
    menuBar_.bind("window.spell", [this] { togglePanel(&PanelVisibility::spellPreviewer); });
    menuBar_.bind("window.chunk", [this] { togglePanel(&PanelVisibility::chunkOperations); });
    menuBar_.bind("window.debug", [this] { togglePanel(&PanelVisibility::debug); });
    menuBar_.bind("window.log", [this] { togglePanel(&PanelVisibility::outputLog); });
    menuBar_.bind("spells.previewer", [this] { panels_.spellPreviewer = true; });
    menuBar_.bind("chunk.rotate_cw", [this] { chunkClipboard_.rotateClockwise(); panels_.chunkOperations = true; });
    menuBar_.bind("chunk.rotate_ccw", [this] { chunkClipboard_.rotateCounterClockwise(); panels_.chunkOperations = true; });
    menuBar_.bind("chunk.mirror_horizontal", [this] { chunkClipboard_.setMirrorHorizontal(true); panels_.chunkOperations = true; });
    menuBar_.bind("chunk.mirror_vertical", [this] { chunkClipboard_.setMirrorVertical(true); panels_.chunkOperations = true; });
    menuBar_.bind("view.camera_reset", [this] { viewport_.resetCamera(); events_.publish({EventType::CameraMoved, 0, "MenuBar", {}}); });
    menuBar_.bind("view.textured", [this] { viewport_.setDisplayMode(ViewportDisplayMode::Textured); });
    menuBar_.bind("view.wireframe", [this] { viewport_.setDisplayMode(ViewportDisplayMode::Wireframe); });
    menuBar_.bind("view.collision", [this] { viewport_.setDisplayMode(ViewportDisplayMode::CollisionMesh); });
    menuBar_.bind("objects.select", [this] { toolbar_.setActiveTool(ActiveTool::Select); });
    menuBar_.bind("objects.move", [this] { toolbar_.setActiveTool(ActiveTool::Move); });
    menuBar_.bind("objects.rotate", [this] { toolbar_.setActiveTool(ActiveTool::Rotate); });
    menuBar_.bind("objects.scale", [this] { toolbar_.setActiveTool(ActiveTool::Scale); });
    menuBar_.bind("terrain.raise_lower", [this] { toolbar_.setActiveTool(ActiveTool::TerrainRaise); });
    menuBar_.bind("terrain.smooth", [this] { toolbar_.setActiveTool(ActiveTool::TerrainSmooth); });
    menuBar_.bind("terrain.flatten", [this] { toolbar_.setActiveTool(ActiveTool::TerrainFlatten); });
    menuBar_.bind("texture.paint", [this] { toolbar_.setActiveTool(ActiveTool::TexturePaint); });
}

void MainWindow::togglePanel(bool PanelVisibility::*member) { panels_.*member = !(panels_.*member); }
} // namespace wowedit
