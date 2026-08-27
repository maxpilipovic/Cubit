# Cubit Performance Issues

_Last updated: 2026-08-27_

A catalog of known performance problems in the engine, with where they live, when
they bite, and the intended fix. Ordered by priority. This is a working checklist —
tick items off as they are addressed. See also [engine-roadmap.md](engine-roadmap.md).

Context: the shipped battlefield map is **512×64×512** — 4096 chunks, of which 2408
hold geometry, meshing ~1.93M faces. Figures below that predate 2026-08-11 were taken
on the earlier 256×64×256 map (602 chunks, ~476k faces) and say so where it matters.

---

## P1 — Whole world meshes in a single frame (load stall)

**Where:** `Cubit/src/Renderer/WorldRenderer.cpp` — `WorldRenderer::Update` (lines
~11–44).

**What happens:** `Update` iterates *every* dirty chunk and builds its mesh
synchronously on the main thread in one call. A freshly loaded world marks **all**
chunks dirty (`World` constructor), so the entire map meshes during the first
`OnRender`, blocking that frame.

**Impact:** first-frame stall that scales with chunk count. Invisible at 192 chunks;
at ~1024 (256×256×64) it becomes a visible hitch (~1s order); larger stalls worse.
This is the specific thing gating bigger maps.

**Fix:**
- **Amortize** — cap how many chunks mesh per frame (a per-frame budget), spreading
  a big load over several frames.
- **Thread** — build `ChunkMeshData` (pure CPU, no GL) on a background worker, then
  upload the finished vertex/index data to GPU on the main thread (GL calls must stay
  on the render thread). Double-buffer so the main thread never blocks on a worker.

**Priority:** highest. **Status:** DONE 2026-07-25 — `WorldRenderer::Update` now
absorbs dirty chunks into an internal `m_Pending` set and meshes against a per-frame
budget. Threading remains deferred.

**Revised 2026-07-28:** the budget was a fixed count of 16 chunks, chosen when a chunk
build was cheap. AO and sky light later made a build several times more expensive, and
a count cannot absorb that: chunks differ several-fold in geometry, and the same count
costs very different amounts in a debug and an optimised build, so any count is a stall
in one of them or needless dawdling in the other. It is now a **time slice**,
`MeshBudgetMilliseconds` (4.0), checked after each build so at least one chunk always
completes and the pending set always drains. The trade is load time: at ~1.6 ms a chunk
in a debug build that is ~2 chunks a frame rather than 16, so the initial 1024-chunk
mesh spreads over ~8 s of smooth frames instead of ~1 s of stuttering ones. Threading
(above) is the real fix for load; the slice is the fix for edits.

---

## P2 — No frustum culling (every chunk drawn every frame)

**Where:** `Cubit/src/Renderer/WorldRenderer.cpp` — `WorldRenderer::Render` (lines
~46–61).

**What happens:** `Render` loops over all cached chunk meshes and `Submit`s each one
regardless of whether it is inside the camera's view. Chunks behind the camera, or
far outside the frustum, are still drawn.

**Impact:** draw-call count and GPU vertex work scale with **total** chunks, not
**visible** chunks. At 192 chunks it is cheap; at 1000+ it wastes most draw calls on
geometry that is off-screen.

**Fix:** compute the camera's view frustum (6 planes) and test each chunk's
world-space AABB against it before submitting. Requires exposing the frustum (or
view-projection) from `PerspectiveCamera`. Pairs naturally with P1.

**Priority:** high. **Status:** DONE 2026-07-25 — `WorldRenderer::Render` builds a
`Frustum` from the camera view-projection and skips chunks whose world-space AABB is
outside it. Verified in-sandbox on the 256 map (`DRAWN 232/602`).

---

## P3 — Per-face meshing (no greedy merging)

**Where:** `Cubit/src/Voxel/ChunkMesher.cpp` — `AddExposedFaces` and the per-face
`Add*Face` helpers.

**What happens:** the mesher emits one quad (4 vertices, 2 triangles) per exposed
block face. Coplanar, same-colour faces are never merged, so a flat 128×128 grass
plane emits thousands of individual quads instead of a handful of large ones.

**Impact:** inflates vertex/index counts and GPU memory. Note the premise below
turned out to be wrong: the battlefield's faces are *not* mostly mergeable.

