# Transparency Phase 2 (Solidity) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make water non-solid so the player falls through it and stands on the riverbed, while clicking water still breaks it.

**Architecture:** Three properties currently hide under two names. Task 1 splits *present* (`block != 0`, palette-blind) out of `IsSolid` as a pure rename with no behaviour change. Task 2 adds a *solid* predicate on `World` backed by the existing `m_Opaque` table, since both derive from palette alpha. Tasks 3–5 point each consumer at the predicate it actually means.

**Tech Stack:** C++20, GLM, doctest, premake5 + MSBuild (Visual Studio 18), OpenGL 3.3.

Design spec: [`docs/superpowers/specs/2026-08-08-transparency-solidity-design.md`](../specs/2026-08-08-transparency-solidity-design.md)

## Global Constraints

- **Build (also runs the test suite as a post-build step):**
  `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64`
  Use **forward slashes** in the `.vcxproj` path — backslashes fail with MSB1009.
- **Run one test case:** `./bin/Debug-windows-x86_64/Tests/Tests.exe -tc="<case name>"`
- **Run the whole suite:** `./bin/Debug-windows-x86_64/Tests/Tests.exe`
- Do **not** run `GenerateProjects.bat` — it deletes `bin/` and ends in a blocking `pause`. No task here adds a source file, so premake never needs re-running.
- Comment style: `//` with no space before the text, sentences explaining *why*, matching surrounding code.
- Commit after every task. Do not add Claude co-author trailers or attribution.
- Solidity is derived from palette alpha and **shares** the `m_Opaque` table. Do not add an `m_Solid` array.
- No swimming, buoyancy, drag, or new movement state in the Sandbox.

---

### Task 1: Split `present` out of `IsSolid`

A pure rename across the engine and tests, plus deletion of one dead function. **No behaviour changes.** `World`'s constructor installs `DefaultPalette()`, which is alpha `1.0` for every id except air, so nothing observable moves — the existing suite is the proof.

This task must land as one commit: renaming a function used in 13 files cannot be split without leaving the build broken in between.

**Files:**
- Modify: `Cubit/include/Cubit/Voxel/Block.h:23-27`
- Modify: `Cubit/include/Cubit/Voxel/Chunk.h:32-33`
- Modify: `Cubit/src/Voxel/Chunk.cpp:31-34`
- Modify: `Cubit/include/Cubit/Voxel/World.h:70-71`
- Modify: `Cubit/src/Voxel/World.cpp:137-140`
- Modify: `Cubit/src/Voxel/Neighbourhood.h:89`
- Modify: `Cubit/src/Voxel/ChunkMesher.cpp:16-42,315`
- Modify: `Cubit/src/Voxel/TerrainGen.cpp:155`
- Modify: `Cubit/src/Voxel/VoxelRaycast.cpp:96`
- Modify: `Cubit/src/Voxel/VoxelCollision.cpp:33`
- Modify: `Cubit/src/Voxel/SkyLight.cpp:212`
- Test: `Tests/src/BuildWorldTests.cpp:47`, `Tests/src/ChunkMesherTests.cpp:38,42`, `Tests/src/ChunkTests.cpp:35,46-49`, `Tests/src/TerrainGenTests.cpp:42,43,62,66,77`, `Tests/src/VoxelRaycastTests.cpp:107,256,259`, `Tests/src/WorldTests.cpp:56`

**Interfaces:**
- Consumes: nothing.
- Produces: `constexpr bool IsPresent(BlockId)`; `bool Chunk::IsBlockPresent(int,int,int) const`; `bool World::IsBlockPresent(int,int,int) const`; `bool Neighbourhood::IsPresent(int) const`. `IsSolid` and `IsBlockSolid` cease to exist until Task 2 reintroduces `IsBlockSolid` with different meaning.

- [ ] **Step 1: Rename the predicate in `Block.h`**

Replace lines 23-27 of `Cubit/include/Cubit/Voxel/Block.h`:

```cpp
//Reports whether a block occupies a cell at all, as opposed to being air.
//Palette-blind on purpose: whether that block stops light or stops movement
//are questions only the world can answer, because only the world holds the
//palette those answers come from.
constexpr bool IsPresent(BlockId block)
{
    return block != 0;
}
```

- [ ] **Step 2: Rename `Chunk::IsBlockSolid`**

In `Cubit/include/Cubit/Voxel/Chunk.h`, replace lines 32-33:

```cpp
    //Reports whether a block occupies this position at all. A chunk holds no
    //palette, so presence is the only one of the three block properties it can
    //answer; solidity and opacity live on World.
    bool IsBlockPresent(int x, int y, int z) const;
```

In `Cubit/src/Voxel/Chunk.cpp`, replace lines 31-34:

```cpp
bool Chunk::IsBlockPresent(int x, int y, int z) const
{
    return IsPresent(GetBlock(x, y, z));
}
```

- [ ] **Step 3: Rename `World::IsBlockSolid`**

In `Cubit/include/Cubit/Voxel/World.h`, replace lines 70-71:

```cpp
    //Reports whether a block occupies this position at all.
    bool IsBlockPresent(int x, int y, int z) const;
```

In `Cubit/src/Voxel/World.cpp`, replace lines 137-140:

```cpp
bool World::IsBlockPresent(int x, int y, int z) const
{
    return IsPresent(GetBlock(x, y, z));
}
```

- [ ] **Step 4: Rename in `Neighbourhood.h` and delete the dead `WorldCells::IsSolid`**

In `Cubit/src/Voxel/Neighbourhood.h`, replace line 89:

```cpp
    bool IsPresent(int cell) const { return ::IsPresent(m_Blocks[cell]); }
```

In `Cubit/src/Voxel/ChunkMesher.cpp`, delete the whole `IsSolid` member from `WorldCells` (lines 22-25 plus its trailing blank line). It has no callers — `CornerAo` and `CornerLight` are the only users of `WorldCells` and both ask `IsOpaque`. It is a phase 1 leftover.

`WorldCells` becomes:

```cpp
    //Reads straight from the world, for the callers that hand one over rather
    //than meshing a whole chunk.
    struct WorldCells
    {
        const World& Cells;

        bool IsOpaque(const glm::ivec3& cell) const
        {
            return Cells.IsBlockOpaque(cell.x, cell.y, cell.z);
        }

        int Light(const glm::ivec3& cell) const
        {
            return Cells.GetSkyLight(cell.x, cell.y, cell.z);
        }
    };
```

Fix the now-stale comment on line 39 (it names a function that no longer exists) — in the `CornerAo` doc comment, change `IsSolid and Light` to `IsOpaque and Light`.

Then update the call site at line 315:

```cpp
                if (!cells.IsPresent(cell))
                    continue;
```

- [ ] **Step 5: Rename the three remaining engine call sites**

`Cubit/src/Voxel/TerrainGen.cpp:155` — `IsSolid(Get(m, x, top, z))` becomes `IsPresent(Get(m, x, top, z))`.

`Cubit/src/Voxel/VoxelRaycast.cpp:96` — becomes:

```cpp
        if (world.IsBlockPresent(voxel.x, voxel.y, voxel.z))
```

`Cubit/src/Voxel/VoxelCollision.cpp:33` — becomes `world.IsBlockPresent(x, y, z)`. **Still presence at this stage** — Task 3 is what changes collision's meaning. Keeping it presence here is what makes this task a no-op.

`Cubit/src/Voxel/SkyLight.cpp:212` — becomes `world.IsBlockPresent(x, y, z)`. Also unchanged in meaning; Task 4 fixes it.

- [ ] **Step 6: Rename in the six test files**

Mechanical, assertions unchanged:

- `Tests/src/BuildWorldTests.cpp:47` — `world.IsBlockSolid` → `world.IsBlockPresent`
- `Tests/src/ChunkMesherTests.cpp:38,42` — `world.IsBlockSolid` → `world.IsBlockPresent`
- `Tests/src/ChunkTests.cpp:35,46,47,48,49` — `chunk.IsBlockSolid` → `chunk.IsBlockPresent`
- `Tests/src/TerrainGenTests.cpp:42,43,62,66,77` — `IsSolid(` → `IsPresent(`
- `Tests/src/VoxelRaycastTests.cpp:107,256,259` — `world.IsBlockSolid` → `world.IsBlockPresent`
- `Tests/src/WorldTests.cpp:56` — `world.IsBlockSolid` → `world.IsBlockPresent`

- [ ] **Step 7: Confirm no `IsSolid` survives**

Run: `grep -rn "IsSolid\|IsBlockSolid" Cubit Sandbox Tests MapGen`
Expected: **no matches.** A hit means a call site was missed and the build will fail.

- [ ] **Step 8: Build and run the whole suite**

Run: `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64`
Expected: builds clean, all 165 existing tests pass. Any failure here is a rename mistake, not a design problem — this task changes no behaviour.

- [ ] **Step 9: Commit**

```bash
git add -A
git commit -m "Split block presence out of solidity

IsSolid answered two questions: is there a block here, and does it stop
you. Only the first is palette-blind, so it keeps the free function and
becomes IsPresent. Nothing changes behaviour: the default palette is
opaque for every id but air, so presence and solidity still coincide."
```

---

### Task 2: Give `World` a solidity predicate

Adds the predicate with no callers, so again nothing changes behaviour. Solidity reads the opacity table rather than a second array — both derive from alpha, so a second array could only drift.

**Files:**
- Modify: `Cubit/include/Cubit/Voxel/World.h` (after the `IsIdOpaque`/`IsBlockOpaque` declarations, ~lines 63-68)
- Modify: `Cubit/src/Voxel/World.cpp` (beside `IsBlockOpaque`, ~line 150)
- Test: `Tests/src/WorldTests.cpp` (append after the opacity cases, ~line 257)

**Interfaces:**
- Consumes: `World::IsIdOpaque`, `World::IsBlockPresent` from Task 1.
- Produces: `bool World::IsIdSolid(BlockId) const`; `bool World::IsBlockSolid(int,int,int) const`. Tasks 3 and 5 consume these.

- [ ] **Step 1: Write the failing tests**

Append to `Tests/src/WorldTests.cpp`:

```cpp
TEST_CASE("A block is solid when its palette alpha is full")
{
    // Solidity is derived from alpha exactly as opacity is, so a transparent
    // block is something you can walk through as well as see through.
    World world(1, 1, 1);
    Palette palette = DefaultPalette();
    palette[4] = glm::vec4(0.2f, 0.4f, 0.6f, 1.0f);
    palette[5] = glm::vec4(0.2f, 0.4f, 0.6f, 0.5f);
    world.SetPalette(palette);

    CHECK(world.IsIdSolid(BlockId{4}));
    CHECK_FALSE(world.IsIdSolid(BlockId{5}));
}

TEST_CASE("Air is never solid")
{
    const World world(1, 1, 1);

    CHECK_FALSE(world.IsIdSolid(BlockId{0}));
}

TEST_CASE("A transparent block is present but not solid")
{
    // The whole point of the phase: presence and solidity are different
    // questions, and water is the block that answers them differently.
    World world(1, 1, 1);
    Palette palette = DefaultPalette();
    palette[5] = glm::vec4(0.2f, 0.4f, 0.6f, 0.5f);
    world.SetPalette(palette);

    world.SetBlock(8, 8, 8, BlockId{5});

    CHECK(world.IsBlockPresent(8, 8, 8));
    CHECK_FALSE(world.IsBlockSolid(8, 8, 8));
}

TEST_CASE("Positions outside the world are not solid")
{
    // Matching GetBlock returning air: the space around a map must not read as
    // a wall the player can stand on.
    const World world(1, 1, 1);

    CHECK_FALSE(world.IsBlockSolid(-1, 0, 0));
    CHECK_FALSE(world.IsBlockSolid(0, world.GetHeight(), 0));
}

TEST_CASE("Replacing the palette updates solidity")
{
    // The table is derived, so it must not go stale when a map installs its own
    // palette — a world loaded from a map has to know its water is walkable.
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{4});
    REQUIRE(world.IsBlockSolid(8, 8, 8));

    Palette palette = DefaultPalette();
    palette[4] = glm::vec4(0.2f, 0.4f, 0.6f, 0.25f);
    world.SetPalette(palette);

    CHECK_FALSE(world.IsBlockSolid(8, 8, 8));
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64`
Expected: **compile error** — `'IsIdSolid': is not a member of 'World'`. That is the correct failure for a missing method in C++; there is no runtime red phase to reach.

