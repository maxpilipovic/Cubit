# Cubit Engine Roadmap

_Last updated: 2026-08-20_

A living view of what the Cubit engine has, what it still needs to be a complete
voxel engine, and the order we intend to finish it in. Most of it is scoped to
the *voxel* engine, with gameplay itself — teams, combat, networking — out of
scope. The exception is "Beyond the voxel engine" below, which records the
engine-side systems a multiplayer FPS will need and that a single-player voxel
sandbox never asked for.

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
8. ~~**Camera aim API**~~ **DONE 2026-08-16** — `PerspectiveCameraController::SetRotation`
   aims the camera through the controller that owns yaw and pitch (setting the camera
   directly works only until the next mouse move recomputes from the controller's
   stale copy), and `PerspectiveCamera::YawPitchToward` derives that pair from a point
   to look at, inverting the convention that makes `-90°` mean `-z`. Alongside them,
   `FindSpawn` (`Cubit/Voxel/SpawnFinder.h`) resolves an `x,z` hint into a standable
   position: it takes the first solid block from the top of a column, stands the box
   on it, rejects a surface under water, and spirals outward through rings of
   increasing Chebyshev distance when a column will not do. The Sandbox composes the
   three — resolve the ground, face the map centre level with the eye.

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

**The arc is complete, and so is the engine gap list.** Item 8, the camera aim API,
closed on 2026-08-16 — the last thing on this document that was still open. Picking a
spawn used to mean searching for a spot whose view along the camera's fixed `-z`
facing happened to be worth looking at, and getting it wrong rendered a black screen
that read as a rendering bug. A spawn now finds its own ground and its own facing.

What is left is **P8 — load cost**, and then gameplay. P8 was measured on 2026-08-16
and turned out not to be the problem it was written up as: the documented fix was
threading the mesher, but meshing was only 15% of load while `SkyLight::PropagateAll`
was 57%. Rewriting the flood as a downward column scan took it from **19.6 s to 3.7 s**
in a debug build and **halved total load** (33.1 s → 17.2 s), with the resulting light
proved identical cell for cell against a reference implementation.

The largest remaining piece is now **`parse` + `BuildWorld` at 49% of debug load**,
which has never been optimised. See [performance.md](performance.md) P8 and the
[load-cost breakdown](superpowers/investigations/2026-08-16-load-cost-breakdown.md).

**What the greedy-meshing attempt taught us.** The ordering note above said AO
should land before greedy meshing, so the merge criterion could be written
AO-aware from the start. That was right as far as it went, but it understated the
consequence: requiring matching AO *and* light does not merely complicate the
merge criterion, it removes most of the opportunity. Per-vertex shading and greedy
meshing are close to mutually exclusive on lit outdoor terrain — the faces worth
merging are the ones a shading gradient disqualifies.

Worth remembering before adopting another technique whose headline figure comes
from engines with flat-shaded faces.

## Beyond the voxel engine — gaps found 2026-08-20

Everything above is about voxels: meshing, lighting, load time, map format. That
list is genuinely nearly closed. What follows is the other axis — the systems a
multiplayer FPS needs from an engine that a single-player voxel sandbox never
asked for. None of it is on the arc above, because the arc was scoped to voxels.

**These are candidates, not a plan.** Recorded so they are not rediscovered, and
deliberately split by whether they get more expensive to delay.

### Worth doing before gameplay

Only three, and each for a specific reason rather than general tidiness.

1. **Fixed timestep, and a delta-time clamp.** `Application::Run` passes raw
   wall-clock delta straight to `Layer::OnUpdate`. Two problems. The clamp is the
   small one: `glfwPollEvents` blocks for the whole of a window drag on Win32, so
   the next frame's delta is however long the window was held, and Sandbox's
   gravity integrates across all of it. It does not tunnel — `MaxStep = 0.25` in
   `VoxelCollision` saves that — but it is a hitch and a snap to the ground, and
   an F9 reload has the same shape because the reload runs inside the key
   callback.

   The real one is the fixed tick. Client-side prediction and server
   reconciliation require the client and the server to run *the same*
   simulation step over the same input; a variable delta makes that impossible.
   This is the single item here that gets dramatically more expensive to delay,
   because every line of gameplay written against a variable `Timestep` has to be
   rewritten when it lands. Accumulator loop, fixed step, render interpolates
   between the last two states.

2. **Debug line and box rendering, plus a `KHR_debug` callback.** There is no way
   to draw a line or a wireframe box, so a frustum, an AABB, a raycast, or
   `FindSpawn`'s outward spiral cannot be looked at — every visual bug so far was
   found by screenshotting and squinting. Separately, `glGetError` and
   `GL_DEBUG_OUTPUT` appear nowhere in the codebase: a GL error today is silent.
   Both are small, and both pay for themselves on the next bug rather than
   eventually.

