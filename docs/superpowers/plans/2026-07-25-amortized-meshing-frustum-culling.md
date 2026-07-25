# Amortized Meshing + Frustum Culling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stop the whole world meshing in one frame (amortize it) and stop drawing off-screen chunks (frustum culling), then ship a 256×64×256 battlefield map the fixes make viable.

**Architecture:** A new pure-math `Frustum` (doctest-tested) culls chunk AABBs in `WorldRenderer::Render`. `WorldRenderer::Update` gains an internal pending queue and a per-frame mesh budget. HUD counters make both fixes observable. `MapGen` regenerates the map at the single-`.vox` maximum size.

**Tech Stack:** C++20, GLM, doctest (Tests project, runs as a postbuild step), premake5 (`vs2026`), OpenGL. Windows/MSVC.

## Global Constraints

- C++20, MSVC, `CB_PLATFORM_WINDOWS`. Engine symbols crossing the DLL boundary are `CB_API` (from `Cubit/Core.h`).
- Chunk dimensions are `16 × 16 × 16` (`Chunk::Width/Height/Depth`).
- `WorldRenderer` makes GL calls and cannot run headless, so it has **no unit tests**; its per-task gate is "the solution compiles and the existing doctest suite still passes," and its behaviour is verified in the sandbox in Task 5. Only `Frustum` (pure math) is unit-tested.
- The camera view-projection is `PerspectiveCamera::GetViewProjectionMatrix()` (already exists), reached from the Sandbox via `m_CameraController.GetCamera().GetViewProjectionMatrix()`.
- Frustum convention: planes stored as `glm::vec4(a, b, c, d)`, a point is inside when `a*x + b*y + c*z + d >= 0`. AABB test uses the positive-vertex method (conservative: never a false negative).

### Toolchain commands (this machine)

- Regenerate projects after adding any file: `/c/dev/premake/premake5 vs2026` (from repo root `C:\dev\Cubit`).
- Build: `"/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe" "C:\dev\Cubit\Cubit.slnx" //p:Configuration=Debug //p:Platform=x64 //v:minimal //nologo`
- The Tests project runs `Tests.exe` as a postbuild step; a failing test breaks the build. The suite currently has **90** test cases. Watch the count.

---

## Task 1: `Frustum` — view frustum with AABB test (unit-tested)

**Files:**
- Create: `Cubit/include/Cubit/Renderer/Frustum.h`
- Create: `Cubit/src/Renderer/Frustum.cpp`
- Test: `Tests/src/FrustumTests.cpp`

**Interfaces:**
- Consumes: GLM.
- Produces:
  - `explicit Frustum(const glm::mat4& viewProjection);`
  - `bool Frustum::IntersectsAABB(const glm::vec3& min, const glm::vec3& max) const;`

- [ ] **Step 1: Create the header `Frustum.h`**

```cpp
#pragma once

#include "Cubit/Core.h"

#include <glm/glm.hpp>
#include <array>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

//A camera view frustum as six planes, for culling. Built from a view-projection
//matrix; tests axis-aligned boxes for visibility.
class CB_API Frustum
{
public:
    //Extracts the six clip planes from a view-projection matrix.
    explicit Frustum(const glm::mat4& viewProjection);

    //True if the axis-aligned box is at least partially inside the frustum.
    bool IntersectsAABB(const glm::vec3& min, const glm::vec3& max) const;

private:
    //Each plane is (a, b, c, d); a point is inside when a*x+b*y+c*z+d >= 0.
    std::array<glm::vec4, 6> m_Planes;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
```

- [ ] **Step 2: Write the failing tests `FrustumTests.cpp`**

