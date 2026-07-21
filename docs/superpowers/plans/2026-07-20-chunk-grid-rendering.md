# Chunk Grid Rendering Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render a grid of chunks instead of a single one, remeshing only the chunks an edit actually touches.

**Architecture:** `World` gains a set of dirty chunk coordinates that `SetBlock` maintains (touched chunk plus in-bounds boundary neighbours). A new engine class `WorldRenderer` owns one GPU mesh per chunk, rebuilds only the dirty ones each frame, then clears the world's dirty set. The sandbox runs a 4×1×4 world through `WorldRenderer` and stops holding GPU buffers itself.

**Tech Stack:** C++20, OpenGL (GLAD/GLFW), GLM, premake5 (vs2026 generator), doctest.

## Global Constraints

- Cubit builds as a DLL. Every class exposed to the Sandbox or Tests projects must be tagged `CB_API` (from `Cubit/Core.h`). — see existing `World`, `ChunkMesher`.
- Cubit `.cpp` files use a precompiled header: the **first** include in every Cubit `.cpp` must be `#include "cub.h"`. — see `Cubit/src/Voxel/World.cpp:1`.
- Classes holding `std::set` / `std::map` / `std::unique_ptr` members that cross the DLL boundary must wrap the class body in `#pragma warning(push)` / `disable: 4251` / `pop`. — see `Cubit/include/Cubit/Voxel/World.h:11-14,64-66`.
- premake globs source files (`Cubit/include/**.h`, `Cubit/src/**.cpp`, `Tests/src/**.cpp`). **Adding a new file requires regenerating the vcxproj** before it will build.
- Coordinate ordering for `glm::ivec3` set/map keys uses one shared comparator, `IVec3Less`, defined once in `World.h`.

### Build and test commands (used by multiple tasks)

Regenerate project files (only needed after adding/removing a source file):

```bash
cd /c/dev/Cubit && premake5 vs2026
```

