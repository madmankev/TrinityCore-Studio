# WowEdit user manual

## 1. Getting started

WowEdit is the world-authoring workflow inside TrinityCore Studio plus a portable
project core. It can work with a plain `.wowedit` project for terrain/object
prototyping or with a configured WoW 3.3.5a client + TrinityCore/AzerothCore
world database for live data-aware editing.

> **Safe asset rule:** point Studio at a legally obtained local client `Data`
> folder. Do not add MPQs, DBCs, or extracted Blizzard art to this repository.

### First launch

1. Create/open a Studio project and select **World Editor**.
2. Set the client-data directory. Select **Core → Auto**, **TrinityCore**, or
   **AzerothCore** and connect to a world DB if you need live spawns.
3. Open a map from **World Browser**. Use **View → Reset Layout** for the
   navigation/canvas/inspector layout.
4. Use **File → New Map** in a portable WowEdit host to start an original map
   without a client install.

## 2. Tutorial: create a first zone

1. Pick **Terrain → Generate Terrain From** and choose a noise/preset base.
2. Use Raise/Lower to block out coastlines; use Flatten to establish towns and
   Smooth to remove unintended spikes.
3. Paint grass, dirt, rock, sand, and snow layers. Keep one layer dominant where
   you need crisp material transitions; use soft falloff for natural blends.
4. Place starter trees/rocks/props from Content Browser. Q/W/E/R select/move/
   rotate/scale; enable grid and angle snap for constructed spaces.
5. Add water cells and choose lake/river/ocean/lava/slime/poison properties.
6. Add creature spawners and patrol routes; then save an atomic `.wowedit` file
   or intentionally save the live-core/SQL changes from Studio.

## 3. Terrain sculpting

### Raise / Lower

Set brush radius from 0.1–100m and strength from 0.001–10m. The named Linear,
Smoothstep, Gaussian, and Constant falloffs determine edge shape. Tablet pressure
multiplies stroke strength. Short repeated strokes give more control than one
large spike.

### Smooth

Smooth applies a local Laplacian neighborhood blend. Increase iterations slowly;
excessive smoothing removes ridges. Preserve-edge behavior should be used around
cliffs, roads, and structures.

### Flatten / Ramp

Sample a target point, then flatten with a blend rate to avoid a hard plateau.
Ramp accepts start/end heights, width, and a maximum slope constraint. Use it for
roads, ramps, stairs foundations, and traversable cliffs.

### Noise, stamps, erosion

Noise is deterministic from frequency/amplitude/octaves/seed. Stamps can be
scaled and rotated; thermal erosion moves material downhill above a talus angle,
while hydraulic erosion simulates short rain droplets. All outcomes are commands:
undo immediately if an iteration is too destructive.

## 4. Texturing basics

A tile has four active RGBA splat channels; a zone library registers up to 13
textures. Use **Layer Manager** to choose the active quartet.

- **Paint** changes selected-layer weight with opacity/hardness.
- **Gradient** is appropriate for elevation/slope transitions.
- **Flood Fill** respects paint tolerance and optional terrain height limit.
- **Eyedropper** reads dominant material/opacity.
- **Smudge** pushes nearby weights; **Clone** copies a source pattern.
- **Vertex Color** tints the final surface and is ideal for localized grime,
  vegetation tone, or baked mood.

Use auto-blend as a starting point only; inspect cliffs, shorelines, and paths
manually.

## 5. Object composition

The Object Browser supports hierarchy/category filtering, search, tags,
favorites, recent assets, thumbnails, model statistics, and drag/drop placement.

- **Single placement:** select an asset and click ground.
- **Scatter brush:** set density, seed, scale/rotation variance, normal alignment,
  overlap avoidance, and height offset.
- **Array/grid:** choose rows, columns, and spacing for fences/interiors.
- **Path placement:** lay points along a spline for lamps, trees, or walls.
- **Surface density:** paint an object density field for foliage.

Use Center/Origin/Selection Bounds pivots deliberately. The transform gizmo has
axis, plane, screen-space, uniform/non-uniform scale, grid snap (0.1–10m), and
angle snap (1–90°). Gizmo input is captured before camera input; left-dragging an
object handle must not orbit the viewport camera.

## 6. Creatures and patrols

Place a creature spawner, choose a template/display, and set count, respawn,
variance, radius, faction, NPC flags, equipment overrides, time window, and
condition hook.

Open **Waypoint Editor**:

1. Ctrl-click to append a waypoint or click an existing sphere to drag it.
2. Right-click for insert before/after, delete, and wait duration.
3. Toggle loop/reverse and generate terrain-following points where useful.
4. Show aggro (solid), leash (dashed), spawn (translucent), and direction arrows.
5. Set AI configuration, then use the preview target for deterministic patrol,
   chase, leash-return, and resume simulation.

For live core persistence, Studio only saves recognized runtime schema fields.
Patrol interpretation and preview-only values remain transparent metadata when a
stock core has no matching column.

## 7. Quests and event triggers

Use Quest Browser to associate quests with NPCs/items. Script Triggers supports
area entry/exit, interaction, proximity, and timer conditions. A trigger can
record a custom hook, event ID, SmartAI list reference, ordered delays, spell,
talk, and GameObject preview action.

Arbitrary 3D trigger volumes are not a universal stock TrinityCore/AzerothCore
world table. Export/copy the manifest and install a matching server module or
SmartAI wiring to execute gameplay behavior. Studio's preview is an accurate
authoring/simulation aid, not a misleading promise of automatic server support.

## 8. Spell Effect Previewer

Open **Spells → Spell Effect Previewer**. Search/select a spell; inspect the real
available `Spell.dbc` range, cast/cooldown, mana, projectile speed, `SpellVisual`,
and effect IDs; set target/caster/time/weather; then Play, Pause, Stop, Loop, or
scrub the timeline. The viewport renders a camera-aware cast ring, ballistic
projectile trail, and impact/sustain volume in real time, including in In-game view.
A `CastSpell` action in Script Triggers launches the same preview. It is deliberately
a procedural authoring fallback, not a false assertion that every historic
`SpellVisual` has a portable M2/particle/sound mapping; actual client effect assets
and real server casts still depend on configured client data and SmartAI/custom
server wiring. Use **Copy preview manifest** to transfer the selected IDs/timing to
an integration module.

## 9. World chunk copy/paste

1. Choose **Terrain → Chunk Operations → Copy Area**, drag a marquee, and choose
   terrain, textures, doodads, creatures, water, and relative-position options.
2. Position the ghost, mirror/flip/rotate it, choose Overwrite/Merge/Additive/
   Height-Based behavior, then confirm Paste.
3. Undo treats the complete paste as one command. Inspect IDs and path links when
   moving server-bound spawns to a different map.

## 10. Advanced techniques

- Save camera/bookmark positions for recurring review locations.
- Use macros for a repeated placement/terrain operation, then inspect undo history.
- Build biomes through texture presets + scatter seeds; retain the seed for repeatability.
- Use real-time lighting profiles for review, while keeping canonical game light
  data and Studio preview metadata clearly separated.
- Run **Tools → World Validation** before export: it inspects loaded client/project
  readiness, staged ADT edits, spawn transforms/display IDs, sub-yard spawn overlaps,
  waypoint movement/path consistency, formations, trigger targets/hooks, and authored
  light data without mutating the database or ADT files. Click a finding to select/frame
  the referenced object, then copy the Markdown report into a review or issue.

## Troubleshooting

| Symptom | Resolution |
| --- | --- |
| NPC marker but no model | Configure full client data; inspect NPC Instance for resolved display ID/source. |
| Camera moves while dragging gizmo | Rebuild current branch; gizmo drag capture blocks orbit/fly input. Report whether it is press/drag/release if it persists. |
| Missing client textures/models | Verify client path and case-normalized M2/WMO/BLP source. The starter assets are only placeholders. |
| Save refuses or SQL omits fields | Check live schema/core flavor; Studio deliberately skips unsupported columns. |
| Trigger does not run in game | Install/export matching custom hook or SmartAI configuration; visual trigger volumes are Studio metadata. |
| Terrain appears only after save | Realtime terrain preview is live; save ADT edits to bake MCVT/MCNR to the loose overlay. |

## FAQ

**Can I use this as an official Blizzard editor?** No. It is an independent,
open-source tool and must use only assets you are licensed to use.

**Does it support AzerothCore?** Yes, Studio detects current AzerothCore schemas
and uses live column introspection. Test against your exact revision.

**Does every preview save to a stock server database?** No. Terrain/spawn/schema
features with native representations persist; lights/triggers/some simulations may
be map-scoped Studio metadata unless a server adapter is installed.

**How do I recover after a crash?** Use the latest autosave/recovery project.
Normal saves write binary attachments first and replace the JSON manifest last.
