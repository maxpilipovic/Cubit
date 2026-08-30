# Cubit Engine Roadmap

_Last updated: 2026-08-27_

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

**Load is finished, 2026-08-27.** Four fixes in one day took debug load from 33.1 s
to **1.42 s**, a 96% cut: P9 read the map file in one call instead of a byte at a
time, P10 stopped `BuildWorld` marking six million chunks dirty redundantly, and P11
moved sky light off `World`'s divide-and-modulo addressing onto a flat padded array.
What makes this the end of the arc rather than another inversion is that **no phase
dominates any more** — the file read, `BuildWorld` and `PropagateAll` are within
1.3x of each other and none is doing anything obviously wasteful. Every previous
round was found by one phase being three to twenty times the others.

Each of those three rounds also began by correcting a written prediction on this
page or `performance.md`, which is the durable lesson: P8 predicted threading the
mesher and the cost was the flood; P10 predicted ~130 ms and got 427; P11's own
entry predicted the scan dominated and it was the flood, at 51%. Every one of those
was inferred from a timer wrapped around too much at once. **Measure the phase you
are about to change, not the function containing it** — the profiler exists now, so
this costs one build and one run.

The largest single cost in getting a map on screen is now **meshing**, ~5 s in a
debug build, which is larger than the whole of load. It does not stall — the 4 ms
budget slice spreads it over frames — but it is the half-minute of the world
visibly building itself around you. That is P1's remaining half, and threading it
is the only performance item left with real leverage.

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

1. ~~**Fixed timestep, and a delta-time clamp.**~~ **DONE 2026-08-22** — a new
   `FrameClock` turns the variable wall-clock frame delta into a whole number of
   fixed 1/60s steps plus an interpolation alpha, capping surplus at 5 ticks per
   frame; a stall past that cap is discarded rather than repaid, so a long hitch
   skips wall-clock time instead of draining in slow motion — the clamp problem
   named above is handled by construction rather than by a separate check.
   `Layer::OnUpdate` was removed outright rather than redefined, split into
   `OnFixedUpdate(Timestep)` for simulation and `OnFrameUpdate(Timestep)` for
   per-frame work, with `OnRender` gaining an `alpha`; removal was deliberate —
   redefining in place would have left every existing override silently running
   on the wrong clock, where removal forced each call site to say which one it
   wanted. `Application::Run` fans out the tick loop outer and the layers inner,
   so every layer takes one step before any layer takes the next. The Sandbox
   camera now interpolates between the previous and current fixed-step player
   position by `alpha`, with every discontinuous move (spawn, F9 reload, and
   the like) routed through a `TeleportPlayer` helper that snaps both positions
   so they are never interpolated through. There are three call sites — the
   constructor, the fall reset, and `LiftPlayerClearOfTerrain` — the last of
   which used to produce two separate outcomes (the lift result and the
   solid-column fallback) before this rewrite collapsed them into one
   teleport; that rewrite is also what closed the last gap, because the old
   version mutated `m_PlayerPosition` in a loop and would have left the
   previous position stale after an F9 reload. On screen at 144 fps against
   the 60 Hz tick, frames outrun steps by roughly 2.4x, so most frames run
   zero ticks and `STEPS` is seen alternating between 0 and 1 — roughly 0, 0,
   1 repeating — rather than sitting at a steady 1.
   Two checks remain unverified rather than passed, because keyboard input
   cannot be delivered to the window from a script: jumping and dragging the
   window while airborne (confirming no snap to the ground on release), and the
   F9 reload path (confirming the player stays put with no camera smear). Both
   are left for the user to check by hand.

   What this item does not do is bind input to the numbered ticks a client
   predicting its own movement would replay — the fixed step is the
   precondition for that, not the prediction itself, and that binding belongs
   with the `BlockEdit` value type below (item 3), once an edit is a piece of
   data rather than a direct call.

2. ~~**Debug line and box rendering, plus a `KHR_debug` callback.**~~ **DONE
   2026-08-22** — a `DebugLineBatch` accumulates line and wireframe-box
   geometry on the CPU with no GL headers in sight, so it is unit tested
   rather than eyeballed; the test that actually matters checks that each of
   a box's 8 corners has edge-degree exactly 3, because a wrong edge table
   still produces 24 vertices all sitting on real corners, so neither a count
   nor an on-a-corner check would have caught it (mutation-tested by breaking
   an edge and watching degrees come back 4,4,2,2). `DebugDraw` wraps one
   batch plus lazily created GPU resources behind static calls, so
   `SpawnFinder` and `VoxelCollision` can draw without a renderer reference
   and `Cubit/src/Voxel/` stays GL-free; the flush is explicit and takes a
   camera rather than running automatically at end-of-frame, because
   `HudLayer` leaves an orthographic matrix current when it renders last, and
   an implicit flush would have drawn world-space lines in HUD screen space.
   The Sandbox now outlines the voxel the edit ray is aimed at, using the same
   ray the editing path uses — confirmed on screen as a wireframe box
   correctly occluded against neighbouring blocks, with no z-fighting.

   The design had argued for keeping the 3.3 context and probing
   `glDebugMessageCallback` for null, reasoning that most drivers expose
   `GL_KHR_debug` even on 3.3. That was wrong, and not for a driver reason:
   GLAD had been generated without the `GL_KHR_debug` extension, so
   `glad_glDebugMessageCallback` is only ever assigned inside
   `load_GL_VERSION_4_3`, which returns early below a 4.3 context. On a 3.3
   context the pointer is null on every driver regardless of what the driver
   actually supports — there was no probe that could have succeeded. Debug
   builds now request a 4.3 core context while Release keeps 3.3, shaders
   stay `#version 330`, and the log confirms `OpenGL debug output enabled`
   with zero driver messages since.

   Left out: a `Frustum` debug helper, because Cubit's `Frustum` stores six
   planes rather than eight corners and recovering corners means intersecting
   plane triples; and thick lines, which need quad expansion since
   `glLineWidth` above 1.0 isn't supported in core profile.