**Fix tried and rejected, 2026-08-06.** Greedy meshing was built, measured, and
reverted. `ChunkMesher::Build` walked face planes instead of blocks, filling a
16×16 mask per plane and consuming it into maximal rectangles. A face joined the
mask only when its four corners carried one shade, so merging never flattened an
ambient-occlusion gradient.

Measured over `battlefield.vox`, meshing every chunk:

| | quads | vertices | Release | Debug |
|---|---|---|---|---|
| per-face | 480262 | 1921048 | 118.0 ms | 1398 ms |
| greedy | 380802 | 1523208 | 236.8 ms | 2734 ms |

Covered area was 480262 in every run, so the merge was correct — it regrouped
faces without gaining or losing one.

**Why the win was small.** Only faces whose four corners share a shade can merge,
and per-vertex AO plus sky light leave most outdoor faces with a gradient. The
textbook 10× greedy-meshing figure assumes flat-shaded faces; this engine does not
have those. 20.7% is what the conservative rule is worth here.

**Why the cost was large.** Sweeping face planes visits every cell once per
direction — six times per chunk — where the block walk visited each cell once and
checked all six neighbours from that single visit. That 6× iteration is inherent
to the direction-major structure, not tunable overhead. Release made it worse
(2.0×) than Debug (1.9×), so it is structural rather than an artefact of an
unoptimised build.

**Why the geometry saved did not pay for it.** Draw calls are one per chunk either
way. The saving is ~400k vertices of GPU memory and vertex processing, which is
nowhere near the frame-time bottleneck, while the meshing cost lands on load and
on every block edit.

A block-major variant — walk blocks once, deposit each exposed face into its
direction's plane mask, merge all 96 planes afterwards — would avoid the 6×
iteration, at the cost of keeping every mask resident (~24576 cells per chunk).
Not attempted. It would have to beat parity *and* the reduction would still be
capped near 20%, so the payoff does not obviously justify it.

Design and plan kept for the record:
`docs/superpowers/specs/2026-08-06-greedy-meshing-design.md`,
`docs/superpowers/plans/2026-08-06-greedy-meshing.md`.

**Priority:** medium-high. **Status:** closed — measured, rejected.

---

## P4 — GPU buffers reallocated on every remesh

**Where:** `Cubit/src/Renderer/WorldRenderer.cpp` — `WorldRenderer::Update` (lines
~27–40).

**What happens:** each time a chunk is remeshed, a brand-new `VertexArray`,
`VertexBuffer`, and `IndexBuffer` are allocated and the old ones freed, rather than
reusing/orphaning the existing GPU buffers.

**Impact:** minor for single edits (1–4 chunks per block change), but it churns GPU
allocations. Matters more once meshing is threaded and frequent.

**Fix:** reuse a chunk's existing buffers, re-uploading with `glBufferData`
orphaning or `glBufferSubData` when the new data fits. Low priority until edits or
threaded remeshing are frequent.

**Priority:** low. **Status:** open.

---

## P5 — One draw call per chunk (no batching)

**Where:** `Cubit/src/Renderer/WorldRenderer.cpp` — `WorldRenderer::Render`, via
`Renderer::Submit` per chunk.

**What happens:** every chunk is a separate draw call with its own VAO and transform
uniform. 192 chunks = 192 draw calls; 1024 = 1024.

**Impact:** draw-call overhead grows with visible chunk count. Frustum culling (P2)
mitigates this by cutting the number submitted; per-chunk draws are otherwise a
standard and acceptable approach.

**Fix (optional, later):** batch or instance chunk draws if draw-call count becomes a
bottleneck after culling. Not needed near-term.

**Priority:** low. **Status:** open (revisit after P2).

---

## P6 — Relighting an edit cost the size of the box, not the change

**Where:** `Cubit/src/Voxel/SkyLight.cpp` — `SkyLight::Repropagate`.

**What happens:** every block edit blanked a radius-15, full-height box around itself
and flooded it again from scratch. The radius is right — light cannot travel further
than `Max` sideways, and the full height is needed because full-strength light falls
for free — but paying for the whole box meant paying for everything the edit *might*
have touched rather than what it did.

**Impact:** the frame handling a click stalled. On the 256×64×256 battlefield the box
is 31×64×31 = **61,504 cells**; breaking a block on open ground changed the light of
**one** of them and cost **173 ms** in a debug build (8.2 ms optimised) to discover
that. 88% of it was the re-flood. This is what "the FPS drops every time I place or
break a block" was.

