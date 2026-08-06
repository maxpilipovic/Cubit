# Greedy Meshing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Merge coplanar, identically shaded faces into larger quads so flat terrain costs a handful of quads instead of thousands, with the rendered image unchanged.

**Architecture:** `ChunkMesher::Build` stops walking blocks and starts walking face planes: for each of the 6 face directions, for each slice along that normal, a 16×16 mask is filled with the faces in that plane and then consumed into maximal rectangles. A face joins the mask only when its four corners share one shade; faces with a shading gradient are emitted 1×1 exactly as today.

**Tech Stack:** C++20, MSVC, OpenGL 3.3, GLM, doctest, premake5.

Full design: [`docs/superpowers/specs/2026-08-06-greedy-meshing-design.md`](../specs/2026-08-06-greedy-meshing-design.md).

## Global Constraints

- **Build + test command** (builds `Cubit` then `Tests`, and runs the suite as a postbuild step). Use FORWARD slashes in the project path — backslashes fail in Git Bash with MSB1009:
  ```bash
  "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64
  ```
- **Build the Sandbox:** same command with `Sandbox/Sandbox.vcxproj`.
- **Run one test case:** `./bin/Debug-windows-x86_64/Tests/Tests.exe -tc="<name>"`
- The suite passes **165 cases** at the start of this plan. A failing test breaks the build by design.
- **Do not add a new test *file*.** That needs premake re-run. Append to the existing files named in each task.
- **Comment style:** `//` with no space above a declaration; `// ` with a space inside a function body. Comments explain *why*.
- **Face existence and shading rules are not touched by this plan.** `IsOpaque(neighbour) || Block(neighbour) == self` still decides whether a face exists, and `CornerAo`/`CornerLight` still produce shading. This plan only regroups the resulting faces into quads. If a task tempts you to change what a face looks like, you have misread it.
- **Never** add Claude co-author trailers or attribution to commits.

---

### Task 1: Move the neighbourhood cache to its own header

Pure refactor. No behaviour change, no test change.

**Files:**
- Create: `Cubit/src/Voxel/Neighbourhood.h`
- Modify: `Cubit/src/Voxel/ChunkMesher.cpp`

**Interfaces:**
- Produces: class `Neighbourhood` at global scope in a new engine-only header. Tasks 3 and 4 include it. Its public members are unchanged: `Neighbourhood(const World&, const glm::ivec3&)`, `static int At(int,int,int)`, `static int Step(const glm::ivec3&)`, `BlockId Block(int)`, `bool IsSolid(int)`, `bool IsOpaque(int)`, `int Light(int)`, and the constants `Shell`, `SpanX`, `SpanY`, `SpanZ`, `Count`.

- [ ] **Step 1: Create the header**

`Cubit/src/Voxel/Neighbourhood.h` is engine-only, so it lives under `src/` and uses `#pragma once` with no namespace, matching `Cubit/src/Core/CoreLogger.h`.

Create the file with this content, then **cut** the `Neighbourhood` class body verbatim out of `ChunkMesher.cpp` (currently lines 24–106, from `class Neighbourhood` through its closing `};`) and paste it where marked. Do not retype it — cut and paste, so the comments and the `m_Opaque` fill added by the transparency work come across intact.

```cpp
#pragma once

#include "Cubit/Voxel/Block.h"
#include "Cubit/Voxel/Chunk.h"
#include "Cubit/Voxel/World.h"

#include <glm/glm.hpp>
#include <cstdint>

<<< paste the Neighbourhood class here, unindented by 4 spaces >>>
```

The class sits inside an anonymous namespace in `ChunkMesher.cpp` today, so it is indented 4 spaces. Remove that indent when pasting; it is at file scope in the header.

- [ ] **Step 2: Include it from the mesher**

In `Cubit/src/Voxel/ChunkMesher.cpp`, add to the include block, after the existing `Cubit/Voxel/...` includes:

```cpp
#include "Voxel/Neighbourhood.h"
```

`Cubit/src` is already on the include path — `ChunkMesher.cpp` includes `"Core/CoreLogger.h"` the same way.

The anonymous namespace in `ChunkMesher.cpp` now opens directly onto the `WorldCells` struct that followed `Neighbourhood`.

- [ ] **Step 3: Run the build to verify nothing changed**

Run the build+test command. Expected: `Status: SUCCESS!`, **165 cases**, 0 failed. Any failure here is a paste error, not a design problem.

- [ ] **Step 4: Commit**

```bash
git add Cubit
git commit -m "Move the neighbourhood cache into its own header"
```

---

### Task 2: Make the test oracle count area rather than quads

Pure test refactor. Quad count still equals covered area at this point, so every assertion holds both before and after. This is the harness that makes Task 4 verifiable.

**Files:**
- Modify: `Tests/src/ChunkMesherTests.cpp`

**Interfaces:**
- Produces: `std::size_t CoveredArea(const ChunkMeshData&)` and `std::size_t QuadCount(const ChunkMeshData&)` in the anonymous namespace at the top of `ChunkMesherTests.cpp`. Task 4 asserts with both.

- [ ] **Step 1: Add the area helper**

In `Tests/src/ChunkMesherTests.cpp`, replace the `MeshedFaceCount` helper (currently just after `CountExposedFaces`) with these three. Keep `CountExposedFaces` exactly as it is — it is the independent oracle and must not learn about merging:

