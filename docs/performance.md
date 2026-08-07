# Cubit Performance Issues

_Last updated: 2026-08-06_

A catalog of known performance problems in the engine, with where they live, when
they bite, and the intended fix. Ordered by priority. This is a working checklist —
tick items off as they are addressed. See also [engine-roadmap.md](engine-roadmap.md).

Context: the shipped battlefield map is 256×64×256 = **602 chunks**, meshing ~476k
faces. The problems below bite harder as maps grow toward the single-`.vox` max of
256×256×256 (up to ~4096 chunks) and beyond.

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

**Impact:** inflates vertex/index counts and GPU memory (the battlefield's ~120k
faces are mostly mergeable flat terrain). More geometry to build, upload, and draw.

**Fix:** greedy meshing — merge adjacent coplanar faces of the same block/colour into
larger quads per chunk face-plane. Significant vertex-count reduction, and it makes
P1/P2 cheaper too. Larger change to the mesher; do after P1/P2. Now that faces carry
per-vertex AO and sky light, the merge criterion has to widen: two coplanar faces can
only merge when their AO *and* light values match, not just their block/colour, or the
merged quad's corner-interpolated shading would misrepresent one of the faces it
absorbed.

**Fix applied 2026-08-06:** `ChunkMesher::Build` walks face planes rather than
blocks, filling a 16×16 mask per plane and consuming it into maximal rectangles.
A face joins the mask only when its four corners share one shade, so merging
never flattens an ambient-occlusion gradient; those faces still go out 1×1.

Measured in Debug over `battlefield.vox`: quads 480262 → 380802, vertices
1921048 → 1523208, whole-map mesh time 1398.58 ms → 2631.02 ms. Quads and
vertices both fell by 20.7%, but meshing got slower in this Debug build — about
88% slower — because the mask-and-rectangle bookkeeping adds work per plane that
a straight one-quad-per-face walk never paid, and Debug does not optimise that
overhead away.

**Priority:** medium-high. **Status:** done.

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

## Where an edit stands now

A break on open ground, 256×64×256 battlefield, debug build:

| | before 2026-07-28 | after P6 | after P6 + P7 |
|---|---|---|---|
| relight | 173.3 ms | 0.03 ms | 0.03 ms |
| remesh 4 chunks | 26.1 ms | 26.1 ms | 7.06 ms |
| **worst case in one frame** | **199 ms** | 26 ms | **~4 ms** (P1 slice) |

Optimised build: 0.93 ms of work per click. Still open at load: `SkyLight::PropagateAll`
floods the whole world once and takes ~5.2 s in a debug build (254 ms optimised).

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
| P3 | Per-face meshing (no greedy) | `ChunkMesher` | Med-High | **Done 2026-08-06** |
| P4 | GPU buffers reallocated per remesh | `WorldRenderer::Update` | Low | Open |
| P5 | One draw call per chunk | `WorldRenderer::Render` | Low | Open |
| P6 | Relight cost followed the box, not the edit | `SkyLight::Repropagate` | Was highest | **Done 2026-07-28** |
| P7 | AO/light sampled through `World` | `ChunkMesher::Build` | Was high | **Done 2026-07-28** |
