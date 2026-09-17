# Cubit Engine Roadmap

_Last updated: 2026-09-14_

A living view of what the Cubit engine has, what it still needs to be a complete
voxel engine, and the order we intend to finish it in. Most of it is scoped to
the *voxel* engine, with gameplay itself — teams, combat, networking — out of
scope. The exception is "Beyond the voxel engine" below, which records the
engine-side systems a multiplayer FPS will need and that a single-player voxel
sandbox never asked for.

## Before the game — engine punch list (2026-09-14)

An audit on 2026-09-14 checked the engine against `Documentation/Cubit.pdf` and against
this page, once the networking arc had nothing left open. **Everything here is to be done
before the match-rules arc starts.** Tick items as they land and say how; do not delete
them. Every item was checked in the code unless it says otherwise. References are as of
`91a08e6`.

### A. Wrong today — bugs and robustness

- [x] **A1. A server stall may add input delay for the rest of the session.** **Confirmed
  and fixed 2026-09-14.** A test measuring what other players see — ticks from the client
  starting to walk until the server's copy of the player moves — read 4 before a 500 ms
  stall and 9 five seconds after it. `FrameClock::DiscardedTicks` now reports the whole
  ticks a frame dropped, `Server.exe` passes them to `MatchServer::SkipTicks`, and that
  skips as many of each client's oldest queued inputs; the test reads 4 and 4. Two more
  changes turned out to be needed: an overflowing queue now drops its oldest inputs rather
  than refusing the newest (keeping the oldest left a gap the next bundle refilled with two
  stale ticks, a measured lasting 2-tick remainder), and any edit on a thrown-away input is
  refused (see A7). **Tried first and rejected:** trimming whenever the queue stayed deep
  for a window. A jittery link normally holds two queued inputs, and cutting that buffer
  broke three passing tests — the lag-compensation precision oracle, the pillar gate and
  the 5% loss correction count — so the skip fires only when the server actually lost
  time, and clock drift is left to A8. Jitter alone was measured not to build a backlog:
  6 ticks at the start of 2,000 ticks at 5% loss and 1-tick jitter, 7 at the end.
  The original entry follows. `FrameClock` discards any surplus past
  `MaxTicksPerFrame = 5` (`FrameClock.h:18`), so a server hitch longer than 83 ms loses
  ticks. The client's tick free-runs from the welcome (`MatchClient.cpp:242`) and is never
  resynchronised. The server applies one queued input per tick, holding at most
  `MaxQueuedInputs = 8` (`MatchServer.cpp:21`). So inputs sent during a server stall pile
  up, and afterwards one arrives for every one applied: the queue would stay deep and every
  input up to 8 ticks (133 ms) late. A client-side stall is harmless — a 204 ms Alt+Tab was
  measured at zero corrections in Stage 4. **Done when:** a `SimulatedTransport` test with
  the server skipping steps shows what the queue does after a stall, and, if it stays deep,
  a fix returns it to its pre-stall depth, pinned by that test failing without the fix.
- [x] **A2. A crash leaves no record.** **Fixed 2026-09-16.** `CrashHandler::Install`, called
  at the top of `main` in Sandbox, Server and MapGen, installs a terminate handler and an
  unhandled-exception filter. An exception nobody catches is logged as
  `Terminating: uncaught exception <type>: <message>`; a native fault as `Crashed: <what>
  at <module>+<offset>`, with an access violation's address and whether it read or wrote.
  Both are followed by a symbolised stack, and both write a minidump (about 100 KB) to
  `crashes/<program>-<date>-<time>-<pid>.dmp`. The filter hands C++ exceptions back to the
  runtime's own filter, which is what calls `std::terminate` with the exception still
  current, so the terminate handler can name it and the stack still shows the throw.
  It ends the process with `TerminateProcess`, not `std::abort`, because Debug `abort`
  raises a modal dialog that would hang an unattended server. **Tests:** crashes cannot be
  tested in-process, so `Tests.exe --crash-probe=<kind>` installs the handler and crashes
  on purpose, and three tests run it as a child process and read its log and dump
  directory: a `std::runtime_error`, a thrown `int`, and a write through null. They pass in
  Debug and Release, and went red with the two headline log lines removed. **Limits:** a
  stack overflow is not reliably recorded (the handler has no stack to run on); the
  terminate handler covers only the thread that called `Install`, because MSVC keeps it per
  thread (every thread the engine has today); `Server.exe` still catches `std::exception`
  in `main` for a clean exit, so a startup failure such as a missing map is logged there
  without a dump. The original entry follows. `Application::Run` (`Application.cpp:76`) has no
  try/catch, and nothing installs a terminate handler, an unhandled-exception filter or a
  minidump writer. An exception thrown from any layer ends the process after whatever it
  last logged. Scope doc TOL-05. **Done when:** an exception out of a layer is logged with
  its message before the process exits, and a native crash leaves a dump or stack trace.