```cpp
    //Quads actually emitted. Changes with the merge rule, so it is only asserted
    //where the expected value is hand-checkable.
    std::size_t QuadCount(const ChunkMeshData& mesh)
    {
        return (mesh.Opaque.Vertices.size() + mesh.Transparent.Vertices.size()) / 4;
    }

    //The number of block faces a mesh covers: per quad, the product of its two
    //non-zero extents. Merging changes how faces are grouped into quads but not
    //how much surface they cover, so this holds under any merge rule — which is
    //what makes it the oracle CountExposedFaces can be compared against.
    std::size_t CoveredArea(const MeshGeometry& geometry)
    {
        std::size_t area = 0;

        for (std::size_t quad = 0; quad * 4 < geometry.Vertices.size(); ++quad)
        {
            const VoxelVertex* corners = &geometry.Vertices[quad * 4];

            glm::vec3 low = corners[0].Position;
            glm::vec3 high = corners[0].Position;
            for (int i = 1; i < 4; ++i)
            {
                low = glm::min(low, corners[i].Position);
                high = glm::max(high, corners[i].Position);
            }

            const glm::vec3 extent = high - low;

            // A quad is flat along its normal, so that axis contributes nothing
            // and the other two multiply out to the face count it covers.
            std::size_t cells = 1;
            for (int axis = 0; axis < 3; ++axis)
                if (extent[axis] > 0.0f)
                    cells *= static_cast<std::size_t>(std::lround(extent[axis]));

            area += cells;
        }

        return area;
    }

    std::size_t CoveredArea(const ChunkMeshData& mesh)
    {
        return CoveredArea(mesh.Opaque) + CoveredArea(mesh.Transparent);
    }
```

- [ ] **Step 2: Point `RequireWellFormed` at the structural invariants only**

`RequireWellFormed` currently uses `MeshedFaceCount`, which no longer exists. Replace the whole helper with:

```cpp
    //Fails when a mesh is not made of well-formed indexed quads. Says nothing
    //about how many quads there should be — that is the merge rule's business.
    void RequireWellFormed(const ChunkMeshData& mesh)
    {
        REQUIRE(mesh.Opaque.Vertices.size() % 4 == 0);
        REQUIRE(mesh.Opaque.Indices.size() % 6 == 0);
        REQUIRE(mesh.Opaque.Indices.size() == (mesh.Opaque.Vertices.size() / 4) * 6);

        for (const std::uint32_t index : mesh.Opaque.Indices)
            REQUIRE(index < mesh.Opaque.Vertices.size());

        REQUIRE(mesh.Transparent.Vertices.size() % 4 == 0);
        REQUIRE(mesh.Transparent.Indices.size() % 6 == 0);
        REQUIRE(mesh.Transparent.Indices.size() ==
            (mesh.Transparent.Vertices.size() / 4) * 6);

        for (const std::uint32_t index : mesh.Transparent.Indices)
            REQUIRE(index < mesh.Transparent.Vertices.size());
    }
```

- [ ] **Step 3: Convert the oracle comparisons**

Every remaining `MeshedFaceCount` call becomes either `CoveredArea` or `QuadCount`. Change exactly these, locating by content:

Comparisons against the independent oracle become `CoveredArea`:

- `"A solid chunk at the world edge meshes its whole shell"` — both lines:
  ```cpp
    CHECK(CoveredArea(mesh) == 6 * Chunk::Width * Chunk::Height);
    CHECK(CoveredArea(mesh) == 1536);
  ```
- `"Chunks do not mesh the faces they share with a solid neighbour"`:
  ```cpp
    CHECK(CoveredArea(left) == 5 * faceOfAChunk);
    CHECK(CoveredArea(right) == 5 * faceOfAChunk);
  ```
- `"Meshed faces match an independent count for varied terrain"`:
  ```cpp
    CHECK(CoveredArea(mesh) == CountExposedFaces(world, 0, 0, 0));
  ```
- `"Every chunk of a multi-chunk world matches the independent count"`:
  ```cpp
                REQUIRE(CoveredArea(mesh) ==
                    CountExposedFaces(world, chunkX, chunkY, chunkZ));
  ```
- `"A buried block contributes no geometry"` — the `before` line and both checks:
  ```cpp
    const std::size_t before = CoveredArea(ChunkMesher::Build(world, 0, 0, 0));
  ```
  ```cpp
    CHECK(CoveredArea(mesh) == CountExposedFaces(world, 0, 0, 0));
    CHECK(CoveredArea(mesh) == 30);
  ```
- `"Meshing a chunk costs little more than reading its blocks"` — the accumulator:
  ```cpp
            faces += CoveredArea(ChunkMesher::Build(world, chunkX, 0, chunkZ));
  ```

Counts that are hand-checkable and stay quad counts:

- `"A lone block is meshed as six quads"`:
  ```cpp
    CHECK(QuadCount(mesh) == 6);
  ```
  Leave its `mesh.Opaque.Vertices.size() == 24` and `Indices.size() == 36` alone — a lone block has no neighbour to merge with, so those hold after Task 4 too.
- `"Touching blocks do not mesh the faces between them"`:
  ```cpp
    CHECK(CoveredArea(mesh) == 10);
  ```
- `"A block is hidden by a solid block in the next chunk"` — all three:
  ```cpp
    CHECK(CoveredArea(alone) == 6);
  ```
  ```cpp
    CHECK(CoveredArea(ChunkMesher::Build(world, 0, 0, 0)) == 5);
  ```
  ```cpp
    CHECK(CoveredArea(ChunkMesher::Build(world, 1, 0, 0)) == 5);
  ```

- [ ] **Step 4: Relax the known-size test**