```cpp
#include <doctest.h>

#include "Cubit/Renderer/Frustum.h"

#include <glm/gtc/matrix_transform.hpp>

namespace
{
    //A camera at the origin looking down -z, standard perspective.
    glm::mat4 TestViewProjection()
    {
        const glm::mat4 proj =
            glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
        const glm::mat4 view = glm::lookAt(
            glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        return proj * view;
    }
}

TEST_CASE("A box in front of the camera is inside the frustum")
{
    const Frustum f(TestViewProjection());
    CHECK(f.IntersectsAABB(glm::vec3(-1, -1, -6), glm::vec3(1, 1, -4)));
}

TEST_CASE("A box behind the camera is culled")
{
    const Frustum f(TestViewProjection());
    CHECK_FALSE(f.IntersectsAABB(glm::vec3(-1, -1, 4), glm::vec3(1, 1, 6)));
}

TEST_CASE("A box far to the side is culled")
{
    const Frustum f(TestViewProjection());
    CHECK_FALSE(f.IntersectsAABB(glm::vec3(999, -1, -6), glm::vec3(1001, 1, -4)));
}

TEST_CASE("A box straddling the near plane still intersects")
{
    const Frustum f(TestViewProjection());
    CHECK(f.IntersectsAABB(glm::vec3(-1, -1, -1.0f), glm::vec3(1, 1, -0.05f)));
}
```

- [ ] **Step 3: Regenerate and confirm the tests fail to link**

Run: `/c/dev/premake/premake5 vs2026`
Then build. Expected: link error — `Frustum` has no definition. (Add an empty `Frustum.cpp` with the include if needed to reach the link error.)

- [ ] **Step 4: Implement `Frustum.cpp`**

```cpp
#include "cub.h"

#include "Cubit/Renderer/Frustum.h"

#include <cmath>

namespace
{
    //Scales a plane so its normal is unit length (keeps distances meaningful).
    glm::vec4 NormalizePlane(const glm::vec4& p)
    {
        const float length = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        return (length > 0.0f) ? p / length : p;
    }
}

Frustum::Frustum(const glm::mat4& m)
{
    // glm is column-major (m[col][row]); pull the four rows of the matrix.
    const glm::vec4 row0(m[0][0], m[1][0], m[2][0], m[3][0]);
    const glm::vec4 row1(m[0][1], m[1][1], m[2][1], m[3][1]);
    const glm::vec4 row2(m[0][2], m[1][2], m[2][2], m[3][2]);
    const glm::vec4 row3(m[0][3], m[1][3], m[2][3], m[3][3]);

    m_Planes[0] = NormalizePlane(row3 + row0); // left
    m_Planes[1] = NormalizePlane(row3 - row0); // right
    m_Planes[2] = NormalizePlane(row3 + row1); // bottom
    m_Planes[3] = NormalizePlane(row3 - row1); // top
    m_Planes[4] = NormalizePlane(row3 + row2); // near
    m_Planes[5] = NormalizePlane(row3 - row2); // far
}

bool Frustum::IntersectsAABB(const glm::vec3& min, const glm::vec3& max) const
{
    for (const glm::vec4& plane : m_Planes)
    {
        // Positive vertex: the box corner furthest along the plane normal. If even
        // that corner is behind the plane, the whole box is outside it.
        const glm::vec3 positive(
            plane.x >= 0.0f ? max.x : min.x,
            plane.y >= 0.0f ? max.y : min.y,
            plane.z >= 0.0f ? max.z : min.z);

        if (plane.x * positive.x + plane.y * positive.y +
            plane.z * positive.z + plane.w < 0.0f)
            return false;
    }
    return true;
}
```

- [ ] **Step 5: Build and confirm the tests pass**

Build. Expected: the four `Frustum` cases pass; total case count rises 90 → 94. (If "behind"/"side" cases fail, a plane sign is flipped — re-check the row add/subtract order.)

- [ ] **Step 6: Commit**

```bash
git add Cubit Tests
git commit -m "Add a view frustum with an AABB visibility test"
```

---

## Task 2: Amortized meshing in `WorldRenderer`

Budget how many chunks mesh per frame so a big load spreads across frames instead of stalling one.

**Files:**
- Modify: `Cubit/include/Cubit/Renderer/WorldRenderer.h`
- Modify: `Cubit/src/Renderer/WorldRenderer.cpp`