3. ~~**A `BlockEdit` value type.**~~ **DONE 2026-08-22** — `BlockEdit` is one
   block and one position, applied through `ApplyBlockEdit(World&, const
   BlockEdit&)`, which returns `std::optional<BlockEdit>` — the inverse —
   rather than the design carrying a `Previous` field: a client sending an
   edit cannot honestly report the previous block, it only believes it knows,
   so the previous value comes back from applying instead. Applying also
   relights, calling `SkyLight::Repropagate` before returning, so an edit is
   one operation rather than a ritual whose second half every caller has to
   remember — a caller that forgot would produce wrong light that reads as a
   lighting bug, not a missing call. An out-of-range position returns
   `nullopt` rather than throwing the way `World::SetBlock` does, because once
   an edit is data that can arrive from a file or a socket, a bad coordinate
   is malformed input rather than a caller bug; and a no-op — setting a block
   to what it already is — is rejected the same way, which is what keeps undo
   meaningful: an inverse that does nothing when popped would make the player
   press `U` twice for one change.

   The Sandbox proves the seam rather than just compiling against it:
   `SandboxLayer` now routes its edit through `ApplyBlockEdit` instead of
   calling `World::SetBlock` directly, and keeps the returned inverses on a
   256-deep undo stack popped by `U`, cleared on reload because those
   inverses describe a world that no longer exists. Ten unit tests back
   it, and the one that actually carries the design builds a sealed air
   chamber under an intact roof, breaks the roof to flood it with light,
   applies the inverse, and checks the chamber is dark again cell by cell.
   Review found that assertion was initially unfalsifiable — a comparator
   stubbed to always report "no difference" passed the whole suite — so a
   further test now proves the comparator itself can fail before trusting it
   to prove anything else.

   Left out: redo, edit recording for demo playback, publishing edits on the
   `EventBus`, and tick numbering — `FrameClock` counts steps per frame, not
   total ticks, so a tick field would have nothing honest to fill it. With
   this item done, all three "worth doing before gameplay" entries are
   closed. What remains on that list is the "let the game pull these" set,
   which deliberately waits for a game to ask for it.

### Let the game pull these

Real gaps, but building them now means designing against a guess. Each becomes
obvious, and better shaped, the moment something concretely needs it.

- **An entity or actor concept.** ~~The player is three fields on
  `SandboxLayer`~~ **Half done 2026-08-27.** The extraction this bullet called
  for has happened: `CharacterController` (`Cubit/include/Cubit/Voxel/`) owns the
  position, velocity, grounded and in-fluid state, with gravity, jumping,
  swimming and collision behind `Step`. `SandboxLayer` reads the keyboard,
  resolves the camera, and hands over a `CharacterInput` — so `Step` is a
  function of state, input and world, and sixteen tests exercise rules that
  previously could not be reached at all. It lives with the rest of the GL-free
  simulation core, so it already runs headless.

  **A general entity system is still not done, and still should not be.** There
  is exactly one character today, and an abstraction over one instance is a
  guess. What the extraction bought is that the guess is no longer forced: when
  a second actor appears, the thing to generalise is a type that exists rather
  than forty lines inlined in a layer.

  The input-as-a-value seam is the part that matters beyond tidiness. A
  character whose movement is a pure step over an explicit input is what
  prediction, replay and an authoritative server all need, and it was free to
  build that way now. See "Networking is not an eighth bullet" below.

  **Stage 1 of the networking arc shipped 2026-08-30**, adding `MatchState` above
  the controller: it owns the world, the roster and the tick, and both a server
  and a client will step it. Still not an entity system, and still deliberately
  so — one kind of actor.
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
- **Lifetime handles on `EventBus` and `LayerStack`.** `Subscribe` stores
  `this`-capturing lambdas with no way to remove them, and there is no
  `PopLayer`/`PopOverlay` at all. Safe today only because nothing is ever
  removed; a menu, a map transition, or a disconnect makes it a dangling call.
  (`Publish` also copies the whole callback vector on every publish.)
- **Threading.** No `<thread>`, `<mutex>` or `<atomic>` outside `Profiler.cpp`,
  which has them only for its own per-thread buffers — no engine work is
  threaded. **Meshing is now the one that matters**, at ~5 s of debug work
  spread across frames, larger than the whole of load. `parse` + `BuildWorld`
  was named here as the largest load cost; it is not any more (P9/P10/P11 took
  load to 1.42 s), and threading it would now be chasing a third of a second.

**Profiling instrumentation shipped early, 2026-08-27.** This list used to carry a
bullet for it, predicting that the ad hoc timing rig behind every figure in
`performance.md` — written by hand and deleted three times over — was "about to
be written a fourth time" now that `parse` + `BuildWorld` was the documented next
target. It was not. `CB_PROFILE_SCOPE` times named scopes into a Chrome trace,
compiled into Debug and Release and out of `CB_DIST`, and its first run split
`parse` into the file read and the actual parsing — a distinction each of the
three previous hand-rolled investigations had folded into one number. See
[performance.md](performance.md) and
[superpowers/specs/2026-08-25-profiler-design.md](superpowers/specs/2026-08-25-profiler-design.md).

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
