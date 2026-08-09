# TrinityCore Studio

A desktop world-building toolkit for browsing and editing World of Warcraft data in
**TrinityCore 3.3.5a (build 12340)** and **AzerothCore 3.3.5a** *world* databases. It combines record editors for
quests, items, creatures, gameobjects, scripting, conditions, loot, and reference tables
with a streamed **3D World Editor**. It resolves item / creature / gameobject / faction /
spell IDs to names and can either write changes **live** (transactional) or **export a
reviewable `.sql` file** - selectable per session.

Built with Dear ImGui (docking) + GLFW + Vulkan 1.4. Optional World of Warcraft client
data (MPQ/DBC) adds icons, zone maps, name resolution, and a Blizzard-styled theme.

## Editors

Pick an editor from the left rail. Each one shares the same shape: a searchable
**browser** on the left, a tabbed **editor** in the center, and a **validation**
panel + log across the bottom.

- **Quests** - the full quest picture across `quest_template` and every related
  table (addon, offer_reward, request_items, details, mail_sender, greeting,
  questgivers, conditions, all `*_locale`), plus an interactive **POI map** rendered
  over the real zone map (drag/add/delete points).
- **Items** - every `item_template` field across General, Flags, Requirements,
  Stats, Weapon/Armor, Spells, Sockets, Text & Set, Locales, with a live icon
  preview.
- **Creatures** - `creature_template` and its child tables (addon, movement,
  resistances, spells, equipment, locales) **plus** the systems keyed by the
  creature: **vendor** inventory, **trainer** spells, **loot** tables, and **world
  spawns** (edited in place). Faction resolves to a name via a FactionTemplate.dbc
  lookup.