`"The sandbox test terrain meshes to its known size"` hardcodes vertex and index totals that merging will change, and its correct new value cannot be worked out by hand. Replace its three assertions with the invariant alone:

```cpp
TEST_CASE("The sandbox test terrain meshes to its known size")
{
    World world(1, 1, 1);
    BuildTestTerrain(world);

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);

    // The surface this terrain exposes is fixed. How many quads cover it is the
    // merge rule's business, so it is not asserted here.
    CHECK(CoveredArea(mesh) == 1122);
}
```

Task 4 adds the bound proving merging actually happened. It belongs with the merge work, not here, where it could only be written as a line that cannot yet pass.

- [ ] **Step 5: Fix the remaining transparency-test helper**

`FaceCount` (in the second anonymous namespace, near `TransparentPaletteWorld`) counts opaque quads only and is used by one test. Delete it and change its single caller, `"Two touching opaque blocks of different ids share no face"`:

```cpp
    CHECK(CoveredArea(mesh) == 10);
```

- [ ] **Step 6: Run the build to verify everything still passes**

Run the build+test command. Expected: `Status: SUCCESS!`, **165 cases**, 0 failed. Nothing about the mesher has changed yet, so any failure is a bad conversion above.

- [ ] **Step 7: Commit**

```bash
git add Tests
git commit -m "Measure meshes by covered area rather than quad count"
```

---

### Task 3: Walk face planes instead of blocks

Restructures `Build` to iterate direction → slice → plane, still emitting one quad per face. Output is identical in content, different only in order, so all 165 tests keep passing. Doing this on its own means the axis mapping — the fiddliest part — is verified before any merging is layered on.

**Files:**
- Modify: `Cubit/src/Voxel/ChunkMesher.cpp`

**Interfaces:**
- Consumes: `Neighbourhood` from Task 1.
- Produces: file-local `AxisIndex(const glm::ivec3&)` and `AxisExtent(int)` helpers, and `void MeshFacePlanes(ChunkMeshData&, const Neighbourhood&, const Palette&, const FaceGeometry&, const FaceSteps&)`. Task 4 rewrites the body of `MeshFacePlanes` and keeps its signature.

- [ ] **Step 1: Add the axis helpers**

In `Cubit/src/Voxel/ChunkMesher.cpp`, inside the anonymous namespace, immediately after the `FaceSteps` struct:

```cpp
    //The index of the one non-zero component of a unit axis vector: 0 is x,
    //1 is y, 2 is z. Every Normal, U and V in the Faces table is a unit axis.
    constexpr int AxisIndex(const glm::ivec3& axis)
    {
        return axis.x != 0 ? 0 : (axis.y != 0 ? 1 : 2);
    }

    //How many blocks a chunk spans along one axis.
    constexpr int AxisExtent(int axis)
    {
        return axis == 0 ? Chunk::Width : (axis == 1 ? Chunk::Height : Chunk::Depth);
    }
```

- [ ] **Step 2: Add the plane walker**

In the same anonymous namespace, immediately after `AddExposedFaces`:

```cpp
    //Emits every face pointing one direction, plane by plane. Walking planes
    //rather than blocks is what lets coplanar faces meet each other; a
    //block-major walk never has two faces of the same plane in hand at once.
    void MeshFacePlanes(
        ChunkMeshData& mesh,
        const Neighbourhood& cells,
        const Palette& palette,
        const FaceGeometry& face,
        const FaceSteps& steps)
    {
        const int normalAxis = AxisIndex(face.Normal);
        const int uAxis = AxisIndex(face.U);
        const int vAxis = AxisIndex(face.V);

        const int sliceCount = AxisExtent(normalAxis);
        const int uCount = AxisExtent(uAxis);
        const int vCount = AxisExtent(vAxis);

        for (int slice = 0; slice < sliceCount; ++slice)
        {
            for (int v = 0; v < vCount; ++v)
            {
                for (int u = 0; u < uCount; ++u)
                {
                    glm::ivec3 local(0);
                    local[normalAxis] = slice;
                    local[uAxis] = u;
                    local[vAxis] = v;

                    const int cell = Neighbourhood::At(local.x, local.y, local.z);
                    if (!cells.IsSolid(cell))
                        continue;

                    const BlockId self = cells.Block(cell);
                    const int neighbourCell = cell + steps.Normal;

                    // A face is worth drawing when what is beyond it does not
                    // hide it, and is not more of the same block: two water
                    // cells meet at a face that would only blend against itself.
                    if (cells.IsOpaque(neighbourCell) ||
                        cells.Block(neighbourCell) == self)
                        continue;

                    // A block's own opacity decides which pass draws it; the
                    // faces of one block never span both.
                    MeshGeometry& target = cells.IsOpaque(cell)
                        ? mesh.Opaque
                        : mesh.Transparent;

                    AddFace(target, cells, cell, glm::vec3(local),
                        face, steps, palette[self]);
                }
            }
        }
    }
```

- [ ] **Step 3: Rewrite `Build` to drive it**

Replace the body of `ChunkMesher::Build` — from `FaceSteps steps[6];` down to but not including `return mesh;` — with:

```cpp
    for (int f = 0; f < 6; ++f)
    {
        const FaceGeometry& face = Faces[f];
        const FaceSteps steps = {
            Neighbourhood::Step(face.Normal),
            Neighbourhood::Step(face.U),
            Neighbourhood::Step(face.V) };

        MeshFacePlanes(mesh, cells, palette, face, steps);
    }
```

- [ ] **Step 4: Delete `AddExposedFaces`**

Nothing calls it now. Remove the whole function.

- [ ] **Step 5: Run the build to verify nothing changed**