**Fix:** propagate incrementally, outward from the edit, stopping where the light stops
moving.

**Priority:** was highest. **Status:** DONE 2026-07-28 — placing a block now unfloods
only the region that cell used to light and floods the surviving border back in;
breaking one seeds a flood from the neighbours that survived. A recorder keeps each
touched cell's original value so only genuinely changed chunks are marked dirty, which
is what the old before/after compare over the box provided. Relighting a broken surface
block: **173 ms → 0.03 ms** (debug), 8.2 ms → under 0.01 ms (optimised).

The subtle part, and the one with its own named test: sky light falls without dimming,
so the cell below a free-falling one holds the *same* level, not a lower one. A removal
pass that only chases dimmer neighbours leaves those columns wrongly lit under a block
you just placed.

---

## P7 — Meshing resolved every AO and light sample through `World`

**Where:** `Cubit/src/Voxel/ChunkMesher.cpp` — `ChunkMesher::Build` and the corner
helpers.

**What happens:** AO and light sampling read each cell through `World`, which converts
a world position into a chunk and an offset — a bounds check, three divisions and three
modulos — on every read. Per exposed face that is roughly 40 reads (4 corners × ~10),
against 1 for the fixed per-face shading it replaced.

**Impact:** a chunk build went from cheap to **6.4 ms** (debug) / 0.37 ms (optimised).
Once P6 landed, this *was* what an edit cost: 4 dirty chunks × 6.4 ms = 26 ms in one
frame. Measured split: **85% of `Build` was world lookups**, 15% vector building.

**Fix:** copy the chunk plus its one-block shell into a flat array once per mesh, then
sample it by flat index.

**Priority:** was high. **Status:** DONE 2026-07-28 — `Build` fills a `Neighbourhood`
(18³, chunk + shell) and addresses cells by **flat index**, so a sample is an integer
add and an array read. Face normals and tangent axes become flat offsets once per mesh.
The AO and light rules stayed a single implementation: they are templates over the cell
source, so the cached path and the public `CornerAoLevel`/`CornerLightShade` share one
copy of the rule rather than drifting apart as two. Chunk build: **6.4 ms → 1.6 ms**
(debug), 0.37 → 0.30 ms (optimised).

Three optimisations along the way measured *slower* and were reverted — including a
first version of this same cache. They are written up, with why, in the investigation
log below; the short lesson is that in a debug build total call count dominates, so
cost-per-call optimisations do not show up.

---

## P8 — Load floods light and meshes the whole world on one thread

**Where:** `Cubit/src/Voxel/SkyLight.cpp` — `SkyLight::PropagateAll`;
`Cubit/src/Renderer/WorldRenderer.cpp` — `WorldRenderer::Update`.

**Measured 2026-08-16** on `battlefield512.vox` (512x64x512, 16.8M cells). Full method
and counters:
[superpowers/investigations/2026-08-16-load-cost-breakdown.md](superpowers/investigations/2026-08-16-load-cost-breakdown.md).

| Phase | Debug | Release | Share |
|---|---:|---:|---:|
| parse `.vox` | 3,606 ms | 127 ms | 11% |
| `BuildWorld` | 4,911 ms | 211 ms | 15% |
| **`PropagateAll`** | **19,578 ms** | **1,003 ms** | **59%** |
| — clear every cell | 807 ms | 55 ms | 2% |
| — seed top layer | 34 ms | 3 ms | 0.1% |
| — **the flood** | **18,736 ms** | **945 ms** | **57%** |
| mesh all 4,096 chunks | 4,985 ms | 289 ms | 15% |
| **Total** | **33,079 ms** | **1,631 ms** | |

**The ~5.2 s quoted below for `PropagateAll` is a 256-map figure.** On the shipped map
it is 19.6 s.

**Meshing is the smallest of the three costs, not the biggest.** The half-minute of
"the world builds itself around you" is the deliberate 4 ms `MeshBudgetMilliseconds`
slice spreading 5 s of work across frames — not 30 s of work. The cost that blocks
before a single frame renders is the flood.

**So threading the mesher targets 15% of load, and is no longer the recommended first
move** — which is what this entry used to say.

