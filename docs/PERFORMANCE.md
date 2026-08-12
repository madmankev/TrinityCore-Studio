# WowEdit performance baseline

This document records repeatable **CPU-side** development checks, not a claim that
an unprofiled GPU host reaches a particular frame rate. Measure release builds on
the target workstation with the configured client assets before accepting a map.

## Latest sandbox baseline

Command:

```bash
./WowEditPerformanceBenchmark
```

Result from this repository's C++17 `-O2` Linux sandbox validation:

```text
Placed 100000 doodads in 86.6364 ms
```

The benchmark deliberately exercises command-backed placement and incremental
Octree insertion. It does **not** issue Vulkan draw calls, decode M2 assets, or
measure a 100k-object visible frame. It verifies that brush/scatter placement does
not rebuild the entire spatial index for each new item.

## Release acceptance procedure

1. Build `Release` with the target Vulkan backend and release GPU driver.
2. Open the provided Starter Island, then a representative client-data map.
3. Capture GPU/CPU frame timing with the Debug/Profiler panel at 10k visible
   doodads, 100 streamed chunks, NPCs/GameObjects, water, and overlays enabled.
4. Run scripted terrain strokes and measure command submission and undo latency.
5. Record RSS/VRAM after a 30-minute edit session and inspect validation/log output.

| Metric | Minimum target | Stretch target | Measurement owner |
| --- | ---: | ---: | --- |
| 10k-object world FPS | 60 FPS | 144 FPS | Vulkan renderer GPU timestamps |
| <100 chunk map load | <5 s | <1 s | streamer timings |
| Full-zone RAM | <2 GB | <512 MB | OS/process profiler |
| Undo latency | <50 ms | <10 ms | command profiler |
| Brush feedback | <16 ms | <16 ms | tool + upload profiler |

## World Editor render controls

The live World Editor now avoids treating every loaded ADT as one always-visible mesh:

- Each MCNK carries a conservative local bounding sphere.
- The Vulkan world pass frustum-culls MCNK ranges and submits only visible ranges through one
  `vkCmdDrawIndexedIndirect` multi-draw per terrain tile.
- Ground textures discovered while a tile streams in are uploaded in one batch, and terrain VBO,
  IBO, and chunk-parameter buffers share one staging submission. This reduces queue-idle hitches
  while crossing into new terrain.
- **Realtime Preview → Viewport performance** exposes a persisted 50–100% render-resolution
  control. **Balanced** defaults to 85%; the scene is upscaled only for the ImGui image, while
  camera matrices, picking, and gizmo coordinates remain full viewport precision.

The viewport HUD reports GPU time, CPU `BuildFrame` time, render resolution, visible terrain
chunks, and culled terrain chunks. Use those numbers before lowering content ranges: if GPU time is
high and cull count is low, reduce render resolution or stream radius; if CPU build time is high,
reduce NPC/GameObject caps or inspect active overlay/simulation tools.

If a target misses, capture the render-plan batch count, draw calls, visible/culled terrain chunks,
texture-memory use, streamed tile count, render-resolution scale, and command-history size before
changing algorithms.