Run the build+test command. Expected: `Status: SUCCESS!`, **165 cases**, 0 failed.

This is the checkpoint that matters most in this plan. Every count assertion in the suite still holds because the same faces are emitted — only their order in the buffer differs. A wrong axis mapping shows up here as a face-count mismatch against `CountExposedFaces`, not as a subtle visual bug later.

- [ ] **Step 6: Commit**

```bash
git add Cubit
git commit -m "Walk face planes instead of blocks when meshing"
```

---

### Task 4: Merge uniform faces into rectangles

**Files:**
- Modify: `Cubit/src/Voxel/ChunkMesher.cpp`
- Test: `Tests/src/ChunkMesherTests.cpp`

**Interfaces:**
- Consumes: `MeshFacePlanes`, `AxisIndex`, `AxisExtent` from Task 3.
- Produces: no public signature changes. `ChunkMesher::Build` still returns one `ChunkMeshData`.

- [ ] **Step 1: Write the failing tests**

Append to `Tests/src/ChunkMesherTests.cpp`:

```cpp

TEST_CASE("Coplanar faces of the same block merge into one quad")
{
    // Two blocks side by side share four coplanar face pairs — top, bottom,
    // front and back — and each pair is one quad after merging. The two end
    // faces have no partner, so eleven faces come out as six quads.
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});
    world.SetBlock(9, 8, 8, BlockId{1});

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);

    RequireWellFormed(mesh);
    CHECK(CoveredArea(mesh) == 10);
    CHECK(QuadCount(mesh) == 6);
}

TEST_CASE("Each side of a filled chunk merges to a single quad")
{
    World world(1, 1, 1);
    FillChunk(world, 0, 0, 0);

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);

    RequireWellFormed(mesh);
    CHECK(CoveredArea(mesh) == 1536);
    CHECK(QuadCount(mesh) == 6);
}

TEST_CASE("A lit floor slab merges its whole top plane")
{
    // Full daylight and no occluders means every top face carries the same
    // shade, which is exactly the condition merging needs.
    World world(1, 1, 1);
    for (int z = 0; z < Chunk::Depth; ++z)
        for (int x = 0; x < Chunk::Width; ++x)
            world.SetBlock(x, 0, z, BlockId{1});

    for (int z = 0; z < world.GetDepth(); ++z)
        for (int y = 0; y < world.GetHeight(); ++y)
            for (int x = 0; x < world.GetWidth(); ++x)
                world.SetSkyLight(x, y, z, SkyLight::Max);

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);

    // The top plane is one quad; the four sides and the bottom are one each.
    CHECK(QuadCount(mesh) == 6);
    CHECK(CoveredArea(mesh) == CountExposedFaces(world, 0, 0, 0));
}

TEST_CASE("A shading gradient stops faces merging")
{
    // A wall standing on a lit floor darkens the floor faces beside it by
    // ambient occlusion. Those faces have corners that disagree, so they must
    // stay 1x1 rather than being flattened into the open floor's quad.
    World world(1, 1, 1);
    for (int z = 0; z < Chunk::Depth; ++z)
        for (int x = 0; x < Chunk::Width; ++x)
            world.SetBlock(x, 0, z, BlockId{1});

    for (int z = 0; z < Chunk::Depth; ++z)
        world.SetBlock(8, 1, z, BlockId{1});

    for (int z = 0; z < world.GetDepth(); ++z)
        for (int y = 0; y < world.GetHeight(); ++y)
            for (int x = 0; x < world.GetWidth(); ++x)
                world.SetSkyLight(x, y, z, SkyLight::Max);

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);

    RequireWellFormed(mesh);
    CHECK(CoveredArea(mesh) == CountExposedFaces(world, 0, 0, 0));

    // The occluded strips either side of the wall are 16 faces each and cannot
    // merge, so the mesh cannot have collapsed to a handful of quads.
    CHECK(QuadCount(mesh) > 32);
}

TEST_CASE("A merged quad never spans the opaque and transparent passes")
{
    // Opacity follows the block id and merging requires equal ids, so this
    // cannot happen by construction — asserted because the passes silently
    // blending into each other would be very hard to see in a screenshot.
    World world = TransparentPaletteWorld(1, 1, 1);
    for (int x = 0; x < 4; ++x)
        world.SetBlock(x, 8, 8, BlockId{1}); // opaque run
    for (int x = 4; x < 8; ++x)
        world.SetBlock(x, 8, 8, BlockId{2}); // transparent run, touching it

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);

    RequireWellFormed(mesh);

    // The opaque run buries 3 internal pairs: 4*6 - 6 = 18. It keeps the face
    // toward the water, because water neither hides it nor matches its id.
    CHECK(CoveredArea(mesh.Opaque) == 18);

    // The transparent run buries the same 3 pairs and loses one more face to
    // the opaque block beside it, which does hide what is behind it: 24 - 7.
    CHECK(CoveredArea(mesh.Transparent) == 17);
}
```

`TransparentPaletteWorld` and `FillChunk` already exist in this file. `TransparentPaletteWorld` is declared in the *second* anonymous namespace, below these helpers — move the new `TEST_CASE`s after it, or move that namespace up. Appending at the end of the file, as instructed, puts them after it already.

- [ ] **Step 2: Run the build to verify they fail**

Run the build+test command. Expected: build fails, `Tests.exe` non-zero, with exactly **three** failures — nothing merges yet, so quad count still equals face count:

| Test | Reports | Expected |
|---|---|---|
| `Coplanar faces of the same block merge into one quad` | 10 | 6 |
| `Each side of a filled chunk merges to a single quad` | 1536 | 6 |
| `A lit floor slab merges its whole top plane` | 576 | 6 |

`A shading gradient stops faces merging` and `A merged quad never spans the opaque and transparent passes` should already **pass**. That is intended — they assert properties that must hold before *and* after, so they are regression cover rather than drivers. If either fails here, the expected value is wrong and needs fixing before continuing.

- [ ] **Step 3: Split face shading out of `AddFace`**

The mask needs a face's corner shades *before* deciding whether to emit it, so the computation has to come out of `AddFace`. In `Cubit/src/Voxel/ChunkMesher.cpp`, immediately above `AddFace`, add:

```cpp
    //The four corner shades of one face, before they are turned into colours.
    struct FaceCorners
    {
        int Ao[4];
        float Light[4];
    };

    //Samples occlusion and light at a face's four corners.
    FaceCorners CornerShades(
        const Neighbourhood& cells,
        int airCell,
        const FaceGeometry& face,
        const FaceSteps& steps)
    {
        FaceCorners corners{};

        for (int i = 0; i < 4; ++i)
        {
            const int sideA = steps.U * face.CornerU[i];
            const int sideB = steps.V * face.CornerV[i];

            corners.Ao[i] = CornerAo(cells, airCell, sideA, sideB);
            corners.Light[i] = CornerLight(cells, airCell, sideA, sideB);
        }

        return corners;
    }

    //The colour one corner ends up wearing.
    glm::vec4 ShadeCorner(
        const glm::vec4& blockColor, float faceShade, int ao, float light)
    {
        const float lit = faceShade * ChunkMesher::AoShade[ao] * light;

        // The floor applies to the finished shading, not to light alone: the
        // three factors multiply, so flooring only the light term still lets an
        // occluded ceiling underside reach near-black.
        //
        // Shading scales the colour channels only. Alpha is the block's opacity
        // and has nothing to do with how lit the face is.
        const glm::vec3 shaded = glm::vec3(blockColor)
            * (ChunkMesher::LightFloor
                + (1.0f - ChunkMesher::LightFloor) * lit);

        return glm::vec4(shaded, blockColor.a);
    }

    //Emits one quad spanning w cells along the face's U axis and h along V.
    //A 1x1 quad is the w = h = 1 case, so merged and unmerged faces share one
    //emission path rather than drifting apart as two.
    void AddQuad(
        MeshGeometry& mesh,
        const FaceGeometry& face,
        const glm::vec3& origin,
        int w,
        int h,
        const glm::vec4 (&cornerColors)[4],
        bool flip)
    {
        for (int i = 0; i < 4; ++i)
        {
            // Corner already carries the unit step, so only the corners on the
            // far side of each tangent axis move, and only by the extra extent.
            const glm::vec3 stretched =
                glm::vec3(face.U) * static_cast<float>(face.CornerU[i] > 0 ? w - 1 : 0) +
                glm::vec3(face.V) * static_cast<float>(face.CornerV[i] > 0 ? h - 1 : 0);

            mesh.Vertices.push_back(
                { origin + face.Corner[i] + stretched, cornerColors[i] });
        }

        AddFaceIndices(mesh, flip);
    }
```

Then replace the whole body of `AddFace` with:

```cpp
        const int airCell = blockCell + steps.Normal;
        const FaceCorners corners = CornerShades(cells, airCell, face, steps);

        glm::vec4 colors[4];
        for (int i = 0; i < 4; ++i)
            colors[i] = ShadeCorner(
                blockColor, face.Shade, corners.Ao[i], corners.Light[i]);

        // Splitting a quad along its darker diagonal keeps the shading gradient
        // smooth; splitting the other way leaves a visible seam across it.
        const bool flip =
            corners.Ao[0] + corners.Ao[2] > corners.Ao[1] + corners.Ao[3];

        AddQuad(mesh, face, blockOrigin, 1, 1, colors, flip);
```

- [ ] **Step 4: Run the build to verify the refactor changed nothing**

Run the build+test command. Expected: the same four failures as Step 2, no more and no fewer. If a previously passing test now fails, `AddQuad`'s corner arithmetic is wrong for some face — check it against the `Faces` table's `CornerU`/`CornerV` signs.

- [ ] **Step 5: Add the mask cell**

In `Cubit/src/Voxel/ChunkMesher.cpp`, in the anonymous namespace immediately above `MeshFacePlanes`:

```cpp
    //One cell of a face plane while it is being merged. Cells that hold no
    //face, and faces too varied to merge, are simply absent: those are emitted
    //as they are found rather than being carried through the merge pass.
    struct MaskCell
    {
        bool Present = false;
        bool Opaque = false;
        BlockId Block = 0;
        glm::vec4 Color{ 0.0f };
    };

    //Two cells merge only when they are the same block wearing the same colour.
    //Equal colour is what makes the merged quad's flat shading truthful; equal
    //block id costs one compare and keeps the rule readable.
    bool Mergeable(const MaskCell& cell, const MaskCell& key)
    {
        return cell.Present && cell.Block == key.Block && cell.Color == key.Color;
    }

    //Every chunk face plane is the same size, so one mask array serves all six
    //directions.
    static_assert(
        Chunk::Width == Chunk::Height && Chunk::Height == Chunk::Depth,
        "The face-plane mask assumes cubic chunks");

    constexpr int PlaneCells = Chunk::Width * Chunk::Width;
```

- [ ] **Step 6: Fill the mask and merge it**

Replace the body of `MeshFacePlanes` — everything from `const int normalAxis` to the closing brace — with:

```cpp
        const int normalAxis = AxisIndex(face.Normal);
        const int uAxis = AxisIndex(face.U);
        const int vAxis = AxisIndex(face.V);

        const int sliceCount = AxisExtent(normalAxis);
        const int uCount = AxisExtent(uAxis);
        const int vCount = AxisExtent(vAxis);

        MaskCell mask[PlaneCells];

        for (int slice = 0; slice < sliceCount; ++slice)
        {
            for (int v = 0; v < vCount; ++v)
            {
                for (int u = 0; u < uCount; ++u)
                {
                    MaskCell& entry = mask[v * uCount + u];
                    entry = MaskCell{};

                    glm::ivec3 local(0);
                    local[normalAxis] = slice;
                    local[uAxis] = u;
                    local[vAxis] = v;

                    const int cell = Neighbourhood::At(local.x, local.y, local.z);
                    if (!cells.IsSolid(cell))
                        continue;

                    const BlockId self = cells.Block(cell);
                    const int neighbourCell = cell + steps.Normal;

                    // A face is worth drawing when what is beyond it does not
                    // hide it, and is not more of the same block: two water
                    // cells meet at a face that would only blend against itself.
                    if (cells.IsOpaque(neighbourCell) ||
                        cells.Block(neighbourCell) == self)
                        continue;

                    // A block's own opacity decides which pass draws it; the
                    // faces of one block never span both.
                    const bool opaque = cells.IsOpaque(cell);
                    MeshGeometry& target = opaque
                        ? mesh.Opaque
                        : mesh.Transparent;

                    const glm::vec4 blockColor = palette[self];
                    const FaceCorners corners =
                        CornerShades(cells, neighbourCell, face, steps);

                    const bool uniform =
                        corners.Ao[0] == corners.Ao[1] &&
                        corners.Ao[1] == corners.Ao[2] &&
                        corners.Ao[2] == corners.Ao[3] &&
                        corners.Light[0] == corners.Light[1] &&
                        corners.Light[1] == corners.Light[2] &&
                        corners.Light[2] == corners.Light[3];

                    if (!uniform)
                    {
                        // Corners that disagree cannot survive being stretched
                        // across a merged quad, because the shading in between
                        // is interpolated linearly from them. Emit it alone.
                        glm::vec4 colors[4];
                        for (int i = 0; i < 4; ++i)
                            colors[i] = ShadeCorner(blockColor, face.Shade,
                                corners.Ao[i], corners.Light[i]);

                        const bool flip = corners.Ao[0] + corners.Ao[2]
                            > corners.Ao[1] + corners.Ao[3];

                        AddQuad(target, face, glm::vec3(local), 1, 1,
                            colors, flip);
                        continue;
                    }

                    entry.Present = true;
                    entry.Opaque = opaque;
                    entry.Block = self;
                    entry.Color = ShadeCorner(blockColor, face.Shade,
                        corners.Ao[0], corners.Light[0]);
                }
            }

            for (int v = 0; v < vCount; ++v)
            {
                for (int u = 0; u < uCount; ++u)
                {
                    const MaskCell key = mask[v * uCount + u];
                    if (!key.Present)
                        continue;

                    // Widen along U first, then grow along V by whole rows of
                    // that width. Taking the widest row first is what makes the
                    // result a maximal rectangle rather than a ragged strip.
                    int w = 1;
                    while (u + w < uCount &&
                        Mergeable(mask[v * uCount + u + w], key))
                        ++w;

                    int h = 1;
                    while (v + h < vCount)
                    {
                        bool wholeRow = true;
                        for (int i = 0; i < w && wholeRow; ++i)
                            wholeRow =
                                Mergeable(mask[(v + h) * uCount + u + i], key);

                        if (!wholeRow)
                            break;

                        ++h;
                    }

                    for (int dv = 0; dv < h; ++dv)
                        for (int du = 0; du < w; ++du)
                            mask[(v + dv) * uCount + u + du].Present = false;

                    glm::ivec3 local(0);
                    local[normalAxis] = slice;
                    local[uAxis] = u;
                    local[vAxis] = v;

                    // A uniform quad's corners are equal, so neither diagonal
                    // is the darker one and the split never shows.
                    const glm::vec4 colors[4] =
                        { key.Color, key.Color, key.Color, key.Color };

                    AddQuad(key.Opaque ? mesh.Opaque : mesh.Transparent,
                        face, glm::vec3(local), w, h, colors, false);
                }
            }
        }
```

- [ ] **Step 7: Run the build to verify the merge tests pass**

Run the build+test command. Expected: `Status: SUCCESS!`, **170 cases**, 0 failed.

Every pre-existing test must still pass. In particular `Meshed faces match an independent count for varied terrain` and `Every chunk of a multi-chunk world matches the independent count` are the proof that merging conserved surface area rather than losing or duplicating faces.

- [ ] **Step 8: Assert that the terrain actually merged**

In `Tests/src/ChunkMesherTests.cpp`, add a second assertion to `"The sandbox test terrain meshes to its known size"`, below the existing `CoveredArea` check:

```cpp
    // Rolling terrain has large flat runs, so merging must beat one quad per
    // face by a wide margin. A bound rather than a number: the exact count is
    // the merge rule's business and would have to be re-derived on any change.
    CHECK(QuadCount(mesh) < 900);
```

Run the build+test command. Expected: `Status: SUCCESS!`, **170 cases**, 0 failed. If the quad count is at or above 900, merging is running but finding almost nothing — check that `uniform` is not accidentally always false.

