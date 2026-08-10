# Changelog

## Unreleased — WowEdit foundation

### Added

- World Editor top-level Terrain, Objects, Creatures, Quest, Spells, Tools, and Window menus
  wired to live dock panels, terrain/gizmo tools, real Quest/Spell modules, and shared ADT save logic.
- World Editor Spell Effect Previewer with `Spell.dbc` field resolution, searchable spell selection,
  target/time/weather controls, play/pause/stop/loop/scrub timeline, procedural in-world cast/projectile/
  impact visualization, manifest copy, persisted preferences, and Script Trigger CastSpell dispatch.
- Non-destructive World Validation panel with selectable findings and Markdown report export for
  project/client readiness, staged ADT edits, spawn transforms/displays/overlap, routes, formations,
  triggers, and authored lights.
- Live World Editor Ramp / Stairs terrain composer: two-click terrain endpoints, width/slope/stair
  controls, contiguous staged Flatten strokes, real-time terrain preview, one-step undo/redo, and
  authoritative MCVT/MCNR overlay save/reload.
- Live Noise / Terrainify brush with persisted radius/amplitude/frequency/octave/seed settings;
  deterministic fractal samples expand to staged Raise/Lower strokes for exact preview, undo, and
  ADT replay.
- Live Terrain Stamp presets for Hill, Valley, Crater, and yaw-rotated Ridge compositions; each
  expands to normal staged ADT strokes and therefore keeps live preview/undo/save behavior.
- Portable `WowEdit/` C++17 architecture with MVC-facing panel controllers,
  complete menu command model, `ICommand`/macro history, and EventBus.
- Heightmap, texture-splat, water, terrain chunk LOD, terrain tools, erosion,
  command-backed Road / Path Tool grading/material/preview meshes, object/octree
  selection, transform snapping, creature/waypoint/AI preview, spell timeline,
  and world clipboard systems.
- JSON + atomic binary `.wowedit` serializer, asset inspection, export, version
  control facade, plugins, scripting bridge, profiling, procedural starter assets,
  GLSL 4.5 shader sources, CMake/CTest, unit/integration/benchmark targets.
- Architecture, manual, API reference, conceptual workspace image, and expanded
  README build/quick-start/contributing information.

### Changed

- Existing World Editor work remains the live Vulkan/ImGui TrinityCore and
  AzerothCore integration surface. The portable core keeps tool/data logic
  testable independently of a Windows GPU host.

### Notes

- A production Windows `.exe` still requires the documented Visual Studio +
  Vulkan Studio build; this Linux environment validates portable C++ sources and
  tests but cannot emit that Windows executable.
- Arbitrary triggers, lighting profiles, and simulations remain Studio metadata
  where stock 3.3.5 server schemas have no native equivalent.
