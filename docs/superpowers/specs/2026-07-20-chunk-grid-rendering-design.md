# Chunk Grid Rendering (Multi-Chunk Plan, Step 3)

Render a grid of chunks instead of a single one, and remesh only the chunks an
edit actually affects. This is step 3 of the four-step multi-chunk plan. Steps 1
(`World` grid) and 2 (neighbour-aware meshing) have landed but nothing downstream
uses them yet: the sandbox still runs a `1x1x1` world, builds one mesh hardcoded
to chunk `(0,0,0)`, and draws it with one `Renderer::Submit`.

## Scope

In scope:

- `World` tracks which chunks are dirty as blocks change.
- A new engine class, `WorldRenderer`, owns per-chunk GPU meshes and rebuilds only
  dirty chunks.
- The sandbox renders a `4x1x4` world through `WorldRenderer`.

Out of scope (deferred to step 4):

- Raycast and collision stay single-chunk. `VoxelRaycast::Cast` and
  `VoxelCollision::MoveBox` still take `const Chunk&`, and the sandbox bridges via
  `m_World.GetChunk(0, 0, 0)`. Breaking and placing blocks therefore still only
  works inside chunk `(0,0,0)`. Moving these to world space, which also fixes the
  can't-edit-across-a-chunk-border limitation, is step 4.

Not doing: threading. A chunk is 16^3 = 4096 blocks and remeshes in well under a
millisecond; an edit dirties at most four chunks. The GL context is single-threaded,
so a worker could build vertices but the buffer upload returns to the render thread
anyway. Threading pays off later at map load, when hundreds of chunks mesh at once
with nothing interactive to protect.

## Section 1: World dirty tracking

`World` gains a `std::set<glm::ivec3>` of dirty chunk coordinates, ordered by a
small lexicographic comparator on x/y/z (`glm::ivec3` has no built-in ordering).
A set makes deduplication automatic.

- `SetBlock(x, y, z, block)` marks the chunk containing `(x, y, z)`. Then, for each
  axis where the block sits on a chunk boundary (local coordinate `0` or
  `Chunk::Width - 1` etc.), it also marks the neighbour chunk on that side, when
  that neighbour is in bounds. Interior edit marks 1 chunk; a face edit marks 2; an
  edge edit marks 3; a corner edit marks 4. A boundary neighbour outside the world
  is not marked.
- A newly constructed world starts with every chunk dirty, so the first frame meshes
  the whole world through the same path as an edit. There is no separate initial
  build.
- Two new methods:
  - `const std::set<glm::ivec3>& DirtyChunks() const`
  - `void ClearDirty()`

  The renderer reads the set, meshes those chunks, then calls `ClearDirty()`.
  Draining is an explicit non-const call, so reading the world stays const.

### Tests (doctest, in `Tests/`)

The dirty-set logic is pure and GPU-free, so it is unit tested like `Chunk` and
`ChunkMesher`:

- Interior edit marks exactly 1 chunk.
- Face edit (block on one boundary plane) marks 2.
- Edge edit marks 3; corner edit marks 4.
- A boundary edit at the world edge does not mark an out-of-bounds neighbour.
- Editing the same chunk twice leaves 1 entry (set dedup).
- `ClearDirty` empties the set.
- A freshly built world reports every chunk dirty.

## Section 2: WorldRenderer (new class, Cubit/Renderer)

Owns per-chunk GPU meshes and nothing about game logic.

- `ChunkMesh`: a move-only RAII bundle of the `VertexArray`, `VertexBuffer`, and
  `IndexBuffer` that currently live loose in `SandboxLayer`, plus a cached face
  count for the HUD. This is the one genuinely new value type.
- Storage: `std::map<glm::ivec3, ChunkMesh>` keyed by chunk coordinate, using the
  same comparator as the dirty set.
- `void Update(World& world)`: for each coordinate in `world.DirtyChunks()`, call
  `ChunkMesher::Build(world, cx, cy, cz)` and rebuild that chunk's buffers. A chunk
  that meshes to zero faces drops its map entry so it is skipped when drawing. After
  processing all dirty chunks, call `world.ClearDirty()`.
- `void Render(const Shader& shader, const glm::vec3& worldOffset)`: one
  `Renderer::Submit` per stored chunk, each translated by
  `World::GetChunkOrigin(cx, cy, cz)` plus `worldOffset`. Replaces the single
  `Submit` in the sandbox.
- `int TotalFaceCount() const`: sum of the cached per-chunk face counts, for the HUD.

The sandbox still owns the `Shader` and passes it in. `WorldRenderer` is verified by
running the sandbox, like the rest of the rendering code.

## Section 3: Sandbox wiring

The sandbox shrinks; most of what it held moves into `WorldRenderer`.

- Remove the loose `m_VertexArray` / `m_VertexBuffer` / `m_IndexBuffer` members and
  the `RebuildMesh()` method. Add a `WorldRenderer m_WorldRenderer`.
- Grow the world: `World m_World{ 4, 1, 4 }` (was `{ 1, 1, 1 }`). One chunk tall
  keeps the test terrain unchanged while exercising seams on the X and Z axes, which
  is what step 2's culling touches.
- Constructor: keep `BuildTestTerrain(m_World)`; drop the `RebuildMesh()` call. The
  world starts all-dirty, so the first `Update` meshes everything.
- `OnRender`: call `m_WorldRenderer.Update(m_World)` then
  `m_WorldRenderer.Render(*m_Shader, worldOffset)`, replacing the single-Submit block.
- Edit path (`OnMouseButtonPressed`): delete the `RebuildMesh()` call. `SetBlock`
  has already marked the dirty chunks; the next `Update` picks them up. The HUD face
  count and the log line read `m_WorldRenderer.TotalFaceCount()` instead of one index
  buffer.
- `ChunkOrigin` cleanup: today a hardcoded `ChunkOrigin{ -8, -6, -18 }` shoves the
  lone chunk in front of the camera and is woven into camera placement and the
  raycast. With a `4x1x4` world this becomes a computed world-placement offset that
  centers the world and sits the player above it. Contained to the sandbox; raycast
  and collision are not touched, since they stay single-chunk until step 4.

## Verification

Build, then run the sandbox and screenshot per the standing Cubit verification
routine (close via WM_CLOSE so logs flush). Expected: a `4x4` terrain grid visible
with no buried seam walls between chunks, the player walking the full surface, and
breaking/placing still working inside chunk `(0,0,0)`. The doctest suite runs on
build and must stay green.
