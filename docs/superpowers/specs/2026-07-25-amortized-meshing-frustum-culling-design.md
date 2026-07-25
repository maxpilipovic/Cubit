# Amortized Meshing + Frustum Culling — Design

**Date:** 2026-07-25
**Status:** Approved (pending spec review)

## Goal

Remove the two rendering performance problems that gate bigger maps (P1 and P2 in
[performance.md](../../performance.md)): the whole world meshing in a single frame at
load, and every chunk being drawn every frame regardless of the camera view. Then
ship a 4×-larger battlefield map (256×64×256) that these fixes make viable.

## Context

Today `WorldRenderer::Update` meshes every dirty chunk synchronously in one call, so a
freshly loaded world (all chunks dirty) meshes entirely during the first frame — a
stall that scales with chunk count. `WorldRenderer::Render` submits every cached chunk
mesh each frame with no view test. Both are fine at the shipped 128×48×128 map (192
chunks) but bite as maps grow toward the single-`.vox` maximum of 256³ (up to ~1024
chunks at 256×64×256).

Decision from brainstorming: **amortize** the meshing (single-threaded per-frame
budget) rather than thread it — it kills the load stall simply and safely; true
threading is deferred. Frustum culling is bundled into the same slice.

## Architecture

Three components. The pure-math piece is unit-tested; the GL-bound renderer is verified
by running the sandbox.

### 1. `Frustum` (new — `Cubit/…/Renderer/Frustum.{h,cpp}`)

A view frustum as 6 planes, for culling.

- **Interface:**
  - `explicit Frustum(const glm::mat4& viewProjection);` — extracts the 6 planes from
    a VP matrix (Gribb–Hartmann).
  - `bool IntersectsAABB(const glm::vec3& min, const glm::vec3& max) const;` — true if
    the axis-aligned box is at least partially inside the frustum.
- Pure math, no GL, no engine state — **doctest-able**. Planes stored as
  `glm::vec4` (xyz = normal, w = distance), normalised. AABB test uses the
  positive-vertex ("p-vertex") method: for each plane, if the box corner furthest along
  the plane normal is behind the plane, the box is fully outside → cull.

### 2. `WorldRenderer` — amortized meshing (P1)

- New member `std::set<glm::ivec3, IVec3Less> m_Pending;` — chunks awaiting (re)mesh,
  owned by the renderer so *tracking changes* (World) stays separate from *scheduling
  remeshes* (renderer).
- `Update(World&)` each frame:
  1. Absorb `world.DirtyChunks()` into `m_Pending`, then `world.ClearDirty()`.
  2. Mesh at most `MeshBudgetPerFrame` chunks from `m_Pending` (front of the set),
     erasing each as it is built (or its empty-mesh entry dropped).
- **`MeshBudgetPerFrame`** — named constant, default **16**. At 1024 chunks the world
  fills in over ~64 frames (~1s at 60 fps) with no frozen frame. Tunable.
- Order is arbitrary (set order). Nearest-to-camera-first ordering is **out of scope**
  (deferred enhancement).

### 3. `WorldRenderer` — frustum culling (P2)

- `Render` gains the camera view-projection so it can cull:
  `void Render(const Shader&, const glm::mat4& viewProjection, const glm::vec3& worldOffset);`
- Build one `Frustum(viewProjection)` per call. For each cached chunk mesh, compute its
  world-render-space AABB — `min = worldOffset + chunkOrigin`,
  `max = min + vec3(Chunk::Width, Chunk::Height, Chunk::Depth)` — and `Submit` only if
  `frustum.IntersectsAABB(min, max)`. The AABB uses the same `worldOffset + origin` as
  the draw transform, so culling matches what is drawn.
- `Render` becomes non-`const` to record the drawn-chunk count (below).

**Camera dependency:** `PerspectiveCamera` already exposes a view-projection matrix
(used by `Renderer::BeginScene`). The Sandbox passes
`m_CameraController.GetCamera().GetViewProjectionMatrix()` to `Render`. (Confirm the
exact accessor name during implementation; if only separate projection/view accessors
exist, multiply them.)

### 4. HUD counters (make the fixes observable)

`WorldRenderer` exposes three accessors:
- `std::size_t TotalChunkCount() const;` — cached meshes.
- `std::size_t DrawnChunkCount() const;` — chunks submitted in the last `Render`.
- `std::size_t PendingCount() const;` — chunks still queued to mesh.

`HudState` gains `DrawnChunks`, `TotalChunks`, `PendingChunks`; `HudLayer` renders:
- `DRAWN n/m` — evidence frustum culling works (n < m when facing part of the map).
- `PENDING k` — evidence amortization works (counts down to 0 over the first second,
  no frozen frame).

The Sandbox sets these on `HudState` after `Update`/`Render` each frame.

## Ship the bigger map

`MapGen` sets the shipped map size explicitly to **256×64×256** (`TerrainConfig::Size =
{256, 64, 256}` in `MapGen.cpp`), regenerating `Sandbox/assets/maps/battlefield.vox`
(now ~1024 chunks, several MB). `TerrainGen` is size-agnostic, so no generator changes
are needed. The committed asset is replaced; the Sandbox already loads
`battlefield.vox`. The spawn may be re-tuned for the larger map during verification.

The existing test `"The committed battlefield map loads at the expected size"`
(`Tests/src/TerrainGenTests.cpp`) asserts `128×48×128` and must be updated to expect
`256×64×256` when the map is regenerated.

## Testing

- **`Frustum` — doctest** (`Tests/src/FrustumTests.cpp`): build a frustum from a known
  perspective VP (camera at origin looking down −Z). Assert: an AABB a few units ahead
  intersects; an AABB behind the camera does not; an AABB straddling the near region
  intersects; a box far to the side (outside the horizontal FOV) does not.
- **Amortized meshing & culling — sandbox verification** (`WorldRenderer` is GL-bound,
  not headless-testable):
  - Regenerate the 256 map, launch, screenshot, close via `WM_CLOSE`.
  - Confirm no multi-second freeze on load; `PENDING` drains to 0.
  - Confirm `DRAWN` < `TotalChunks` when the camera faces part of the map.
  - Confirm the frame renders correctly (no chunks wrongly culled from view).

## Out of scope (deferred)

- True background-thread meshing (amortization is enough for now).
- Nearest-to-camera-first mesh ordering.
- Greedy meshing (P3), GPU buffer reuse (P4), draw-call batching (P5).
- Any change to `ChunkMesher` output.
