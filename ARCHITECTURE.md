# WowEdit architecture

## Scope and design decisions

WowEdit is an original, portable world-authoring core that powers the World Editor
workflows in TrinityCore Studio. It is **not** a clone of proprietary Blizzard
source code, a redistribution of client assets, or a claim of compatibility with
undocumented internal data. It implements publicly describable level-design
concepts through C++17 domain models, a Vulkan-capable render path, and a Dear
ImGui docking host.

The production choice is **C++17 + Vulkan + Dear ImGui**:

- C++ keeps ADT/M2/WMO parsing, GPU resource ownership, database adapters, and
  plug-in calls in one native memory model.
- Vulkan supports explicit synchronization, instancing, compute/GPU culling, and
  a Windows-first / Linux-macOS-portable renderer. Existing `RenderEngine` is the
  production Vulkan backend; `WowEdit/rendering` is a backend-neutral planner.
- Dear ImGui is the initial View implementation. `WowEdit/ui` exposes controllers
  and a full menu model so UI rendering can evolve independently (including Qt).

## Component diagram

```text
                    ┌───────────────────────────────────────────┐
                    │ Dear ImGui desktop host / future Qt host   │
                    │ App shell, docks, dialogs, input, menus    │
                    └──────────────────────┬────────────────────┘
                                           │ MVC presentation models
 ┌─────────────────────────────────────────▼─────────────────────────────────────────┐
 │ WowEdit/ui: MainWindow, Viewport, Toolbar, MenuBar, inspectors, browsers, panels │
 └──────┬───────────────────────┬───────────────────────────┬───────────────────────┘
        │ commands/events       │ project load/save          │ draw lists/settings
 ┌──────▼───────────┐  ┌────────▼───────────┐      ┌────────▼─────────────────────┐
 │ core             │  │ terrain / objects   │      │ creatures / editing           │
 │ ICommand         │  │ height/splat/water  │      │ spawn/waypoint/AI/clipboard   │
 │ CommandManager   │  │ doodad/octree       │      │ brush/selection/transform     │
 │ EventBus         │  │ chunk/LOD           │      │ paint/ramp/erosion             │
 └──────┬───────────┘  └────────┬───────────┘      └────────────┬──────────────────┘
        │                       │                                │
 ┌──────▼───────────────────────▼────────────────────────────────▼──────────────────┐
 │ data / io: WorldProject, MapTile, serializer, atomic file I/O, import/export      │
 │      GameDatabase adapters, VersionControl facade                                  │
 └──────┬───────────────────────────────────────────────────────────────────────────┘
        │                        │                                      │
 ┌──────▼───────────┐   ┌────────▼───────────┐               ┌──────────▼───────────┐
 │ scripting         │   │ plugins            │               │ rendering             │
 │ command bindings  │   │ IWowEditPlugin     │               │ frustum/LOD/batches   │
 │ Lua/Python bridge │   │ Event/API access   │               │ Vulkan RenderEngine   │
 └──────────────────┘   └────────────────────┘               └──────────────────────┘
```

## MVC / MVVM boundaries

| Layer | Responsibility | Must not do |
| --- | --- | --- |
| Model | `Heightmap`, `TerrainChunk`, `Doodad`, `CreatureSpawner`, `WorldProject` | Access ImGui, GLFW, Vulkan handles, or SQL UI widgets |
| Controller | Tools, `CommandManager`, `EventBus`, import/export, menu dispatch | Encode visual layout or mutably access a GPU command buffer |
| View model | `ui/*Panel`, `MenuBar`, `Toolbar`, `MainWindow` | Serialize raw ADT/client bytes or bypass command history |
| View/backend | ImGui host, existing Vulkan `RenderEngine`, optional Qt shell | Implement terrain/business logic |

A feature must cross these boundaries through value data, commands, events, or a
small explicit service interface. This keeps automated tests headless.

## Critical data flows

### Terrain brush → GPU preview / undo / save

```text
Input sample (position, pressure)
        │
        ▼
BrushTool / TerrainToolController
        │ creates before/after vertex deltas
        ▼
TerrainEditor ──► TerrainHeightModifyCommand ──► CommandManager history
        │                                               │
        │ EventType::TerrainModified                    │ undo / redo
        ▼                                               ▼
Heightmap → TerrainChunk mesh/normal dirty region → renderer upload queue
        │
        ▼
MapSerializer writes temp blobs → atomic rename → JSON manifest written last
```

The live Studio host also uses its staged ADT overlay for original 3.3.5a map
files. `.wowedit` is a portable authoring format, not a replacement for client
file validation; ADT adaptation remains explicit and reversible.