- [ ] **Step 3: Declare the predicates**

In `Cubit/include/Cubit/Voxel/World.h`, immediately after the `IsBlockOpaque` declaration (~line 68), add:

```cpp
    //Reports whether a block id occupies space and stops a moving box.
    //
    //Backed by the same table as IsIdOpaque: both properties are derived from
    //palette alpha, so a separate array would hold identical values on all 256
    //entries and could only drift. The names stay apart because call sites
    //should say which question they are asking — and because giving solidity
    //its own source later (glass: see-through but solid) then changes only how
    //the table is filled, not a single caller.
    bool IsIdSolid(BlockId block) const { return m_Opaque[block]; }

    //Reports whether the block at this position stops a moving box. Positions
    //outside the world are not solid, matching GetBlock reading as air.
    bool IsBlockSolid(int x, int y, int z) const;
```

- [ ] **Step 4: Define `IsBlockSolid`**

In `Cubit/src/Voxel/World.cpp`, directly after the `IsBlockOpaque` definition:

```cpp
bool World::IsBlockSolid(int x, int y, int z) const
{
    return IsIdSolid(GetBlock(x, y, z));
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `./bin/Debug-windows-x86_64/Tests/Tests.exe -tc="A block is solid when its palette alpha is full,Air is never solid,A transparent block is present but not solid,Positions outside the world are not solid,Replacing the palette updates solidity"`
Expected: all 5 PASS.

Then the whole suite: `./bin/Debug-windows-x86_64/Tests/Tests.exe`
Expected: all pass — nothing calls the new predicate yet.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "Add a solidity predicate derived from palette alpha

Shares the opacity table rather than duplicating it: both properties
read the same alpha, so two arrays could only drift apart. The names
stay separate so call sites state which question they ask."
```

---

### Task 3: Collision asks solidity

The behaviour change. One line in the engine; the test is where the work is.

**Files:**
- Modify: `Cubit/src/Voxel/VoxelCollision.cpp:20-37`
- Test: `Tests/src/VoxelCollisionTests.cpp` (append)

**Interfaces:**
- Consumes: `World::IsBlockSolid` from Task 2.
- Produces: no new API. `VoxelCollision::MoveBox` and `Overlaps` keep their signatures and change meaning.

- [ ] **Step 1: Write the failing tests**

Append to `Tests/src/VoxelCollisionTests.cpp`. Note the file already has an `AddFloor` helper in its anonymous namespace — these cases reuse it.

