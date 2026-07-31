# Transparency Phase 1 (Opacity) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make water render see-through, with a visible and correctly lit riverbed beneath it. Water stays solid — the player still walks on its surface.

**Architecture:** Palette entries gain an alpha channel; a block is opaque when its alpha is 1.0. `World` derives a 256-entry opacity table from its palette. The mesher emits faces at opaque/non-opaque boundaries instead of solid/air ones, splits its output into opaque and transparent geometry, and the renderer draws the transparent set last, sorted back to front, with depth writes off.

**Tech Stack:** C++20, MSVC, OpenGL 3.3, GLM, doctest, premake5.

Full design: [`docs/superpowers/specs/2026-07-31-transparency-opacity-design.md`](../specs/2026-07-31-transparency-opacity-design.md).

## Global Constraints

- **Build + test command** (builds `Cubit` then `Tests`, and runs the suite as a postbuild step). Use FORWARD slashes in the project path — backslashes fail in Git Bash with MSB1009:
  ```bash
  "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64
  ```
- **Build the Sandbox:** same command with `Sandbox/Sandbox.vcxproj`.
- **Run one test case:** `./bin/Debug-windows-x86_64/Tests/Tests.exe -tc="<name>"`
- The suite passes **150 cases** at the start of this plan. A failing test breaks the build by design.
- **A block is opaque when `alpha >= 1.0f`.** Use that exact comparison everywhere; never `== 1.0f`.
- **Comment style:** `//` with no space above a declaration; `// ` with a space inside a function body. Comments explain *why*.
- **`IsSolid` is not touched by this plan.** Collision and raycast keep their current behaviour — that is Phase 2. If a task tempts you to change `IsSolid`, you have misread it.
- **Never** add Claude co-author trailers or attribution to commits.

---

### Task 1: Palette entries carry alpha

**Files:**
- Modify: `Cubit/include/Cubit/Voxel/Block.h`, `Cubit/include/Cubit/Voxel/World.h`, `Cubit/src/Voxel/VoxLoader.cpp`, `Cubit/src/Voxel/VoxWriter.cpp`, `Cubit/src/Voxel/TerrainGen.cpp`, `Cubit/src/Voxel/ChunkMesher.cpp`
- Test: `Tests/src/VoxWriterTests.cpp`, plus mechanical updates to `Tests/src/WorldTests.cpp`, `Tests/src/BuildWorldTests.cpp`, `Tests/src/WorldSaveTests.cpp`, `Tests/src/ChunkAoTests.cpp`

**Interfaces:**
- Produces: `using Palette = std::array<glm::vec4, 256>;` and `glm::vec4 World::GetBlockColor(BlockId) const`. Every later task depends on both.

- [ ] **Step 1: Write the failing test for alpha round-tripping**

Append to `Tests/src/VoxWriterTests.cpp`:

```cpp

TEST_CASE("VoxWriter round-trips palette alpha")
{
    // Alpha is how a block declares itself transparent, so it has to survive a
    // save and load like any other channel.
    VoxModel m = MakeModel(1, 1, 1);
    m.Colors[7] = glm::vec4(0.2f, 0.4f, 0.8f, 0.55f);
    SetId(m, 0, 0, 0, 7);

    const VoxModel r = VoxLoader::Parse(VoxWriter::Write(m));

    CHECK(r.Colors[7].a == doctest::Approx(0.55f).epsilon(1.0 / 255));
}

TEST_CASE("An unwritten palette entry loads back fully opaque")
{
    VoxModel m = MakeModel(1, 1, 1);
    m.Colors[3] = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
    SetId(m, 0, 0, 0, 3);

    const VoxModel r = VoxLoader::Parse(VoxWriter::Write(m));

    CHECK(r.Colors[3].a == doctest::Approx(1.0f).epsilon(1.0 / 255));
}
```

- [ ] **Step 2: Run the build to verify it fails**

Run the build+test command. Expected: **compile failure** — `glm::vec4` cannot be assigned to a `glm::vec3` palette entry. That is the red state.

- [ ] **Step 3: Widen `Palette` to vec4**

In `Cubit/include/Cubit/Voxel/Block.h`, replace the `Palette` alias and `DefaultPalette` with:

```cpp
//256 colours addressed directly by BlockId. Index 0 (air) is never rendered.
//The alpha channel is what makes a block transparent: a block is opaque when its
//alpha is 1.0, and anything less lets the blocks behind it show through.
using Palette = std::array<glm::vec4, 256>;

//Reports whether a palette colour hides what is behind it. Air is alpha 0, so it
//falls out of this rule rather than needing a special case.
constexpr bool IsOpaqueColor(const glm::vec4& color)
{
    return color.a >= 1.0f;
}

//The palette a world uses before a map overrides it. Reproduces the colours the
//engine shipped with, at the indices the old named blocks used, so test terrain
//reads the same after the refactor. Everything but air is fully opaque.
inline Palette DefaultPalette()
{
    Palette palette;
    const glm::vec4 solid(0.78f, 0.85f, 1.00f, 1.0f);
    palette.fill(solid);
    palette[0] = glm::vec4(0.0f);                       // Air, never rendered
    palette[1] = solid;                                 // Solid
    palette[2] = glm::vec4(0.85f, 0.25f, 0.25f, 1.0f);  // Red
    palette[3] = glm::vec4(0.90f, 0.55f, 0.20f, 1.0f);  // Orange
    palette[4] = glm::vec4(0.92f, 0.85f, 0.30f, 1.0f);  // Yellow
    palette[5] = glm::vec4(0.35f, 0.75f, 0.35f, 1.0f);  // Green
    palette[6] = glm::vec4(0.30f, 0.50f, 0.90f, 1.0f);  // Blue
    palette[7] = glm::vec4(0.62f, 0.38f, 0.80f, 1.0f);  // Purple
    palette[8] = glm::vec4(0.95f, 0.95f, 0.95f, 1.0f);  // White
    palette[9] = glm::vec4(0.55f, 0.57f, 0.60f, 1.0f);  // Grey
    return palette;
}
```

`glm::vec4(0.0f)` sets all four channels to 0, so air's alpha is 0 as intended.

- [ ] **Step 4: Return vec4 from `World::GetBlockColor`**

In `Cubit/include/Cubit/Voxel/World.h`, change the one-line accessor:

```cpp
    //Returns the colour of a block by looking its id up in this world's palette.
    //The alpha channel carries the block's opacity.
    glm::vec4 GetBlockColor(BlockId block) const { return m_Palette[block]; }
```

- [ ] **Step 5: Read the alpha byte in the loader**

In `Cubit/src/Voxel/VoxLoader.cpp`, in the `RGBA` branch, replace the palette assignment with:

```cpp
                palette[static_cast<std::size_t>(j) + 1] = glm::vec4(
                    bytes[p + 0] / 255.0f,
                    bytes[p + 1] / 255.0f,
                    bytes[p + 2] / 255.0f,
                    bytes[p + 3] / 255.0f);
```

- [ ] **Step 6: Write the alpha byte in the writer**

In `Cubit/src/Voxel/VoxWriter.cpp`, in the `RGBA` loop, replace the four `rgba.push_back` calls in the `j < 255` branch with:

```cpp
            const glm::vec4& c = model.Colors[static_cast<std::size_t>(j) + 1];
            rgba.push_back(ToByte(c.r));
            rgba.push_back(ToByte(c.g));
            rgba.push_back(ToByte(c.b));
            rgba.push_back(ToByte(c.a));
```

Note the declaration changes from `const glm::vec3&` to `const glm::vec4&`.

- [ ] **Step 7: Give every generated palette entry an alpha**

In `Cubit/src/Voxel/TerrainGen.cpp`, in `TerrainGen::MapPalette`, wrap each colour so it compiles against the widened palette. Water keeps alpha 1.0 for now — Task 7 is what makes it transparent:

```cpp
Palette TerrainGen::MapPalette()
{
    Palette p = DefaultPalette();
    p[Grass]       = glm::vec4(glm::vec3(70, 145, 55) / 255.0f, 1.0f);
    p[GrassDark]   = glm::vec4(glm::vec3(55, 115, 45) / 255.0f, 1.0f);
    p[Dirt]        = glm::vec4(glm::vec3(120, 85, 50) / 255.0f, 1.0f);
    p[Stone]       = glm::vec4(glm::vec3(130, 130, 135) / 255.0f, 1.0f);
    p[StoneDark]   = glm::vec4(glm::vec3(80, 80, 85) / 255.0f, 1.0f);
    p[Sand]        = glm::vec4(glm::vec3(210, 195, 140) / 255.0f, 1.0f);
    p[Water]       = glm::vec4(glm::vec3(55, 110, 200) / 255.0f, 1.0f);
    p[Wood]        = glm::vec4(glm::vec3(95, 65, 40) / 255.0f, 1.0f);
    p[Leaves]      = glm::vec4(glm::vec3(45, 110, 45) / 255.0f, 1.0f);
    p[LeavesLight] = glm::vec4(glm::vec3(70, 140, 60) / 255.0f, 1.0f);
    p[Snow]        = glm::vec4(glm::vec3(235, 240, 245) / 255.0f, 1.0f);
    p[RedBase]     = glm::vec4(glm::vec3(190, 45, 45) / 255.0f, 1.0f);
    p[BlueBase]    = glm::vec4(glm::vec3(55, 80, 200) / 255.0f, 1.0f);
    return p;
}
```

- [ ] **Step 8: Truncate the palette colour in the mesher**

`VoxelVertex::Color` is still `vec3` until Task 6, so the mesher must drop the alpha for now. In `Cubit/src/Voxel/ChunkMesher.cpp`, in `AddExposedFaces`:

```cpp
        // Alpha is dropped here until the vertex format carries it.
        const glm::vec3 color = glm::vec3(palette[cells.Block(blockCell)]);
```

- [ ] **Step 9: Update the existing tests mechanically**

These comparisons construct a `glm::vec3` and must become `glm::vec4` with an explicit alpha. Change exactly these lines:

- `Tests/src/WorldTests.cpp:143` → `CHECK(world.GetBlockColor(BlockId{5}) == glm::vec4(0.35f, 0.75f, 0.35f, 1.0f));`
- `Tests/src/WorldTests.cpp:144` → `CHECK(world.GetBlockColor(BlockId{2}) == glm::vec4(0.85f, 0.25f, 0.25f, 1.0f));`
- `Tests/src/WorldTests.cpp:152` → `palette[3] = glm::vec4(0.1f, 0.2f, 0.3f, 1.0f);`
- `Tests/src/WorldTests.cpp:155` → `CHECK(world.GetBlockColor(BlockId{3}) == glm::vec4(0.1f, 0.2f, 0.3f, 1.0f));`
- `Tests/src/BuildWorldTests.cpp:53` → `model.Colors[4] = glm::vec4(0.2f, 0.4f, 0.6f, 1.0f);`
- `Tests/src/BuildWorldTests.cpp:57` → `CHECK(world.GetBlockColor(BlockId{4}) == glm::vec4(0.2f, 0.4f, 0.6f, 1.0f));`
- `Tests/src/WorldSaveTests.cpp:99` → `palette[4] = glm::vec4(0.2f, 0.4f, 0.6f, 1.0f);`
- `Tests/src/WorldSaveTests.cpp:102` → `CHECK(ToVoxModel(world).Colors[4] == glm::vec4(0.2f, 0.4f, 0.6f, 1.0f));`
- `Tests/src/WorldSaveTests.cpp:111` → `palette[6] = glm::vec4(0.1f, 0.2f, 0.3f, 1.0f);`
- `Tests/src/VoxWriterTests.cpp:48` → `m.Colors[7] = glm::vec4(60 / 255.0f, 120 / 255.0f, 200 / 255.0f, 1.0f);`
- `Tests/src/VoxWriterTests.cpp:49` → `m.Colors[12] = glm::vec4(190 / 255.0f, 45 / 255.0f, 45 / 255.0f, 1.0f);`
- `Tests/src/ChunkAoTests.cpp:100-103` — the helper's return type: change `glm::vec3 OpenTopColor(const World& world)` to `glm::vec4 OpenTopColor(const World& world)`.

Line numbers may have shifted; locate by content. Do not change assertions that read `.x`, `.y`, `.z`, or `.r` — those compile unchanged against `vec4`.

- [ ] **Step 10: Run the build to verify everything passes**

Run the build+test command. Expected: `Status: SUCCESS!`, **152 cases**, 0 failed.

- [ ] **Step 11: Commit**

