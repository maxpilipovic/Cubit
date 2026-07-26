# Ambient Occlusion + Sky Lighting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace Cubit's six fixed per-face shading constants with per-vertex ambient occlusion and a propagated sky-light grid, so crevices darken and enclosed spaces actually go dark.

**Architecture:** `ChunkMesher`'s six near-identical face emitters collapse into one table-driven `AddFace` that samples the three cells around each face corner — yielding both an occlusion level and a smooth light average from the same lookup. A new pure-CPU `SkyLight` class floods sky light through the world at load and re-floods a bounded box on each block edit. All shading stays baked into the vertex colour, so the vertex format and shaders are untouched.

**Tech Stack:** C++20, GLM, doctest (Tests project, runs as a postbuild step), premake5 (`vs2026`), OpenGL. Windows/MSVC.

**Spec:** [2026-07-26-ambient-occlusion-lighting-design.md](../specs/2026-07-26-ambient-occlusion-lighting-design.md)

## Global Constraints

- C++20, MSVC, `CB_PLATFORM_WINDOWS`. Engine symbols crossing the DLL boundary are `CB_API` (from `Cubit/Core.h`).
- Chunk dimensions are `16 × 16 × 16` (`Chunk::Width/Height/Depth`).
- `BlockId` is `std::uint8_t`; `0` is air. `IsSolid(block)` is `block != 0`.
- Maximum sky light level is **15**. Out-of-bounds light reads return **15** (open sky); out-of-bounds block reads return air.
- Comment style in this codebase is `//No space after the slashes` for doc comments on declarations, and `// With a space` for inline explanations inside function bodies. Match the file you are editing.
- Shading stays **baked CPU-side into `VoxelVertex::Color`**. Do not add vertex attributes, do not touch the shaders in `Sandbox.cpp`.
- The dependency direction is `SkyLight` → `World`, never the reverse. `World` must not include or call `SkyLight`.
- The existing doctest suite has **94** test cases before this plan starts. Watch the count — it should only grow.

### Toolchain commands (this machine)

- Regenerate projects after adding any file: `/c/dev/premake/premake5 vs2026` (from repo root `C:\dev\Cubit`).
- Build: `"/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe" "C:\dev\Cubit\Cubit.slnx" //p:Configuration=Debug //p:Platform=x64 //v:minimal //nologo`
- The Tests project runs `Tests.exe` as a postbuild step, so **a failing test breaks the build**. There is no separate test command — building is running the tests.
- To run one test case in isolation: `C:\dev\Cubit\bin\Debug-windows-x86_64\Tests\Tests.exe --test-case="<name>"`

---

# Phase 1 — Ambient Occlusion

Phase 1 is independently shippable. After Task 2 the sandbox shows real contact shading on trenches, craters and overhangs, with no lighting subsystem present.

---

## Task 1: `CornerAoLevel` — the occlusion computation

Exposes the core AO calculation as a tested pure function before wiring it into geometry.

**Files:**
- Modify: `Cubit/include/Cubit/Voxel/ChunkMesher.h`
- Modify: `Cubit/src/Voxel/ChunkMesher.cpp`
- Create: `Tests/src/ChunkAoTests.cpp`

**Interfaces:**
- Consumes: `World::IsBlockSolid(int, int, int)`, GLM.
- Produces:
  - `static constexpr float ChunkMesher::AoShade[4]` — brightness per occlusion level.
  - `static int ChunkMesher::CornerAoLevel(const World& world, const glm::ivec3& airCell, const glm::ivec3& sideA, const glm::ivec3& sideB);`

- [ ] **Step 1: Write the failing test**

Create `Tests/src/ChunkAoTests.cpp`:

```cpp
#include <doctest.h>

#include "Cubit/Voxel/Chunk.h"
#include "Cubit/Voxel/ChunkMesher.h"
#include "Cubit/Voxel/World.h"

namespace
{
    //The cell just above a block at (8, 8, 8): the air side of its top face.
    constexpr glm::ivec3 AirAboveBlock{ 8, 9, 8 };

    //The two tangent offsets picking out one corner of that top face, pointing
    //toward +X and +Z.
    constexpr glm::ivec3 SideX{ 1, 0, 0 };
    constexpr glm::ivec3 SideZ{ 0, 0, 1 };
}

TEST_CASE("An unobstructed corner is fully open")
{
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});

    CHECK(ChunkMesher::CornerAoLevel(world, AirAboveBlock, SideX, SideZ) == 3);
}

TEST_CASE("A block diagonally across a corner occludes it by one")
{
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});

    // Diagonally opposite the corner: touches it, but neither side.
    world.SetBlock(9, 9, 9, BlockId{1});

    CHECK(ChunkMesher::CornerAoLevel(world, AirAboveBlock, SideX, SideZ) == 2);
}

TEST_CASE("A block on one side of a corner occludes it by one")
{
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});
    world.SetBlock(9, 9, 8, BlockId{1});

    CHECK(ChunkMesher::CornerAoLevel(world, AirAboveBlock, SideX, SideZ) == 2);
}

TEST_CASE("A corner with one side and the diagonal filled is occluded by two")
{
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});
    world.SetBlock(9, 9, 8, BlockId{1});
    world.SetBlock(9, 9, 9, BlockId{1});

    CHECK(ChunkMesher::CornerAoLevel(world, AirAboveBlock, SideX, SideZ) == 1);
}

TEST_CASE("Both sides filled pins a corner fully dark regardless of the diagonal")
{
    // Two walls meeting at a right angle. The diagonal behind them cannot make
    // the corner any lighter, and must not be allowed to.
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});
    world.SetBlock(9, 9, 8, BlockId{1});
    world.SetBlock(8, 9, 9, BlockId{1});

    CHECK(ChunkMesher::CornerAoLevel(world, AirAboveBlock, SideX, SideZ) == 0);

    world.SetBlock(9, 9, 9, BlockId{1});
    CHECK(ChunkMesher::CornerAoLevel(world, AirAboveBlock, SideX, SideZ) == 0);
}

TEST_CASE("Occlusion outside the world reads as open sky")
{
    // A block on the world's top face has nothing above or beside it, so its
    // top corners must be fully open rather than dark.
    World world(1, 1, 1);
    const int top = world.GetHeight() - 1;
    world.SetBlock(8, top, 8, BlockId{1});

    const glm::ivec3 air{ 8, top + 1, 8 };
    CHECK(ChunkMesher::CornerAoLevel(world, air, SideX, SideZ) == 3);
}

TEST_CASE("The occlusion shade table darkens monotonically")
{
    CHECK(ChunkMesher::AoShade[3] == doctest::Approx(1.00f));
    CHECK(ChunkMesher::AoShade[2] < ChunkMesher::AoShade[3]);
    CHECK(ChunkMesher::AoShade[1] < ChunkMesher::AoShade[2]);
    CHECK(ChunkMesher::AoShade[0] < ChunkMesher::AoShade[1]);
    CHECK(ChunkMesher::AoShade[0] > 0.0f);
}
```

- [ ] **Step 2: Regenerate projects so the new test file is compiled**

Run: `/c/dev/premake/premake5 vs2026`

- [ ] **Step 3: Run the build to verify the test fails**

Run the build command from Global Constraints.
Expected: FAIL to compile — `CornerAoLevel` and `AoShade` are not members of `ChunkMesher`.

- [ ] **Step 4: Declare the interface in `ChunkMesher.h`**

Add to the `public:` section of `class CB_API ChunkMesher`, after the `Build` declaration:

```cpp
    //Brightness multiplier per ambient-occlusion level: index 0 is the most
    //enclosed corner, index 3 is fully open.
    static constexpr float AoShade[4] = { 0.55f, 0.70f, 0.85f, 1.00f };

    //How exposed one corner of a face is, from 0 (fully enclosed) to 3 (open).
    //airCell is the cell in front of the face; sideA and sideB are the offsets
    //from it toward the two edges meeting at the corner. Public so it can be
    //tested directly, and so a future greedy mesher can merge on equal values.
    static int CornerAoLevel(
        const World& world,
        const glm::ivec3& airCell,
        const glm::ivec3& sideA,
        const glm::ivec3& sideB);
```