```cpp
TEST_CASE("A falling box passes through water and lands on the bed")
{
    // The riverbed is what holds the player up, not the surface above it. Two
    // layers of water over a floor mirrors the shipped map's river depth.
    World world(1, 1, 1);
    Palette palette = DefaultPalette();
    palette[7] = glm::vec4(0.2f, 0.4f, 0.8f, 0.55f); // water
    world.SetPalette(palette);

    AddFloor(world);
    for (int z = 0; z < Chunk::Depth; ++z)
        for (int x = 0; x < Chunk::Width; ++x)
        {
            world.SetBlock(x, 1, z, BlockId{7});
            world.SetBlock(x, 2, z, BlockId{7});
        }

    const VoxelMoveResult result = VoxelCollision::MoveBox(
        world,
        glm::vec3(8.0f, 6.0f, 8.0f),
        PlayerHalfExtents,
        glm::vec3(0.0f, -10.0f, 0.0f));

    // The floor block spans y 0 to 1, so a box resting on it sits one half
    // extent above y=1 — the same result as landing on bare floor.
    CHECK(result.Position.y == doctest::Approx(1.0f + PlayerHalfExtents.y));
    CHECK(result.Grounded);
}

TEST_CASE("A box does not overlap water it is standing in")
{
    // LiftPlayerClearOfTerrain steps the player up while Overlaps is true, so a
    // box submerged in water must read as clear or a reload would launch the
    // player out of the river.
    World world(1, 1, 1);
    Palette palette = DefaultPalette();
    palette[7] = glm::vec4(0.2f, 0.4f, 0.8f, 0.55f);
    world.SetPalette(palette);

    world.SetBlock(8, 8, 8, BlockId{7});

    CHECK_FALSE(VoxelCollision::Overlaps(
        world, glm::vec3(8.5f, 8.5f, 8.5f), PlayerHalfExtents));
}

TEST_CASE("An opaque block still stops a box after the solidity change")
{
    // The companion case: switching the predicate must not make ordinary
    // terrain stop blocking movement.
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});

    CHECK(VoxelCollision::Overlaps(
        world, glm::vec3(8.5f, 8.5f, 8.5f), PlayerHalfExtents));
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64`

Expected: the suite builds, and the first two new cases FAIL:
- "A falling box passes through water and lands on the bed" — box stops on the water at `2.0 + halfExtent` instead of `1.0 + halfExtent`.
- "A box does not overlap water it is standing in" — `Overlaps` returns true.
- "An opaque block still stops a box after the solidity change" — already PASSES. That is expected and correct; it is a regression guard, not a red test.

- [ ] **Step 3: Point collision at solidity**

In `Cubit/src/Voxel/VoxelCollision.cpp`, update the helper's comment and its predicate (lines 20-21 and 33):

```cpp
    //Reports whether the box spanning these corners covers any solid block.
    //Solidity rather than presence: water is a block you can see and break but
    //walk straight through.
    bool OverlapsSolid(const World& world, const glm::vec3& min, const glm::vec3& max)
```

and inside the loop:

```cpp
                    if (world.IsBlockSolid(x, y, z))
                        return true;
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `./bin/Debug-windows-x86_64/Tests/Tests.exe -tc="A falling box passes through water and lands on the bed,A box does not overlap water it is standing in,An opaque block still stops a box after the solidity change"`
Expected: all 3 PASS.

Then the whole suite: `./bin/Debug-windows-x86_64/Tests/Tests.exe`
Expected: all pass. Existing collision tests use the default palette, where every non-air id is alpha 1.0 and therefore still solid.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "Let a moving box pass through non-solid blocks

Collision asked whether a block was present; it now asks whether it is
solid, so water stops holding the player up and the riverbed is what
they land on."
```

---

### Task 4: Fix the sky light straggler

A phase 1 defect: `Repropagate` decides whether an edit filled a cell in or opened it up by asking solidity, while the three other lighting sites moved to opacity. Placing a transparent block therefore takes the *unflood* path and takes back light that still passes through it.

**Files:**
- Modify: `Cubit/src/Voxel/SkyLight.cpp:205-217`
- Test: `Tests/src/SkyLightTests.cpp` (append)

**Interfaces:**
- Consumes: `World::IsBlockOpaque` (already exists from phase 1).
- Produces: no new API.

- [ ] **Step 1: Write the failing test**

Append to `Tests/src/SkyLightTests.cpp`:

```cpp
TEST_CASE("Placing a transparent block does not darken the column below it")
{
    // Repropagate has to ask the same question the flood asks. A transparent
    // block does not stop light, so placing one must not take back the light
    // beneath it — and because full-strength light falls for free, getting this
    // wrong darkens the whole column rather than a single cell.
    World world(1, 4, 1);
    Palette palette = DefaultPalette();
    palette[2] = glm::vec4(0.2f, 0.4f, 0.8f, 0.5f); // transparent
    world.SetPalette(palette);

    SkyLight::PropagateAll(world);

    const int placed = 20;
    REQUIRE(world.GetSkyLight(8, placed - 1, 8) == SkyLight::Max);

    world.SetBlock(8, placed, 8, BlockId{2});
    SkyLight::Repropagate(world, 8, placed, 8);

    CHECK(world.GetSkyLight(8, placed, 8) == SkyLight::Max);
    CHECK(world.GetSkyLight(8, placed - 1, 8) == SkyLight::Max);
    CHECK(world.GetSkyLight(8, 0, 8) == SkyLight::Max);
}

TEST_CASE("Placing an opaque block still darkens the column below it")
{
    // The companion case: the fix must not stop ordinary blocks casting shadow.
    World world(1, 4, 1);

    SkyLight::PropagateAll(world);

    const int placed = 20;
    world.SetBlock(8, placed, 8, BlockId{1});
    SkyLight::Repropagate(world, 8, placed, 8);

    CHECK(world.GetSkyLight(8, placed - 1, 8) < SkyLight::Max);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64`

Expected: both cases PASS immediately.

**Corrected 2026-08-08 during execution — this plan originally predicted the transparent case would fail here. It does not, and the reason matters.** At a mid-column depth the buggy `IsBlockPresent` branch calls `Unflood`, which clears the origin and the column below it — but the cell *above* the origin survives into the `readd` queue at `Max`, and `Flood` pushes straight back down through the transparent block at full strength (`SkyLight.cpp:105-107`, full-strength light falls without dimming). Every value is restored, so the buggy and fixed paths converge and no assertion can tell them apart.

The two branches diverge only on the **top layer**, `y == world.GetHeight() - 1`, where the origin has no cell above it to refill from and only lateral neighbours reach it, paying a level: **14 instead of 15**. That is exactly why the correct `else` branch carries its `y == world.GetHeight() - 1` special case (`SkyLight.cpp:224-228`).

Both cases above are therefore characterisation tests. They are kept because they would catch a future change that made transparent blocks attenuate light, but **Step 3a is the case that actually pins this fix.**

- [ ] **Step 3: Ask opacity instead of presence**

In `Cubit/src/Voxel/SkyLight.cpp`, replace line 212's condition:

```cpp
    if (world.IsBlockOpaque(x, y, z))
```

and update the comment just below it so it says what the branch now tests:

```cpp
    {
        // The edit put something light-stopping here. Take back the light it
        // used to give, keeping whatever still-lit cells border the darkened
        // region.
        Unflood(world, recorder, edit, queue);
    }
    else
    {
        // The edit left this cell passable to light — either it opened it up,
        // or what it placed is transparent. A cell at the very top is open sky
        // and lights itself; anywhere else, the surrounding cells fill it in.
```

- [ ] **Step 3a: Add the case that pins the fix**

Added 2026-08-08 during execution, for the reason recorded under Step 2. Append to `Tests/src/SkyLightTests.cpp`:

```cpp
TEST_CASE("Placing a transparent block on the top layer keeps it at full strength")
{
    // The case that separates the two branches. Anywhere lower, an unflood is
    // undone by light falling back down the column for free, so the bug hides.
    // At the top there is nothing above to refill from, and only lateral
    // neighbours reach the cell — and they pay a level to get there.
    World world(1, 4, 1);
    Palette palette = DefaultPalette();
    palette[2] = glm::vec4(0.2f, 0.4f, 0.8f, 0.5f); // transparent
    world.SetPalette(palette);

    SkyLight::PropagateAll(world);

    const int top = world.GetHeight() - 1;
    world.SetBlock(8, top, 8, BlockId{2});
    SkyLight::Repropagate(world, 8, top, 8);

    CHECK(world.GetSkyLight(8, top, 8) == SkyLight::Max);
    CHECK(world.GetSkyLight(8, top - 1, 8) == SkyLight::Max);
}
```