**Why the flood is slow.** 11.35M cell visits, 68.1M neighbour tests (exactly 6 per
visit), 11.09M light writes. Visits ~= writes ~= air-cell count, so the BFS is *not*
thrashing — each cell is written about once. The cost is per-operation: every neighbour
test does `IsInBounds` + `IsBlockOpaque` + `GetSkyLight`, three `World`-addressed calls
of a bounds check plus three divides and three modulos, ~200M lookups in all. And
**88.9% of the writes are full-strength light falling straight down open columns**,
one queue entry at a time.

**Fix:** replace the open-column part with a downward scan per column, seeding the
remaining sideways BFS from the boundary between lit and unlit columns. Design, and why
flat-indexing the BFS was deferred rather than done first:
[superpowers/specs/2026-08-16-sky-light-column-scan-design.md](superpowers/specs/2026-08-16-sky-light-column-scan-design.md).

**Priority:** high. **Status:** flood **DONE 2026-08-16**; meshing untouched and no
longer the priority.

### Result

| | before | after | |
|---|---:|---:|---|
| `PropagateAll`, Debug | 19,578 ms | **3,736 ms** | **5.2x** |
| `PropagateAll`, Release | 1,003 ms | **230 ms** | **4.4x** |
| total load, Debug | 33,079 ms | **~17,237 ms** | **48% cut** |
| total load, Release | 1,631 ms | **~858 ms** | **47% cut** |

Proved by comparing cell for cell against a naive reference implementation kept in
`Tests/src/SkyLightTests.cpp`, over seven synthetic worlds and a generated 128-map on
every build, plus a one-off run over all 16.8M cells of `battlefield512.vox`. The light
is identical, not merely similar.

### What load looks like now

| Phase | Debug | share | Release | share |
|---|---:|---:|---:|---:|
| parse `.vox` | 3,606 ms | 21% | 127 ms | 15% |
| `BuildWorld` | 4,911 ms | 28% | 211 ms | 25% |
| `PropagateAll` | 3,736 ms | 22% | 230 ms | 27% |
| mesh all chunks | 4,985 ms | 29% | 289 ms | 34% |

**The ordering has inverted.** `parse` + `BuildWorld` is now **49% of Debug load** and
the largest target by a wide margin, and it has never been optimised. Meshing is now
the biggest single line item.

### The parse row splits — measured with the profiler, 2026-08-27

Every figure above came from a hand-timed rig, written for one investigation and
deleted afterwards — the third time that had happened. `CB_PROFILE_SCOPE`
(design: [superpowers/specs/2026-08-25-profiler-design.md](superpowers/specs/2026-08-25-profiler-design.md))
instruments six scopes in total, but the Sandbox wraps only four of them —
`VoxLoader::LoadFile`, `VoxLoader::Parse`, `BuildWorld`, and
`SkyLight::PropagateAll` — in the load session it opens around `LoadWorld`. The
other two, `ChunkMesher::Build` and `WorldRenderer::Update`, are instrumented
too, but they fire after `LoadWorld` returns, across the frames that follow,
and the load session has already closed by then. That is deliberate: extending
the session across the first render would make a capture named "load" cover
frame work as well, which is worse than the gap it leaves. **Meshing is
therefore not in the table below.** It still carries only its previously
recorded figure — 4,985 ms Debug / 289 ms Release, 29% / 34% share, from the
table two sections up — not a profiler-measured one.

The four phases the load session does capture were run three times each,
Debug and Release, over `battlefield512.vox`; the table below is their
medians. Those medians agree with the figures above to within ~12% on every
phase except one: `LoadFile` had never been split from `Parse` before.

| Phase | Debug | Release | Debug/Release |
|---|---:|---:|---:|
| `VoxLoader::LoadFile` (contains `Parse`) | 2,508.8 ms | 141.7 ms | 17.7x |
| — `VoxLoader::Parse` | 389.1 ms | 32.1 ms | 12.1x |
| — **file read** (`LoadFile` − `Parse`) | **2,101.0 ms** | **111.4 ms** | 18.9x |
| `BuildWorld` | 4,496.4 ms | 219.6 ms | 20.5x |
| `SkyLight::PropagateAll` | 3,376.4 ms | 234.7 ms | 14.4x |

The file-read row is computed per run — file read = LoadFile − Parse on each of
the three runs — and only then is the median taken across runs, which is why it
does not equal the printed medians subtracted from each other: 2508.8 − 389.1 =
2119.7, not the 2101.0 shown; 141.7 − 32.1 = 109.6, not 111.4. The figures are
right; subtracting the two median rows above is simply the wrong arithmetic to
reproduce them.