### Object placement / selection

```text
Content Browser drag/drop or placement brush
  → DoodadPlaceCommand → DoodadManager::insertDirect
  → bounds update + Octree rebuild/dirty update
  → DoodadPlaced event → Outliner/Properties/renderer update
```

The octree is a broad phase; a picking ID buffer or ray/AABB test performs the
final selection. Transform gizmo controls capture pointer input before camera
controls in the Studio host, preventing gizmo drags from orbiting the camera.

### Game server / schema data

```text
TrinityCore or AzerothCore DB → live schema introspection → DataEngine adapter
  → GameDatabase/cache → World Editor panels / renderer
  → transactional live write OR ordered reviewable SQL export
```

Core flavor and actual columns are detected at runtime. Server-specific metadata
is never emitted as guessed SQL. Studio-only triggers, lights, and preview data
remain clearly labeled map-scoped metadata unless a server module is installed.

## Threading model

| Thread | Work |
| --- | --- |
| UI/main | ImGui docking, input, command submission, immutable render-plan snapshot |
| Render | Vulkan command recording/present, resource upload and deferred destruction |
| Asset/stream workers | ADT/M2/WMO/texture decode, client-data streaming, thumbnail preparation |
| Database worker | schema probes, paged queries, transactional writes/SQL export preparation |
| File worker (optional) | autosave blob writes, import validation, Git status/history requests |

`EventBus::enqueue()` is the thread hand-off into the UI thread. Render resources
are not accessed by tools directly. Any background result must have a generation
or map identifier so stale work is discarded after a map change.

## Memory management

- RAII owns all files, buffers, commands, threads, and plugin handles.
- `CommandManager` caps history (default 1000). Large world actions use compact
  deltas/RLE or chunk snapshots instead of duplicating GPU resources.
- Height/splat blobs are external to the JSON manifest; meshes/textures are
  generated or streamed on demand.
- Object spatial index entries are rebuilt/updated when ownership is stable; no
  raw pointer crosses a worker thread boundary.
- GPU resources use renderer-owned deferred destruction after the last fence.

## Plug-in design

`IWowEditPlugin` exposes a stable C++ interface with identity/version/lifecycle
methods. `PluginContext` grants deliberately narrow access to EventBus,
CommandManager, GameDatabase, and ScriptEngine. Plugins register tools, menu
commands, importers, schema adapters, or scripts through those services; they do
not reach into ImGui internals or static singleton world state.

A dynamic loader can resolve `CreateWowEditPlugin` from a DLL/shared library; the
current manager already provides deterministic registration/unload behavior for
built-ins and test plugins. Use a C ABI wrapper when distributing plugins built
with a different compiler/runtime.

## File formats

### `.wowedit`

A UTF-8 JSON manifest contains project identity, settings, objects, creatures,
quest metadata, and tile descriptors. Each descriptor references atomic binary
attachments in `<project>.blobs/`:

- `tile_N.height.bin`: row-major 32-bit float height samples.
- `tile_N.splat.bin`: row-major RGBA8 texture weights.

The serializer writes replacement blobs first and the manifest last. This avoids
a corrupt manifest after a crash. Objects/creatures preserve IDs, transforms,
waypoints, AI values, names, flags, and custom metadata.

### ADT interoperability

`RenderEngine/adt` contains the WotLK 3.3.5a ADT parser/writer for MCNK, MCVT,
MCNR, MCLY, MCAL, MCRF, MCRW/placements, and liquid data. The Studio edit overlay
writes loose project files rather than modifying MPQs. ADT is a constrained game
format; `.wowedit` retains tool metadata that cannot be represented in stock ADT.

## Performance plan and bottlenecks

| Technique | Implementation point |
| --- | --- |
| Frustum culling | `RenderPipeline::plan`, ADT stream window |
| Object spatial partitioning | `Octree<Doodad*>` broad phase |
| Terrain/object LOD | `TerrainChunk::generateLOD`, mesh LOD selection |
| Instancing | Render batches grouped by mesh/LOD/material |
| Async streaming | existing `AdtStreamer` workers and generation checks |
| Texture atlas/arrays | terrain texture-array shader and active splat layers |
| Occlusion/GPU culling | renderer extension point; retain CPU fallback |
| Pooling | stream/model/particle allocation pools in backend |

Targets are measured on a release host with real client assets: 60 FPS at 10k
visible objects, sub-16 ms brush feedback, under 50 ms undo, and under 5 seconds
for a 100-chunk load. `WowEditPerformanceBenchmark` supplies a repeatable 100k
placement baseline; it is not a substitute for GPU frame profiling.