```bash
git add Cubit Tests
git commit -m "Carry an alpha channel on palette colours"
```

---

### Task 2: World knows which blocks are opaque

**Files:**
- Modify: `Cubit/include/Cubit/Voxel/World.h`, `Cubit/src/Voxel/World.cpp`
- Test: `Tests/src/WorldTests.cpp`

**Interfaces:**
- Consumes: `Palette` (vec4) and `IsOpaqueColor(const glm::vec4&)` from Task 1.
- Produces: `bool World::IsIdOpaque(BlockId) const` and `bool World::IsBlockOpaque(int x, int y, int z) const`. Tasks 3, 4 and 5 all use them.

- [ ] **Step 1: Write the failing tests**

Append to `Tests/src/WorldTests.cpp`:

```cpp

TEST_CASE("A block is opaque when its palette alpha is full")
{
    World world(1, 1, 1);
    Palette palette = DefaultPalette();
    palette[4] = glm::vec4(0.2f, 0.4f, 0.6f, 1.0f);
    palette[5] = glm::vec4(0.2f, 0.4f, 0.6f, 0.5f);
    world.SetPalette(palette);

    CHECK(world.IsIdOpaque(BlockId{4}));
    CHECK_FALSE(world.IsIdOpaque(BlockId{5}));
}

TEST_CASE("Air is never opaque")
{
    const World world(1, 1, 1);

    CHECK_FALSE(world.IsIdOpaque(BlockId{0}));
}

TEST_CASE("Block opacity follows the block at a position")
{
    World world(1, 1, 1);
    Palette palette = DefaultPalette();
    palette[5] = glm::vec4(0.2f, 0.4f, 0.6f, 0.5f);
    world.SetPalette(palette);

    world.SetBlock(8, 8, 8, BlockId{1});
    world.SetBlock(9, 8, 8, BlockId{5});

    CHECK(world.IsBlockOpaque(8, 8, 8));
    CHECK_FALSE(world.IsBlockOpaque(9, 8, 8));
    CHECK_FALSE(world.IsBlockOpaque(0, 0, 0)); // air
}

TEST_CASE("Positions outside the world are not opaque")
{
    // Matching GetBlock returning air and GetSkyLight returning open sky: the
    // space around a map must not read as a wall.
    const World world(1, 1, 1);

    CHECK_FALSE(world.IsBlockOpaque(-1, 0, 0));
    CHECK_FALSE(world.IsBlockOpaque(0, world.GetHeight(), 0));
}

TEST_CASE("Replacing the palette updates opacity")
{
    // The opacity table is derived, so it must not go stale when a map installs
    // its own palette.
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{4});
    REQUIRE(world.IsBlockOpaque(8, 8, 8));

    Palette palette = DefaultPalette();
    palette[4] = glm::vec4(0.2f, 0.4f, 0.6f, 0.25f);
    world.SetPalette(palette);

    CHECK_FALSE(world.IsBlockOpaque(8, 8, 8));
}
```

- [ ] **Step 2: Run the build to verify it fails**

Run the build+test command. Expected: compile failure — `IsIdOpaque` is not a member of `World`.

- [ ] **Step 3: Add the opacity table to the header**

In `Cubit/include/Cubit/Voxel/World.h`, replace the existing inline `SetPalette` with a declaration, and add the two queries plus the member. `SetPalette` moves to the `.cpp` because it now has to rebuild the table:

```cpp
    //Replaces the palette, e.g. with one loaded from a map file, and rebuilds the
    //derived opacity table.
    void SetPalette(const Palette& palette);

    //Reports whether a block id hides what is behind it.
    //
    //A table lookup rather than a comparison against the palette: the mesher
    //samples this tens of thousands of times per chunk, and resolving it per
    //sample is exactly the cost that made chunk builds slow before (see
    //docs/performance.md, P7).
    bool IsIdOpaque(BlockId block) const { return m_Opaque[block]; }

    //Reports whether the block at this position hides what is behind it.
    //Positions outside the world are not opaque, matching GetBlock reading as
    //air and GetSkyLight reading as open sky.
    bool IsBlockOpaque(int x, int y, int z) const;
```

Add the member alongside `m_Palette`:

```cpp
    std::array<bool, 256> m_Opaque{};
```

and `#include <array>` to the header's include block.

- [ ] **Step 4: Implement in the source file**

In `Cubit/src/Voxel/World.cpp`, add after `World::IsBlockSolid`:

```cpp

void World::SetPalette(const Palette& palette)
{
    m_Palette = palette;

    for (std::size_t i = 0; i < m_Opaque.size(); ++i)
        m_Opaque[i] = IsOpaqueColor(m_Palette[i]);
}

bool World::IsBlockOpaque(int x, int y, int z) const
{
    if (!IsInBounds(x, y, z))
        return false;

    return IsIdOpaque(GetBlock(x, y, z));
}
```

The constructor's member-init list already sets `m_Palette(DefaultPalette())`, but that bypasses `SetPalette`, so the table would stay all-false. In the `World::World` constructor body, add as the **first** statement:

```cpp
    SetPalette(m_Palette);
```

- [ ] **Step 5: Run the build to verify it passes**

Run the build+test command. Expected: `Status: SUCCESS!`, **157 cases**, 0 failed.

- [ ] **Step 6: Commit**

```bash
git add Cubit Tests
git commit -m "Derive a block opacity table from the world palette"
```

---

### Task 3: Sky light passes through non-opaque blocks

**Files:**
- Modify: `Cubit/src/Voxel/SkyLight.cpp`
- Test: `Tests/src/SkyLightTests.cpp`

**Interfaces:**
- Consumes: `World::IsBlockOpaque(int, int, int)` from Task 2.
- Produces: nothing new. Behaviour change only.

- [ ] **Step 1: Write the failing test**

Append to `Tests/src/SkyLightTests.cpp`:

```cpp

TEST_CASE("Light passes through a non-opaque block")
{
    // A roof of transparent blocks should light the space under it just as an
    // open sky would, which is what makes a riverbed visible through water.
    World world(1, 4, 1);
    Palette palette = DefaultPalette();
    palette[2] = glm::vec4(0.2f, 0.4f, 0.8f, 0.5f); // transparent
    world.SetPalette(palette);

    const int roof = 20;
    for (int z = 0; z < world.GetDepth(); ++z)
        for (int x = 0; x < world.GetWidth(); ++x)
            world.SetBlock(x, roof, z, BlockId{2});

    SkyLight::PropagateAll(world);

    CHECK(world.GetSkyLight(8, roof, 8) == SkyLight::Max);
    CHECK(world.GetSkyLight(8, roof - 1, 8) == SkyLight::Max);
    CHECK(world.GetSkyLight(8, 0, 8) == SkyLight::Max);
}

TEST_CASE("An opaque roof still blocks light after the opacity change")
{
    // The companion to the case above: switching the predicate must not make
    // ordinary solid blocks stop casting shadow.
    World world(1, 4, 1);
    const int roof = 20;

    for (int z = 0; z < world.GetDepth(); ++z)
        for (int x = 0; x < world.GetWidth(); ++x)
            world.SetBlock(x, roof, z, BlockId{1});

    SkyLight::PropagateAll(world);

    CHECK(world.GetSkyLight(8, roof - 1, 8) == 0);
}
```

- [ ] **Step 2: Run the build to verify the first test fails**

Run the build+test command. Expected: build fails because `Tests.exe` returns non-zero. `Light passes through a non-opaque block` FAILS — the transparent roof currently blocks light, so `GetSkyLight(8, roof - 1, 8)` is 0, not 15. The second new test already passes.

- [ ] **Step 3: Switch the three predicates**

In `Cubit/src/Voxel/SkyLight.cpp`, change exactly three lines from solidity to opacity:

In `Flood`:
```cpp
                if (world.IsBlockOpaque(next.x, next.y, next.z))
                    continue;
```

In `Unflood`:
```cpp
                if (world.IsBlockOpaque(next.x, next.y, next.z))
                    continue;
```

In `PropagateAll`, the top-layer seeding loop:
```cpp
            if (world.IsBlockOpaque(x, top, z))
                continue;
```

Leave `Repropagate`'s `if (world.IsBlockSolid(x, y, z))` **unchanged** — that one asks "did this edit fill the cell in", which is about the block existing, not about it hiding light. A transparent block placed in open air still displaces the light that was there.

- [ ] **Step 4: Run the build to verify it passes**

Run the build+test command. Expected: `Status: SUCCESS!`, **159 cases**, 0 failed. In particular the existing cases `A roof darkens the column beneath it`, `Light spreads sideways under an overhang, losing one level a step`, and both `matches a full propagation` cases must still pass — they are the regression net for this change.

- [ ] **Step 5: Commit**

```bash
git add Cubit Tests
git commit -m "Let sky light through blocks that are not opaque"
```

---

### Task 4: The mesher culls faces by opacity

**Files:**
- Modify: `Cubit/src/Voxel/ChunkMesher.cpp`
- Test: `Tests/src/ChunkMesherTests.cpp`

**Interfaces:**
- Consumes: `World::IsIdOpaque(BlockId)` from Task 2.
- Produces: no signature changes. `ChunkMesher::Build` still returns one `ChunkMeshData`; Task 5 splits it.

**The rule:** for a rendered block `B` with neighbour `N`, emit the face when `N` is not opaque **and** `N != B`.

- [ ] **Step 1: Write the failing tests**

Append to `Tests/src/ChunkMesherTests.cpp`. Read the top of that file first — it already has helpers for counting faces; use `mesh.Indices.size() / 6` for a face count as the existing tests do:

```cpp

namespace
{
    //A world whose palette makes id 2 transparent, for the opacity cases below.
    World TransparentPaletteWorld(int cx, int cy, int cz)
    {
        World world(cx, cy, cz);
        Palette palette = DefaultPalette();
        palette[2] = glm::vec4(0.2f, 0.4f, 0.8f, 0.5f);
        world.SetPalette(palette);
        return world;
    }

    std::size_t FaceCount(const ChunkMeshData& mesh)
    {
        return mesh.Indices.size() / 6;
    }
}

TEST_CASE("An opaque block facing a transparent one is meshed")
{
    // The riverbed case: stone under water must produce a face, where today the
    // water hides it.
    World world = TransparentPaletteWorld(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1}); // opaque
    world.SetBlock(8, 9, 8, BlockId{2}); // transparent, directly above

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);

    // 6 faces for the opaque block (its top is now exposed) + 5 for the
    // transparent one (its bottom faces the opaque block and is hidden).
    CHECK(FaceCount(mesh) == 11);
}

TEST_CASE("Two touching transparent blocks share no face")
{
    // Internal faces inside a body of water would blend against each other and
    // darken it in bands.
    World world = TransparentPaletteWorld(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{2});
    world.SetBlock(8, 9, 8, BlockId{2});

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);

    CHECK(FaceCount(mesh) == 10); // 12 minus the two touching faces
}

TEST_CASE("A transparent block against air is meshed")
{
    World world = TransparentPaletteWorld(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{2});

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);

    CHECK(FaceCount(mesh) == 6);
}

TEST_CASE("Two touching opaque blocks of different ids share no face")
{
    // The pre-existing rule, restated so the new id comparison cannot break it:
    // different opaque blocks still hide each other.
    World world = TransparentPaletteWorld(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});
    world.SetBlock(8, 9, 8, BlockId{3});

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);

    CHECK(FaceCount(mesh) == 10);
}
```

- [ ] **Step 2: Run the build to verify they fail**

Run the build+test command. Expected: build fails, `Tests.exe` non-zero. `An opaque block facing a transparent one is meshed` reports 10 faces, not 11 — the transparent block still hides the stone's top face.

- [ ] **Step 3: Cache opacity in the neighbourhood**

In `Cubit/src/Voxel/ChunkMesher.cpp`, in the `Neighbourhood` class, add a parallel opacity array. Add the member beside `m_Blocks` and `m_Light`:

```cpp
        bool m_Opaque[Count];
```

In the constructor, both branches of the fill loop must populate it. In the inside-the-chunk branch, after the existing `m_Light[index] = ...` assignment and before the `continue;`:

```cpp
                            m_Opaque[index] = world.IsIdOpaque(m_Blocks[index]);
```

and in the shell branch, after the existing `m_Light[index] = ...` assignment:

```cpp
                        m_Opaque[index] = world.IsIdOpaque(m_Blocks[index]);
```

Add the accessor beside `IsSolid`:

```cpp
        bool IsOpaque(int cell) const { return m_Opaque[cell]; }
```

- [ ] **Step 4: Give `WorldCells` the same accessor**