`BuildWorld` is still the largest single line item in load. What the split adds is
the second thing hiding inside the old "parse `.vox`" row: **83.7% of `LoadFile`
in Debug (78.7% in Release) is spent reading the bytes off disk, not interpreting
them** — reading costs 5.4x what parsing does in Debug and 3.5x in Release. The
cause is visible in `Cubit/src/Voxel/VoxLoader.cpp`: `LoadFile` builds its buffer
with `std::istreambuf_iterator<char>`, byte at a time, over a 23.8 MB file. That
is a five-line fix, and nobody could see it while "parse `.vox`" was one row. It
was made the same day — see P9 below.

Two caveats, not to be dropped:

- **Debug `LoadFile` is cache-sensitive — a real 33.7% run-to-run spread, not
  noise.** A cold run taken right after a build landed within 8.4% of the 3,606 ms
  this page quotes above; two warm runs came in 30-32% lower. That is OS
  file-cache state, not a disagreement between the old rig and the new tool — both
  are measuring a disk-bound path, just in different cache states. Every other
  phase's spread was under 5%, and every other phase agrees with the figures above
  within ~12%.
- **The "~20x" Debug/Release multiplier this page cites elsewhere is not uniform
  per phase.** It holds for `BuildWorld` (20.5x) and the file read (18.9x), but is
  lower for `Parse` (12.1x) and `PropagateAll` (14.4x).

Figures from here on are reproducible by running the Sandbox and reading the
`profile-load.json` it writes next to the executable — not by rebuilding
instrumentation.

### On option C — flat-indexing the flood

Deferred by the design, to be decided on measurement rather than expectation. The
measurement says: **not next.** What remains in `PropagateAll` is dominated by the scan
itself — 16.8M `SetSkyLight` plus up to 16.8M `IsBlockOpaque`, all still resolved
through `World`'s bounds check and three divides and three modulos. Flat indexing would
plausibly take 3,736 ms to roughly 1 s, worth ~2.7 s of Debug load.

`parse` + `BuildWorld` is worth 8.5 s. Do that first. Option C stays on the table.

**Done 2026-08-27 — see P11.** Two things above turned out wrong, and both were
wrong in the same way: they were inferred from a single timer around the whole of
`PropagateAll` rather than measured per phase. The scan did **not** dominate what
remained — the flood did, 51% to the scan's 43%. And "roughly 1 s" undersold the
result by more than a factor of two: it is **413 ms**.

---

## P9 — Reading the map file one byte at a time

**Where:** `Cubit/src/Voxel/VoxLoader.cpp` — `VoxLoader::LoadFile`.

**What happened:** the byte buffer was built from a pair of
`std::istreambuf_iterator<char>`, which appends one byte at a time and regrows
the vector as it goes. Over the shipped 23.8 MB map that was the single largest
thing inside the old "parse `.vox`" row, and none of it was parsing.

**Fix:** open with `std::ios::ate`, take the length from `tellg`, size the buffer
once, and fill it with one `read`. A short read is now checked rather than
assumed — the iterator form stopped early and handed `Parse` a truncated buffer,
which surfaced as a complaint about the file's contents rather than about reading
it.

**Priority:** was highest of the remaining load cost. **Status:** DONE 2026-08-27.

| file read | before | after | |
|---|---:|---:|---|
| Debug, warm cache | 2,112 ms | **30.5 ms** | **69x** |
| Debug, cold cache | 3,209 ms | 35.1 ms | 91x |
| Release | 111.4 ms | **24.5 ms** | 4.5x |

Medians of three runs each, captured back to back with `CB_PROFILE_SCOPE` so the
before and after share a machine state — which matters here, given the cache
sensitivity recorded above. `Parse` was unchanged across the same runs (~400 ms
Debug, ~32 ms Release), which is the control: only the read moved.

`LoadFile` as a whole went from 2,508.8 ms to ~439 ms in Debug and from 141.7 ms
to ~58 ms in Release, taking roughly **17% off total Debug load** for about
fifteen lines.

**Two things this measurement taught, beyond the number.**

The **cache sensitivity disappeared with the fix**. Before, a cold run was 52%
slower than a warm one; after, the three runs sit within 20% of each other. The
byte-at-a-time path turned every cache miss into a per-byte cost, so cache state
dominated; a single bulk read does not amplify it.