**Interfaces:**
- Consumes: `World::DirtyChunks`, `World::ClearDirty`, `ChunkMesher::Build`.
- Produces: `std::size_t TotalChunkCount() const;`, `std::size_t PendingCount() const;`.

- [ ] **Step 1: Add the pending queue, budget, and accessors to `WorldRenderer.h`**

Add `#include <set>` and `#include <cstddef>` to the existing includes. Change the class body to add the budget constant, the two accessors, and the pending member:

Add these public members (near `TotalFaceCount`):
```cpp
    //Cached chunk meshes ready to draw.
    std::size_t TotalChunkCount() const { return m_Meshes.size(); }

    //Chunks still waiting to be (re)meshed.
    std::size_t PendingCount() const { return m_Pending.size(); }
```

In the `private:` section add the budget and the pending set:
```cpp
    //How many chunks to (re)mesh per Update, so a full load spreads over frames
    //instead of stalling one.
    static constexpr std::size_t MeshBudgetPerFrame = 16;

    std::set<glm::ivec3, IVec3Less> m_Pending;
```

- [ ] **Step 2: Rewrite `WorldRenderer::Update` in `WorldRenderer.cpp`**

Replace the whole `Update` function with:

```cpp
void WorldRenderer::Update(World& world)
{
    // Absorb this frame's changes into the pending set, then let the world forget
    // them: tracking changes is the world's job, scheduling remeshes is ours.
    for (const glm::ivec3& coord : world.DirtyChunks())
        m_Pending.insert(coord);
    world.ClearDirty();

    // Mesh up to the per-frame budget, removing each chunk as it is handled.
    std::size_t built = 0;
    for (auto it = m_Pending.begin();
         it != m_Pending.end() && built < MeshBudgetPerFrame; )
    {
        const glm::ivec3 coord = *it;
        const ChunkMeshData mesh =
            ChunkMesher::Build(world, coord.x, coord.y, coord.z);

        if (mesh.Indices.empty())
        {
            //A chunk that meshes to nothing keeps no buffers; drop any it had.
            m_Meshes.erase(coord);
        }
        else
        {
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

        it = m_Pending.erase(it);
        ++built;
    }
}
```

- [ ] **Step 3: Build and confirm the suite still passes**

Build. Expected: compiles; all 94 test cases still pass (no test changes this task). Behaviour is verified in the sandbox in Task 5.

- [ ] **Step 4: Commit**

```bash
git add Cubit
git commit -m "Amortize chunk meshing over frames with a per-frame budget"
```

---

## Task 3: Frustum culling in `WorldRenderer::Render`

Skip chunks outside the camera view, and record how many were drawn.

**Files:**
- Modify: `Cubit/include/Cubit/Renderer/WorldRenderer.h`
- Modify: `Cubit/src/Renderer/WorldRenderer.cpp`
- Modify: `Sandbox/src/Sandbox.cpp` (call site)

**Interfaces:**
- Consumes: `Frustum` (Task 1), `PerspectiveCamera::GetViewProjectionMatrix`.
- Produces: `void Render(const Shader&, const glm::mat4& viewProjection, const glm::vec3& worldOffset);` (non-const), `std::size_t DrawnChunkCount() const;`.

- [ ] **Step 1: Update the `Render` declaration and add the drawn-count accessor in `WorldRenderer.h`**

Change the `Render` declaration to:
```cpp
    //Draws every non-empty chunk mesh that is inside the camera frustum, each
    //translated to its chunk origin plus worldOffset. Records the drawn count.
    void Render(const Shader& shader, const glm::mat4& viewProjection,
        const glm::vec3& worldOffset);
```

Add the accessor near the other counts:
```cpp
    //Chunks actually submitted in the last Render (after frustum culling).
    std::size_t DrawnChunkCount() const { return m_LastDrawnChunks; }
```

In `private:` add the member:
```cpp
    std::size_t m_LastDrawnChunks = 0;
```

- [ ] **Step 2: Rewrite `WorldRenderer::Render` in `WorldRenderer.cpp`**