`WorldCells` is the non-cached source used by the public `CornerAoLevel` / `CornerLightShade`. Add to it:

```cpp
        bool IsOpaque(const glm::ivec3& cell) const
        {
            return Cells.IsBlockOpaque(cell.x, cell.y, cell.z);
        }
```

- [ ] **Step 5: Switch the corner helpers to opacity**

In `CornerAo`, replace the three `cells.IsSolid(...)` calls with `cells.IsOpaque(...)`:

```cpp
        const bool solidA = cells.IsOpaque(airCell + sideA);
        const bool solidB = cells.IsOpaque(airCell + sideB);
```
and
```cpp
        const bool solidCorner = cells.IsOpaque(airCell + sideA + sideB);
```

In `CornerLight`, replace `if (cells.IsSolid(cell))` with:

```cpp
            if (cells.IsOpaque(cell))
```

Water holds light now, so its cells belong in the corner average rather than being skipped.

- [ ] **Step 6: Apply the new face rule**

In `AddExposedFaces`, replace the face loop with:

```cpp
        const BlockId self = cells.Block(blockCell);

        for (int f = 0; f < 6; ++f)
        {
            const int neighbourCell = blockCell + steps[f].Normal;

            // A face is worth drawing when what is beyond it does not hide it,
            // and is not more of the same block: two water cells meet at a face
            // that would only blend against itself.
            if (cells.IsOpaque(neighbourCell) ||
                cells.Block(neighbourCell) == self)
                continue;

            AddFace(mesh, cells, blockCell, blockOrigin,
                Faces[f], steps[f], color);
        }
```

- [ ] **Step 7: Run the build to verify it passes**

Run the build+test command. Expected: `Status: SUCCESS!`, **163 cases**, 0 failed. Every pre-existing mesher and AO test must still pass — with an all-opaque palette the new rule is identical to the old one, which is what those tests are now checking.

- [ ] **Step 8: Commit**

```bash
git add Cubit Tests
git commit -m "Cull mesh faces by opacity rather than solidity"
```

---

### Task 5: Split chunk geometry into opaque and transparent

**Files:**
- Modify: `Cubit/include/Cubit/Voxel/ChunkMesher.h`, `Cubit/src/Voxel/ChunkMesher.cpp`, `Cubit/src/Renderer/WorldRenderer.cpp`
- Test: `Tests/src/ChunkMesherTests.cpp`, `Tests/src/ChunkAoTests.cpp`

**Interfaces:**
- Produces: `struct MeshGeometry { std::vector<VoxelVertex> Vertices; std::vector<std::uint32_t> Indices; };` and `struct ChunkMeshData { MeshGeometry Opaque; MeshGeometry Transparent; };`. Task 6 consumes both fields.

- [ ] **Step 1: Restructure `ChunkMeshData` in the header**

In `Cubit/include/Cubit/Voxel/ChunkMesher.h`, replace the `ChunkMeshData` definition:

```cpp
//One draw's worth of geometry: vertices and the indices that reference them.
struct MeshGeometry
{
    std::vector<VoxelVertex> Vertices;
    std::vector<std::uint32_t> Indices;
};

//A chunk's mesh, split by how its faces have to be drawn. Transparent faces are
//drawn in a second pass, back to front, so they blend against what is behind
//them — which means they cannot share a draw with the opaque ones.
struct ChunkMeshData
{
    MeshGeometry Opaque;
    MeshGeometry Transparent;
};
```

- [ ] **Step 2: Route faces to the right set**

In `Cubit/src/Voxel/ChunkMesher.cpp`:

Change `AddFaceIndices` and `AddFace` to take `MeshGeometry&` instead of `ChunkMeshData&` — the parameter name `mesh` can stay, only the type changes. Their bodies are unchanged.

In `AddExposedFaces`, change the parameter type to `MeshGeometry&` as well.

In `ChunkMesher::Build`, select the target set per block, immediately before the call to `AddExposedFaces`:

```cpp
                // A block's own opacity decides which pass draws it; the faces
                // of one block never span both.
                MeshGeometry& target = cells.IsOpaque(cell)
                    ? mesh.Opaque
                    : mesh.Transparent;

                AddExposedFaces(target, cells, palette, steps, cell,
                    glm::vec3(x, y, z));
```

- [ ] **Step 3: Update `WorldRenderer` to keep compiling**

`WorldRenderer::Update` reads `mesh.Indices` and `mesh.Vertices`. Task 6 gives it a real second pass; for now point it at the opaque set only, so this task builds and the transparent geometry is simply not drawn yet:

In `Cubit/src/Renderer/WorldRenderer.cpp`, in `Update`, replace `mesh.Indices` with `mesh.Opaque.Indices` and `mesh.Vertices` with `mesh.Opaque.Vertices` throughout the function (four references: the `empty()` check, the vertex data pointer, the vertex byte count, and the index data plus count).

- [ ] **Step 4: Rename the test references**

`Tests/src/ChunkMesherTests.cpp` and `Tests/src/ChunkAoTests.cpp` contain **31 references** to `.Vertices` and `.Indices` on a `ChunkMeshData`. Every one becomes `.Opaque.Vertices` / `.Opaque.Indices`.

These tests build only opaque blocks, so their assertions hold unchanged against the `Opaque` set. The four opacity tests added in Task 4 also need updating — including `FaceCount`, whose body becomes:

```cpp
        return mesh.Opaque.Indices.size() / 6;
```

but the three tests involving transparent blocks must now count both sets, since their geometry has moved. Replace those three tests' assertions:

- `An opaque block facing a transparent one is meshed`:
  ```cpp
    CHECK(mesh.Opaque.Indices.size() / 6 == 6);       // the stone block
    CHECK(mesh.Transparent.Indices.size() / 6 == 5);  // the water block
  ```
- `Two touching transparent blocks share no face`:
  ```cpp
    CHECK(mesh.Opaque.Indices.empty());
    CHECK(mesh.Transparent.Indices.size() / 6 == 10);
  ```
- `A transparent block against air is meshed`:
  ```cpp
    CHECK(mesh.Opaque.Indices.empty());
    CHECK(mesh.Transparent.Indices.size() / 6 == 6);
  ```

- [ ] **Step 5: Add a test that the split itself is right**

Append to `Tests/src/ChunkMesherTests.cpp`:

```cpp

TEST_CASE("Faces are split by the opacity of the block they belong to")
{
    World world = TransparentPaletteWorld(1, 1, 1);
    world.SetBlock(2, 2, 2, BlockId{1}); // opaque, isolated
    world.SetBlock(8, 8, 8, BlockId{2}); // transparent, isolated

    const ChunkMeshData mesh = ChunkMesher::Build(world, 0, 0, 0);

    CHECK(mesh.Opaque.Indices.size() / 6 == 6);
    CHECK(mesh.Transparent.Indices.size() / 6 == 6);

    // Each set's indices must address its own vertices, not the other's.
    for (const std::uint32_t index : mesh.Opaque.Indices)
        REQUIRE(index < mesh.Opaque.Vertices.size());
    for (const std::uint32_t index : mesh.Transparent.Indices)
        REQUIRE(index < mesh.Transparent.Vertices.size());
}
```

- [ ] **Step 6: Run the build to verify it passes**

Run the build+test command. Expected: `Status: SUCCESS!`, **164 cases**, 0 failed.

- [ ] **Step 7: Commit**

```bash
git add Cubit Tests
git commit -m "Split chunk meshes into opaque and transparent geometry"
```

---

### Task 6: Draw the transparent pass

**Files:**
- Modify: `Cubit/include/Cubit/Voxel/ChunkMesher.h`, `Cubit/src/Voxel/ChunkMesher.cpp`, `Cubit/include/Cubit/Renderer/Renderer.h`, `Cubit/src/Renderer/Renderer.cpp`, `Cubit/include/Cubit/Renderer/WorldRenderer.h`, `Cubit/src/Renderer/WorldRenderer.cpp`, `Sandbox/src/Sandbox.cpp`

**Interfaces:**
- Consumes: `ChunkMeshData::Opaque` / `::Transparent` from Task 5, `World::GetBlockColor -> glm::vec4` from Task 1.
- Produces: `void Renderer::SetDepthWrite(bool)`, and `WorldRenderer::Render(const Shader&, const glm::mat4& viewProjection, const glm::vec3& worldOffset, const glm::vec3& cameraPosition)`.

This task has no unit tests — it is GL state and draw order, which this project verifies by running the sandbox.

- [ ] **Step 1: Widen the vertex colour**

In `Cubit/include/Cubit/Voxel/ChunkMesher.h`:

```cpp
struct VoxelVertex
{
    glm::vec3 Position{ 0.0f };
    glm::vec4 Color{ 1.0f };
};
```

- [ ] **Step 2: Carry alpha through the mesher**

In `Cubit/src/Voxel/ChunkMesher.cpp`:

In `AddExposedFaces`, undo Task 1's truncation:

```cpp
        const glm::vec4 color = palette[cells.Block(blockCell)];
```

and change the parameter type of `AddFace`'s `blockColor` from `const glm::vec3&` to `const glm::vec4&`.

In `AddFace`, the shading multiplies only the colour channels — alpha must pass through untouched, or shading would make water more transparent in shadow:

```cpp
            // Shading scales the colour channels only. Alpha is the block's
            // opacity and has nothing to do with how lit the face is.
            const glm::vec3 shaded = glm::vec3(blockColor)
                * (ChunkMesher::LightFloor
                    + (1.0f - ChunkMesher::LightFloor) * lit);

            mesh.Vertices.push_back(
                { blockOrigin + face.Corner[i], glm::vec4(shaded, blockColor.a) });
```

- [ ] **Step 3: Add the depth-write toggle**

In `Cubit/include/Cubit/Renderer/Renderer.h`, after `SetDepthTest`:

```cpp
    //Enables or disables writing to the depth buffer. Transparent geometry is
    //drawn with this off so surfaces behind it are not rejected before they
    //blend.
    static void SetDepthWrite(bool enabled);
```

In `Cubit/src/Renderer/Renderer.cpp`, after `SetDepthTest`:

```cpp

void Renderer::SetDepthWrite(bool enabled)
{
    glDepthMask(enabled ? GL_TRUE : GL_FALSE);
}
```

- [ ] **Step 4: Hold two sets of buffers per chunk**

In `Cubit/include/Cubit/Renderer/WorldRenderer.h`, replace the `ChunkMesh` struct and the `Render` declaration:

```cpp
    //One draw's GPU buffers plus its face count.
    struct GpuGeometry
    {
        std::unique_ptr<VertexArray> Array;
        std::unique_ptr<VertexBuffer> Buffer;
        std::unique_ptr<IndexBuffer> Indices;
        std::uint32_t FaceCount = 0;
    };

    //A chunk's geometry, split by pass. Either half may be empty.
    struct ChunkMesh
    {
        GpuGeometry Opaque;
        GpuGeometry Transparent;
    };
```

and

```cpp
    //Draws every non-empty chunk mesh inside the camera frustum, each translated
    //to its chunk origin plus worldOffset. Opaque geometry is drawn first, then
    //transparent geometry back to front with depth writes off, so it blends
    //against what is behind it. cameraPosition must be in the same space as the
    //chunk transforms — that is, worldOffset already applied.
    void Render(const Shader& shader, const glm::mat4& viewProjection,
        const glm::vec3& worldOffset, const glm::vec3& cameraPosition);
```

- [ ] **Step 5: Build both sets of buffers**

In `Cubit/src/Renderer/WorldRenderer.cpp`, replace the body of the per-chunk work in `Update`. Extract a file-local helper above `WorldRenderer::Update`:

```cpp
namespace
{
    //Uploads one geometry set, or leaves it empty when there is nothing to draw.
    void UploadGeometry(WorldRenderer::GpuGeometry& gpu, const MeshGeometry& source)
    {
        if (source.Indices.empty())
            return;

        gpu.Array = std::make_unique<VertexArray>();
        gpu.Buffer = std::make_unique<VertexBuffer>(
            source.Vertices.data(),
            static_cast<std::uint32_t>(source.Vertices.size() * sizeof(VoxelVertex)));
        gpu.Array->AddBuffer(
            *gpu.Buffer,
            BufferLayout{ ShaderDataType::Float3, ShaderDataType::Float4 });
        gpu.Indices = std::make_unique<IndexBuffer>(
            source.Indices.data(),
            static_cast<std::uint32_t>(source.Indices.size()));
        gpu.FaceCount = gpu.Indices->GetCount() / 6;
    }
}
```