- [ ] **Step 9: Commit**

```bash
git add Cubit Tests
git commit -m "Merge coplanar faces that share a colour into single quads"
```

---

### Task 5: Relabel the HUD and record the measurement

**Files:**
- Modify: `Sandbox/src/HudLayer.h`, `docs/performance.md`, `docs/engine-roadmap.md`, `README.md`
- Test: `Tests/src/ChunkMesherTests.cpp` (temporary probe, removed in this task)

- [ ] **Step 1: Relabel the HUD counter**

`WorldRenderer::TotalFaceCount` returns `indices / 6`, which counts quads now, not faces. In `Sandbox/src/HudLayer.h:187`:

```cpp
        DrawText("QUADS " + std::to_string(m_State->MeshFaceCount), TextMargin, y);
```

Leave the `MeshFaceCount` field name and `TotalFaceCount` alone — renaming those touches the engine's public surface for a label, which is not worth it.

- [ ] **Step 2: Add a temporary measurement probe**

Append to `Tests/src/ChunkMesherTests.cpp`. This is removed in Step 5 — it exists only to produce numbers for the docs:

```cpp

TEST_CASE("PROBE: greedy meshing on the battlefield map")
{
    World lit = BuildWorld(VoxLoader::LoadFile(
        "C:/dev/cubit/Sandbox/assets/maps/battlefield.vox"));
    SkyLight::PropagateAll(lit);

    const auto start = std::chrono::steady_clock::now();

    std::size_t quads = 0;
    std::size_t vertices = 0;
    std::size_t area = 0;

    for (int chunkZ = 0; chunkZ < lit.GetChunksZ(); ++chunkZ)
        for (int chunkY = 0; chunkY < lit.GetChunksY(); ++chunkY)
            for (int chunkX = 0; chunkX < lit.GetChunksX(); ++chunkX)
            {
                const ChunkMeshData mesh =
                    ChunkMesher::Build(lit, chunkX, chunkY, chunkZ);

                quads += QuadCount(mesh);
                vertices += mesh.Opaque.Vertices.size()
                    + mesh.Transparent.Vertices.size();
                area += CoveredArea(mesh);
            }

    const double ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();

    MESSAGE("quads=", quads, " vertices=", vertices, " area=", area,
        " mesh_ms=", ms);

    // Every quad covers at least the one face it came from, so this holds both
    // before and after merging. The probe exists to print numbers rather than
    // to gate anything, but a case that asserts nothing is not worth running.
    CHECK(area > 0);
    CHECK(quads <= area);
}
```

Add `#include "Cubit/Voxel/VoxLoader.h"` to the file's include block.

`MESSAGE` prints unconditionally, unlike `INFO`, which only surfaces on failure. If the line does not appear in the build log, temporarily change the last line to `CHECK(area == 0)` to force the output, then change it back.

- [ ] **Step 3: Record the numbers**

Run the build+test command and read the `PROBE` line out of the build log. That is the **after** measurement.

Compare the two `area` figures once you have both: merging regroups faces but must not change how much surface they cover, so a difference means something is wrong and the timing numbers are measuring two different meshes.

Then get the before-numbers by restoring the pre-merge mesher. Find the commit by message rather than counting back from `HEAD`, which is fragile if extra commits were made:

```bash
BEFORE=$(git log --format=%H --grep="Measure meshes by covered area rather than quad count")
git checkout "$BEFORE" -- Cubit/src/Voxel/ChunkMesher.cpp
```

That is the last commit before Task 3 restructured the mesher, so its `ChunkMesher.cpp` is the original one-quad-per-face version. It still compiles against the extracted `Neighbourhood.h`, because Task 1 committed both together.

Run the build+test command again and read the `PROBE` line, then restore:

```bash
git checkout HEAD -- Cubit/src/Voxel/ChunkMesher.cpp
```

Expect the before-run to fail the three merge tests from Task 4 — that is fine, the `PROBE` line still prints. Record both sets of numbers.

- [ ] **Step 4: Write the numbers into performance.md**

In `docs/performance.md`, replace the P3 section's closing two lines. Substitute the real measured values for every `<...>` below — do not leave a placeholder:

```markdown
**Fix applied 2026-08-06:** `ChunkMesher::Build` walks face planes rather than
blocks, filling a 16×16 mask per plane and consuming it into maximal rectangles.
A face joins the mask only when its four corners share one shade, so merging
never flattens an ambient-occlusion gradient; those faces still go out 1×1.

Measured in Debug over `battlefield.vox`: quads <before> → <after>, vertices
<before> → <after>, whole-map mesh time <before> ms → <after> ms. <One sentence
on whether meshing got faster or slower and by how much.>

**Priority:** medium-high. **Status:** done.
```

- [ ] **Step 5: Remove the probe**

Delete the `PROBE` test case added in Step 2, and the `VoxLoader.h` include if nothing else in the file uses it.

Run the build+test command. Expected: `Status: SUCCESS!`, **170 cases**, 0 failed.

- [ ] **Step 6: Commit the code and the measurement**

```bash
git add Cubit Sandbox Tests docs/performance.md
git commit -m "Record the greedy meshing measurement and relabel the HUD"
```

- [ ] **Step 7: Update the roadmap and readme**

In `docs/engine-roadmap.md`, bump `_Last updated:_` to `2026-08-06`, and mark item 3 under *Performance / scale* done in the same style as items 1 and 2:

```markdown
3. ~~**Greedy meshing**~~ **DONE 2026-08-06** — `ChunkMesher::Build` walks face
   planes and merges coplanar faces that share a block and a colour into maximal
   rectangles. Faces carrying an AO or light gradient stay 1×1, so the image is
   unchanged.
```