- [ ] **A3. The cursor can never be released.** The Sandbox captures it once
  (`Sandbox.cpp:132`) and nothing gives it back: no Escape binding, and the window's focus
  events (`WindowsWindow.cpp:128`) are not used. Its visible cost: clicks meant for another
  window land in the game as edits. **Done when:** Escape releases the cursor, a click
  recaptures it, losing focus releases it, and a click that recaptures does not also edit
  or fire.
- [x] **A4. Log lines have no timestamps and never reach a file.** **Fixed 2026-09-16.**
  `Logger` and `CoreLogger` now write through one internal `LogSink`, so both channels share
  a clock, a lock and a file, and interleave in the order things happened. Every line
  reads `HH:MM:SS.mmm [CHANNEL] [Level] message`. The time is wall-clock local time, not
  time since start, because lining a client's log up against the server's is what it is
  for, and two processes share only the wall clock. `Logger::OpenFile(program)` copies
  every later line to `logs/<program>-<yyyymmdd-hhmmss>-<pid>.log`, flushed per line;
  Sandbox and Server call it at the top of `main`, and MapGen, an offline tool that prints
  with `std::cout`, does not. A file that cannot be created is a console warning, not a
  failure. The crash handler's lines reach the file too, which the crash-probe tests check.
  **Tests:** the timestamp is checked against the clock (within 2 s), not just for its
  shape; a file gets lines written while open and none before or after; an uncreatable
  file leaves the console working. Each went red under a mutation (no timestamp, no file
  write, no directory creation). **Not done:** server lines still say `[CLIENT]`, because
  `CB_INFO` is the client channel (recorded in the Stage 2 ledger); log files are never
  deleted. The original entry follows. `Logger.cpp:17–40`
  writes channel, level and message to `std::cout` only. The 2026-09-14 loss investigation
  had to timestamp lines from the outside. **Done when:** every line carries a time, and a
  file sink exists.
- [ ] **A5. A refused send is dropped without a word, and a join has a size ceiling.**
  `EnetTransport::Send` destroys a packet ENet refuses (`EnetTransport.cpp:153`) and logs
  nothing. The case that matters: a welcome carries 14 bytes per changed cell
  (`Protocol.cpp:34`) against ENet's 32 MB packet limit (`enet.h:217`), so once about 2.4
  million cells — roughly 14% of a 512x64x512 map — differ from the map, a joiner silently
  never gets a welcome. **Done when:** a refused send is logged, and the ceiling is either
  lifted (chunk-based join is the recorded answer) or written down as an accepted limit.
- [ ] **A6. The docs describe an older engine.** `README.md` says the suite has 270 cases
  (it has 495), says the window is OpenGL 3.3 (Debug builds request 4.3), and its "What's
  next" lists shipped work (multi-model stitching) and puts networking in the future. This
  page's "What Cubit is today" below still says "single-player" and "two-thirds of the way".
  **Done when:** both describe the engine as it is.