Verify it pins the fix: temporarily restore `world.IsBlockPresent(x, y, z)` at `SkyLight.cpp:212`, build, and confirm this case FAILS reporting **14** where it wants 15. Restore `IsBlockOpaque` and confirm it passes. Do not commit the temporary revert.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `./bin/Debug-windows-x86_64/Tests/Tests.exe -tc="Placing a transparent block does not darken the column below it,Placing an opaque block still darkens the column below it,Placing a transparent block on the top layer keeps it at full strength"`
Expected: all three PASS.

Then the whole suite: `./bin/Debug-windows-x86_64/Tests/Tests.exe`
Expected: all pass. The existing `SkyLight` cases all edit opaque blocks, where opacity and presence agree.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "Relight around a transparent edit without unflooding it

Repropagate still asked solidity while the rest of the flood moved to
opacity in phase 1, so placing a see-through block took back light that
passes straight through it."
```

---

### Task 5: Pin the raycast divergence and fix the Sandbox comment

Collision and the editing raycast now deliberately disagree: you pass through water but can still click it. Nothing in the code changes here — Task 1 already left the raycast on presence — but that divergence is load-bearing and undefended, so it gets a test that fails if someone "tidies" the raycast onto solidity.

**Files:**
- Test: `Tests/src/VoxelRaycastTests.cpp` (append)
- Modify: `Sandbox/src/Sandbox.cpp:306-312` (comment only)

**Interfaces:**
- Consumes: `World::IsBlockPresent` (Task 1), `World::IsBlockSolid` (Task 2).
- Produces: no new API.

- [ ] **Step 1: Write the test**

Append to `Tests/src/VoxelRaycastTests.cpp`:

```cpp
TEST_CASE("A ray stops at water rather than passing through it")
{
    // Deliberately different from collision, which passes through. Water is
    // placeable with key 7, so a ray that skipped it would hand the player a
    // block they can create and never break.
    World world(1, 1, 1);
    Palette palette = DefaultPalette();
    palette[7] = glm::vec4(0.2f, 0.4f, 0.8f, 0.55f); // water
    world.SetPalette(palette);

    world.SetBlock(8, 8, 8, BlockId{7});  // water
    world.SetBlock(8, 8, 12, BlockId{1}); // opaque, further along the ray

    const VoxelRayHit hit = VoxelRaycast::Cast(
        world,
        glm::vec3(8.5f, 8.5f, 4.5f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        16.0f);

    REQUIRE(hit.Hit);
    CHECK(hit.Block == glm::ivec3(8, 8, 8));
    CHECK_FALSE(world.IsBlockSolid(hit.Block.x, hit.Block.y, hit.Block.z));
}
```

- [ ] **Step 2: Run the test to verify it passes**

Run: `./bin/Debug-windows-x86_64/Tests/Tests.exe -tc="A ray stops at water rather than passing through it"`
Expected: PASS immediately. This test has no red phase — it pins behaviour Task 1 preserved. If it fails, the raycast was wrongly moved onto solidity in Task 1 and that is the bug to fix.

- [ ] **Step 3: Correct the Sandbox comment**

`LiftPlayerClearOfTerrain` no longer lifts a player out of water, because water no longer overlaps. Update the comment at `Sandbox/src/Sandbox.cpp:306-312`:

```cpp
    //Steps the player up until their box is clear of solid blocks.
    //
    //A reload can restore terrain where the player was standing, and
    //VoxelCollision only pushes a box out of a block on a move it detects, so a
    //player who starts embedded stays embedded with no escape but falling out of
    //the world. Keeping x and z preserves the part of the map being worked on,
    //which is the point of reloading quickly.
    //
    //Only solid blocks count, so reloading while standing in the river leaves
    //the player in the water rather than lifting them onto its surface.
```

- [ ] **Step 4: Run the whole suite**

Run: `./bin/Debug-windows-x86_64/Tests/Tests.exe`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "Pin the raycast stopping at water

Collision passes through water and the editing raycast does not. That
divergence is deliberate and was undefended, so a test now fails if the
raycast is tidied onto solidity."
```

---

### Task 6: Update the docs and verify in the sandbox

**Files:**
- Modify: `docs/engine-roadmap.md:53-56,80`
- Modify: `README.md` (the transparency bullet under **Rendering**, and the "What works" paragraph)
- Build + run: `Sandbox`

**Interfaces:**
- Consumes: everything above.
- Produces: nothing.

- [ ] **Step 1: Build and run the sandbox**

Run: `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Sandbox/Sandbox.vcxproj -p:Configuration=Debug -p:Platform=x64`

Then launch `./bin/Debug-windows-x86_64/Sandbox/Sandbox.exe`.

Rendering and physics are not unit tested — this is how they get verified. Screenshot the **GLFW30** window, not `MainWindowHandle`, and close via `WM_CLOSE` or the log is lost. Mouse input works from a script; keyboard does not.

- [ ] **Step 2: Confirm the three success criteria**

Walk off the bank into the river and check:
1. The player **drops to the riverbed** rather than standing on the water surface.
2. Water still renders see-through, from below the surface as well as above.
3. Left-clicking the water surface **breaks a water block**.

If (1) fails the collision change did not take; if (2) fails phase 1 regressed; if (3) fails the raycast was moved onto solidity.

- [ ] **Step 3: Update the roadmap**

In `docs/engine-roadmap.md`, replace the phase-2 caveat on lines 53-56:

```markdown
5. ~~**Transparency / alpha blending**~~ **DONE 2026-08-08** — palette alpha marks
   a block non-opaque *and* non-solid. The mesher splits chunk geometry and the
   renderer draws the transparent set back to front; collision passes through
   water while the editing raycast still stops at it, so the river is something
   you wade into and can still dig out.
```

and line 80:

```markdown
3. ~~**Transparency**~~ **DONE 2026-08-08** (opacity 2026-08-06, solidity 2026-08-08).
```

Update the `_Last updated:_` line at the top to `2026-08-08`.

Add a note under the transparency entry recording the cost the spec identified:

```markdown
   Solidity is derived from alpha, so a block that is see-through is also
   walk-through. Glass — non-opaque but solid — is deliberately not expressible;
   adding it means giving solidity its own table and source, which changes how
   the table is filled and no call site.
```

- [ ] **Step 4: Update the README**

In `README.md`, replace lines 18-19:

```markdown
You load a map, walk around it under gravity, and dig into it or build on it, lit by
sky light and ambient occlusion. Water is see-through and you wade into it rather
than walking on it. The current map is a 256x64x256 battlefield.
```

and replace lines 63-65:

```markdown
- `VoxelRaycast` (grid traversal, reporting the entry face) and `VoxelCollision` (a
  stepped, per-axis box move that reports which axes were blocked and whether the box
  is grounded). They ask different questions of a block on purpose: collision tests
  whether it is *solid*, so the player falls through water, while the raycast tests
  whether it is *present*, so water can still be clicked and dug out
```

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "Record transparency phase 2 in the roadmap and readme"
```

- [ ] **Step 6: Push**

```bash
git push origin master
```

---

## Notes for the implementer

- **Task 1 is the risky one and it is risky in a boring way.** It touches 13 files and changes nothing. If the suite goes red, a call site was missed or mistyped — do not start reasoning about water.
- **Two tasks have no red phase.** Task 5's raycast test passes on arrival by design, and each of Tasks 3 and 4 ships a companion "still works" case that passes immediately. These are regression guards. Do not manufacture a failure for them.
- **The default palette is why this is safe.** `DefaultPalette()` is alpha `1.0` for every id but air, so in any world that has not loaded a map, solid and present agree exactly. Every pre-existing test is therefore a valid regression net across all six tasks.
- **Debug is a ~20× multiplier and total call count dominates.** Nothing here is on a hot path, but do not "optimise" the new predicate into the palette — `IsIdSolid` is a table lookup for the reason recorded in `docs/performance.md` P7.