Build (this also runs the doctest suite — the Tests project's post-build step executes `Tests.exe`, which returns non-zero on any failed test, failing the build):

```powershell
$msbuild = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -find 'MSBuild\**\Bin\MSBuild.exe'
& $msbuild C:\dev\Cubit\Cubit.slnx /p:Configuration=Debug /p:Platform=x64
```

A green build means every doctest case passed. Read the MSBuild output for the `Tests.exe` run near the end; doctest prints `[doctest] Status: SUCCESS!` on success.

---

## Task 1: World dirty-chunk tracking

Pure logic, no GPU. Fully covered by doctest.

**Files:**
- Modify: `Cubit/include/Cubit/Voxel/World.h`
- Modify: `Cubit/src/Voxel/World.cpp`
- Create: `Tests/src/WorldDirtyTests.cpp`

**Interfaces:**
- Produces:
  - `struct IVec3Less { bool operator()(const glm::ivec3&, const glm::ivec3&) const; }` — lexicographic x,y,z ordering, defined in `World.h`, reused by Task 2.
  - `const std::set<glm::ivec3, IVec3Less>& World::DirtyChunks() const`
  - `void World::ClearDirty()`
  - A freshly constructed `World` reports every chunk coordinate dirty.
  - `World::SetBlock` marks the touched chunk plus in-bounds boundary neighbours.

- [ ] **Step 1: Write the failing tests**

Create `Tests/src/WorldDirtyTests.cpp`:

```cpp
#include <doctest.h>

#include "Cubit/Voxel/World.h"

namespace
{
    // A 3x3x3 chunk world (48 blocks per axis) so a centre chunk has a
    // neighbour on every side. Chunk (1,1,1) spans world blocks 16..31.
    World MakeWorld()
    {
        return World(3, 3, 3);
    }
}

TEST_CASE("A new world starts with every chunk dirty")
{
    const World world = MakeWorld();

    CHECK(world.DirtyChunks().size() == 27);
}

TEST_CASE("ClearDirty empties the dirty set")
{
    World world = MakeWorld();

    world.ClearDirty();

    CHECK(world.DirtyChunks().empty());
}

TEST_CASE("An interior edit marks exactly one chunk")
{
    World world = MakeWorld();
    world.ClearDirty();

    // Local (8,8,8) inside chunk (1,1,1): touches no boundary.
    world.SetBlock(24, 24, 24, BlockType::Solid);

    CHECK(world.DirtyChunks().size() == 1);
    CHECK(world.DirtyChunks().count(glm::ivec3(1, 1, 1)) == 1);
}

TEST_CASE("A face edit marks the chunk and one neighbour")
{
    World world = MakeWorld();
    world.ClearDirty();

    // Local (0,8,8) inside chunk (1,1,1): touches the -X boundary.
    world.SetBlock(16, 24, 24, BlockType::Solid);

    CHECK(world.DirtyChunks().size() == 2);
    CHECK(world.DirtyChunks().count(glm::ivec3(1, 1, 1)) == 1);
    CHECK(world.DirtyChunks().count(glm::ivec3(0, 1, 1)) == 1);
}

TEST_CASE("An edge edit marks the chunk and two neighbours")
{
    World world = MakeWorld();
    world.ClearDirty();

    // Local (0,0,8) inside chunk (1,1,1): touches -X and -Y boundaries.
    world.SetBlock(16, 16, 24, BlockType::Solid);

    CHECK(world.DirtyChunks().size() == 3);
}

TEST_CASE("A corner edit marks the chunk and three neighbours")
{
    World world = MakeWorld();
    world.ClearDirty();

    // Local (0,0,0) inside chunk (1,1,1): touches -X, -Y and -Z boundaries.
    world.SetBlock(16, 16, 16, BlockType::Solid);

    CHECK(world.DirtyChunks().size() == 4);
}

TEST_CASE("A boundary edit at the world edge marks no out-of-bounds neighbour")
{
    World world = MakeWorld();
    world.ClearDirty();

    // Local (0,8,8) inside chunk (0,1,1): the -X neighbour would be (-1,1,1),
    // which is outside the world and must not be marked.
    world.SetBlock(0, 24, 24, BlockType::Solid);

    CHECK(world.DirtyChunks().size() == 1);
    CHECK(world.DirtyChunks().count(glm::ivec3(0, 1, 1)) == 1);
}

TEST_CASE("Editing the same chunk twice leaves one dirty entry")
{
    World world = MakeWorld();
    world.ClearDirty();

    world.SetBlock(24, 24, 24, BlockType::Solid);
    world.SetBlock(25, 25, 25, BlockType::Solid);

    CHECK(world.DirtyChunks().size() == 1);
}
```

- [ ] **Step 2: Regenerate projects so the new test file is compiled**

Run: `cd /c/dev/Cubit && premake5 vs2026`
Expected: `Done` / no errors. `Tests/src/WorldDirtyTests.cpp` is now in `Tests.vcxproj`.

- [ ] **Step 3: Build to verify the tests fail**

Run the build command from Global Constraints.
Expected: FAIL — compile error, `class World` has no member `DirtyChunks` / `ClearDirty`.

- [ ] **Step 4: Add the dirty set and comparator to `World.h`**

In `Cubit/include/Cubit/Voxel/World.h`:

Add `#include <set>` to the include block (alongside the existing `#include <vector>`).

Immediately before the `class CB_API World` line (after the `#endif` that closes the `#pragma warning` guard, at file scope), add:

```cpp
//Orders chunk coordinates so they can be stored in a set or map. Compares x,
//then y, then z; the values are only ever equal for the same chunk.
struct IVec3Less
{
    bool operator()(const glm::ivec3& a, const glm::ivec3& b) const
    {
        if (a.x != b.x) return a.x < b.x;
        if (a.y != b.y) return a.y < b.y;
        return a.z < b.z;
    }
};
```

In the `public:` section, after `IsChunkInBounds`, add:

```cpp
    //Returns the chunks whose meshes are out of date because a block in them,
    //or in a chunk sharing a face with them, changed.
    const std::set<glm::ivec3, IVec3Less>& DirtyChunks() const { return m_DirtyChunks; }

    //Forgets every dirty chunk. The renderer calls this once it has remeshed
    //them.
    void ClearDirty() { m_DirtyChunks.clear(); }
```

In the `private:` section, after the `GetChunkIndex` declaration, add:

```cpp
    //Marks the chunk holding a world position dirty, plus any chunk sharing a
    //face the position lies on, when that neighbour is inside the world.
    void MarkChunkDirtyForEdit(int x, int y, int z);
```

Add the member alongside `m_Chunks`:

```cpp
    std::set<glm::ivec3, IVec3Less> m_DirtyChunks;
```

- [ ] **Step 5: Fill every chunk dirty on construction and mark on edit in `World.cpp`**

In `Cubit/src/Voxel/World.cpp`:

At the end of the constructor body (after the `m_Chunks.resize(...)` call), add:

```cpp
    //A freshly loaded world has no GPU meshes yet, so every chunk needs building.
    for (int z = 0; z < chunksZ; ++z)
        for (int y = 0; y < chunksY; ++y)
            for (int x = 0; x < chunksX; ++x)
                m_DirtyChunks.insert(glm::ivec3(x, y, z));
```

In `SetBlock`, after the `chunk.SetBlock(...)` call, add:

```cpp
    MarkChunkDirtyForEdit(x, y, z);
```

Add the new method (anywhere after `SetBlock`, e.g. before `IsBlockSolid`):

```cpp
void World::MarkChunkDirtyForEdit(int x, int y, int z)
{
    const int chunkX = x / Chunk::Width;
    const int chunkY = y / Chunk::Height;
    const int chunkZ = z / Chunk::Depth;
    m_DirtyChunks.insert(glm::ivec3(chunkX, chunkY, chunkZ));

    //A block on a chunk's boundary plane also changes the shared faces of the
    //neighbour on that side, so that neighbour's mesh is dirty too.
    const int localX = x % Chunk::Width;
    const int localY = y % Chunk::Height;
    const int localZ = z % Chunk::Depth;

    const auto markNeighbour = [this](int cx, int cy, int cz)
    {
        if (IsChunkInBounds(cx, cy, cz))
            m_DirtyChunks.insert(glm::ivec3(cx, cy, cz));
    };

    if (localX == 0)                markNeighbour(chunkX - 1, chunkY, chunkZ);
    if (localX == Chunk::Width - 1) markNeighbour(chunkX + 1, chunkY, chunkZ);
    if (localY == 0)                markNeighbour(chunkX, chunkY - 1, chunkZ);
    if (localY == Chunk::Height - 1)markNeighbour(chunkX, chunkY + 1, chunkZ);
    if (localZ == 0)                markNeighbour(chunkX, chunkY, chunkZ - 1);
    if (localZ == Chunk::Depth - 1) markNeighbour(chunkX, chunkY, chunkZ + 1);
}
```

- [ ] **Step 6: Build to verify the tests pass**

Run the build command from Global Constraints.
Expected: PASS — build succeeds, `Tests.exe` prints `[doctest] Status: SUCCESS!`.

- [ ] **Step 7: Commit**

```bash
cd /c/dev/Cubit
git add Cubit/include/Cubit/Voxel/World.h Cubit/src/Voxel/World.cpp Tests/src/WorldDirtyTests.cpp
git commit -m "Track dirty chunks as blocks change"
```

---

## Task 2: WorldRenderer engine class

Owns one GPU mesh per chunk. No unit test — it needs a live GL context, so it is verified by compiling here and by running the sandbox in Task 3. The deliverable for this task is a clean build with the class in place.

**Files:**
- Create: `Cubit/include/Cubit/Renderer/WorldRenderer.h`
- Create: `Cubit/src/Renderer/WorldRenderer.cpp`
- Modify: `Cubit/include/Cubit/Cubit.h`

**Interfaces:**
- Consumes (Task 1): `World::DirtyChunks()`, `World::ClearDirty()`, `IVec3Less` from `World.h`.
- Consumes (existing): `ChunkMesher::Build(const World&, int, int, int) -> ChunkMeshData`; `ChunkMeshData{ std::vector<VoxelVertex> Vertices; std::vector<std::uint32_t> Indices; }`; `Renderer::Submit(const VertexArray&, const IndexBuffer&, const Shader&, const glm::mat4&)`; `World::GetChunkOrigin(int,int,int) -> glm::ivec3`.
- Produces (Task 3):
  - `void WorldRenderer::Update(World& world)`
  - `void WorldRenderer::Render(const Shader& shader, const glm::vec3& worldOffset) const`
  - `std::uint32_t WorldRenderer::TotalFaceCount() const`

- [ ] **Step 1: Create the header `Cubit/include/Cubit/Renderer/WorldRenderer.h`**

```cpp
#pragma once

#include "Cubit/Core.h"
#include "Cubit/Renderer/VertexArray.h"
#include "Cubit/Renderer/VertexBuffer.h"
#include "Cubit/Renderer/IndexBuffer.h"
#include "Cubit/Voxel/World.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <map>
#include <memory>

class Shader;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

//Holds one GPU mesh per chunk and keeps them in step with the world. Only the
//chunks the world reports dirty are rebuilt, so an edit does not remesh the
//whole world.
class CB_API WorldRenderer
{
public:
    //Rebuilds the GPU mesh of every dirty chunk, then clears the world's dirty
    //set. Call once per frame before Render. Needs a current GL context.
    void Update(World& world);

    //Draws every non-empty chunk mesh, each translated to its chunk origin plus
    //worldOffset.
    void Render(const Shader& shader, const glm::vec3& worldOffset) const;

    //Total exposed faces across all chunk meshes, for the HUD.
    std::uint32_t TotalFaceCount() const;

private:
    //A chunk's GPU buffers plus its face count. Move-only through its unique_ptr
    //members, so it can live in a map without copying GPU resources.
    struct ChunkMesh
    {
        std::unique_ptr<VertexArray> Array;
        std::unique_ptr<VertexBuffer> Buffer;
        std::unique_ptr<IndexBuffer> Indices;
        std::uint32_t FaceCount = 0;
    };

    std::map<glm::ivec3, ChunkMesh, IVec3Less> m_Meshes;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
```

- [ ] **Step 2: Create the source `Cubit/src/Renderer/WorldRenderer.cpp`**

```cpp
#include "cub.h"

#include "Cubit/Renderer/WorldRenderer.h"

#include "Cubit/Renderer/Renderer.h"
#include "Cubit/Renderer/Shader.h"
#include "Cubit/Voxel/ChunkMesher.h"

#include <glm/gtc/matrix_transform.hpp>

void WorldRenderer::Update(World& world)
{
    for (const glm::ivec3& coord : world.DirtyChunks())
    {
        const ChunkMeshData mesh =
            ChunkMesher::Build(world, coord.x, coord.y, coord.z);

        //A chunk that meshes to nothing keeps no buffers and is skipped when
        //drawing. Dropping the entry also releases the GPU memory of a chunk
        //that was carved empty.
        if (mesh.Indices.empty())
        {
            m_Meshes.erase(coord);
            continue;
        }

        ChunkMesh gpu;
        gpu.Array = std::make_unique<VertexArray>();
        gpu.Buffer = std::make_unique<VertexBuffer>(
            mesh.Vertices.data(),
            static_cast<std::uint32_t>(mesh.Vertices.size() * sizeof(VoxelVertex)));
        gpu.Array->AddBuffer(
            *gpu.Buffer,
            BufferLayout{ ShaderDataType::Float3, ShaderDataType::Float3 });
        gpu.Indices = std::make_unique<IndexBuffer>(
            mesh.Indices.data(),
            static_cast<std::uint32_t>(mesh.Indices.size()));
        gpu.FaceCount = gpu.Indices->GetCount() / 6;

        m_Meshes[coord] = std::move(gpu);
    }

    world.ClearDirty();
}

void WorldRenderer::Render(const Shader& shader, const glm::vec3& worldOffset) const
{
    for (const auto& entry : m_Meshes)
    {
        const glm::ivec3& coord = entry.first;
        const ChunkMesh& mesh = entry.second;

        const glm::ivec3 origin =
            World::GetChunkOrigin(coord.x, coord.y, coord.z);
        const glm::mat4 transform = glm::translate(
            glm::mat4(1.0f),
            worldOffset + glm::vec3(origin));

        Renderer::Submit(*mesh.Array, *mesh.Indices, shader, transform);
    }
}

std::uint32_t WorldRenderer::TotalFaceCount() const
{
    std::uint32_t total = 0;
    for (const auto& entry : m_Meshes)
        total += entry.second.FaceCount;

    return total;
}
```

- [ ] **Step 3: Add the header to the Cubit umbrella include**

In `Cubit/include/Cubit/Cubit.h`, add after the `Renderer/VertexBuffer.h` line (line 27):

```cpp
#include "Cubit/Renderer/WorldRenderer.h"
```

- [ ] **Step 4: Regenerate projects so the new files are compiled**

Run: `cd /c/dev/Cubit && premake5 vs2026`
Expected: `Done` / no errors.

- [ ] **Step 5: Build to verify it compiles**

Run the build command from Global Constraints.
Expected: PASS — build succeeds (the Task 1 tests still run and pass; there are no new tests).

- [ ] **Step 6: Commit**

```bash
cd /c/dev/Cubit
git add Cubit/include/Cubit/Renderer/WorldRenderer.h Cubit/src/Renderer/WorldRenderer.cpp Cubit/include/Cubit/Cubit.h
git commit -m "Add a per-chunk world renderer"
```

---

## Task 3: Render a grid in the sandbox

Wire the sandbox to `WorldRenderer`, grow the world to 4×1×4, and drop the sandbox's own GPU buffers. Verified by running the sandbox.

**Files:**
- Modify: `Sandbox/src/Sandbox.cpp`

**Interfaces:**
- Consumes (Task 2): `WorldRenderer::Update`, `WorldRenderer::Render`, `WorldRenderer::TotalFaceCount`.

- [ ] **Step 1: Rename `ChunkOrigin` to `WorldOffset`**

In `Sandbox/src/Sandbox.cpp`, the anonymous-namespace constant at line 22:

```cpp
    //Chunk-space origin of the rendered chunk, in world units. Placed so the
    //camera starts looking straight at terrain that is within editing reach.
    const glm::vec3 ChunkOrigin{ -8.0f, -6.0f, -18.0f };
```

becomes:

```cpp
    //Where the world's chunk (0,0,0) is placed in view, in world units. Chosen
    //so the camera starts looking at terrain within editing reach. The rest of
    //the grid extends from here. Player physics still live in chunk (0,0,0)'s
    //local space until step 4, so this is not yet a centred offset.
    const glm::vec3 WorldOffset{ -8.0f, -6.0f, -18.0f };
```

- [ ] **Step 2: Grow the world and swap the mesh members for a WorldRenderer**

In the member list (lines 348-355), delete:

```cpp
    std::unique_ptr<VertexArray> m_VertexArray;
    std::unique_ptr<VertexBuffer> m_VertexBuffer;
    std::unique_ptr<IndexBuffer> m_IndexBuffer;
```

and delete the `m_ChunkTransform` member (line 355):

```cpp
    glm::mat4 m_ChunkTransform{ 1.0f };
```

Change the world size (line 353) from:

```cpp
    World m_World{ 1, 1, 1 };
```

to:

```cpp
    World m_World{ 4, 1, 4 };
```

Add a `WorldRenderer` member next to `m_Shader`:

```cpp
    WorldRenderer m_WorldRenderer;
```

- [ ] **Step 3: Delete `RebuildMesh` and its call in the constructor**

Delete the whole `RebuildMesh` method (lines 257-275) and its comment.

In the constructor (lines 115-118), change:

```cpp
        BuildTestTerrain(m_World);
        RebuildMesh();

        m_ChunkTransform = glm::translate(glm::mat4(1.0f), ChunkOrigin);
        UpdateCameraPosition();
```

to:

```cpp
        BuildTestTerrain(m_World);
        //The world starts with every chunk dirty, so the first render meshes it.

        UpdateCameraPosition();
```

- [ ] **Step 4: Update the two remaining `ChunkOrigin` references**

`UpdateCameraPosition` (line 254):

```cpp
        m_CameraController.SetPosition(
            m_PlayerPosition + WorldOffset + glm::vec3(0.0f, EyeOffset, 0.0f));
```

The raycast origin in `OnMouseButtonPressed` (line 287):

```cpp
            camera.GetPosition() - WorldOffset,
```

- [ ] **Step 5: Drive the world renderer from `OnRender`**

Replace the body of `OnRender` (lines 188-197):

```cpp
    void OnRender() override
    {
        m_WorldRenderer.Update(m_World);
        m_HudState->MeshFaceCount = m_WorldRenderer.TotalFaceCount();

        Renderer::BeginScene(m_CameraController.GetCamera());
        m_WorldRenderer.Render(*m_Shader, WorldOffset);
        Renderer::EndScene();
    }
```

- [ ] **Step 6: Simplify the edit path**

In `OnMouseButtonPressed`, remove the `RebuildMesh();` call (line 311) — the edit already marked the affected chunks dirty via `SetBlock`, and the next `OnRender` remeshes them.

Replace the log block (lines 313-318) with one that does not read the now-removed index buffer:

```cpp
        CB_INFO(
            std::string(button == MouseCode::Left ? "Broke" : "Placed") +
            " block at " + std::to_string(target.x) + "," +
            std::to_string(target.y) + "," + std::to_string(target.z));
```

- [ ] **Step 7: Build**

Run the build command from Global Constraints.
Expected: PASS — build succeeds, Task 1 tests still green.

- [ ] **Step 8: Run the sandbox and verify visually**

WARN THE USER FIRST: running the sandbox grabs the mouse cursor for a few seconds.

Follow the standing Cubit verification routine: run `Sandbox.exe` detached with stdout redirected to a file, screenshot the window with `Graphics.CopyFromScreen`, then close it with `PostMessage(hwnd, WM_CLOSE=0x0010, 0, 0)` so GLFW shuts down and the log buffer flushes. Do **not** `Stop-Process`.

Expected in the screenshot:
- A 4-chunk-wide by 4-chunk-deep terrain grid (64×64 blocks) is visible, extending to the +X and +Z sides of where the single chunk used to sit.
- No buried wall of faces at the seams between chunks (step 2's neighbour-aware culling working through the grid for the first time).
- Breaking (left click) and placing (right click) still work on the chunk under the player, i.e. chunk (0,0,0).

Expected in the log: startup lines, and a `Broke`/`Placed block at x,y,z` line for each edit, with no errors.

- [ ] **Step 9: Commit**

```bash
cd /c/dev/Cubit
git add Sandbox/src/Sandbox.cpp
git commit -m "Render the chunk grid through a world renderer"
```

---

## Notes for the implementer

- **Scope boundary:** raycast and collision still read `m_World.GetChunk(0, 0, 0)` (lines 163, 286). That is deliberate — moving them to world space is step 4. The player will visually see the whole grid but only interact with, and physically stand on, chunk (0,0,0). Stepping off chunk 0 makes the player fall, because collision treats everything outside that one chunk as air. Do not "fix" this here.
- **Why `WorldOffset` is not centred:** the spec described a centred offset. Because player physics run in chunk (0,0,0)'s local coordinates until step 4, keeping `WorldOffset` at the original value leaves chunk 0 under the player and correct for editing. Centring the full world would float the player off-centre. This is called out in the design's step-4 dependency and is the honest minimal change.