The **Debug/Release multiplier for this phase collapsed from 18.9x to about
1.2x**. That is the signature of work moving out of our code and into the
operating system: a per-byte loop is instruction-bound and an unoptimised build
punishes it, while a bulk read is I/O-bound and barely notices which build asked
for it. Worth remembering when reading any Debug figure on this page — a large
Debug/Release ratio marks work *we* are doing, and those are the rows worth
attacking.

---

## P10 — `BuildWorld` marked chunks dirty once per solid block

**Where:** `Cubit/src/Voxel/VoxLoader.cpp` — `BuildWorld`; `Cubit/src/Voxel/World.cpp`
— `World::SetBlock`.

**What happened:** `BuildWorld` wrote every solid block through `World::SetBlock`,
which ends in `MarkChunkDirtyAt` — three divides, three modulos, a 3x3x3 loop, and
a `std::set<glm::ivec3>` insert that descends a red-black tree of 4,096 entries.
On the shipped map that ran **5,952,784 times**.

**And every one of them was wasted.** The `World` constructor inserts all 4,096
chunk coordinates into the dirty set before the first block is written — that costs
10.8 ms. So all six million subsequent inserts found the key already present and
changed nothing.

Full method and the probe that isolated it:
[the investigation](superpowers/investigations/2026-08-27-buildworld-cost.md).

**Fix:** `World::SetBlockAssumingDirty` writes a block without marking, and
`BuildWorld` uses it. Both setters share one private `WriteBlock`, so the bounds
check and the addressing exist once and the two differ by exactly the marking call.
`SetBlock` is unchanged — it has one other production caller, `ApplyBlockEdit`,
where marking is the whole point, and `WorldDirtyTests` pins its behaviour.

**Priority:** was highest of the remaining load cost. **Status:** DONE 2026-08-27.

| `BuildWorld` | before | after | |
|---|---:|---:|---|
| Debug | 4,496.4 ms | **427.1 ms** | **10.5x** |
| Release | 219.6 ms | **43.4 ms** | 5.1x |

Medians of three runs. What remains is what the work actually is: ~123 ms scanning
the model and ~300 ms of writes.

**A calibration note.** The prediction from the probe was ~130 ms, and the answer
is ~427 ms. The probe showed `SetBlock`'s own work — bounds check, six divides and
modulos, an array write — was negligible *relative to marking*, and that got
rounded to negligible outright. Six million of anything is not free. The direction
was right and the magnitude was off by 3x; worth remembering when reading a "the
rest is free" conclusion off a profile.

### Where load stands now

| Phase | Debug | Release |
|---|---:|---:|
| file read | 31.3 ms | ~24 ms |
| `VoxLoader::Parse` | 427.3 ms | ~32 ms |
| `BuildWorld` | 427.1 ms | 43.4 ms |
| **`SkyLight::PropagateAll`** | **3,379.4 ms** | **227.3 ms** |
| **Total** | **4,265 ms** | **333 ms** |

Against the 10,381 ms Debug that P9 and P10 started from, that is a **59% cut**,
and against the 33,079 ms this page opened P8 with, an **87% cut**.

**The ordering has inverted again.** `SkyLight::PropagateAll` is now **79% of Debug
load** and every other phase is under 10%. Option C below — flat-indexing the
flood, deferred in 2026-08-16 on the grounds that `parse` + `BuildWorld` was worth
more — is now the only load item worth measuring, because `parse` + `BuildWorld`
together are 20% of what is left. That deferral was correct when it was made and
has now expired.

---

## P11 — Sky light resolved every cell through `World`

**Where:** `Cubit/src/Voxel/SkyLight.cpp` — `SkyLight::PropagateAll`.

**What happened:** the column scan and the flood both addressed cells through
`World`, which turns a world position into a chunk and an offset: a bounds check,
three divides and three modulos, per read and per write. The scan does that for all
16.8M cells; the flood does it again for every neighbour of every cell it touches.

**First, the split — because the page had guessed it wrong.** `PropagateAll` was a
single profiler scope, so which of its phases held the 79% was inference, and the
inference above (P8, "On option C") said the scan. Splitting it into `Scan`, `Seed`
and `Flood` scopes said otherwise:

| phase | Debug | share |
|---|---:|---:|
| `PropagateAll/Scan` | 1,457.8 ms | 43% |
| `PropagateAll/Seed` | 146.7 ms | 4% |
| **`PropagateAll/Flood`** | **1,707.8 ms** | **51%** |