- [ ] **Step 5: Implement it in `ChunkMesher.cpp`**

Add at the bottom of the file, after `ChunkMesher::Build`:

```cpp
int ChunkMesher::CornerAoLevel(
    const World& world,
    const glm::ivec3& airCell,
    const glm::ivec3& sideA,
    const glm::ivec3& sideB)
{
    const glm::ivec3 a = airCell + sideA;
    const glm::ivec3 b = airCell + sideB;

    const bool solidA = world.IsBlockSolid(a.x, a.y, a.z);
    const bool solidB = world.IsBlockSolid(b.x, b.y, b.z);

    // Two walls meeting at a right angle seal the corner completely, so what
    // sits diagonally behind them cannot lighten it.
    if (solidA && solidB)
        return 0;

    const glm::ivec3 c = airCell + sideA + sideB;
    const bool solidCorner = world.IsBlockSolid(c.x, c.y, c.z);

    return 3
        - static_cast<int>(solidA)
        - static_cast<int>(solidB)
        - static_cast<int>(solidCorner);
}
```

- [ ] **Step 6: Run the build to verify all tests pass**

Run the build command.
Expected: PASS, test count **101** (94 + 7).

- [ ] **Step 7: Commit**

```bash
git add Cubit/include/Cubit/Voxel/ChunkMesher.h Cubit/src/Voxel/ChunkMesher.cpp Tests/src/ChunkAoTests.cpp Cubit.slnx Tests/Tests.vcxproj
git commit -m "Add per-corner ambient occlusion computation"
```

---

## Task 2: Table-driven faces, AO shading, and the triangulation flip

Collapses the six near-identical face emitters into one table-driven function, which is what makes per-corner AO expressible without writing the same corner loop six times.

**Files:**
- Modify: `Cubit/src/Voxel/ChunkMesher.cpp` (replaces the six `Add*Face` helpers and `AddFaceIndices`)
- Modify: `Tests/src/ChunkAoTests.cpp` (append)

**Interfaces:**
- Consumes: `ChunkMesher::CornerAoLevel`, `ChunkMesher::AoShade` from Task 1.
- Produces: no new public API. `ChunkMesher::Build` keeps its existing signature and its existing vertex and index *counts*; only vertex colours and index winding change.

- [ ] **Step 1: Write the failing test**

Append to `Tests/src/ChunkAoTests.cpp`:

```cpp
#include <algorithm>

namespace
{
    //The colour a fully open top face has: the palette colour at full top shade
    //and no occlusion. Any darker vertex on a top face is occluded.
    glm::vec3 OpenTopColor(const World& world)
    {
        return world.GetBlockColor(BlockId{1}) * ChunkMesher::AoShade[3];
    }

    //The darkest red channel among vertices lying on a given horizontal plane.
    float DarkestOnPlane(const ChunkMeshData& mesh, float y)
    {
        float darkest = 1.0f;
        for (const VoxelVertex& vertex : mesh.Vertices)
            if (vertex.Position.y == y)
                darkest = std::min(darkest, vertex.Color.r);

        return darkest;
    }
}

TEST_CASE("A lone block's top face is unoccluded on every corner")
{
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);

    // Nothing else is solid, so the only vertices at y == 9 are the top face.
    int topVertices = 0;
    for (const VoxelVertex& vertex : mesh.Vertices)
    {
        if (vertex.Position.y != 9.0f)
            continue;

        ++topVertices;
        CHECK(vertex.Color.r == doctest::Approx(OpenTopColor(world).r));
    }

    CHECK(topVertices == 4);
}

TEST_CASE("An inside corner darkens the floor beside it")
{
    // A floor slab with a wall standing on it. The floor vertices that touch
    // the wall must come out darker than the open floor away from it.
    World world(1, 1, 1);

    for (int z = 0; z < Chunk::Depth; ++z)
        for (int x = 0; x < Chunk::Width; ++x)
            world.SetBlock(x, 0, z, BlockId{1});

    const ChunkMeshData open = ChunkMesher::Build(world, 0, 0, 0);
    const float openFloor = DarkestOnPlane(open, 1.0f);

    for (int z = 0; z < Chunk::Depth; ++z)
        world.SetBlock(8, 1, z, BlockId{1});

    const ChunkMeshData walled = ChunkMesher::Build(world, 0, 0, 0);

    CHECK(openFloor == doctest::Approx(OpenTopColor(world).r));
    CHECK(DarkestOnPlane(walled, 1.0f) < openFloor);
}

TEST_CASE("Ambient occlusion changes no geometry counts")
{
    // AO only recolours vertices and may reorder a quad's indices. The mesh
    // must stay the same size, so the sandbox terrain's known totals hold.
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});
    world.SetBlock(9, 9, 9, BlockId{1});
    world.SetBlock(7, 9, 8, BlockId{1});

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);

    REQUIRE(mesh.Vertices.size() % 4 == 0);
    CHECK(mesh.Indices.size() == (mesh.Vertices.size() / 4) * 6);

    for (const std::uint32_t index : mesh.Indices)
        CHECK(index < mesh.Vertices.size());
}

TEST_CASE("A flipped quad still references each of its four vertices")
{
    // Whichever diagonal a quad is split along, both triangles together must
    // cover all four corners, or the quad renders with a missing wedge.
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});
    world.SetBlock(9, 9, 8, BlockId{1});

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);

    for (std::size_t quad = 0; quad < mesh.Vertices.size() / 4; ++quad)
    {
        const std::uint32_t first = static_cast<std::uint32_t>(quad) * 4;
        bool used[4] = { false, false, false, false };

        for (std::size_t i = quad * 6; i < quad * 6 + 6; ++i)
        {
            REQUIRE(mesh.Indices[i] >= first);
            REQUIRE(mesh.Indices[i] < first + 4);
            used[mesh.Indices[i] - first] = true;
        }

        CHECK(used[0]);
        CHECK(used[1]);
        CHECK(used[2]);
        CHECK(used[3]);
    }
}
```

- [ ] **Step 2: Run the build to verify the tests fail**

Run the build command.
Expected: FAIL — "A lone block's top face is unoccluded on every corner" passes by luck (current shading already gives full top shade), but "An inside corner darkens the floor beside it" FAILS, because every top face currently gets the identical `TopShade` and `DarkestOnPlane` returns the same value both times.

- [ ] **Step 3: Replace `AddFaceIndices` with a flippable version**

In `ChunkMesher.cpp`, replace the whole existing `AddFaceIndices` function with:

```cpp
    //Adds two triangles referencing the four vertices most recently appended.
    //A quad can be split along either diagonal; flipping picks the other one.
    void AddFaceIndices(ChunkMeshData& mesh, bool flip)
    {
        CB_CORE_ASSERT(
            mesh.Vertices.size() >= 4,
            "A face must append its four vertices before its indices");

        const std::uint32_t firstVertex =
            static_cast<std::uint32_t>(mesh.Vertices.size()) - 4;

        if (flip)
        {
            mesh.Indices.push_back(firstVertex + 1);
            mesh.Indices.push_back(firstVertex + 2);
            mesh.Indices.push_back(firstVertex + 3);
            mesh.Indices.push_back(firstVertex + 3);
            mesh.Indices.push_back(firstVertex + 0);
            mesh.Indices.push_back(firstVertex + 1);
        }
        else
        {
            mesh.Indices.push_back(firstVertex + 0);
            mesh.Indices.push_back(firstVertex + 1);
            mesh.Indices.push_back(firstVertex + 2);
            mesh.Indices.push_back(firstVertex + 2);
            mesh.Indices.push_back(firstVertex + 3);
            mesh.Indices.push_back(firstVertex + 0);
        }
    }
```

- [ ] **Step 4: Replace the six `Add*Face` helpers with one face table**

Delete `AddFrontFace`, `AddBackFace`, `AddRightFace`, `AddLeftFace`, `AddTopFace` and `AddBottomFace` entirely. Keep the six `*Shade` constants exactly as they are. In their place, add:

```cpp
    //One block face, described rather than hand-written. Corner holds the four
    //vertex offsets from the block's minimum corner, in the winding order the
    //face is emitted in. U and V are the two axes spanning the face, and
    //CornerU/CornerV give each vertex's sign along them — which is what lets a
    //corner's two occluding neighbours be found without a switch per face.
    struct FaceGeometry
    {
        glm::ivec3 Normal;
        glm::vec3 Corner[4];
        glm::ivec3 U;
        glm::ivec3 V;
        int CornerU[4];
        int CornerV[4];
        float Shade;
    };

    constexpr FaceGeometry Faces[6] =
    {
        // Front (+Z)
        { {  0,  0,  1 },
          { { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 1.0f },
            { 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f, 1.0f } },
          { 1, 0, 0 }, { 0, 1, 0 },
          { -1, +1, +1, -1 }, { -1, -1, +1, +1 },
          FrontShade },

        // Back (-Z)
        { {  0,  0, -1 },
          { { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f },
            { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } },
          { 1, 0, 0 }, { 0, 1, 0 },
          { +1, -1, -1, +1 }, { -1, -1, +1, +1 },
          BackShade },

        // Right (+X)
        { {  1,  0,  0 },
          { { 1.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f },
            { 1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } },
          { 0, 0, 1 }, { 0, 1, 0 },
          { +1, -1, -1, +1 }, { -1, -1, +1, +1 },
          RightShade },

        // Left (-X)
        { { -1,  0,  0 },
          { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f },
            { 0.0f, 1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
          { 0, 0, 1 }, { 0, 1, 0 },
          { -1, +1, +1, -1 }, { -1, -1, +1, +1 },
          LeftShade },

        // Top (+Y)
        { {  0,  1,  0 },
          { { 0.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f },
            { 1.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
          { 1, 0, 0 }, { 0, 0, 1 },
          { -1, +1, +1, -1 }, { +1, +1, -1, -1 },
          TopShade },

        // Bottom (-Y)
        { {  0, -1,  0 },
          { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f },
            { 1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },
          { 1, 0, 0 }, { 0, 0, 1 },
          { -1, +1, +1, -1 }, { -1, -1, +1, +1 },
          BottomShade },
    };

    //Emits one face: four vertices shaded by their own corner occlusion, then
    //the two triangles joining them.
    void AddFace(
        ChunkMeshData& mesh,
        const World& world,
        const glm::ivec3& worldPosition,
        const glm::vec3& blockOrigin,
        const FaceGeometry& face,
        const glm::vec3& blockColor)
    {
        const glm::ivec3 airCell = worldPosition + face.Normal;

        int ao[4];
        for (int i = 0; i < 4; ++i)
        {
            ao[i] = ChunkMesher::CornerAoLevel(
                world,
                airCell,
                face.U * face.CornerU[i],
                face.V * face.CornerV[i]);
        }

        for (int i = 0; i < 4; ++i)
        {
            const glm::vec3 color =
                blockColor * face.Shade * ChunkMesher::AoShade[ao[i]];

            mesh.Vertices.push_back({ blockOrigin + face.Corner[i], color });
        }

        // Splitting a quad along its darker diagonal keeps the shading gradient
        // smooth; splitting the other way leaves a visible seam across it.
        AddFaceIndices(mesh, ao[0] + ao[2] > ao[1] + ao[3]);
    }
```

- [ ] **Step 5: Rewrite `AddExposedFaces` to drive the table**

Replace the whole existing `AddExposedFaces` function with:

```cpp
    //Emits the faces of one block that are exposed to air. Neighbours are looked
    //up in world coordinates so blocks in the next chunk are visible, while the
    //vertices use chunk-local coordinates.
    void AddExposedFaces(
        ChunkMeshData& mesh,
        const World& world,
        const glm::ivec3& worldPosition,
        const glm::ivec3& localPosition)
    {
        const glm::vec3 blockOrigin(localPosition);
        const glm::vec3 color = world.GetBlockColor(
            world.GetBlock(worldPosition.x, worldPosition.y, worldPosition.z));

        for (const FaceGeometry& face : Faces)
        {
            const glm::ivec3 neighbour = worldPosition + face.Normal;

            if (!world.IsBlockSolid(neighbour.x, neighbour.y, neighbour.z))
                AddFace(mesh, world, worldPosition, blockOrigin, face, color);
        }
    }
```

- [ ] **Step 6: Run the build to verify all tests pass**

Run the build command.
Expected: PASS, test count **105** (101 + 4). The pre-existing `ChunkMesherTests.cpp` cases must all still pass unchanged — in particular "The sandbox test terrain meshes to its known size" (1122 faces / 4488 vertices / 6732 indices), which proves the refactor changed no geometry.

- [ ] **Step 7: Commit**

```bash
git add Cubit/src/Voxel/ChunkMesher.cpp Tests/src/ChunkAoTests.cpp
git commit -m "Shade block faces with per-corner ambient occlusion"
```

---

## Task 3: Verify ambient occlusion in the sandbox

Phase 1's visual gate. `ChunkMesher` is unit-tested, but only a rendered frame proves the shading reads correctly.

**Files:** none modified — this task is verification only.

**Interfaces:**
- Consumes: everything from Tasks 1–2.
- Produces: a screenshot confirming Phase 1, and a decision on the `AoShade` constants.

- [ ] **Step 1: Warn the user before running**

The sandbox captures the mouse cursor for the duration of the run. Tell the user before launching it.

- [ ] **Step 2: Run the sandbox and capture a frame**