`GpuGeometry` must be public for this helper to name it — move the two structs above the `private:` label in the header, or declare the helper as a static member. Moving them to `public:` is the smaller change.

Then in `Update`, replace the `if (mesh.Indices.empty()) ... else ...` block with:

```cpp
        if (mesh.Opaque.Indices.empty() && mesh.Transparent.Indices.empty())
        {
            //A chunk that meshes to nothing keeps no buffers; drop any it had.
            m_Meshes.erase(coord);
        }
        else
        {
            ChunkMesh gpu;
            UploadGeometry(gpu.Opaque, mesh.Opaque);
            UploadGeometry(gpu.Transparent, mesh.Transparent);

            m_Meshes[coord] = std::move(gpu);
        }
```

Note the vertex layout changed to `Float3, Float4`.

- [ ] **Step 6: Draw in two passes**

Replace `WorldRenderer::Render` entirely:

```cpp
void WorldRenderer::Render(const Shader& shader, const glm::mat4& viewProjection,
    const glm::vec3& worldOffset, const glm::vec3& cameraPosition)
{
    const Frustum frustum(viewProjection);
    m_LastDrawnChunks = 0;

    //Transparent chunks are collected rather than drawn as they are found: they
    //have to go out far to near, and the mesh map is ordered by coordinate.
    struct TransparentDraw
    {
        float DistanceSquared;
        const GpuGeometry* Geometry;
        glm::vec3 Origin;
    };
    std::vector<TransparentDraw> transparent;

    for (const auto& entry : m_Meshes)
    {
        const glm::ivec3& coord = entry.first;
        const ChunkMesh& mesh = entry.second;

        const glm::vec3 origin =
            glm::vec3(World::GetChunkOrigin(coord.x, coord.y, coord.z));
        const glm::vec3 min = worldOffset + origin;
        const glm::vec3 extent(Chunk::Width, Chunk::Height, Chunk::Depth);

        if (!frustum.IntersectsAABB(min, min + extent))
            continue; // outside the view

        if (mesh.Opaque.Indices != nullptr)
        {
            const glm::mat4 transform = glm::translate(glm::mat4(1.0f), min);
            Renderer::Submit(*mesh.Opaque.Array, *mesh.Opaque.Indices,
                shader, transform);
            ++m_LastDrawnChunks;
        }

        if (mesh.Transparent.Indices != nullptr)
        {
            const glm::vec3 centre = min + extent * 0.5f;
            const glm::vec3 toCamera = centre - cameraPosition;

            transparent.push_back({ glm::dot(toCamera, toCamera),
                &mesh.Transparent, min });
        }
    }

    if (transparent.empty())
        return;

    // Farthest first: a near surface has to blend over what is already behind
    // it, so what is behind has to be on screen first.
    std::sort(transparent.begin(), transparent.end(),
        [](const TransparentDraw& a, const TransparentDraw& b)
        {
            return a.DistanceSquared > b.DistanceSquared;
        });

    // Depth testing stays on so terrain in front still hides water, but writing
    // is off so two water surfaces do not reject each other.
    Renderer::SetDepthWrite(false);

    for (const TransparentDraw& draw : transparent)
    {
        const glm::mat4 transform = glm::translate(glm::mat4(1.0f), draw.Origin);
        Renderer::Submit(*draw.Geometry->Array, *draw.Geometry->Indices,
            shader, transform);
        ++m_LastDrawnChunks;
    }

    Renderer::SetDepthWrite(true);
}
```

Add `#include <algorithm>` and `#include <vector>` to the file's include block.

- [ ] **Step 7: Sum face counts across both sets**

Replace `WorldRenderer::TotalFaceCount`:

```cpp
std::uint32_t WorldRenderer::TotalFaceCount() const
{
    std::uint32_t total = 0;
    for (const auto& entry : m_Meshes)
        total += entry.second.Opaque.FaceCount + entry.second.Transparent.FaceCount;

    return total;
}
```

- [ ] **Step 8: Update the sandbox shader and call site**

In `Sandbox/src/Sandbox.cpp`, in the vertex shader source, change the colour attribute and varying to `vec4`:

```glsl
            layout(location = 1) in vec4 a_Color;
            uniform mat4 u_ViewProjection;
            uniform mat4 u_Transform;
            out vec4 v_Color;
```

In the fragment shader source:

```glsl
            in vec4 v_Color;

            void main()
            {
                color = v_Color;
            }
```

And in `OnRender`, pass the camera position:

```cpp
        m_WorldRenderer.Render(
            *m_Shader,
            m_CameraController.GetCamera().GetViewProjectionMatrix(),
            WorldOffset,
            m_CameraController.GetCamera().GetPosition());
```

- [ ] **Step 9: Build both projects**

Run the build+test command for `Tests/Tests.vcxproj`. Expected: `Status: SUCCESS!`, **164 cases**, 0 failed.

Then build the sandbox:
```bash
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Sandbox/Sandbox.vcxproj -p:Configuration=Debug -p:Platform=x64
```
Expected: succeeds with no warnings from `Sandbox.cpp`.

At this point nothing looks different — the map's water is still fully opaque, so the transparent set is empty. Task 7 is what makes it visible.

- [ ] **Step 10: Commit**

```bash
git add Cubit Sandbox
git commit -m "Draw transparent chunk geometry in a sorted second pass"
```

---

### Task 7: Make the water transparent

**Files:**
- Modify: `Cubit/src/Voxel/TerrainGen.cpp`, `Sandbox/assets/maps/battlefield.vox`, `docs/engine-roadmap.md`, `README.md`
- Test: `Tests/src/TerrainGenTests.cpp`

- [ ] **Step 1: Write the failing test**

Append to `Tests/src/TerrainGenTests.cpp`:

```cpp

TEST_CASE("Water is the only transparent entry in the map palette")
{
    const Palette palette = TerrainGen::MapPalette();

    CHECK(palette[MapBlocks::Water].a < 1.0f);

    // Everything else must stay opaque, or terrain would blend against itself.
    for (std::size_t i = 1; i < palette.size(); ++i)
        if (i != MapBlocks::Water)
            REQUIRE(palette[i].a >= 1.0f);
}
```

- [ ] **Step 2: Run the build to verify it fails**

Run the build+test command. Expected: build fails; `Water is the only transparent entry in the map palette` FAILS because water's alpha is still 1.0.