The flood was the larger half. A fix aimed only at the scan would have left more
behind than it removed.

**Fix:** copy the world's opacity into one flat array, propagate there, copy the
finished light back. A neighbour is reached by adding a constant.

Three details carry most of the win, and only the first was the plan:

- **A padded border, left opaque.** The flood skips opaque neighbours anyway, so a
  one-cell border that is always opaque stops it at the world edge with no bounds
  check existing at all. Same trick as `ChunkMesher`'s 18-cube neighbourhood. The
  old `IsInBounds` test folded into `IsOpaque` rather than sitting beside it.
- **Cells laid out y-fastest** — the opposite of a `Chunk` — so a column is
  contiguous and the scan is a straight sweep. This also made the scan's second
  loop disappear: the array arrives zeroed, where the old scan had to blank the
  cells below each column because it was writing over the previous load's light.
- **A 4-byte index instead of a 12-byte `ivec3`**, in a `std::vector` walked by
  cursor instead of a `std::deque` popped from the front. In a debug build a deque
  pop is several checked operations, and this turned out to be a large part of what
  the flood was spending.

`Flood` became a template over its cell source so the edit path keeps addressing
cells through `World`. `Repropagate` must not touch this array — it settles a
handful of cells in 0.03 ms, and building a 35 MB copy of the world per click would
be a catastrophic regression. One rule, two addressings, for the reason P7 gives:
the free-fall case in the middle of that rule is exactly what would drift between
two copies of it.

**Priority:** was the whole of the remaining load cost. **Status:** DONE 2026-08-27.

| phase | Debug before | Debug after | | Release before | Release after |
|---|---:|---:|---|---:|---:|
| `Gather` | — | 35.1 ms | | — | 8.1 ms |
| `Scan` | 1,457.8 ms | **18.9 ms** | **77x** | 115.7 ms | 4.5 ms |
| `Seed` | 146.7 ms | 46.8 ms | 3.1x | 19.7 ms | 4.0 ms |
| `Flood` | 1,707.8 ms | **257.2 ms** | **6.6x** | 105.1 ms | 33.0 ms |
| `Scatter` | — | 41.4 ms | | — | 7.1 ms |
| **`PropagateAll`** | **3,370.7 ms** | **413.5 ms** | **8.2x** | **250.4 ms** | **63.9 ms** |

Medians of three runs, before and after captured back to back in one session so
they share machine state. Every run's first pass was cold-cache and inflated every
phase including ones this change does not touch; those are the outliers the medians
discard.

Proved unchanged by nine cases comparing the result cell for cell against the naive
reference implementation in `Tests/src/SkyLightTests.cpp`. Two were added
beforehand for the chunk seams this rewrite crosses, and both were mutation-tested
— dropping the chunk y-origin in the gather makes them fail and name the first bad
cell. On screen the Sandbox reports `FACES 1927774`, matching the recorded face
count exactly.

### Where load stands now

| Phase | Debug | share | Release |
|---|---:|---:|---:|
| file read + `Parse` (`LoadFile`) | 546.5 ms | 38% | 59.2 ms |
| `BuildWorld` | 462.6 ms | 33% | 47.7 ms |
| `SkyLight::PropagateAll` | 413.5 ms | 29% | 63.9 ms |
| **Total** | **~1,423 ms** | | **~171 ms** |

Against the 4,291 ms Debug measured immediately before this change, a **67% cut**;
against the 33,079 ms this page opened P8 with, a **96% cut**.

**There is no longer a dominant phase.** Every load item that has been optimised was
found by one being three to twenty times the others; the three that remain are
within 1.3x of each other, and none is doing anything obviously wasteful. Load is
done unless something else makes it matter again.

`LoadFile` and `BuildWorld` read slightly higher here than in P10's table (546 vs
458, 463 vs 427). Nothing touched those paths — it is run-to-run drift, and it is
the reason the phase this change *did* touch was measured back to back rather than
against a figure from another day.

**What is now the largest single cost in getting a map on screen is meshing**, at
~4,985 ms Debug — larger than the whole of load. It does not stall, because the 4 ms
budget slice spreads it over frames, which is why it is not in the table above; what
it costs is the half-minute of the world visibly building itself. See P1, whose
remaining half is threading it.

