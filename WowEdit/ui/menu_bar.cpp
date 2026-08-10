#include "ui/menu_bar.h"

namespace wowedit
{
namespace
{
MenuEntry Item(const char* label, const char* id = "", const char* shortcut = "") { return {label, id, shortcut, {}, false}; }
MenuEntry Sep() { MenuEntry item; item.separator = true; return item; }
MenuEntry Menu(const char* label, std::initializer_list<MenuEntry> children) { MenuEntry item; item.label = label; item.children = children; return item; }
} // namespace

MenuBar::MenuBar()
{
    // This complete command tree is renderer-independent. Dear ImGui, Qt, and a
    // future native menu backend all draw the same menu model and dispatch actionId.
    menus_ = {
        Menu("File", {
            Item("New Map...", "file.new_map"), Item("Open Map...", "file.open_map", "Ctrl+O"),
            Menu("Open Recent", {Item("Recent Project", "file.open_recent")}), Item("Save", "file.save", "Ctrl+S"),
            Item("Save As...", "file.save_as", "Ctrl+Shift+S"), Item("Save All", "file.save_all"), Item("Revert to Saved", "file.revert"), Sep(),
            Menu("Import", {Item("Heightmap Image...", "file.import_heightmap"), Item("Model (.m2, .obj, .fbx)...", "file.import_model"),
                            Item("Texture (.blp, .png, .tga)...", "file.import_texture"), Item("Creature Data...", "file.import_creatures"), Item("Quest Data...", "file.import_quests")}),
            Menu("Export", {Item("Heightmap Image...", "file.export_heightmap"), Item("Object List (CSV)...", "file.export_objects"),
                            Item("Screenshot...", "file.export_screenshot"), Item("Minimap...", "file.export_minimap"), Item("Collision Mesh...", "file.export_collision")}),
            Sep(), Item("Map Properties...", "file.map_properties"), Item("Zone Settings...", "file.zone_settings"), Item("Exit", "file.exit", "Ctrl+Q")
        }),
        Menu("Edit", {
            Item("Undo", "edit.undo", "Ctrl+Z"), Item("Redo", "edit.redo", "Ctrl+Y"), Item("Undo History...", "edit.undo_history"), Sep(),
            Item("Cut", "edit.cut", "Ctrl+X"), Item("Copy", "edit.copy", "Ctrl+C"), Item("Paste", "edit.paste", "Ctrl+V"), Item("Paste Special...", "edit.paste_special"),
            Item("Duplicate", "edit.duplicate", "Ctrl+D"), Item("Delete", "edit.delete", "Delete"), Sep(),
            Item("Select All", "edit.select_all", "Ctrl+A"), Item("Select None", "edit.select_none", "Esc"), Item("Select Inverse", "edit.select_inverse"),
            Item("Select By Name...", "edit.select_name"), Menu("Select By Type", {Item("Doodads", "edit.select_doodads"), Item("Creatures", "edit.select_creatures"), Item("Quest Items", "edit.select_quest_items")}),
            Sep(), Item("Find...", "edit.find", "Ctrl+F"), Item("Find Next", "edit.find_next", "F3"), Sep(), Item("Preferences/Settings...", "edit.preferences")
        }),
        Menu("View", {
            Menu("Viewport Layout", {Item("Single Viewport", "view.single"), Item("Split Vertical", "view.split_vertical"), Item("Split Horizontal", "view.split_horizontal"), Item("Quad View", "view.quad"), Item("Custom...", "view.custom")}), Sep(),
            Menu("Camera", {Item("Reset Camera", "view.camera_reset", "Home"), Item("Focus on Selection", "view.focus", "F"), Item("Save Camera Position...", "view.camera_save"), Item("Recall Camera Position", "view.camera_recall"), Item("Top View", "view.top", "NumPad 7"), Item("Front View", "view.front", "NumPad 1"), Item("Side View", "view.side", "NumPad 3"), Item("Perspective", "view.perspective", "NumPad 5")}),
            Menu("Display Mode", {Item("Textured", "view.textured"), Item("Wireframe", "view.wireframe", "F3"), Item("Wireframe on Shaded", "view.wireframe_shaded"), Item("Solid Color", "view.solid"), Item("Bounding Boxes Only", "view.bounds"), Item("Collision Mesh", "view.collision")}),
            Menu("Show/Hide", {Item("Grid", "view.grid", "G"), Item("Axes Helper", "view.axes"), Item("Skybox", "view.skybox"), Item("Fog/Atmosphere", "view.fog"), Item("Water", "view.water"), Item("Doodads", "view.doodads", "D"), Item("Creatures", "view.creatures", "C"), Item("Waypoints", "view.waypoints"), Item("Spawn Radii", "view.spawn_radii"), Item("Zone Boundaries", "view.zone_boundaries"), Item("Chunk Borders", "view.chunk_borders"), Item("Heightmap Lines", "view.contours"), Item("Normals", "view.normals"), Item("Statistics Overlay", "view.stats")}),
            Menu("LOD Preview", {Item("Full Quality", "view.lod_full"), Item("LOD Level 1", "view.lod_1"), Item("LOD Level 2", "view.lod_2"), Item("Auto LOD", "view.lod_auto")}),
            Item("Time of Day Slider...", "view.time_of_day"), Item("Toggle Fullscreen", "view.fullscreen", "F11")
        }),
        Menu("Terrain", {
            Menu("Terrain Tools", {Item("Raise/Lower", "terrain.raise_lower", "T"), Item("Smooth", "terrain.smooth", "S"), Item("Flatten", "terrain.flatten", "L"), Item("Ramp", "terrain.ramp", "R"), Item("Noise", "terrain.noise", "N"), Item("Terrain Stamp/Stencil", "terrain.stamp"), Item("Erosion Simulation...", "terrain.erosion"), Item("Road / Path Tool...", "terrain.road")}),
            Menu("Texture Tools", {Item("Paint Texture", "texture.paint", "P"), Item("Sample/Eyedropper", "texture.sample", "I"), Item("Fill/Flood Fill", "texture.fill", "Shift+F"), Item("Smear/Smudge", "texture.smear"), Item("Clone Stamp", "texture.clone", "C"), Item("Gradient Paint", "texture.gradient"), Item("Vertex Color Paint", "texture.vertex_paint", "V"), Item("Clear Texture Layer", "texture.clear"), Item("Generate Auto-blend", "texture.auto_blend")}),
            Menu("Water Tools", {Item("Set Global Water Level...", "water.level"), Item("Paint Water Cells", "water.paint"), Item("Erase Water Cells", "water.erase"), Menu("Water Type", {Item("Ocean", "water.ocean"), Item("Lake", "water.lake"), Item("River", "water.river"), Item("Lava", "water.lava"), Item("Slime", "water.slime"), Item("Poison", "water.poison")}), Item("Water Properties...", "water.properties")}),
            Menu("Texture Layers", {Item("Layer Manager...", "terrain.layers"), Item("Active Layers Setup...", "terrain.active_layers"), Item("Import Texture...", "terrain.import_texture"), Item("Reload All Textures", "terrain.reload_textures"), Item("Texture Info...", "terrain.texture_info")}),
            Menu("Terrain Properties", {Item("Height Range...", "terrain.height_range"), Item("Scale Settings...", "terrain.scale"), Item("Chunk Size...", "terrain.chunk_size"), Item("Generate Terrain From...", "terrain.generate")}),
            Menu("Validate Terrain", {Item("Check for Holes/Cracks", "terrain.validate_holes"), Item("Check Slope Angles", "terrain.validate_slopes"), Item("Check Texture Coverage", "terrain.validate_textures"), Item("Generate Report", "terrain.validate_report")}),
            Menu("Chunk Operations", {Item("Copy Area...", "chunk.copy"), Item("Paste Area...", "chunk.paste"), Item("Mirror Horizontal", "chunk.mirror_horizontal"), Item("Mirror Vertical", "chunk.mirror_vertical"), Item("Rotate 90° CW", "chunk.rotate_cw"), Item("Rotate 90° CCW", "chunk.rotate_ccw"), Item("Flip...", "chunk.flip")})
        }),
        Menu("Objects", {
            Menu("Placement Mode", {Item("Single Place", "objects.place_single"), Item("Brush/Scatter Paint", "objects.scatter"), Item("Array/Grid Place...", "objects.array"), Item("Path Place...", "objects.path"), Item("Surface Density Paint", "objects.density")}),
            Menu("Transform Tools", {Item("Select", "objects.select", "Q"), Item("Move", "objects.move", "W"), Item("Rotate", "objects.rotate", "E"), Item("Scale", "objects.scale", "R"), Item("Universal Transform", "objects.universal")}),
            Menu("Snap Settings", {Item("Snap to Grid", "objects.snap_grid"), Menu("Grid Size", {Item("0.1", "objects.grid_0_1"), Item("0.25", "objects.grid_0_25"), Item("0.5", "objects.grid_0_5"), Item("1", "objects.grid_1"), Item("2", "objects.grid_2"), Item("5", "objects.grid_5"), Item("10", "objects.grid_10")}), Item("Snap to Ground", "objects.snap_ground"), Item("Snap to Objects", "objects.snap_objects"), Item("Angle Snap", "objects.angle_snap"), Menu("Angle Increment", {Item("1°", "objects.angle_1"), Item("5°", "objects.angle_5"), Item("15°", "objects.angle_15"), Item("22.5°", "objects.angle_22_5"), Item("45°", "objects.angle_45"), Item("90°", "objects.angle_90")})}),
            Menu("Alignment", {Item("Align to Ground Normal", "objects.align_normal"), Item("Align to Terrain Slope", "objects.align_slope"), Item("Align to Grid", "objects.align_grid"), Item("Align Selection...", "objects.align_selection"), Item("Randomize Rotation/Scale...", "objects.randomize")}),
            Menu("Grouping", {Item("Group Selected", "objects.group", "Ctrl+G"), Item("Ungroup", "objects.ungroup", "Ctrl+Shift+G"), Item("Enter Group", "objects.enter_group"), Item("Exit Group", "objects.exit_group")}),
            Menu("Doodad Sets", {Item("Create New Set...", "objects.create_set"), Item("Add to Set...", "objects.add_set"), Item("Remove from Set", "objects.remove_set"), Item("Select Set Contents", "objects.select_set"), Item("Hide Set", "objects.hide_set"), Item("Lock Set", "objects.lock_set"), Item("Manage Sets...", "objects.manage_sets")}),
            Menu("Properties", {Item("LOD Settings...", "objects.lod"), Item("Collision...", "objects.collision"), Item("Lighting...", "objects.lighting"), Item("Batch Rename...", "objects.rename")}),
            Menu("Libraries", {Item("Refresh Library", "objects.refresh_library"), Item("Scan Directory for New Models...", "objects.scan_library"), Item("Import Model Pack...", "objects.import_pack"), Item("Manage Favorites...", "objects.favorites")})
        }),
        Menu("Creatures", {
            Item("Place Creature Spawner...", "creatures.place"), Item("Edit Selected Spawner...", "creatures.edit"), Item("Delete Selected Spawners", "creatures.delete"), Sep(),
            Menu("Waypoint Editor", {Item("Add Waypoint", "creatures.waypoint_add"), Item("Insert Waypoint Before", "creatures.waypoint_before"), Item("Insert Waypoint After", "creatures.waypoint_after"), Item("Delete Waypoint", "creatures.waypoint_delete"), Item("Clear All Waypoints", "creatures.waypoint_clear"), Item("Loop Path", "creatures.waypoint_loop"), Item("Reverse Path", "creatures.waypoint_reverse"), Item("Set Wait Time...", "creatures.waypoint_wait"), Item("Generate Path Along Terrain...", "creatures.waypoint_generate")}),
            Menu("Spawn Parameters", {Item("Respawn Time...", "creatures.respawn"), Item("Max Spawn Count...", "creatures.count"), Item("Spawn Radius...", "creatures.radius"), Item("Spawn Conditions...", "creatures.conditions")}),
            Menu("AI Configuration", {Item("Aggro Radius...", "creatures.aggro"), Item("Leash Distance...", "creatures.leash"), Item("Faction Assignment...", "creatures.faction"), Item("NPC Flags...", "creatures.flags"), Item("Equipment Overrides...", "creatures.equipment")}),
            Menu("Visual Aids", {Item("Show Aggro Radius", "creatures.show_aggro"), Item("Show Leash Radius", "creatures.show_leash"), Item("Show Spawn Radius", "creatures.show_spawn"), Item("Show Waypoints", "creatures.show_waypoints"), Item("Show Patrol Path", "creatures.show_patrol"), Item("Show All Creature Spawners", "creatures.show_all")}),
            Menu("Creature Database", {Item("Browse Creatures...", "creatures.browse"), Item("Search by Name/ID...", "creatures.search"), Item("Filter by Faction/Type...", "creatures.filter"), Item("Import Creature Data...", "creatures.import"), Item("Refresh Database", "creatures.refresh")}),
            Menu("Batch Operations", {Item("Replace All of Type...", "creatures.replace"), Item("Change Faction of Selected...", "creatures.batch_faction"), Item("Delete All in Area...", "creatures.delete_area"), Item("Export Creature List...", "creatures.export")})
        }),
        Menu("Quest", {Item("Quest Browser...", "quest.browser"), Item("Link Quest to NPC...", "quest.link_npc"), Item("Place Quest Item...", "quest.place_item"), Item("Define Quest Area/Trigger Volume...", "quest.area"), Menu("Trigger Editor", {Item("Create Area Trigger...", "quest.trigger_area"), Item("Create Proximity Trigger...", "quest.trigger_proximity"), Item("Create Interaction Trigger...", "quest.trigger_interaction"), Item("Create Timer Trigger...", "quest.trigger_timer"), Item("Edit Trigger Script...", "quest.trigger_script")}), Item("Quest Giver Markers", "quest.markers_giver"), Item("Objective Markers", "quest.markers_objective"), Item("Validate Quest Placements...", "quest.validate")}),
        Menu("Spells", {Item("Spell Effect Previewer...", "spells.previewer"), Item("Particle Editor...", "spells.particles")}),
        Menu("Tools", {Menu("Measurement Tools", {Item("Measure Distance", "tools.measure_distance"), Item("Measure Angle", "tools.measure_angle"), Item("Measure Area", "tools.measure_area"), Item("Ruler", "tools.ruler")}), Menu("Debug Tools", {Item("Performance Profiler...", "tools.profiler"), Item("Polygon Counter...", "tools.polygons"), Item("Draw Call Analyzer...", "tools.draw_calls"), Item("Texture Memory Usage...", "tools.texture_memory"), Item("Collision Debugger...", "tools.collision"), Item("Pathfinding Visualizer...", "tools.pathfinding"), Item("Console/Log Window...", "tools.console"), Item("Scene Statistics...", "tools.scene_stats")}), Menu("Validation", {Item("Validate Entire Map...", "tools.validate_map"), Item("Check for Missing Assets...", "tools.validate_assets"), Item("Check for Invalid References...", "tools.validate_refs"), Item("Check for Overlapping Objects...", "tools.validate_overlap"), Item("Generate Validation Report...", "tools.validate_report")}), Menu("Version Control", {Item("Commit Changes...", "tools.vcs_commit"), Item("View History...", "tools.vcs_history"), Item("Compare Versions...", "tools.vcs_compare"), Item("Revert to Version...", "tools.vcs_revert"), Item("Update from Repository...", "tools.vcs_update")}), Menu("Plugins", {Item("Manage Plugins...", "tools.plugins"), Item("Plugin Settings...", "tools.plugin_settings"), Item("Available Plugins", "tools.marketplace")}), Item("Options/Preferences...", "tools.preferences")}),
        Menu("Window", {Item("Content Browser", "window.content_browser"), Item("Properties Panel", "window.properties"), Item("Hierarchy/Outliner", "window.hierarchy"), Item("Terrain Tools Panel", "window.terrain_tools"), Item("Road / Path Tool", "window.road_path"), Item("Texture Palette", "window.texture_palette"), Item("Doodad Browser", "window.doodad_browser"), Item("Creature Editor", "window.creature_editor"), Item("Quest Panel", "window.quest"), Item("Spell Previewer", "window.spell"), Item("Chunk Operations", "window.chunk"), Item("Debug Panel", "window.debug"), Item("Output/Log", "window.log"), Sep(), Item("Save Current Layout...", "window.save_layout"), Menu("Load Layout", {Item("Default", "window.layout_default"), Item("Level Design", "window.layout_level"), Item("Terrain Art", "window.layout_terrain"), Item("Object Placement", "window.layout_objects"), Item("Quest Design", "window.layout_quest"), Item("Custom...", "window.layout_custom")}), Item("Reset to Default Layout", "window.reset_layout"), Item("Close All Panels", "window.close_panels")}),
        Menu("Help", {Item("User Manual/Documentation", "help.manual", "F1"), Item("Keyboard Shortcuts Reference...", "help.shortcuts"), Item("Video Tutorials...", "help.tutorials"), Item("Getting Started Guide...", "help.getting_started"), Sep(), Item("Report a Bug...", "help.bug"), Item("Request a Feature...", "help.feature"), Sep(), Item("Check for Updates...", "help.updates"), Item("About WowEdit...", "help.about")})
    };
}

void MenuBar::bind(const std::string& actionId, std::function<void()> callback) { callbacks_[actionId] = std::move(callback); }
bool MenuBar::trigger(const std::string& actionId) const { const auto found = callbacks_.find(actionId); if (found == callbacks_.end() || !found->second) return false; found->second(); return true; }
bool MenuBar::contains(const std::string& actionId) const { return contains(menus_, actionId); }
bool MenuBar::contains(const std::vector<MenuEntry>& entries, const std::string& actionId)
{
    for (const MenuEntry& entry : entries) if (entry.actionId == actionId || contains(entry.children, actionId)) return true;
    return false;
}
} // namespace wowedit