- [ ] **Step 3: Give water its alpha**

In `Cubit/src/Voxel/TerrainGen.cpp`, in `MapPalette`, change the water line only:

```cpp
    //Transparent, so the riverbed shows through. Everything else stays opaque.
    p[Water]       = glm::vec4(glm::vec3(55, 110, 200) / 255.0f, 0.55f);
```

- [ ] **Step 4: Run the build to verify it passes**

Run the build+test command. Expected: `Status: SUCCESS!`, **165 cases**, 0 failed.

- [ ] **Step 5: Regenerate the map**

Build and run `MapGen`, writing over the committed asset:

```bash
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" MapGen/MapGen.vcxproj -p:Configuration=Debug -p:Platform=x64
./bin/Debug-windows-x86_64/MapGen/MapGen.exe "C:/dev/Cubit/Sandbox/assets/maps/battlefield.vox"
```

Expected: prints `Wrote ... (5810064 bytes)`. The byte count should be unchanged — only one palette byte differs, and the palette is a fixed-size block.

Confirm the alpha actually landed:
```bash
tail -c 1024 Sandbox/assets/maps/battlefield.vox | od -An -tu1 -v | awk '{for(i=1;i<=NF;i++)b[n++]=$i} END{printf "water RGBA = %d %d %d %d\n", b[24],b[25],b[26],b[27]}'
```
Expected: `water RGBA = 55 110 200 140` (0.55 × 255 rounds to 140).

- [ ] **Step 6: Rebuild the sandbox so the new asset is copied**

```bash
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Sandbox/Sandbox.vcxproj -p:Configuration=Debug -p:Platform=x64
```

- [ ] **Step 7: Commit**

```bash
git add Cubit Tests Sandbox/assets/maps/battlefield.vox
git commit -m "Make the river water transparent"
```

- [ ] **Step 8: Update the docs**

In `docs/engine-roadmap.md`, mark item 5 under *Visual quality* done, matching the strikethrough format items 1, 2 and 4 already use:

```markdown
5. ~~**Transparency / alpha blending**~~ **DONE 2026-07-31** — palette alpha marks
   a block non-opaque; the mesher splits chunk geometry and the renderer draws the
   transparent set back to front with depth writes off. Water is see-through and
   its bed is lit. Water is still *solid* — collision and raycast are Phase 2.
```

In the same file's *"finish the engine" arc*, replace item 3 with:

```markdown
3. ~~**Transparency**~~ **DONE 2026-07-31** (opacity; solidity is Phase 2).
```

and amend the arc so greedy meshing is next.

In `README.md`, under *What's next*, remove the `**Transparency**` bullet, leaving greedy meshing first. Add to the *Rendering* list in *What works*:

```markdown
- Two-pass drawing: opaque geometry first, then transparent geometry sorted back
  to front with depth writes off, so water blends over the riverbed beneath it
```

```bash
git add docs/engine-roadmap.md README.md
git commit -m "Record transparency phase 1 in the roadmap and readme"
```

---

## Manual verification (human, after the plan is complete)

Not an agent step — an agent cannot drive the GUI.

```bash
cd bin/Debug-windows-x86_64/Sandbox && ./Sandbox.exe
```

Walk to the river down the middle of the map and check:

1. The water surface is see-through, not a flat blue slab.
2. The riverbed is visible through it, and **lit** — not a dark void.
3. Terrain standing between you and the river still hides it (depth testing is on).
4. Looking along the river from either direction, the water does not show uneven
   darkening where surfaces overlap — that is the back-to-front sort working.
5. Break a block underwater and confirm the chunk remeshes correctly.

Close via the window's close button so the log survives.

---

## Self-Review

**Spec coverage:**

| Spec section | Task |
|---|---|
| `Palette` → vec4, alpha ≥ 1.0 is opaque | 1 |
| `DefaultPalette` alphas, air alpha 0 | 1 |
| `GetBlockColor` → vec4 | 1 |
| Loader reads alpha, writer writes it | 1 |
| Existing maps unaffected | 1 (writer/loader round-trip tests), 7 (asset regenerated) |
| `World` opacity table, `IsIdOpaque`, `IsBlockOpaque` | 2 |
| Out of bounds is not opaque | 2 |
| Sky light: three predicates | 3 |
| `Repropagate` untouched | 3 (Step 3 states it explicitly) |
| Meshing rule, all six table rows | 4 |
| AO and corner light by opacity | 4 |
| `Neighbourhood` caches opacity | 4 |
| `MeshGeometry` / `ChunkMeshData` split | 5 |
| 31 test references renamed | 5 |
| `VoxelVertex::Color` → vec4, alpha unshaded | 6 |
| `Renderer::SetDepthWrite` | 6 |
| Two passes, frustum culled, sorted back to front | 6 |
| `Render` gains camera position, same space as transforms | 6 |
| Chunk dropped only when both sets empty | 6 (Step 5) |
| `TotalFaceCount` sums both | 6 |
| Sandbox shader takes vec4 | 6 |
| Water alpha 0.55, map regenerated | 7 |
| Rendering verified by running the sandbox | Manual verification |

No gaps. The spec's *Out of scope* items (solidity, per-block absorption, face-level sorting, other transparent blocks) appear in no task.

**Placeholder scan:** every code step carries the literal code. The one bulk instruction — Task 5 Step 4's 31 renames — states the exact mechanical transformation, names both files, and spells out the three tests whose assertions genuinely change rather than leaving them to judgement.

**Type consistency:** `Palette` is `std::array<glm::vec4, 256>` from Task 1 onward and every later task assumes it. `MeshGeometry`/`ChunkMeshData` are introduced in Task 5 and consumed in Task 6 with matching field names. `GpuGeometry` is named identically in the header (Task 6 Step 4) and the upload helper (Step 5). `World::IsIdOpaque(BlockId)` and `World::IsBlockOpaque(int,int,int)` are declared in Task 2 and called with those exact signatures in Tasks 3 and 4. `Render`'s four-argument form is declared in Task 6 Step 4 and called with four arguments in Step 8.

**Test-count arithmetic:** 150 start → 152 (Task 1, +2) → 157 (Task 2, +5) → 159 (Task 3, +2) → 163 (Task 4, +4) → 164 (Task 5, +1) → 164 (Task 6, +0) → 165 (Task 7, +1).
