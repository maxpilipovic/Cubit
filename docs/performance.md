# Cubit Performance Issues

_Last updated: 2026-07-25_

A catalog of known performance problems in the engine, with where they live, when
they bite, and the intended fix. Ordered by priority. This is a working checklist —
tick items off as they are addressed. See also [engine-roadmap.md](engine-roadmap.md).

Context: the shipped battlefield map is 128×48×128 = **192 chunks**, meshing ~120k
faces. Everything below is fine at that size; the problems bite as maps grow toward
the single-`.vox` max of 256×256×256 (up to ~4096 chunks) and beyond.

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

**Priority:** highest. **Status:** open (next slice).

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

**Priority:** high. **Status:** open (bundled with the next slice).

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
P1/P2 cheaper too. Larger change to the mesher; do after P1/P2.

**Priority:** medium-high. **Status:** open.

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

## Summary table

| ID | Issue | Where | Priority | Status |
|----|-------|-------|----------|--------|
| P1 | Whole world meshes in one frame | `WorldRenderer::Update` | Highest | Open (next) |
| P2 | No frustum culling | `WorldRenderer::Render` | High | Open (next) |
| P3 | Per-face meshing (no greedy) | `ChunkMesher` | Med-High | Open |
| P4 | GPU buffers reallocated per remesh | `WorldRenderer::Update` | Low | Open |
| P5 | One draw call per chunk | `WorldRenderer::Render` | Low | Open |