- [x] **A7. An input dropped for a full queue never answers its edit.** **Confirmed and
  fixed 2026-09-16.** Every way the server discards an input now refuses the edit on it,
  and two client-level tests show the client ends with the server's block: one overflows
  the queue, the other holds back all three bundles carrying the edit's tick until the
  server has moved past it. The second was run before the fix and showed the desync the
  entry below predicted: the server kept the block, the client showed air, and the
  prediction never resolved. The server now keeps `Client::SeenInputs`, a 64-bit mask of
  which ticks at or below `LastInputTick` it has had. `PassInput` moves `LastInputTick`
  for all three paths (taken, skipped, dropped), shifting in zeros for the ticks it jumps
  over. The queue is sorted, so any jumped-over tick really was never received. A late
  input on a clear bit has its edit refused and the bit set, so its two repeat bundles stay
  silent. Past the 64-tick window (over a second) the server cannot tell, and refuses
  anyway: a refusal carries the server's current block, which is true whether or not the
  edit was answered before, and nothing on the client reads `Accepted`. Two guard tests,
  one for an applied input and one for a skipped input repeated by a later bundle, pin that
  a repeat is not answered twice. Each went red under a mutation of the design it guards.
  The original entry follows. _Found by reading
  while working on A1, 2026-09-14 — not yet run._ When a client's queue is full,
  `MatchServer` discards the incoming input (`MatchServer.cpp:235–246`) and sends nothing
  about any edit it carried. The client keeps a predicted edit until that edit's
  `EditResult` arrives (Stage 5 spec, "Replay"), and its bookkeeping bound forgets the
  record but not the block (`MatchClient.cpp:137–140`). So a dropped input more than one
  bundle old leaves a block on that client the server never placed — a desync, not a
  correction. **Done when:** a test sends an edit on an input that overflows the queue and
  shows the client ends with the server's block, and every input the server discards, for
  any reason, has its edit refused rather than ignored.
  **Partly done 2026-09-14, with A1.** Both paths A1 touched now refuse the edit on an input
  they throw away: `SkipTicks` after a stall, and the queue overflow, which now drops its
  oldest inputs. Each has a test that went red without the refusal ("An edit on an input
  skipped after a stall is refused, not ignored" and "... dropped from a full queue ...").
  **Still open:** an input whose tick is at or below the last one applied is ignored as a
  repeat (`HandleMessage`, `tick <= client->LastInputTick`). Normally it is one, and its edit
  was answered the first time. But if reordering and loss hold back every bundle carrying a
  tick until a newer tick has been taken, that input was never seen, and its edit is dropped
  unanswered. Older than today's change and rare — each tick rides in three bundles — and
  telling a never-seen input from a repeat needs the server to remember which ticks it has
  had.
- [x] **A8. A client whose clock runs fast fills its queue over a long match.** **Confirmed,
  found to be wider than written, and fixed 2026-09-16.** Measured first, at 3-tick latency:
  a client clock 0.2% fast took input delay from 4 ticks to 6, 8, 10 and then 11, where the
  queue cap started dropping inputs (4 corrections). A slow clock was harmless. The wider
  problem is that clock drift is only one way to bunch inputs. Holding the client's upload
  for 5 ticks (83 ms) took delay from 4 to 8, and for 30 ticks to 11, and both were unchanged
  3,000 ticks later. So one Wi-Fi hiccup cost a player up to 117 ms of delay, as others see
  them, for the rest of the match.
  **The fix is the client catching up, chosen by the user over the server trimming.** Each
  snapshot entry carries `SpareInputs` (protocol v5, 1 byte): the server samples each
  client's queue depth before every take over a 600-tick window and reports the thinnest
  depth, less 1 if the depth held steady all window, less 2 if it moved. The client then
  skips making that many inputs, `CatchUpSkipSpacingTicks` (10) apart. A skipped step holds
  the player still (previous position set to current, so nothing is interpolated twice)
  and advances the remote-render clock. It acts on a new report only once the server has
  acknowledged a whole window past its last skip, so one spare input is never skipped twice.
  No input is ever thrown away, so catching up costs no correction and refuses no edit.
  **Two numbers were set by measurement, not reasoning.** The first version used a 120-tick
  window with no reserve, and it broke the 5%-loss pillar gate (1 correction). A per-tick
  depth dump showed why: on lossy links the queue sits at 3–4 and runs down only when
  several bundles in a row are lost, which is often further apart than two seconds. Across
  every test link (about 50,000 ticks), the tested rules would have reported spare inputs
  this often: 120 ticks with no reserve, 204 times in the 35,000-tick hit-rate run; 600
  ticks with no reserve, 24 times; 600 ticks plus the reserve on a moving queue, 0 times
  anywhere. The cost is recovery time: a backlog drains starting a window (10 s) after it
  forms. **Tests:** the 30-tick upload spike now reads 4 before and 4 after; a 0.05%-fast
  clock over 20,000 ticks reads 4 at every sample with 0 corrections; server tests pin the
  window, the thinnest-not-latest rule and the reserve; client tests pin the count, the
  spacing, the hold and the stale-report rule. Each went red under a mutation of what it
  guards. Every existing gate reads as before: pillar and dig 0 corrections, clean link 0,
  lag-compensation precision unchanged. The 0.05% rate is used rather than 0.2% because an
  extra input every 500 ticks never leaves a steady 600-tick window, which no real clock
  does either. **Still true:** a jitter peak rarer than the window can be trimmed and cost
  one starved tick when it recurs; none of the suite's links showed one.
  The original entry follows. _Reasoned
  2026-09-14, not measured on real machines._ Each end counts ticks on its own wall clock
  through `FrameClock`, and nothing synchronises the two. A client clock just 0.01% faster
  than the server's sends one extra input about every 10,000 ticks — under three minutes.
  The server takes one input per tick, so each extra input stays queued as a tick of
  delay until the queue cap (8) starts dropping inputs, which costs corrections and, until
  A7 is done, can orphan an edit. A1's fix deliberately does not cover this: it skips
  inputs only when the server itself loses time, because trimming by watching queue depth
  was built first and cut the ordinary jitter buffer, breaking three passing tests. The
  answer the Stage 3 spec already names is approach C, an adaptive clock offset.
  **Done when:** a test with the client stepping slightly faster than the server measures
  what input delay does over a long run, and either shows it stays bounded or a fix keeps
  it bounded without cutting a healthy jitter buffer.

### B. Missing — systems the game will need

- [ ] **B1. Multi-block edits.** `BlockEdit` is one block (`BlockEdit.h:17`) and the
  protocol carries at most one edit per input, 60 a second (`Protocol.h:99`). Explosions,
  grenades and digging more than one block at a time all need more.
- [ ] **B2. Terrain collapse** — blocks left with no support fall. Nothing exists. It is
  what makes a game Ace of Spades-like, but it is **not in the scope doc**, so the first
  step is deciding whether it is in scope. If it is: it must be server-authoritative, and it
  meets both predicted edits (Stage 5's confirmed layer) and relight cost.
- [ ] **B3. A way to draw anything that is not a chunk.** There is no `Mesh` type and no
  model loading; remote players are `DebugDraw` wireframe boxes (`DrawRemotePlayers`,
  `Sandbox.cpp:570`). Player models, a held tool and team colours all need it.
- [ ] **B4. Text and UI beyond the debug font.** `DebugFont` is a 5x7 bitmap with no
  lowercase and no J, Q, X or Z (`DebugFont.h:24`). A scoreboard and menus need more.
  Scope doc ENG-07, POL-03.
- [ ] **B5. Lifetimes for subscriptions and layers.** `EventBus::Subscribe`
  (`EventBus.h:14`) stores `this`-capturing callbacks with no way to remove them, and
  `Publish` copies the callback vector on every call. `LayerStack` can push
  (`LayerStack.h:26–29`) but never pop. Menus, map rotation and leaving a match all remove
  things.
- [ ] **B6. Configuration and settings.** Mouse sensitivity
  (`PerspectiveCameraController.h:57`), field of view (`:49`), the map path
  (`Sandbox.cpp:110`), the spawn hint (`Sandbox.cpp:87`, with a second copy in
  `Server.cpp`) and the resolution are all compile-time constants. Scope doc ENG-06,
  PLY-02, POL-04.
- [ ] **B7. Audio.** No audio library in `vendor/` and no audio code. Scope doc POL-01,
  which the doc puts in the prototype band.
- [ ] **B8. A separate game target.** The projects are GLAD, GLFW, ENet, Cubit, Sandbox,
  MapGen, Server and Tests; game code has grown inside `Sandbox.cpp` (1,069 lines). Scope
  doc ENG-01 asks for engine, sandbox and game to build separately.
- [ ] **B9. Crouch and step-up.** `CharacterController` has neither; the README already
  notes there is no step-up assist. Scope doc PLY-01.

### C. Known and parked — do each, or drop it on purpose

- [ ] **C1. GPU buffers reallocated on every remesh** — P4 in
  [performance.md](performance.md), low priority, open.
- [ ] **C2. One draw call per chunk** — P5, low priority, open.
- [ ] **C3. Threaded meshing** — about 5 s of debug meshing spread across frames; the last
  performance item with real leverage.
- [ ] **C4. Render targets.** No framebuffer objects anywhere, so no post-processing or
  shadows; underwater fog tints geometry but not the sky.
- [ ] **C5. An asset layer.** Shaders are string literals in Sandbox sources
  (`Sandbox.cpp:189`), nothing decodes an image file, and paths are working-directory
  relative.
- [ ] **C6. Debug draw's two left-outs** from 2026-08-22: a `Frustum` helper and thick
  lines.

### D. Game-layer items raised in the same audit

Not engine work, recorded here so they are not lost. They belong to the match-rules arc
or just after it.

- [ ] **D1. Match rules:** teams, spawn ownership, match state (warmup, active, end), one
  objective, a scoreboard, and syncing all of it to joiners. Scope doc GAM-01–05, NET-06,
  NET-07.
- [ ] **D2. Tool slots.** Dig, place and fire are three mouse buttons; the scope doc wants a
  shovel, a build tool and a weapon that switch cleanly. PLY-04, PLY-05, PLY-07.
- [ ] **D3. Ammo, reload and spread** for the hitscan weapon. PLY-03.
- [ ] **D4. Forts that scale with the map.** `FortEdgeOffset = 8` is absolute
  (`TerrainGen.cpp:15`): on the 512-wide map the forts are 10-block specks at the edges.
- [ ] **D5. Team-aware spawning.** One authored spawn column for everybody.

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

  **Stage 2 shipped 2026-09-03.** The wire is real: a headless `Server.exe`, an
  ENet transport, six wire messages over a hand-rolled little-endian codec, and
  `Sandbox --connect`. Two clients share a world, see each other as wireframe
  boxes, and edit terrain everyone sees. There is deliberately **no prediction** —
  pressing W does not move the view until the server says so, which is the point:
  it proves the wire honestly before Stage 3 hides the latency, so any
  rubber-banding seen then is new rather than inherited. Measured cost is 3.9 KB/s
  down per client at 60 Hz snapshots. Suite 315 → 386. With no arguments the
  Sandbox is byte-for-byte the single-player app it was before, verified against
  the same `POS`/`FACES` values.

  **Stage 3 shipped 2026-09-05** (`d24c941..fa4599c`, suite 386 → 415). The client
  now predicts its own movement locally and is corrected against the server without
  the correction being visible — pressing `W` moves the view on the same frame even
  at 150 ms latency, measured at 10 ms from keydown to motion in a real three-process
  run. Remote players are never predicted or extrapolated; they are drawn from a ring
  of snapshot samples interpolated 100 ms behind the server's clock, holding the
  newest sample rather than guessing forward. Reconciliation converges by keeping the
  server's per-tick stepping uniform and bundling each client's last three inputs
  against loss, rather than by giving the server a variable step count per player.
  The in-process `SimulatedTransport` gate test, with no loss and no jitter, reconciled
  1,113 snapshots for zero corrections; a separate three-process localhost run
  (`Server.exe` plus two `Sandbox.exe --connect --latency 150` clients, real sockets, no
  induced loss) reconciled 3,851 and 3,625 snapshots on its two clients for zero
  corrections apiece. Under 20% packet loss injected in the gate test — well past this
  design's target — 3 corrections per 1,000 ticks, comfortably under half a block.

  **Stage 4 shipped 2026-09-12** (`0a6121c` onward, suite 416 → 458). Players can shoot
  each other: middle mouse fires a hitscan shot, three hits kill, and death respawns
  instantly. The server resolves each shot against where the target was on the
  shooter's own screen — the client declares the fractional instant it was drawing,
  and the server rewinds a per-player hitbox history to it, capped at 250 ms. At
  100 ms RTT with 5% loss, 60 of 60 shots aimed at a strafing target hit, against 5 with
  the rewind off; every shot whose rewind fits the window is resolved within a
  millionth of a block of the pose the shooter drew. At 166.7 ms, 189 of 200 hit: the
  misses are all lost-and-resent shots that fall outside the cap. Played by hand
  across three processes at 150 ms, tracers are immediate and hit markers arrive a
  round trip later. The measurement that mattered most was a failure of measurement:
  the hit-rate gate passed while every rewind was a tick short, because hits cannot
  see an error smaller than the target, so the gate now checks position as well.

  **Stage 5 shipped 2026-09-13** (`63c7966` onward, suite 461 → 487). A player's own terrain
  edits are predicted: a placed block appears the moment it is clicked, and the edit rides
  inside that tick's input so the server applies it on exactly the step the client did.
  Both ends run the same reach and overlap rules, so an illegal click does nothing rather
  than being shown and taken back, and the client keeps the server's confirmed blocks
  beneath its pending predictions so conflicting edits converge without flicker. Replay
  undoes and redoes pending edits tick by tick as plain block writes, so it never remeshes.
  At 166.7 ms, pillar-jumping 30 blocks went from 21 corrections to 0 and digging 10 levels
  from 21 to 0, still 0 at 5% loss; played by hand at 150 ms, the only corrections were two
  respawns. Other players' edits still arrive a round trip late. The edit log a joiner
  receives, which grew with every edit, became a diff from the map on 2026-09-14: one entry
  per cell that differs from the map, so a hole dug and filled again costs nothing and the
  log's size follows how much of the map has changed, not how long the match has run.
  The same day the server learned to stop: Ctrl+C, closing its console, or `--duration`
  ends the loop, logs what the match did, and disconnects every client, which shows
  `DISCONNECTED` rather than a world that silently stopped moving.
  **Predicted terrain edits and their rollback problem are still the
  arc's open risk**: rolling back a rejected edit can invalidate predicted movement,
  because the world the character collided against changed underneath it, and that
  is deferred rather than solved.
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