Add `#include "Cubit/Renderer/Frustum.h"` and `#include "Cubit/Voxel/Chunk.h"` to the includes. Replace the whole `Render` function with:

```cpp
void WorldRenderer::Render(const Shader& shader, const glm::mat4& viewProjection,
    const glm::vec3& worldOffset)
{
    const Frustum frustum(viewProjection);
    m_LastDrawnChunks = 0;

    for (const auto& entry : m_Meshes)
    {
        const glm::ivec3& coord = entry.first;
        const ChunkMesh& mesh = entry.second;

        const glm::vec3 origin =
            glm::vec3(World::GetChunkOrigin(coord.x, coord.y, coord.z));
        const glm::vec3 min = worldOffset + origin;
        const glm::vec3 max = min + glm::vec3(
            Chunk::Width, Chunk::Height, Chunk::Depth);

        if (!frustum.IntersectsAABB(min, max))
            continue; // outside the view

        const glm::mat4 transform = glm::translate(glm::mat4(1.0f), min);
        Renderer::Submit(*mesh.Array, *mesh.Indices, shader, transform);
        ++m_LastDrawnChunks;
    }
}
```

- [ ] **Step 3: Update the Sandbox call site**

In `Sandbox/src/Sandbox.cpp`, in `OnRender`, change the render call to pass the camera's view-projection:

```cpp
        m_WorldRenderer.Render(
            *m_Shader,
            m_CameraController.GetCamera().GetViewProjectionMatrix(),
            WorldOffset);
```

- [ ] **Step 4: Build and confirm the suite still passes**

Build. Expected: compiles; all 94 test cases still pass. Culling behaviour is verified in the sandbox in Task 5.

- [ ] **Step 5: Commit**

```bash
git add Cubit Sandbox
git commit -m "Frustum-cull chunks outside the camera view"
```

---

## Task 4: HUD counters (make the fixes observable)

Show `DRAWN n/m` and `PENDING k` so the culling and amortization are visible.

**Files:**
- Modify: `Sandbox/src/HudLayer.h`
- Modify: `Sandbox/src/Sandbox.cpp`

**Interfaces:**
- Consumes: `WorldRenderer::DrawnChunkCount/TotalChunkCount/PendingCount`.

- [ ] **Step 1: Add counters to `HudState` in `HudLayer.h`**

Add `#include <cstddef>` if not present. Extend the struct:
```cpp
struct HudState
{
    glm::vec3 PlayerPosition{ 0.0f };
    bool Grounded = false;
    std::uint32_t MeshFaceCount = 0;
    std::size_t DrawnChunks = 0;
    std::size_t TotalChunks = 0;
    std::size_t PendingChunks = 0;
};
```

- [ ] **Step 2: Draw the two new lines in `DrawReadout`**

In `HudLayer::DrawReadout`, after the `FACES` line and before the `FPS` line, add:
```cpp
        y -= lineHeight;
        DrawText("DRAWN " + std::to_string(m_State->DrawnChunks) +
            "/" + std::to_string(m_State->TotalChunks), TextMargin, y);

        y -= lineHeight;
        DrawText("PENDING " + std::to_string(m_State->PendingChunks), TextMargin, y);
```

- [ ] **Step 3: Publish the counters from the Sandbox**

In `Sandbox/src/Sandbox.cpp` `OnRender`, after the `Render` call and `Renderer::EndScene()`, set the counters (the `Render` call above must already pass the view-projection from Task 3):
```cpp
        m_HudState->MeshFaceCount = m_WorldRenderer.TotalFaceCount();
        m_HudState->DrawnChunks = m_WorldRenderer.DrawnChunkCount();
        m_HudState->TotalChunks = m_WorldRenderer.TotalChunkCount();
        m_HudState->PendingChunks = m_WorldRenderer.PendingCount();
```
Remove the old standalone `m_HudState->MeshFaceCount = ...;` line from earlier in `OnRender` if it now appears twice.

- [ ] **Step 4: Build and confirm the suite still passes**

Build. Expected: compiles; all 94 test cases still pass.