Launch `bin\Debug-windows-x86_64\Sandbox\Sandbox.exe` detached with stdout redirected to a file, working directory `Sandbox/`. Wait ~7 seconds (Discord's game overlay covers the top-left HUD for the first few seconds), screenshot the window via `Graphics.CopyFromScreen`, then close it with `PostMessage(hwnd, 0x0010, 0, 0)` — `WM_CLOSE`.

**Do not `Stop-Process`.** The logger writes to `std::cout`, which is fully buffered when redirected; force-killing discards every line of the log.

- [ ] **Step 3: Read the frame**

Confirm in the screenshot:
- Trench and crater floors darken toward their walls.
- Block edges where terrain meets terrain show a soft dark seam.
- No hard diagonal seams across individual quads — that would mean the flip in Task 2 Step 3 is inverted.
- The HUD still reads a sensible `DRAWN n/m` and `PENDING 0`, and FPS has not collapsed.

- [ ] **Step 4: Retune `AoShade` if needed**

`{ 0.55f, 0.70f, 0.85f, 1.00f }` is a starting guess. If the effect is too subtle, lower index 0; if the world looks grimy, raise it. Edit `ChunkMesher.h`, rebuild, re-screenshot. Show the user the frame and get their read before settling.

- [ ] **Step 5: Commit any retune**

```bash
git add Cubit/include/Cubit/Voxel/ChunkMesher.h
git commit -m "Tune ambient occlusion shade levels"
```

Skip this commit if Step 4 changed nothing.

---

# Phase 2 — Sky Lighting

---

## Task 4: `Chunk` sky-light storage

**Files:**
- Modify: `Cubit/include/Cubit/Voxel/Chunk.h`
- Modify: `Cubit/src/Voxel/Chunk.cpp`
- Modify: `Tests/src/ChunkTests.cpp` (append)

**Interfaces:**
- Consumes: `Chunk::GetIndex`, `Chunk::IsInBounds` (both already exist).
- Produces:
  - `std::uint8_t Chunk::GetSkyLight(int x, int y, int z) const;`
  - `void Chunk::SetSkyLight(int x, int y, int z, std::uint8_t level);`

- [ ] **Step 1: Write the failing test**

Append to `Tests/src/ChunkTests.cpp`:

```cpp
TEST_CASE("A new chunk starts with no sky light")
{
    const Chunk chunk;

    CHECK(chunk.GetSkyLight(0, 0, 0) == 0);
    CHECK(chunk.GetSkyLight(8, 8, 8) == 0);
}

TEST_CASE("Sky light can be set and read back")
{
    Chunk chunk;
    chunk.SetSkyLight(3, 4, 5, 12);

    CHECK(chunk.GetSkyLight(3, 4, 5) == 12);

    // Setting one cell must not disturb its neighbours.
    CHECK(chunk.GetSkyLight(4, 4, 5) == 0);
    CHECK(chunk.GetSkyLight(3, 5, 5) == 0);
}

TEST_CASE("Sky light outside a chunk reads as open sky")
{
    const Chunk chunk;

    CHECK(chunk.GetSkyLight(-1, 0, 0) == 15);
    CHECK(chunk.GetSkyLight(0, Chunk::Height, 0) == 15);
    CHECK(chunk.GetSkyLight(0, 0, Chunk::Depth) == 15);
}
```

- [ ] **Step 2: Run the build to verify the tests fail**

Run the build command.
Expected: FAIL to compile — `GetSkyLight` is not a member of `Chunk`.

- [ ] **Step 3: Declare the storage and accessors**

In `Chunk.h`, add to the `public:` section after `IsBlockSolid`:

```cpp
    //Returns a cell's sky light, treating positions outside this chunk as open
    //sky. The mesher samples corners that straddle a chunk edge, so an
    //out-of-range read must not read as darkness.
    std::uint8_t GetSkyLight(int x, int y, int z) const;

    //Sets a cell's sky light; throws when the position is outside this chunk.
    void SetSkyLight(int x, int y, int z, std::uint8_t level);
```

Add to the `private:` section, after `m_Blocks`:

```cpp
    std::array<std::uint8_t, BlockCount> m_SkyLight;
```

Add `#include <cstdint>` to the include block.

- [ ] **Step 4: Implement in `Chunk.cpp`**

In the `Chunk::Chunk()` constructor body, zero the new array alongside the existing block fill:

```cpp
    m_SkyLight.fill(0);
```

Add the two accessors, matching how `GetBlock`/`SetBlock` in the same file handle bounds:

```cpp
std::uint8_t Chunk::GetSkyLight(int x, int y, int z) const
{
    if (!IsInBounds(x, y, z))
        return 15; // Outside the chunk is open sky, not darkness.

    return m_SkyLight[GetIndex(x, y, z)];
}

void Chunk::SetSkyLight(int x, int y, int z, std::uint8_t level)
{
    CB_CORE_ASSERT(IsInBounds(x, y, z), "Sky light set outside the chunk");

    m_SkyLight[GetIndex(x, y, z)] = level;
}
```

Match the exact assert or throw idiom `Chunk::SetBlock` already uses in this file rather than copying the line above verbatim.

- [ ] **Step 5: Run the build to verify all tests pass**

Run the build command.
Expected: PASS, test count **108** (105 + 3).

- [ ] **Step 6: Commit**

```bash
git add Cubit/include/Cubit/Voxel/Chunk.h Cubit/src/Voxel/Chunk.cpp Tests/src/ChunkTests.cpp
git commit -m "Store sky light per block in Chunk"
```

---

## Task 5: `World` sky-light access and dirty marking

**Files:**
- Modify: `Cubit/include/Cubit/Voxel/World.h`
- Modify: `Cubit/src/Voxel/World.cpp`
- Modify: `Tests/src/WorldTests.cpp` (append)

**Interfaces:**
- Consumes: `Chunk::GetSkyLight`/`SetSkyLight` from Task 4; the existing private `World::MarkChunkDirtyForEdit`.
- Produces:
  - `std::uint8_t World::GetSkyLight(int x, int y, int z) const;`
  - `void World::SetSkyLight(int x, int y, int z, std::uint8_t level);`
  - `void World::MarkChunkDirtyAt(int x, int y, int z);` (was private as `MarkChunkDirtyForEdit`)

- [ ] **Step 1: Write the failing test**

Append to `Tests/src/WorldTests.cpp`:

```cpp
TEST_CASE("World sky light round-trips through the chunk grid")
{
    World world(2, 2, 2);
    world.SetSkyLight(20, 3, 5, 9);

    CHECK(world.GetSkyLight(20, 3, 5) == 9);
    CHECK(world.GetSkyLight(21, 3, 5) == 0);
}

TEST_CASE("Sky light outside the world reads as open sky")
{
    const World world(1, 1, 1);

    CHECK(world.GetSkyLight(-1, 0, 0) == 15);
    CHECK(world.GetSkyLight(0, world.GetHeight(), 0) == 15);
    CHECK(world.GetSkyLight(0, 0, world.GetDepth()) == 15);
}

TEST_CASE("Sky light crosses a chunk boundary independently")
{
    // The last column of chunk 0 and the first of chunk 1 are neighbours in
    // world space but live in different chunks, so this catches an accessor
    // that forgets to convert to chunk-local coordinates.
    World world(2, 1, 1);
    world.SetSkyLight(Chunk::Width - 1, 0, 0, 7);
    world.SetSkyLight(Chunk::Width, 0, 0, 4);

    CHECK(world.GetSkyLight(Chunk::Width - 1, 0, 0) == 7);
    CHECK(world.GetSkyLight(Chunk::Width, 0, 0) == 4);
}

TEST_CASE("Marking a position dirty also marks the chunk across a shared face")
{
    World world(2, 1, 1);
    world.ClearDirty();

    world.MarkChunkDirtyAt(Chunk::Width - 1, 0, 0);

    CHECK(world.DirtyChunks().count(glm::ivec3(0, 0, 0)) == 1);
    CHECK(world.DirtyChunks().count(glm::ivec3(1, 0, 0)) == 1);
}
```

- [ ] **Step 2: Run the build to verify the tests fail**

Run the build command.
Expected: FAIL to compile — `GetSkyLight` and `MarkChunkDirtyAt` are not members of `World`.

- [ ] **Step 3: Declare the interface in `World.h`**

Add to the `public:` section, after `IsBlockSolid`:

```cpp
    //Returns a cell's sky light, treating positions outside the world as open
    //sky so the map's edges and the space above it are lit.
    std::uint8_t GetSkyLight(int x, int y, int z) const;

    //Sets a cell's sky light; throws when the position is outside the world.
    //Does not mark anything dirty — propagation decides which chunks actually
    //changed and marks those, so clearing and re-flooding a cell to the same
    //value costs no remeshing.
    void SetSkyLight(int x, int y, int z, std::uint8_t level);

    //Marks the chunk holding this position dirty, plus any chunk sharing a face
    //the position lies on. Public because a chunk's mesh samples light from the
    //cells just across its boundary, so a light change at an edge invalidates
    //the neighbour's mesh too.
    void MarkChunkDirtyAt(int x, int y, int z);
```

Remove the `MarkChunkDirtyForEdit` declaration from the `private:` section. Add `#include <cstdint>`.

- [ ] **Step 4: Implement in `World.cpp`**

Rename the existing `World::MarkChunkDirtyForEdit` definition to `World::MarkChunkDirtyAt`, leaving its body unchanged, and update its caller inside `World::SetBlock`.

Add the two accessors, following the exact chunk-lookup pattern `World::GetBlock` and `World::SetBlock` already use in this file (world coordinates → chunk coordinates → chunk-local coordinates):

```cpp
std::uint8_t World::GetSkyLight(int x, int y, int z) const
{
    if (!IsInBounds(x, y, z))
        return 15; // Outside the world is open sky.

    const int chunkX = x / Chunk::Width;
    const int chunkY = y / Chunk::Height;
    const int chunkZ = z / Chunk::Depth;

    return m_Chunks[GetChunkIndex(chunkX, chunkY, chunkZ)].GetSkyLight(
        x - chunkX * Chunk::Width,
        y - chunkY * Chunk::Height,
        z - chunkZ * Chunk::Depth);
}

void World::SetSkyLight(int x, int y, int z, std::uint8_t level)
{
    CB_CORE_ASSERT(IsInBounds(x, y, z), "Sky light set outside the world");

    const int chunkX = x / Chunk::Width;
    const int chunkY = y / Chunk::Height;
    const int chunkZ = z / Chunk::Depth;

    m_Chunks[GetChunkIndex(chunkX, chunkY, chunkZ)].SetSkyLight(
        x - chunkX * Chunk::Width,
        y - chunkY * Chunk::Height,
        z - chunkZ * Chunk::Depth,
        level);
}
```

Match the exact assert or throw idiom `World::SetBlock` already uses.

- [ ] **Step 5: Run the build to verify all tests pass**

Run the build command.
Expected: PASS, test count **112** (108 + 4). The existing `WorldDirtyTests.cpp` cases must still pass — they exercise the renamed dirty-marking through `SetBlock`.

- [ ] **Step 6: Commit**

```bash
git add Cubit/include/Cubit/Voxel/World.h Cubit/src/Voxel/World.cpp Tests/src/WorldTests.cpp
git commit -m "Expose sky light and dirty marking on World"
```

---

## Task 6: `SkyLight::PropagateAll`

**Files:**
- Create: `Cubit/include/Cubit/Voxel/SkyLight.h`
- Create: `Cubit/src/Voxel/SkyLight.cpp`
- Create: `Tests/src/SkyLightTests.cpp`

**Interfaces:**
- Consumes: `World::GetSkyLight`/`SetSkyLight`/`IsBlockSolid`/`IsInBounds`/`GetWidth`/`GetHeight`/`GetDepth` from Task 5.
- Produces:
  - `static constexpr std::uint8_t SkyLight::Max = 15;`
  - `static void SkyLight::PropagateAll(World& world);`

- [ ] **Step 1: Write the failing test**

Create `Tests/src/SkyLightTests.cpp`:

```cpp
#include <doctest.h>

#include "Cubit/Voxel/Chunk.h"
#include "Cubit/Voxel/SkyLight.h"
#include "Cubit/Voxel/World.h"

TEST_CASE("An open column is lit from top to bottom")
{
    World world(1, 4, 1);
    SkyLight::PropagateAll(world);

    for (int y = 0; y < world.GetHeight(); ++y)
        CHECK(world.GetSkyLight(8, y, 8) == SkyLight::Max);
}

TEST_CASE("A solid block holds no light")
{
    World world(1, 4, 1);
    world.SetBlock(8, 20, 8, BlockId{1});

    SkyLight::PropagateAll(world);

    CHECK(world.GetSkyLight(8, 20, 8) == 0);
}

TEST_CASE("A roof darkens the column beneath it")
{
    // A solid ceiling spanning the whole world, so no light can creep in from
    // the sides. Everything below it must be pitch dark.
    World world(1, 4, 1);
    const int roof = 20;

    for (int z = 0; z < world.GetDepth(); ++z)
        for (int x = 0; x < world.GetWidth(); ++x)
            world.SetBlock(x, roof, z, BlockId{1});

    SkyLight::PropagateAll(world);

    CHECK(world.GetSkyLight(8, roof + 1, 8) == SkyLight::Max);
    CHECK(world.GetSkyLight(8, roof - 1, 8) == 0);
    CHECK(world.GetSkyLight(8, 0, 8) == 0);
}

TEST_CASE("Light spreads sideways under an overhang, losing one level a step")
{
    // A roof covering everything except the x == 0 column, which stays open to
    // the sky. Light falls down that column and creeps sideways underneath.
    World world(2, 4, 1);
    const int roof = 20;

    for (int z = 0; z < world.GetDepth(); ++z)
        for (int x = 1; x < world.GetWidth(); ++x)
            world.SetBlock(x, roof, z, BlockId{1});

    SkyLight::PropagateAll(world);

    const int under = roof - 1;
    REQUIRE(world.GetSkyLight(0, under, 8) == SkyLight::Max);

    CHECK(world.GetSkyLight(1, under, 8) == SkyLight::Max - 1);
    CHECK(world.GetSkyLight(2, under, 8) == SkyLight::Max - 2);
    CHECK(world.GetSkyLight(3, under, 8) == SkyLight::Max - 3);
}

TEST_CASE("Light dies out after fifteen sideways steps")
{
    World world(2, 4, 1);
    const int roof = 20;

    for (int z = 0; z < world.GetDepth(); ++z)
        for (int x = 1; x < world.GetWidth(); ++x)
            world.SetBlock(x, roof, z, BlockId{1});

    SkyLight::PropagateAll(world);

    const int under = roof - 1;
    CHECK(world.GetSkyLight(15, under, 8) == 0);
    CHECK(world.GetSkyLight(16, under, 8) == 0);
}

TEST_CASE("Light that has already dimmed does not fall for free")
{
    // Sky light falls without attenuation only at full strength. Once it has
    // spread sideways and dimmed, dropping down must cost a level like any
    // other step, or a deep cave stays almost fully lit.
    World world(2, 4, 1);
    const int roof = 20;

    for (int z = 0; z < world.GetDepth(); ++z)
        for (int x = 1; x < world.GetWidth(); ++x)
            world.SetBlock(x, roof, z, BlockId{1});

    SkyLight::PropagateAll(world);

    const int under = roof - 1;
    REQUIRE(world.GetSkyLight(3, under, 8) == SkyLight::Max - 3);

    CHECK(world.GetSkyLight(3, under - 1, 8) == SkyLight::Max - 4);
    CHECK(world.GetSkyLight(3, under - 2, 8) == SkyLight::Max - 5);
}

TEST_CASE("Propagation is idempotent")
{
    World world(2, 2, 2);

    for (int z = 0; z < world.GetDepth(); ++z)
        for (int x = 0; x < world.GetWidth(); ++x)
            world.SetBlock(x, 10, z, BlockId{1});

    SkyLight::PropagateAll(world);
    const std::uint8_t sample = world.GetSkyLight(5, 20, 5);

    SkyLight::PropagateAll(world);

    CHECK(world.GetSkyLight(5, 20, 5) == sample);
}
```

- [ ] **Step 2: Create the header `SkyLight.h`**

```cpp
#pragma once

#include "Cubit/Core.h"

#include <cstdint>

class World;

//Sky lighting for a world: light falls from the open sky, spreads through air,
//and dims with distance. Pure computation over a World's light grid — it makes
//no GL calls and knows nothing about the renderer.
class CB_API SkyLight
{
public:
    SkyLight() = delete;

    //The brightness of open sky, and the maximum any cell can hold.
    static constexpr std::uint8_t Max = 15;

    //Floods sky light through the whole world, discarding whatever was there.
    //Called once after a map loads. Marks nothing dirty: a freshly loaded world
    //already has every chunk dirty.
    static void PropagateAll(World& world);
};
```

- [ ] **Step 3: Create `SkyLight.cpp`**

```cpp
#include "cub.h"

#include "Cubit/Voxel/SkyLight.h"

#include "Cubit/Voxel/Chunk.h"
#include "Cubit/Voxel/World.h"

#include <glm/glm.hpp>
#include <deque>

namespace
{
    //The six directions light can travel. Index 5 is straight down, which is
    //the one direction that can be free.
    constexpr glm::ivec3 Directions[6] =
    {
        {  1,  0,  0 },
        { -1,  0,  0 },
        {  0,  0,  1 },
        {  0,  0, -1 },
        {  0,  1,  0 },
        {  0, -1,  0 },
    };

    constexpr int DownIndex = 5;

    //Spreads light outward from every cell already in the queue until nothing
    //can be brightened. A cell is only enqueued when its value actually rises,
    //so this terminates: each cell can rise at most Max times.
    void Flood(World& world, std::deque<glm::ivec3>& queue)
    {
        while (!queue.empty())
        {
            const glm::ivec3 cell = queue.front();
            queue.pop_front();

            const int level = world.GetSkyLight(cell.x, cell.y, cell.z);
            if (level <= 1)
                continue; // Nothing left to give a neighbour.

            for (int d = 0; d < 6; ++d)
            {
                const glm::ivec3 next = cell + Directions[d];

                if (!world.IsInBounds(next.x, next.y, next.z))
                    continue;
                if (world.IsBlockSolid(next.x, next.y, next.z))
                    continue;

                // Full-strength sky light falls without dimming, which is what
                // makes a shaft lit all the way down. Light that has already
                // spread sideways pays a level to fall, like any other step.
                const int value = (d == DownIndex && level == SkyLight::Max)
                    ? SkyLight::Max
                    : level - 1;

                if (world.GetSkyLight(next.x, next.y, next.z) >= value)
                    continue;

                world.SetSkyLight(
                    next.x, next.y, next.z, static_cast<std::uint8_t>(value));
                queue.push_back(next);
            }
        }
    }
}

void SkyLight::PropagateAll(World& world)
{
    for (int z = 0; z < world.GetDepth(); ++z)
        for (int y = 0; y < world.GetHeight(); ++y)
            for (int x = 0; x < world.GetWidth(); ++x)
                world.SetSkyLight(x, y, z, 0);

    std::deque<glm::ivec3> queue;
    const int top = world.GetHeight() - 1;

    for (int z = 0; z < world.GetDepth(); ++z)
    {
        for (int x = 0; x < world.GetWidth(); ++x)
        {
            if (world.IsBlockSolid(x, top, z))
                continue;

            world.SetSkyLight(x, top, z, Max);
            queue.push_back(glm::ivec3(x, top, z));
        }
    }

    Flood(world, queue);
}
```

- [ ] **Step 4: Regenerate projects so the new files are compiled**

Run: `/c/dev/premake/premake5 vs2026`

- [ ] **Step 5: Run the build to verify all tests pass**

Run the build command.
Expected: PASS, test count **119** (112 + 7).

- [ ] **Step 6: Commit**

```bash
git add Cubit/include/Cubit/Voxel/SkyLight.h Cubit/src/Voxel/SkyLight.cpp Tests/src/SkyLightTests.cpp Cubit.slnx Cubit/Cubit.vcxproj Tests/Tests.vcxproj
git commit -m "Flood sky light through the world"
```

---

## Task 7: `SkyLight::Repropagate` — bounded relight on edit

**Files:**
- Modify: `Cubit/include/Cubit/Voxel/SkyLight.h`
- Modify: `Cubit/src/Voxel/SkyLight.cpp`
- Modify: `Tests/src/SkyLightTests.cpp` (append)

**Interfaces:**
- Consumes: `SkyLight::Max`, the internal `Flood` from Task 6; `World::MarkChunkDirtyAt` from Task 5.
- Produces: `static void SkyLight::Repropagate(World& world, int x, int y, int z);`

**Why a bounded box is correct:** light loses one level per horizontal step from a maximum of 15, so any cell more than 15 blocks away horizontally from the edit can receive at most `15 - 16 < 0` from it — the edit cannot be what limits that cell, whether it opened a path or blocked one. Vertically there is no such bound, because full-strength sky light falls for free, so the box spans the world's full height.

- [ ] **Step 1: Write the failing test**

Append to `Tests/src/SkyLightTests.cpp`:

```cpp
#include <vector>

namespace
{
    //Every light value in the world, for comparing two propagations.
    std::vector<std::uint8_t> LightSnapshot(const World& world)
    {
        std::vector<std::uint8_t> values;
        values.reserve(
            static_cast<std::size_t>(world.GetWidth()) *
            world.GetHeight() * world.GetDepth());

        for (int z = 0; z < world.GetDepth(); ++z)
            for (int y = 0; y < world.GetHeight(); ++y)
                for (int x = 0; x < world.GetWidth(); ++x)
                    values.push_back(world.GetSkyLight(x, y, z));

        return values;
    }

    //A world with a roof over everything but one open shaft, so there is both
    //bright and dark space for an edit to disturb.
    World BuildRoofedWorld()
    {
        World world(3, 4, 3);
        const int roof = 20;

        for (int z = 0; z < world.GetDepth(); ++z)
            for (int x = 0; x < world.GetWidth(); ++x)
                if (x != 24 || z != 24)
                    world.SetBlock(x, roof, z, BlockId{1});

        return world;
    }
}

TEST_CASE("Breaking a roof block lets light into the space below")
{
    World world = BuildRoofedWorld();
    SkyLight::PropagateAll(world);

    const int roof = 20;
    REQUIRE(world.GetSkyLight(5, roof - 1, 5) == 0);

    world.SetBlock(5, roof, 5, BlockId{0});
    SkyLight::Repropagate(world, 5, roof, 5);

    CHECK(world.GetSkyLight(5, roof - 1, 5) == SkyLight::Max);
    CHECK(world.GetSkyLight(5, 0, 5) == SkyLight::Max);
}

TEST_CASE("Sealing a shaft takes the light back out of it")
{
    World world = BuildRoofedWorld();
    SkyLight::PropagateAll(world);

    const int roof = 20;
    REQUIRE(world.GetSkyLight(24, roof - 1, 24) == SkyLight::Max);

    world.SetBlock(24, roof, 24, BlockId{1});
    SkyLight::Repropagate(world, 24, roof, 24);

    CHECK(world.GetSkyLight(24, roof - 1, 24) == 0);
    CHECK(world.GetSkyLight(24, 0, 24) == 0);
}

TEST_CASE("Bounded repropagation matches a full propagation")
{
    // The test that proves the box is big enough. Whatever the edit, relighting
    // only the box must leave the world in exactly the state a from-scratch
    // flood would have produced.
    World world = BuildRoofedWorld();
    SkyLight::PropagateAll(world);

    const int roof = 20;
    glm::ivec3 edit{ 0, 0, 0 };
    BlockId block{ 0 };

    SUBCASE("opening a hole in the roof")
    {
        edit = glm::ivec3(5, roof, 5);
        block = BlockId{0};
    }

    SUBCASE("sealing the open shaft")
    {
        edit = glm::ivec3(24, roof, 24);
        block = BlockId{1};
    }

    SUBCASE("opening a hole beside the shaft")
    {
        edit = glm::ivec3(23, roof, 24);
        block = BlockId{0};
    }

    SUBCASE("placing a block in open air under the shaft")
    {
        edit = glm::ivec3(24, roof - 5, 24);
        block = BlockId{1};
    }

    SUBCASE("opening a hole at the world edge")
    {
        edit = glm::ivec3(0, roof, 0);
        block = BlockId{0};
    }

    world.SetBlock(edit.x, edit.y, edit.z, block);
    SkyLight::Repropagate(world, edit.x, edit.y, edit.z);
    const std::vector<std::uint8_t> bounded = LightSnapshot(world);

    SkyLight::PropagateAll(world);
    const std::vector<std::uint8_t> full = LightSnapshot(world);

    CHECK(bounded == full);
}

TEST_CASE("Repropagation marks the chunks whose light changed")
{
    World world = BuildRoofedWorld();
    SkyLight::PropagateAll(world);
    world.ClearDirty();

    const int roof = 20;
    world.SetBlock(5, roof, 5, BlockId{0});
    world.ClearDirty(); // Ignore the block edit's own marking.

    SkyLight::Repropagate(world, 5, roof, 5);

    // The column below the new hole relit, so its chunks must be dirty.
    CHECK(world.DirtyChunks().count(glm::ivec3(0, 0, 0)) == 1);
    CHECK(!world.DirtyChunks().empty());
}

TEST_CASE("Repropagation after a no-op edit marks nothing dirty")
{
    World world = BuildRoofedWorld();
    SkyLight::PropagateAll(world);
    world.ClearDirty();

    // Placing a block where one already stands changes no light at all.
    const int roof = 20;
    SkyLight::Repropagate(world, 5, roof, 5);

    CHECK(world.DirtyChunks().empty());
}
```

- [ ] **Step 2: Run the build to verify the tests fail**

Run the build command.
Expected: FAIL to compile — `Repropagate` is not a member of `SkyLight`.

- [ ] **Step 3: Declare `Repropagate` in `SkyLight.h`**

Add after `PropagateAll`:

```cpp
    //Re-floods the region an edit at this position can affect, and marks every
    //chunk whose light actually changed dirty. Light cannot travel more than
    //Max blocks horizontally, so a box of that radius is provably enough; it
    //spans the world's full height because full-strength light falls for free.
    static void Repropagate(World& world, int x, int y, int z);
```

- [ ] **Step 4: Implement `Repropagate` in `SkyLight.cpp`**

Add after `PropagateAll`:

```cpp
void SkyLight::Repropagate(World& world, int x, int y, int z)
{
    (void)y; // The box always spans the full height, so the edit's y is unused.

    const int radius = Max;
    const int minX = std::max(0, x - radius);
    const int maxX = std::min(world.GetWidth() - 1, x + radius);
    const int minZ = std::max(0, z - radius);
    const int maxZ = std::min(world.GetDepth() - 1, z + radius);
    const int top = world.GetHeight() - 1;

    // Remember what the box held, so only genuinely changed chunks are marked.
    std::vector<std::uint8_t> before;
    before.reserve(
        static_cast<std::size_t>(maxX - minX + 1) *
        (maxZ - minZ + 1) * (top + 1));

    for (int cz = minZ; cz <= maxZ; ++cz)
        for (int cy = 0; cy <= top; ++cy)
            for (int cx = minX; cx <= maxX; ++cx)
                before.push_back(world.GetSkyLight(cx, cy, cz));

    for (int cz = minZ; cz <= maxZ; ++cz)
        for (int cy = 0; cy <= top; ++cy)
            for (int cx = minX; cx <= maxX; ++cx)
                world.SetSkyLight(cx, cy, cz, 0);

    std::deque<glm::ivec3> queue;

    // Seed one: the open sky above the box.
    for (int cz = minZ; cz <= maxZ; ++cz)
    {
        for (int cx = minX; cx <= maxX; ++cx)
        {
            if (world.IsBlockSolid(cx, top, cz))
                continue;

            world.SetSkyLight(cx, top, cz, Max);
            queue.push_back(glm::ivec3(cx, top, cz));
        }
    }

    // Seed two: the ring of cells just outside the box. They kept their values
    // through the clear, and light flows from them back in. Enqueuing them is
    // safe because Flood only ever raises a cell, and nothing outside the box
    // can be too dark.
    for (int cy = 0; cy <= top; ++cy)
    {
        for (int cz = minZ - 1; cz <= maxZ + 1; ++cz)
        {
            for (int cx = minX - 1; cx <= maxX + 1; ++cx)
            {
                const bool insideBox =
                    cx >= minX && cx <= maxX && cz >= minZ && cz <= maxZ;

                if (insideBox)
                    continue;
                if (!world.IsInBounds(cx, cy, cz))
                    continue;
                if (world.IsBlockSolid(cx, cy, cz))
                    continue;
                if (world.GetSkyLight(cx, cy, cz) == 0)
                    continue;

                queue.push_back(glm::ivec3(cx, cy, cz));
            }
        }
    }

    Flood(world, queue);

    std::size_t index = 0;
    for (int cz = minZ; cz <= maxZ; ++cz)
    {
        for (int cy = 0; cy <= top; ++cy)
        {
            for (int cx = minX; cx <= maxX; ++cx)
            {
                if (world.GetSkyLight(cx, cy, cz) != before[index])
                    world.MarkChunkDirtyAt(cx, cy, cz);

                ++index;
            }
        }
    }
}
```

Add `#include <algorithm>` and `#include <vector>` to the include block at the top of `SkyLight.cpp`.

- [ ] **Step 5: Run the build to verify all tests pass**

Run the build command.
Expected: PASS, test count **128** (119 + 9 — the equivalence case counts once per subcase).

If "Bounded repropagation matches a full propagation" fails, the bug is in the seeding, not the radius. Check that the outer ring is enqueued at *every* height, and that the sky seed covers the whole box footprint.

- [ ] **Step 6: Commit**

```bash
git add Cubit/include/Cubit/Voxel/SkyLight.h Cubit/src/Voxel/SkyLight.cpp Tests/src/SkyLightTests.cpp
git commit -m "Relight a bounded box around each block edit"
```

---

## Task 8: Sample sky light into vertex colours

**Files:**
- Modify: `Cubit/include/Cubit/Voxel/ChunkMesher.h`
- Modify: `Cubit/src/Voxel/ChunkMesher.cpp`
- Modify: `Tests/src/ChunkAoTests.cpp` (append)

**Interfaces:**
- Consumes: `World::GetSkyLight` from Task 5; `SkyLight::Max` from Task 6; the `FaceGeometry` table and `AddFace` from Task 2.
- Produces: `static float ChunkMesher::CornerLightShade(const World& world, const glm::ivec3& airCell, const glm::ivec3& sideA, const glm::ivec3& sideB);`

- [ ] **Step 1: Write the failing test**

Append to `Tests/src/ChunkAoTests.cpp`:

```cpp
#include "Cubit/Voxel/SkyLight.h"

TEST_CASE("Unlit faces are darker than lit ones but never black")
{
    World world(1, 4, 1);

    // A slab with a roofed pocket cut under it: the pocket's floor is lit only
    // by whatever creeps in, the open slab top is under full sky.
    const int floor = 4;
    for (int z = 0; z < Chunk::Depth; ++z)
        for (int x = 0; x < Chunk::Width; ++x)
            world.SetBlock(x, floor, z, BlockId{1});

    SkyLight::PropagateAll(world);

    const ChunkMeshData lit = ChunkMesher::Build(world, 0, 0, 0);
    const float openTop = DarkestOnPlane(lit, static_cast<float>(floor + 1));

    // Now roof the whole world well above the slab, cutting the sky off.
    for (int z = 0; z < world.GetDepth(); ++z)
        for (int x = 0; x < world.GetWidth(); ++x)
            world.SetBlock(x, 40, z, BlockId{1});

    SkyLight::PropagateAll(world);

    const ChunkMeshData dark = ChunkMesher::Build(world, 0, 0, 0);
    const float roofedTop = DarkestOnPlane(dark, static_cast<float>(floor + 1));

    CHECK(roofedTop < openTop);
    CHECK(roofedTop > 0.0f);
}

TEST_CASE("The light shade curve spans the intended range")
{
    World world(1, 1, 1);

    // A cell under full sky shades at 1.0; a fully dark one at the floor.
    world.SetSkyLight(8, 9, 8, SkyLight::Max);
    const glm::ivec3 air{ 8, 9, 8 };
    const glm::ivec3 sideX{ 1, 0, 0 };
    const glm::ivec3 sideZ{ 0, 0, 1 };

    // Every sampled cell is air at Max, so the average is Max.
    world.SetSkyLight(9, 9, 8, SkyLight::Max);
    world.SetSkyLight(8, 9, 9, SkyLight::Max);
    world.SetSkyLight(9, 9, 9, SkyLight::Max);

    CHECK(ChunkMesher::CornerLightShade(world, air, sideX, sideZ)
        == doctest::Approx(1.0f));

    world.SetSkyLight(8, 9, 8, 0);
    world.SetSkyLight(9, 9, 8, 0);
    world.SetSkyLight(8, 9, 9, 0);
    world.SetSkyLight(9, 9, 9, 0);

    CHECK(ChunkMesher::CornerLightShade(world, air, sideX, sideZ)
        == doctest::Approx(0.15f));
}

TEST_CASE("Corner light ignores solid cells when averaging")
{
    // A solid neighbour holds light 0, but it is not a place light could be —
    // averaging it in would darken the surface for no reason.
    World world(1, 1, 1);
    const glm::ivec3 air{ 8, 9, 8 };
    const glm::ivec3 sideX{ 1, 0, 0 };
    const glm::ivec3 sideZ{ 0, 0, 1 };

    world.SetSkyLight(8, 9, 8, SkyLight::Max);
    world.SetSkyLight(9, 9, 8, SkyLight::Max);
    world.SetSkyLight(8, 9, 9, SkyLight::Max);

    world.SetBlock(9, 9, 9, BlockId{1});
    world.SetSkyLight(9, 9, 9, 0);

    CHECK(ChunkMesher::CornerLightShade(world, air, sideX, sideZ)
        == doctest::Approx(1.0f));
}
```

- [ ] **Step 2: Run the build to verify the tests fail**

Run the build command.
Expected: FAIL to compile — `CornerLightShade` is not a member of `ChunkMesher`.

- [ ] **Step 3: Declare `CornerLightShade` in `ChunkMesher.h`**

Add after the `CornerAoLevel` declaration:

```cpp
    //Brightness a face corner gets from sky light, between LightFloor and 1.
    //Averages the light of the air cells touching the corner, which is what
    //makes lighting graduate smoothly across a surface instead of stepping from
    //block to block. Solid cells hold no light and are left out of the average.
    static float CornerLightShade(
        const World& world,
        const glm::ivec3& airCell,
        const glm::ivec3& sideA,
        const glm::ivec3& sideB);

    //How dark a fully unlit surface goes. Not zero: an unlit bunker should read
    //as dark but still be navigable, rather than being a black void.
    static constexpr float LightFloor = 0.15f;
```

- [ ] **Step 4: Implement `CornerLightShade` in `ChunkMesher.cpp`**

Add `#include "Cubit/Voxel/SkyLight.h"` to the include block, then add after `ChunkMesher::CornerAoLevel`:

```cpp
float ChunkMesher::CornerLightShade(
    const World& world,
    const glm::ivec3& airCell,
    const glm::ivec3& sideA,
    const glm::ivec3& sideB)
{
    const glm::ivec3 cells[4] =
    {
        airCell,
        airCell + sideA,
        airCell + sideB,
        airCell + sideA + sideB,
    };

    int total = 0;
    int counted = 0;

    for (const glm::ivec3& cell : cells)
    {
        if (world.IsBlockSolid(cell.x, cell.y, cell.z))
            continue;

        total += world.GetSkyLight(cell.x, cell.y, cell.z);
        ++counted;
    }

    // A corner boxed in on every side has nowhere for light to sit; it is not
    // sampled by any visible face, but guard the division anyway.
    if (counted == 0)
        return LightFloor;

    const float average =
        static_cast<float>(total) / (static_cast<float>(counted) * SkyLight::Max);

    return LightFloor + (1.0f - LightFloor) * average;
}
```

- [ ] **Step 5: Fold the light into `AddFace`**

In `AddFace`, replace the corner loop and the vertex loop with:

```cpp
        int ao[4];
        float light[4];
        for (int i = 0; i < 4; ++i)
        {
            const glm::ivec3 sideA = face.U * face.CornerU[i];
            const glm::ivec3 sideB = face.V * face.CornerV[i];

            ao[i] = ChunkMesher::CornerAoLevel(world, airCell, sideA, sideB);
            light[i] =
                ChunkMesher::CornerLightShade(world, airCell, sideA, sideB);
        }

        for (int i = 0; i < 4; ++i)
        {
            const glm::vec3 color = blockColor
                * face.Shade
                * ChunkMesher::AoShade[ao[i]]
                * light[i];

            mesh.Vertices.push_back({ blockOrigin + face.Corner[i], color });
        }
```

Leave the `AddFaceIndices(mesh, ao[0] + ao[2] > ao[1] + ao[3]);` line as it is — the flip is chosen on occlusion alone, so a lighting gradient does not thrash the triangulation.

- [ ] **Step 6: Fix the Phase 1 tests that assumed no lighting**

The Task 2 tests "A lone block's top face is unoccluded on every corner" and "An inside corner darkens the floor beside it" build worlds whose light was never propagated, so every cell reads 0 and every face now comes out at `LightFloor`.

Add a `SkyLight::PropagateAll(world);` call in each, immediately before every `ChunkMesher::Build` call. In "An inside corner darkens the floor beside it" that means propagating **twice** — once before the open-floor build and again after the wall is added, since the wall changes the light.

`OpenTopColor` itself needs no change: a corner under full sky shades at exactly 1.0, so `blockColor * AoShade[3] * 1.0` is the same value it was in Phase 1. Update only its comment:

```cpp
    //The colour a fully open, fully lit top face has: the palette colour at full
    //top shade, no occlusion, and full sky light.
```

- [ ] **Step 7: Run the build to verify all tests pass**

Run the build command.
Expected: PASS, test count **131** (128 + 3).

- [ ] **Step 8: Commit**

```bash
git add Cubit/include/Cubit/Voxel/ChunkMesher.h Cubit/src/Voxel/ChunkMesher.cpp Tests/src/ChunkAoTests.cpp
git commit -m "Shade block faces with smooth sky light"
```

---

## Task 9: Wire lighting into the sandbox and verify

**Files:**
- Modify: `Sandbox/src/Sandbox.cpp`
- Modify: `docs/engine-roadmap.md`
- Modify: `docs/performance.md`

**Interfaces:**
- Consumes: `SkyLight::PropagateAll`, `SkyLight::Repropagate` from Tasks 6–7.
- Produces: nothing new — this is integration.

- [ ] **Step 1: Propagate light when the map loads**

In `Sandbox.cpp`, add `#include "Cubit/Voxel/SkyLight.h"` to the include block. Then in `OnAttach`, immediately after the `m_World = BuildWorld(...)` line at line 67:

```cpp
        m_World = BuildWorld(VoxLoader::LoadFile("assets/maps/battlefield.vox"));
        //The world starts with every chunk dirty, so the first render meshes it.

        // Light has to exist before anything meshes, or the first frames bake
        // a fully dark world into their vertex colours.
        SkyLight::PropagateAll(m_World);
```

- [ ] **Step 2: Relight after a block edit**

In `OnMouseButtonPressed`, immediately after the `m_World.SetBlock(...)` call at line 245:

```cpp
        m_World.SetBlock(
            target.x,
            target.y,
            target.z,
            button == MouseCode::Left ? BlockId{0} : m_PlaceBlock);

        SkyLight::Repropagate(m_World, target.x, target.y, target.z);
```

- [ ] **Step 3: Build**

Run the build command.
Expected: PASS, test count **131**.

- [ ] **Step 4: Warn the user, then run the sandbox**

The sandbox captures the mouse cursor. Warn the user first.

Launch `bin\Debug-windows-x86_64\Sandbox\Sandbox.exe` detached with stdout redirected, working directory `Sandbox/`. Wait ~7 seconds for Discord's overlay to clear the HUD, screenshot the window, then close with `PostMessage(hwnd, 0x0010, 0, 0)`. **Do not `Stop-Process`** — the log is fully buffered and a kill discards it.

- [ ] **Step 5: Read the frame and the log**

Confirm:
- Open terrain reads at full brightness; the undersides of overhangs and the insides of any enclosed structure are visibly dark.
- The transition from lit to dark is a smooth gradient across surfaces, not a hard per-block step. Hard steps mean `CornerLightShade` is being sampled once per face instead of once per vertex.
- Nothing is pure black.
- `PENDING` drains to 0 and FPS is in the same range as before the change. If load time regressed noticeably, note the propagation cost — `PropagateAll` walks ~4.19M cells.

- [ ] **Step 6: Retune `LightFloor` if needed**

`0.15f` is a starting guess, in `ChunkMesher.h`. Too low and interiors are unreadable; too high and nothing feels dark. Rebuild and re-screenshot after any change, and show the user the frame before settling.

- [ ] **Step 7: Update the docs**

In `docs/engine-roadmap.md`, mark item 2 of the "finish the engine" arc done, matching how item 1 is already struck through, and mark "Lighting / ambient occlusion" done in the Visual quality list. Move the `←  next` marker to item 3.

In `docs/performance.md`, no status changes — but add a line to the P3 entry noting that greedy merging must now compare AO *and* light values, since coplanar faces can only merge when both match.

- [ ] **Step 8: Commit and push**

```bash
git add Sandbox/src/Sandbox.cpp Cubit/include/Cubit/Voxel/ChunkMesher.h docs/engine-roadmap.md docs/performance.md
git commit -m "Light the sandbox world and relight on block edits"
git push origin master
```

---

## Self-review notes

Checked against the spec:

- **Spec §1 (Chunk storage)** → Task 4. **§2 (World access)** → Task 5. **§3 (SkyLight)** → Tasks 6–7. **§4 (per-vertex sampling)** → Tasks 1, 2, 8. **§5 (triangulation flip)** → Task 2. **Data flow** → Task 9. **Testing** → every task's test step, with the equivalence test in Task 7.
- **One spec correction, applied here:** the spec says "a step downward into air preserves the value". That is only true at *full* strength. If light that had already dimmed also fell for free, a cave 30 blocks in would sit at level 14 — nearly full brightness. The plan implements the correct rule (free fall only at `Max`) and Task 6 pins it with "Light that has already dimmed does not fall for free". The spec line should be corrected to match.
- `World::MarkChunkDirtyForEdit` is renamed to `MarkChunkDirtyAt` and made public in Task 5; every later reference uses the new name.
- `Chunk::GetSkyLight` returns 15 out of bounds while a fresh chunk is all zeros. That is deliberate and not a contradiction: an unpropagated *in-bounds* cell is dark, while out of bounds means open sky.
