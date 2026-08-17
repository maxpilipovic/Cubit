# Cubit Performance Issues

_Last updated: 2026-08-16_

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

### On option C — flat-indexing the flood

Deferred by the design, to be decided on measurement rather than expectation. The
measurement says: **not next.** What remains in `PropagateAll` is dominated by the scan
itself — 16.8M `SetSkyLight` plus up to 16.8M `IsBlockOpaque`, all still resolved
through `World`'s bounds check and three divides and three modulos. Flat indexing would
plausibly take 3,736 ms to roughly 1 s, worth ~2.7 s of Debug load.

`parse` + `BuildWorld` is worth 8.5 s. Do that first. Option C stays on the table.

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
