# Changelog

## Unreleased — WowEdit foundation

### Added

- Full Studio UI design overhaul: obsidian/Blizzard visual tokens, redesigned workspace/status bars,
  product identity, action/state pills, card-based navigation/project launcher, command center,
  shared panel headers, intentional empty states, action hierarchy, and stronger field-table surfaces.
- World Editor top-level Terrain, Objects, Creatures, Quest, Spells, Tools, and Window menus
  wired to live dock panels, terrain/gizmo tools, real Quest/Spell modules, and shared ADT save logic.
- Creature General live display cards: each `modelid1..4` now resolves through
  `CreatureDisplayInfo` → `CreatureModelData`, previews the actual M2/skin/display scale before
  save, batches all four cards into one renderer scene, and transparently labels direct-model
  custom-schema fallback. Character display extras drive compact body/hair/armour dressing.
- World Editor Spell Effect Previewer with `Spell.dbc` field resolution, searchable spell selection,
  target/time/weather controls, play/pause/stop/loop/scrub timeline, procedural in-world cast/projectile/
  impact visualization, manifest copy, persisted preferences, and Script Trigger CastSpell dispatch.
- Non-destructive World Validation panel with selectable findings and Markdown report export for
  project/client readiness, staged ADT edits, spawn transforms/displays/overlap, routes, formations,
  triggers, and authored lights.
- Streamed placed/global WMO interior props: selected `MODS`/`MODD` M2 instances now load once per
  path, inherit their parent `MODF` transform, retain per-instance tint, animate/cull/effect-render,
  select through the WMO root, and never leak into ADT save output as invalid standalone doodads.
  Root-only MODD resolution avoids reparsing WMO group geometry per placement and has a Vulkan-free
  transform/identity regression test.
- World Editor performance pass: per-MCNK frustum culling compacted into one indirect multi-draw per
  terrain tile, preallocated indirect command storage, batched terrain VBO/IBO/parameter and ground
  texture uploads, a lower 7×7 default stream neighbourhood, persisted 50–100% viewport resolution
  presets, and live GPU/CPU/terrain-cull diagnostics.
- Live World Editor Ramp / Stairs terrain composer: two-click terrain endpoints, width/slope/stair
  controls, contiguous staged Flatten strokes, real-time terrain preview, one-step undo/redo, and
  authoritative MCVT/MCNR overlay save/reload.
- Live Noise / Terrainify brush with persisted radius/amplitude/frequency/octave/seed settings;
  deterministic fractal samples expand to staged Raise/Lower strokes for exact preview, undo, and
  ADT replay.
- Live Terrain Stamp presets for Hill, Valley, Crater, and yaw-rotated Ridge compositions; each
  expands to normal staged ADT strokes and therefore keeps live preview/undo/save behavior.
- Live Smooth tool with local Laplacian-style sampling, iteration/blend controls, optional
  sharp-edge preservation, deterministic staged Flatten expansion, live preview, and one-step undo.
- Terrain Paint MCCV implementation: radial vertex-tint strokes, safe missing-MCCV creation,
  MCIN offset/size repair, staged undo/redo, edited-client overlay writes, and authoritative reload.
- Spawn Palette Array / Grid placement for NPCs and GameObjects: terrain-snapped rows/columns with
  centered anchoring, skipped-cell reporting, persisted settings, and a compound CommandStack undo.
- Schema-aware SQL conversion import in DB Editor: live target introspection, table/field alias mapping,
  TrinityCore/AzerothCore spawn-layout conversion, legacy creature model row conversion, reviewable
  UPSERT/UPDATE plans, opt-in DELETE handling, transactional Apply, and SQL Export support.
- SQL Export session safety: the first successful save starts a clean script, later saves append
  complete ordered transaction blocks through a temporary-file replace, and a failed write leaves
  the prior review file intact. Regression coverage verifies multi-save ordering, rollback, and
  deliberate session reset behavior.
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