In the same file's *"finish the engine" arc*, replace item 4 with:

```markdown
4. ~~**Greedy meshing**~~ **DONE 2026-08-06**.
5. **Multi-model stitching** (full AoS-scale maps). ← next
```

In `README.md`, remove the `**Greedy meshing**` bullet from *What's next*, and add to the *Rendering* list in *What works*:

```markdown
- Greedy meshing: coplanar faces sharing a block and a colour merge into maximal
  rectangles, so flat terrain costs a handful of quads instead of thousands
```

```bash
git add docs/engine-roadmap.md README.md
git commit -m "Record greedy meshing in the roadmap and readme"
```

---

## Manual verification (after the plan is complete)

Rendering has no unit tests; it is verified by running the sandbox.

```bash
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Sandbox/Sandbox.vcxproj -p:Configuration=Debug -p:Platform=x64
cd bin/Debug-windows-x86_64/Sandbox && ./Sandbox.exe
```

The whole point of the conservative merge rule is that there is nothing new to look at. Check:

1. The terrain looks **identical** to before — same colours, same ambient-occlusion darkening at wall/floor junctions, same smooth light gradients.
2. The HUD `QUADS` figure is far below the 480262 it read before.
3. The river still reads as transparent water over a lit bed.
4. Break and place blocks; the chunk remeshes with no holes, no z-fighting, and no stretched quads.
5. Walk to a wall and confirm the darkened floor strip beside it is still graded, not flat.

Capturing this from a script: the GLFW window is the `GLFW30`-class one, not the process's `MainWindowHandle`. See the `screenshot-cubit-gl-window` note.

---

## Self-Review

**Spec coverage:**

| Spec section | Task |
|---|---|
| Uniform-face merge rule (ao and light all equal) | 4 (Step 6) |
| Merge key: block id + colour | 4 (Step 5, `Mergeable`) |
| Non-uniform faces emitted 1×1 with their flip | 4 (Step 6) |
| Plane sweep: direction → slice → mask → rectangles | 3 (structure), 4 (mask + merge) |
| Merged quad corner arithmetic from `FaceGeometry` | 4 (Step 3, `AddQuad`) |
| Transparency split preserved | 4 (Step 6 routes by `key.Opaque`); tested Step 1 |
| Same-block rule preserved | 3 and 4 (the face-existence test is copied verbatim) |
| Diagonal flip resolves itself | 4 (Step 6 passes `false` for merged quads) |
| `Neighbourhood` moves to its own header | 1 |
| `ChunkMesher.h` public surface unchanged | 1, 3, 4 (none of them touch it) |
| Area oracle replaces quad-count assertions | 2 |
| Targeted merge tests (6 cases in the spec table) | 4 (Step 1) |
| Existing AO tests keep working | 3 (Step 5) and 4 (Step 7) verify the whole suite |
| Debug measurement recorded in P3 | 5 |
| HUD relabel | 5 (Step 1) |

No gaps. The spec's *Out of scope* items — strip-merging gradients, P4, P5, threading, cross-chunk merging, changes to AO or sky light — appear in no task.

**Placeholder scan:** every code step carries literal code. Two deliberate gaps, both flagged in place: Task 5 Step 4's `<before>`/`<after>` values, which cannot be known until the measurement is taken and which the step forbids leaving as placeholders; and Task 2 Step 4's commented-out `QuadCount` bound, which Task 4 Step 8 uncomments.

**Type consistency:** `MeshGeometry`, `ChunkMeshData`, `Neighbourhood`, `FaceGeometry`, `FaceSteps` and `Palette` all pre-date this plan and are used with their existing shapes. `FaceCorners`, `MaskCell`, `CornerShades`, `ShadeCorner`, `AddQuad`, `Mergeable`, `AxisIndex`, `AxisExtent` and `PlaneCells` are introduced in Tasks 3 and 4 and used only with the signatures given there. `MeshFacePlanes` is declared in Task 3 Step 2 and its body replaced in Task 4 Step 6 with the same signature. `QuadCount` and `CoveredArea` are defined in Task 2 and used in Tasks 4 and 5; `CoveredArea` is overloaded for both `MeshGeometry` and `ChunkMeshData`, and both overloads are used.

**Test-count arithmetic:** 165 start → 165 (Task 1, refactor) → 165 (Task 2, refactor) → 165 (Task 3, refactor) → 170 (Task 4, +5) → 170 (Task 5, probe added and removed).

**Hand-checked expected values:**
- *Two blocks side by side:* 10 exposed faces. Top, bottom, front and back each form an adjacent coplanar pair → 4 quads; the two end faces are unpaired → 2 quads. Total 6.
- *Filled 16³ chunk in a 1×1×1 world:* all six sides face out of bounds, which reads as air, not opaque, and open sky, so every corner is fully open at full light → each side is uniform → 1 quad each, 6 total, area 1536.
- *Opaque/transparent run:* 4 blocks each. Both bury 3 internal face-pairs, so 24 − 6 = 18. The transparent run loses one more, because the opaque block beside it hides the face pointing at it, while the reverse is not true — water hides nothing. Hence 18 opaque and 17 transparent, not 18 and 18.
- *Lit floor slab:* 256 top + 256 bottom + 4 sides × 16 = 576 faces, and every plane is uniform, so 6 quads.
- *Shading gradient:* the two occluded floor strips flanking the wall are 16 faces each and none of them can merge, so 32 quads is a floor the mesh cannot go below.
