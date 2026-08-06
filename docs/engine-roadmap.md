# Cubit Engine Roadmap

_Last updated: 2026-08-06_

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
1. ~~**Threaded / amortized meshing**~~ **DONE 2026-07-25** — `WorldRenderer::Update`
   meshes at most a per-frame budget of chunks instead of the whole world at load.
   True threading remains deferred.
2. ~~**Frustum culling**~~ **DONE 2026-07-25** — `WorldRenderer::Render` tests each
   chunk's world-space AABB against the camera frustum before submitting.
3. **Greedy meshing** — the mesher emits one quad per exposed face; coplanar faces
   are never merged, so flat areas produce far more geometry than needed. **← next
   perf item** (see the AO ordering note below).

### Visual quality
4. ~~**Lighting / ambient occlusion**~~ **DONE 2026-07-26** — per-vertex corner AO
   plus sky light propagated through the world (`SkyLight::PropagateAll`/
   `Repropagate`) replace the old fixed per-face shading constants.
5. ~~**Transparency / alpha blending**~~ **DONE 2026-08-06** — palette alpha marks
   a block non-opaque; the mesher splits chunk geometry and the renderer draws the
   transparent set back to front with depth writes off. Water is see-through and
   its bed is lit. Water is still *solid* — collision and raycast are Phase 2.

### Format / smaller gaps
6. **Multi-model stitching** — load maps larger than 256³ (true Ace-of-Spades
   512×512 scale needs several stitched `.vox` models).
7. ~~**World persistence**~~ **DONE 2026-07-31** — `ToVoxModel` plus
   `VoxWriter::WriteFile` write an edited world back to `.vox`; the Sandbox binds
   it to `F5`.
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
2. ~~**Ambient-occlusion lighting**~~ **DONE 2026-07-26** (the visual leap).
3. ~~**Transparency**~~ **DONE 2026-08-06** (opacity; solidity is Phase 2).
4. **Greedy meshing** — the last perf item, and the last thing touching
   `ChunkMesher`. ← next.
5. **Multi-model stitching** (full AoS-scale maps).

After these, the engine is "done" for our purposes, and the remaining work is all
gameplay.

**Ordering note — AO before greedy meshing.** Both touch `ChunkMesher`, and they
interact: per-vertex AO means two coplanar faces can only be merged if their AO
values match. Doing AO first lets the greedy pass be written AO-aware from the
start, instead of retrofitting the merge criterion afterwards.