---

## Where an edit stands now

A break on open ground, 256×64×256 battlefield, debug build:

| | before 2026-07-28 | after P6 | after P6 + P7 |
|---|---|---|---|
| relight | 173.3 ms | 0.03 ms | 0.03 ms |
| remesh 4 chunks | 26.1 ms | 26.1 ms | 7.06 ms |
| **worst case in one frame** | **199 ms** | 26 ms | **~4 ms** (P1 slice) |

Optimised build: 0.93 ms of work per click. Still open at load: `SkyLight::PropagateAll`
floods the whole world once and takes ~5.2 s in a debug build (254 ms optimised) **on
the 256 map**. On the shipped 512 map it is 19.6 s — see P8, which measures it properly.

**Load cost at 512×64×512, measured 2026-08-11** (debug build, RTX 3060 Ti). This
replaces the earlier extrapolation from the 256 map:

| | observed |
|---|---|
| first frame (HUD up, terrain partly meshed) | within ~5 s |
| meshing still running | at 12 s — 207 of 2408 chunk meshes built |
| meshing complete, chunk count stable | by 40 s — 2408 meshes, `PENDING 0` |
| steady-state frame rate once meshed | 140–145 FPS, 933 of 2408 chunks drawn |

The map is 4096 chunks, of which **2408 hold geometry**; the rest are pure air and
never get a mesh, which is why the HUD's total reads 2408 rather than 4096.

Sampling mid-load is noisy — separate launches showed 222 meshes at 5 s and 181 at
25 s — so treat the 12 s and 40 s rows as a bracket, not a curve. The bracket is
the point: **a 512 map takes tens of seconds of wall clock before it is fully
meshed in a debug build**, against a couple of seconds at 256. Play is possible
throughout, since the budgeted mesher keeps the frame rate up and meshes fill in
around you, but the world visibly builds itself for the first half minute. This is
P1's remaining half — the budget stopped the stall, it did not make the work
smaller — and the fix is threading it.

Full write-up — how each cause was found, the numbers, the three optimisations that
turned out slower, and the debug-vs-release multiplier that shapes which fixes are
worth making:
[superpowers/investigations/2026-07-28-block-edit-fps.md](superpowers/investigations/2026-07-28-block-edit-fps.md).

---

## Summary table

| ID | Issue | Where | Priority | Status |
|----|-------|-------|----------|--------|
| P1 | Whole world meshes in one frame | `WorldRenderer::Update` | Highest | **Done 2026-07-25**, budget revised to a time slice **2026-07-28** |
| P2 | No frustum culling | `WorldRenderer::Render` | High | **Done 2026-07-25** |
| P3 | Per-face meshing (no greedy) | `ChunkMesher` | Med-High | **Closed 2026-08-06** — greedy meshing built, measured, rejected |
| P4 | GPU buffers reallocated per remesh | `WorldRenderer::Update` | Low | Open |
| P5 | One draw call per chunk | `WorldRenderer::Render` | Low | Open |
| P6 | Relight cost followed the box, not the edit | `SkyLight::Repropagate` | Was highest | **Done 2026-07-28** |
| P7 | AO/light sampled through `World` | `ChunkMesher::Build` | Was high | **Done 2026-07-28** |
| P8 | Load floods light and meshes the whole world on one thread | `SkyLight::PropagateAll`, `WorldRenderer::Update` | High | Flood **done 2026-08-16** — column scan, 19.6 s → 3.7 s debug, load halved. Largest remaining piece is now `parse` + `BuildWorld` at 49% |
| P9 | Map file read one byte at a time | `VoxLoader::LoadFile` | Was highest of remaining load cost | **Done 2026-08-27** — sized buffer + one `read`, 2,112 ms → 30.5 ms debug (69x), ~17% off total debug load |
| P10 | `BuildWorld` marked chunks dirty per solid block, redundantly | `BuildWorld`, `World::SetBlock` | Was highest of remaining load cost | **Done 2026-08-27** — `SetBlockAssumingDirty`, 4,496 ms → 427 ms debug (10.5x); load now 79% `PropagateAll` |
| P11 | Sky light resolved every cell through `World` | `SkyLight::PropagateAll` | Was all of the remaining load cost | **Done 2026-08-27** — flat padded array, 3,371 ms → 413 ms debug (8.2x); total load 4.29 s → 1.42 s, and no phase dominates any more |