- [ ] **Step 5: Commit**

```bash
git add Sandbox
git commit -m "Show drawn and pending chunk counts on the HUD"
```

---

## Task 5: Ship the 256 map and verify the fixes

Regenerate the battlefield at the single-`.vox` maximum, update its size test, and verify in the sandbox.

**Files:**
- Modify: `MapGen/src/MapGen.cpp`
- Modify: `Sandbox/assets/maps/battlefield.vox` (regenerated, committed)
- Modify: `Tests/src/TerrainGenTests.cpp`

**Interfaces:**
- Consumes: `TerrainGen::Generate`, `VoxWriter::Write`.

- [ ] **Step 1: Set the shipped map size in `MapGen.cpp`**

Change the config construction to size the map explicitly:
```cpp
    TerrainConfig config;
    config.Size = glm::ivec3(256, 64, 256);
    const VoxModel model = TerrainGen::Generate(config);
```

- [ ] **Step 2: Update the committed-asset size test in `TerrainGenTests.cpp`**

Change the expected size in `"The committed battlefield map loads at the expected size"`:
```cpp
    const VoxModel m = VoxLoader::LoadFile(path.string());
    CHECK(m.Size == glm::ivec3(256, 64, 256));
```

- [ ] **Step 3: Rebuild so `MapGen.exe` picks up the new size**

Build. Expected: compiles. (The committed-asset test still checks the *old* 128 map until Step 4 regenerates it, so it may fail here — that is expected; it passes after Step 4.)

- [ ] **Step 4: Regenerate the committed map**

From the repo root `C:\dev\Cubit`:
```
./bin/Debug-windows-x86_64/MapGen/MapGen.exe Sandbox/assets/maps/battlefield.vox
```
Expected: prints `Wrote Sandbox/assets/maps/battlefield.vox (... bytes)` — several MB.

- [ ] **Step 5: Rebuild and confirm the suite passes**

Build. Expected: compiles; the committed-asset test now sees a 256×64×256 map and passes; all 94 cases pass. (The `{COPYDIR} assets` postbuild copies the new `battlefield.vox` beside the Sandbox exe.)

- [ ] **Step 6: Verify rendering and the perf fixes in the sandbox**

Run the Sandbox with its working directory at the target dir (so `assets/` resolves), screenshot, and close via `PostMessage(hwnd, WM_CLOSE=0x0010, 0, 0)` (project convention — do not `Stop-Process`, or the buffered stdout log is lost). Do **not** foreground the cursor-capturing window, or stray clicks edit the map. Warn the user first — running grabs the mouse cursor for a few seconds.

Confirm:
- The window opens without a multi-second freeze (amortization), and the HUD `PENDING` count drains to 0 over the first second or so.
- The HUD `DRAWN n/m` shows `n < m` when the camera faces only part of the map (culling).
- The frame renders correctly — a large battlefield, no chunks wrongly missing from view.

If the spawn puts the camera somewhere dull for the larger map, nudge `SpawnPosition` in `Sandbox.cpp` for a better vantage and rebuild (optional polish).

- [ ] **Step 7: Commit**

```bash
git add MapGen Sandbox Tests
git commit -m "Ship the 256x64x256 battlefield map"
```

---

## Notes for the implementer

- The premake `files` globs pick up new `Cubit/src/**.cpp` and `Tests/src/**.cpp` automatically, but you MUST regenerate (`premake5 vs2026`) after adding files (Task 1 adds `Frustum.cpp`/`FrustumTests.cpp`).
- `WorldRenderer` can't be unit-tested (GL). Tasks 2–4 gate on "compiles + 94 tests still pass"; the real behavioural check is the sandbox run in Task 5. Only `Frustum` gets doctests.
- The 256 map is ~1024 chunks; at `MeshBudgetPerFrame = 16` it fills in over ~64 frames (~1s). If that feels slow, raising the budget is a one-line change — but keep each frame smooth.
- `GetViewProjectionMatrix` already exists on `PerspectiveCamera`; no camera changes are needed.