3. **A `BlockEdit` value type.** Not an entity system — just making an edit a
   piece of *data* that can be applied, sent, and replayed, instead of
   `Sandbox.cpp` calling `World::SetBlock` directly. It is the seam replication,
   undo, and demo recording all key off, and it is a small change now against a
   refactor of every call site later.

### Let the game pull these

Real gaps, but building them now means designing against a guess. Each becomes
obvious, and better shaped, the moment something concretely needs it.

- **An entity or actor concept.** The player is three fields on `SandboxLayer`
  (`m_PlayerPosition`, `m_VerticalVelocity`, `m_Grounded`) with the character
  controller inlined in `Sandbox.cpp`. Nothing represents "a thing in the world
  with a position and a box", which is what other players, projectiles, and
  replication all need. But there is exactly *one* entity today, and an
  abstraction over one instance is a guess. Extracting the character controller
  out of the Sandbox is worth doing on its own; a general entity system is not,
  yet.
- **A way to draw geometry that is not a chunk.** `Renderer::Submit` is generic,
  but it is the only seam — every caller hand-builds its own `VertexArray`
  (`WorldRenderer`, `HudLayer`). There is no `Mesh` type, no model loading, no
  transform hierarchy, so a player model or a held weapon has nowhere to come
  from.
- **A render-target abstraction.** No FBO anywhere. Blocks post-processing,
  shadow maps, and screen effects. Visible symptom today: underwater fog is
  per-vertex in the world shader, so it fogs geometry but not the clear colour —
  the sky stays dry-looking from under the river.
- **An asset layer.** All four shaders are raw string literals in Sandbox
  sources. `Texture2D` takes pixels only; nothing loads an image file and there
  is no image decoder in `vendor/`. Paths are working-directory-relative
  constants.
- **Configuration.** Resolution, field of view, mouse sensitivity, `SpawnHintXZ`
  and the map path are compile-time constants. Changing sensitivity is a rebuild.
- **Profiling instrumentation.** Every figure in `performance.md` came from
  timing code written ad hoc and then deleted; there is no scope timer in the
  engine. Given that `parse` + `BuildWorld` is the documented next target, that
  harness is about to be written a fourth time. A scope macro emitting Chrome
  trace JSON would make P4, P5 and the parse work measurable without a bespoke
  rig each time.
- **Lifetime handles on `EventBus` and `LayerStack`.** `Subscribe` stores
  `this`-capturing lambdas with no way to remove them, and there is no
  `PopLayer`/`PopOverlay` at all. Safe today only because nothing is ever
  removed; a menu, a map transition, or a disconnect makes it a dangling call.
  (`Publish` also copies the whole callback vector on every publish.)
- **Threading.** No `<thread>`, `<mutex>` or `<atomic>` anywhere. Known for
  meshing; note that `parse` + `BuildWorld`, the actual largest load cost, is
  also trivially parallel.

### Two things this list should not be read as saying

**Networking is not an eighth bullet.** It is a second project of roughly the
size of everything built so far: prediction, reconciliation, lag compensation for
hitscan, delta-encoded terrain edits, a headless authoritative server, snapshot
interpolation. The fixed timestep above is a *precondition* for it, not a down
payment on it. The one piece of good news is that `Cubit/src/Voxel/` includes no
GL headers at all — the simulation core already runs without a context, which is
the hardest part of a headless server to retrofit and is done. What stands in the
way is `Application` hard-creating a window and calling `Renderer::Init`.

**And this list does not terminate on its own.** The arc above declared the
engine gap list empty on 2026-08-16, and this section immediately added ten more
items; a further pass would add ten after that. Engine work is unbounded by
nature. The only thing that closes it is a game saying what it actually needs,
which is why all but three of these deliberately wait.

## Follow-ups the engine work deliberately left alone

**The forts do not scale with the map.** `TerrainGen` places them at
`FortEdgeOffset = 8` from the x edges with `FortRadius = 5`, in absolute blocks.
On a 512-wide map that is two 10-block specks 496 apart, tucked into opposite
edges — the same footprint that read as a battlefield at 256 reads as nothing at
all at 512. Stitching deliberately did not touch it: how big a fort should be,
and how far apart, is a question about how the game plays, not about how a map is
stored. The same goes for the rest of `TerrainGen`'s absolute-sized features.

**Spawning is map-aware, but the hint is still authored.** As of 2026-08-16
`Sandbox.cpp` holds `SpawnHintXZ`, a column rather than a point: the height and
whether that column is usable at all are resolved against the loaded map, so a hint
over a hill or the river moves to the nearest spot that can hold the player instead
of burying the camera. What is still by hand is *which column to suggest* — nothing
proposes an interesting starting view on its own, and nothing knows which side of the
map a player belongs on. That last part is a team question, so it waits for gameplay.

Two behaviours of `FindSpawn` are deliberate rather than unfinished: a tree canopy is
a valid spawn (the engine has no notion of "leaves", and the topmost surface is always
standable — the same property that makes a solid-overlap check unnecessary), and caves
are never spawned into, for free, because the scan finds a cave's roof first.