- **GameObjects** - `gameobject_template` with a **type-aware Data tab**: the 24
  generic `Data0..23` columns are labeled per the selected type (a Door's `Data1` is
  a Lock id; a Chest's `Data1` is a loot id) with id-name pickers where a field
  references another table - plus addon, locales, quest items, loot, and spawns.
- **World Editor** - streams whole ADT maps from client data, renders terrain, doodads,
  WMOs, NPCs, GameObjects, transports, phases/events/pools, and provides a map overview,
  coordinate bookmarks, a searchable world outliner, precise transforms, rapid spawn brushes,
  formation links, non-destructive **terrain height sculpting**, right-click placement, deletion,
  undo/redo, spawn-instance forms, and an in-world **waypoint path editor** for NPC routes.

### World Editor workflow

The World Editor needs both client data (for terrain/models) and a project database (for
spawns). Choose a map in **World Browser**, fly or orbit around the streamed terrain, then
turn on **Edit**:

1. **Navigate a real map:** use **Locations** to enter exact TrinityCore X/Y/Z coordinates,
   click the interactive 64×64 map overview to stream/fly to a tile, or save reusable named
   bookmarks. The searchable **World Outliner** lists every NPC and GameObject spawn on the
   map—not only the ones currently within render range—and focusing a row streams it into view
   (or press **F** to frame the current selection).
2. **Move or place content:** click an NPC/GameObject/doodad to select it, then use the
   Move/Rotate/Scale gizmo, **Transform** panel (staged exact coordinates, yaw, copy/paste,
   nudging), or **Snap to ground**. Right-click terrain to add an NPC, GameObject, M2, or WMO;
   right-click an existing object for its context menu. DB spawns save transactionally on gizmo
   release; ADT placements are batched into the project's `edited-client` overlay.
3. **Dress a map quickly:** choose an NPC or GameObject template in **Spawn Palette**, arm the
   brush, and right-click terrain repeatedly. The palette retains yaw and has a per-session
   minimum-spacing guard; each placed spawn remains an ordinary undoable database instance. You
   can also load the selected NPC/GO template straight into the brush from **Transform**.
4. **Build formations:** select an NPC and use **Formation** to create a leader/self row, join a
   leader GUID, set distance/angle/group-AI/path-direction points, or remove membership. Purple
   world links make leader/member relationships visible and selectable in context.
5. **Sculpt terrain safely:** use **Terrain Sculpt** to arm a Raise, Lower, or Flatten height
   brush, then right-click the ground. Smooth radial strokes queue MCVT height changes and rebuilt
   MCNR normals across every intersected existing ADT tile. Pending strokes support Ctrl+Z/Ctrl+Y;
   **Save ADT edits** writes the project overlay and reloads streamed terrain.
6. **Edit an NPC route:** select an NPC and open **Waypoint Path**. The panel identifies whether
   its route comes from the creature template (shared) or its `creature_addon` row (local). Use
   **Make local copy** before changing a shared route when the change is map/spawn-specific;
   creating that local addon carries over the template's visual addon settings so mounted/
   aura-equipped NPCs keep their appearance.
7. **Author paths in 3D:** click blue numbered route markers to select a point. Add a point at
   the NPC home, arm **Place on terrain** and right-click ground to insert a point, or arm
   **Move selected on terrain** to reposition one. The route overlay, loop line, delays,
   orientation, walk/run mode, events, actions, chances, and `wpguid` all preview and edit in
   place. Route-table changes have local Ctrl+Z/Ctrl+Y before Save.
8. **Save deliberately:** route edits are an unsaved live preview until **Save route**. Existing
   `waypoint_data` rows are updated transactionally rather than replaced, so project-specific
   columns survive point moves/reordering. A spawn addon row takes precedence over template addon
   data in TrinityCore; clearing its `path_id` keeps its other addon fields and intentionally
   leaves that spawn without a route.

### Shared features

- Browse & search by ID or name, with type/quality/rank filters, server-side sort,
  and paging.
- Create (blank or from an **archetype template**), **Clone**, **Delete**, **Revert**,
  and full **Undo/Redo**.
- Live **validation** with severity levels; click an issue to jump to the field.
- Tools per editor: **Where-Used** (reverse references), **New-from-Template**, and
  column-level **Batch-Edit** across the current browser list.
- Two write modes: **Live** (direct, transactional, with a confirmation) or **SQL
  Export** (reads live, writes an ordered `.sql` you can review/commit to git).
  **Preview SQL** shows exactly what a save will run.
- Optional SOAP `.reload` after a live save (if a worldserver account is configured).
- Saved connection profiles (`config/connections.json`; passwords stored only if you
  opt in) and per-editor preferences.

### Client data (optional)

Point the app at a WoW 3.3.5a `Data` folder (MPQ archives) to unlock item/spell
**icons**, quest **POI zone maps**, the streamed 3D **World Editor** (ADT terrain, M2s,
WMOs, and NPC/GameObject models), name resolution for factions / spells / areas / skills /
titles / faction templates, and the **Blizzard parchment theme** + UI font. A core server's
extracted `Data` folder is not a substitute for a full WoW client Data folder: Studio now shows
selectable NPC map markers plus model diagnostics when creature M2/DBC files are missing. Record
editors still work without client data - you just get IDs instead of names and the dark theme.

### Core compatibility: TrinityCore + AzerothCore

Projects now carry a **Core** profile: Auto-detect, TrinityCore, or AzerothCore. Auto-detect
probes a live database before editor modules load. It recognizes the current AzerothCore
`creature.id1` / `gameobject.id1` spawn layout, its `bytes1` / `bytes2` creature-addon
layout, and reduced `waypoint_data` variants; TrinityCore layouts continue to use `id` and
expanded addon fields. World Editor spawn placement, outliner, paths, formation data, and
creature/gameobject associated-spawn panels adapt their entry-column and optional-field SQL
at runtime.

For an AzerothCore project, optionally set **Core root** in the project form. Studio detects
common `env/dist/etc` and `env/dist/configs` layouts, can import `WorldDatabaseInfo` from
`worldserver.conf`, and still keeps your **WoW client `Data` folder** separate from the
server's extracted `Data` directory. Core-specific columns are schema-filtered on save, so
one project file can safely target either core.

## Build

Requirements: **Visual Studio 2026** (MSVC v145 toolset) and **git**. All other
dependencies (Dear ImGui, GLFW, the MySQL C client, nlohmann/json, StormLib,
premake5) are **vendored** in `third_party/` and `tools/` - nothing else to install.

```powershell
.\build.ps1 Debug      # or: .\build.ps1 Release
```

If PowerShell blocks the unsigned local helper, use a process-only bypass (it resets when
that terminal closes):

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
.\build.ps1 Release
```

This runs `tools\premake5.exe vs2026` to generate `build\TrinityCoreStudio.slnx`, then
builds it. Output: `bin\Debug\TrinityCoreStudio.exe`. You can also open
`build\TrinityCoreStudio.slnx` in Visual Studio 2026 directly.

## Run

```powershell
.\bin\Debug\TrinityCoreStudio.exe
```

In the app: open the **Connection** panel, enter your world DB host/user/password/db,
pick **Live** or **SQL Export** mode, and Connect. Choose an editor from the rail, use
the **Browser** to find a record, edit it in the tabbed editor, and **Save** (live) or
export to `.sql`. On first run you can point it at your client `Data` folder for icons,
maps, and names.

## Notes / status

- Targets the 3.3.5a build-12340 world schema shipped with this TrinityCore tree.
  Works with **MySQL 8.0** (reserved-word columns like `rank` are quoted).
- Schema-adaptive writes: only columns the live DB actually has are written, so minor
  TrinityCore schema drift is tolerated. `VerifiedBuild` is written as `0`; empty text
  is written as `''` (not SQL `NULL`).
- Editing shared child data (loot templates, trainers) affects every record that
  references it; spawn edits apply per-`guid`.
- Windows-only for now (GLFW + Vulkan backend, Win32 surface; the code is otherwise
  portable). Requires the LunarG Vulkan SDK to build and a Vulkan-1.4 driver to run.
