# Cubit Engine Roadmap

_Last updated: 2026-07-25_

A living view of what the Cubit engine has, what it still needs to be a complete
voxel engine, and the order we intend to finish it in. Gameplay (players as
entities, teams, combat, networking) is deliberately **out of scope** here — this
document is about finishing the *engine* first.

## What Cubit is today

A single-player voxel sandbox engine. You load a map, walk around under gravity with
collision, and place/break blocks. The core is complete and clean:

- **Platform / core loop:** `Application`, `Layer`/`LayerStack`, `Window`, `Input`,
  `EventBus`, `Timestep`, logging, asserts.
- **Rendering:** OpenGL context, vertex/index buffers, `VertexArray`, `Shader`,
  `Texture2D`, ortho + perspective cameras, `Renderer`, `WorldRenderer` (per-chunk
  meshes with dirty-chunk remeshing), HUD text (`DebugFont`, `HudLayer`).
- **Voxel systems:** `Block`/`Chunk`/`World` (fixed chunk grid, palette-indexed
  blocks), `ChunkMesher` (neighbour-aware, face-culled), `VoxelRaycast`,
  `VoxelCollision` (box physics), dirty-chunk tracking.
- **Content pipeline:** `VoxLoader` (load `.vox`), `VoxWriter` (save `.vox`),
  `TerrainGen` + `MapGen` (procedural map generation). See
  `docs/superpowers/specs/2026-07-25-battlefield-map-design.md`.
- **Tests:** doctest suite (`Tests/`) run as a build step.

We are roughly **two-thirds** of the way to a complete voxel engine: the skeleton is
done, but several real systems remain, mostly in rendering performance and visual
quality.

## Remaining engine systems

Ranked by leverage. Performance items are detailed in
[performance.md](performance.md).

### Performance / scale
1. **Threaded / amortized meshing** — the whole world currently meshes in one frame
   at load (`WorldRenderer::Update`). Gating bigger maps. **← next up.**
2. **Frustum culling** — `WorldRenderer::Render` draws every chunk every frame,
   regardless of the camera view.
3. **Greedy meshing** — the mesher emits one quad per exposed face; coplanar faces
   are never merged, so flat areas produce far more geometry than needed.

### Visual quality
4. **Lighting / ambient occlusion** — currently fixed per-face shading constants
   (`ChunkMesher` `TopShade`/`BottomShade`/…). Smooth lighting + AO is the single
   biggest step up in how "real" the world looks.
5. **Transparency / alpha blending** — none today; needed for real water and glass
   (water currently renders as an opaque solid).

### Format / smaller gaps
6. **Multi-model stitching** — load maps larger than 256³ (true Ace-of-Spades
   512×512 scale needs several stitched `.vox` models).
7. **World persistence** — save an *edited* world back to `.vox`. Nearly free now
   that `VoxWriter` exists (World → `VoxModel` → `Write`).
8. **Camera aim API** — `PerspectiveCameraController` exposes only `SetPosition`, no
   yaw/pitch setter, so a spawn cannot choose its facing.

### Likely non-goals (given the flat-colour, fixed-map aesthetic)
- Per-block **textures** (blocks are palette colours by design).
- **LOD / streaming** (maps are a fixed known size — this is why `World` is a fixed
  grid).
- **Audio** (belongs with gameplay, not the core engine).

## The "finish the engine" arc

A bounded sequence — genuinely finishable, not endless:

1. ~~**Amortized meshing + frustum culling**~~ **DONE 2026-07-25** — the map now
   ships at 256×64×256 and loads without a stall. Threading still deferred.
2. **Ambient-occlusion lighting** (the visual leap). ← next.
3. **Transparency** (real water) + **world save** (small, high value).
4. **Multi-model stitching** (full AoS-scale maps).

After these, the engine is "done" for our purposes, and the remaining work is all
gameplay.
