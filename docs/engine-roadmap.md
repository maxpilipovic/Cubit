# Cubit Engine Roadmap

_Last updated: 2026-08-11_

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
3. ~~**Greedy meshing**~~ **TRIED AND REJECTED 2026-08-06** — built, measured, and
   reverted. It cut geometry 20.7% but doubled meshing time in both Debug and
   Release, and draw calls are one per chunk either way, so the geometry saved
   never reached the frame time that would have to pay for it. Full numbers and
   reasoning in [performance.md](performance.md) P3.

### Visual quality
4. ~~**Lighting / ambient occlusion**~~ **DONE 2026-07-26** — per-vertex corner AO
   plus sky light propagated through the world (`SkyLight::PropagateAll`/
   `Repropagate`) replace the old fixed per-face shading constants.
5. ~~**Transparency / alpha blending**~~ **DONE 2026-08-08** — palette alpha marks
   a block non-opaque *and* non-solid. The mesher splits chunk geometry and the
   renderer draws the transparent set back to front; collision passes through
   water while the editing raycast still stops at it, so the river is something
   you wade into and can still dig out.
   Solidity is derived from alpha, so a block that is see-through is also
   walk-through. Glass — non-opaque but solid — is deliberately not expressible;
   adding it means giving solidity its own table and source, which changes how
   the table is filled and no call site.

### Format / smaller gaps
6. ~~**Multi-model stitching**~~ **DONE 2026-08-11** — `VoxLoader::Parse` collects
   every `SIZE`/`XYZI` pair, resolves each model's origin by walking the
   `nTRN`/`nGRP`/`nSHP` scene graph, and flattens the union into one dense
   `VoxModel`; `VoxWriter::Write` does the inverse, cutting a model larger than
   256 into a uniform 256 grid of tiles placed by a generated graph. A model that
   still fits in one `.vox` takes the old path and produces byte-identical
   output. `MaxDimension = 256` now bounds a single model, never the world.
   `Sandbox/assets/maps/battlefield512.vox` is 512×64×512 across four models and
   is what the Sandbox loads.
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
3. ~~**Transparency**~~ **DONE 2026-08-08** (opacity 2026-08-06, solidity 2026-08-08).
4. ~~**Greedy meshing**~~ **TRIED AND REJECTED 2026-08-06** — see P3.
5. ~~**Multi-model stitching**~~ **DONE 2026-08-11** (full AoS-scale maps).

**The arc is complete.** The engine is "done" for our purposes: the one remaining
engine gap is the **camera aim API** (item 8) — `PerspectiveCameraController` has
no yaw/pitch setter, so a spawn cannot choose its facing. That gap was felt
immediately on the 512 map: picking a spawn meant searching for a spot whose view
along the camera's fixed `-z` facing happened to be worth looking at. Everything
else ahead of us is gameplay.

**What the greedy-meshing attempt taught us.** The ordering note above said AO
should land before greedy meshing, so the merge criterion could be written
AO-aware from the start. That was right as far as it went, but it understated the
consequence: requiring matching AO *and* light does not merely complicate the
merge criterion, it removes most of the opportunity. Per-vertex shading and greedy
meshing are close to mutually exclusive on lit outdoor terrain — the faces worth
merging are the ones a shading gradient disqualifies.

Worth remembering before adopting another technique whose headline figure comes
from engines with flat-shaded faces.

## Follow-ups the engine work deliberately left alone

**The forts do not scale with the map.** `TerrainGen` places them at
`FortEdgeOffset = 8` from the x edges with `FortRadius = 5`, in absolute blocks.
On a 512-wide map that is two 10-block specks 496 apart, tucked into opposite
edges — the same footprint that read as a battlefield at 256 reads as nothing at
all at 512. Stitching deliberately did not touch it: how big a fort should be,
and how far apart, is a question about how the game plays, not about how a map is
stored. The same goes for the rest of `TerrainGen`'s absolute-sized features.

**Nothing about spawning is map-aware.** `SpawnPosition` in `Sandbox.cpp` is a
hard-coded point that has to be re-picked by hand whenever the map changes,
because a wrong one puts the camera inside terrain and renders a black screen
that looks like a rendering bug. A spawn that finds open ground for itself
belongs with the camera aim API.
