# Underwater Feel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the river feel like water — submerging washes the screen blue and hazes distance out, water cannot be dug or placed, and Space swims you upward.

**Architecture:** One derived predicate, `World::IsBlockFluid` (present but not solid), is what all three features ask. Tasks 1–2 add it and a box-sweep sibling in the engine. Task 3 makes water undiggable via a `solidOnly` raycast flag that replaces the `skipStartVoxel` flag added on 2026-08-08. Tasks 4–6 add swimming, fog and the screen wash in the Sandbox.

**Tech Stack:** C++20, GLM, OpenGL 3.3 (GLSL 330 core), doctest, premake5 + MSBuild (Visual Studio 18).

Design spec: [`docs/superpowers/specs/2026-08-10-underwater-feel-design.md`](../specs/2026-08-10-underwater-feel-design.md)

## Global Constraints

- **Build (also runs the test suite as a post-build step):**
  `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64`
  Use **forward slashes** in the `.vcxproj` path — backslashes fail with MSB1009.
- **Build the sandbox:** same command with `Sandbox/Sandbox.vcxproj`.
- **Run the whole suite:** `./bin/Debug-windows-x86_64/Tests/Tests.exe`
- **Run selected cases:** `./bin/Debug-windows-x86_64/Tests/Tests.exe -tc="Name one,Name two"`
- Do **not** run `GenerateProjects.bat` — it deletes `bin/` and ends in a blocking `pause`. No task adds a source file, so premake never needs re-running.
- Comment style: `//` with **no space** for function and class doc comments; `// ` **with a space** for inline "why" comments. Match each file's local convention. Comments explain *why*.
- Commit after every task. **Never add Claude co-author trailers or attribution.**
- Do not push until Task 7.
- Tuning constants, exact values:

| name | value | where |
|---|---|---|
| `WaterGravity` | `6.0f` | `Sandbox.cpp` |
| `SinkSpeed` | `1.5f` | `Sandbox.cpp` |
| `SwimUpSpeed` | `3.5f` | `Sandbox.cpp` |
| `WaterDrag` | `0.6f` | `Sandbox.cpp` |
| `FogColor` | `glm::vec3(0.10f, 0.30f, 0.55f)` | `Sandbox.cpp` |
| `FogDensity` | `0.06f` | `Sandbox.cpp` |
| `UnderwaterTint` | `glm::vec4(0.15f, 0.40f, 0.70f, 0.45f)` | `HudLayer.h` |

---

### Task 1: `World::IsBlockFluid`

The predicate everything else keys off. Derived from the two existing tables — **do not add a third array.**

**Files:**
- Modify: `Cubit/include/Cubit/Voxel/World.h` (after the `IsBlockSolid` declaration, ~line 82)
- Modify: `Cubit/src/Voxel/World.cpp` (after the `IsBlockSolid` definition, ~line 162)
- Test: `Tests/src/WorldTests.cpp` (append)

**Interfaces:**
- Consumes: `World::IsBlockPresent(int,int,int)`, `World::IsBlockSolid(int,int,int)` — both exist.
- Produces: `bool World::IsBlockFluid(int,int,int) const`. Tasks 2, 4, 5 consume it.

- [ ] **Step 1: Write the failing tests**

Append to `Tests/src/WorldTests.cpp`:

```cpp
TEST_CASE("A transparent block is fluid")
{
    // Fluid is the question every water behaviour asks: something is there, but
    // it does not stop you. Nothing else in the engine answers yes.
    World world(1, 1, 1);
    Palette palette = DefaultPalette();
    palette[5] = glm::vec4(0.2f, 0.4f, 0.6f, 0.5f);
    world.SetPalette(palette);

    world.SetBlock(8, 8, 8, BlockId{5});

    CHECK(world.IsBlockFluid(8, 8, 8));
}

TEST_CASE("An opaque block is not fluid")
{
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});

    CHECK_FALSE(world.IsBlockFluid(8, 8, 8));
}

TEST_CASE("Air is not fluid")
{
    // Air is not present at all, so it falls out of the rule rather than
    // needing a special case — the same shape as the opacity rule.
    const World world(1, 1, 1);

    CHECK_FALSE(world.IsBlockFluid(8, 8, 8));
}

TEST_CASE("Positions outside the world are not fluid")
{
    const World world(1, 1, 1);

    CHECK_FALSE(world.IsBlockFluid(-1, 0, 0));
    CHECK_FALSE(world.IsBlockFluid(0, world.GetHeight(), 0));
}

TEST_CASE("Replacing the palette updates fluidity")
{
    // Derived from two tables that SetPalette rebuilds, so it must not go stale
    // when a map installs its own palette.
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{4});
    REQUIRE_FALSE(world.IsBlockFluid(8, 8, 8));

    Palette palette = DefaultPalette();
    palette[4] = glm::vec4(0.2f, 0.4f, 0.6f, 0.25f);
    world.SetPalette(palette);

    CHECK(world.IsBlockFluid(8, 8, 8));
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64`
Expected: **compile error** — `'IsBlockFluid': is not a member of 'World'`. In C++ a missing method is a compile error, not a runtime failure; that is the correct red phase and there is no other one to reach.

- [ ] **Step 3: Declare it**

In `Cubit/include/Cubit/Voxel/World.h`, immediately after the `IsBlockSolid` declaration:

```cpp
    //Reports whether a block occupies this cell without stopping movement —
    //water, and anything else you can swim through. Air is not fluid: it is not
    //present at all, so it falls out of the rule rather than needing a case.
    //
    //Derived from the two tables above rather than stored: it is one &&, and a
    //third array would be a third thing to keep in step with a palette change.
    bool IsBlockFluid(int x, int y, int z) const;
```

- [ ] **Step 4: Define it**

In `Cubit/src/Voxel/World.cpp`, directly after the `IsBlockSolid` definition:

```cpp
bool World::IsBlockFluid(int x, int y, int z) const
{
    return IsBlockPresent(x, y, z) && !IsBlockSolid(x, y, z);
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `./bin/Debug-windows-x86_64/Tests/Tests.exe -tc="A transparent block is fluid,An opaque block is not fluid,Air is not fluid,Positions outside the world are not fluid,Replacing the palette updates fluidity"`
Expected: all 5 PASS.

Then the whole suite: `./bin/Debug-windows-x86_64/Tests/Tests.exe`
Expected: all pass (179 before this task, 184 after).

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "Add a fluid predicate for blocks you can swim through

Present but not solid. Derived from the two existing tables rather than
stored: it is one &&, and a third array would be a third thing to keep
in step with a palette change."
```

---

### Task 2: `VoxelCollision::OverlapsFluid`

The swim trigger keys off the body, so it needs a box sweep rather than a cell query.

**Files:**
- Modify: `Cubit/include/Cubit/Voxel/VoxelCollision.h:39-43`
- Modify: `Cubit/src/Voxel/VoxelCollision.cpp:20-37,85-91`
- Test: `Tests/src/VoxelCollisionTests.cpp` (append)

**Interfaces:**
- Consumes: `World::IsBlockFluid(int,int,int)` from Task 1.
- Produces: `static bool VoxelCollision::OverlapsFluid(const World&, const glm::vec3& position, const glm::vec3& halfExtents)`. Task 4 consumes it.

- [ ] **Step 1: Write the failing tests**

Append to `Tests/src/VoxelCollisionTests.cpp`. The file's anonymous namespace already defines `PlayerHalfExtents` as `{0.3f, 0.9f, 0.3f}` — reuse it, do not redefine it.

```cpp
TEST_CASE("A box inside water overlaps fluid")
{
    World world(1, 1, 1);
    Palette palette = DefaultPalette();
    palette[7] = glm::vec4(0.2f, 0.4f, 0.8f, 0.55f); // water
    world.SetPalette(palette);

    world.SetBlock(8, 8, 8, BlockId{7});

    CHECK(VoxelCollision::OverlapsFluid(
        world, glm::vec3(8.5f, 8.5f, 8.5f), PlayerHalfExtents));
}

TEST_CASE("A box in open air overlaps no fluid")
{
    const World world(1, 1, 1);

    CHECK_FALSE(VoxelCollision::OverlapsFluid(
        world, glm::vec3(8.5f, 8.5f, 8.5f), PlayerHalfExtents));
}

TEST_CASE("A box inside stone overlaps no fluid")
{
    // Solid is not fluid. Without this the predicate could be reading presence
    // and every test above would still pass.
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});

    CHECK_FALSE(VoxelCollision::OverlapsFluid(
        world, glm::vec3(8.5f, 8.5f, 8.5f), PlayerHalfExtents));
}

TEST_CASE("A box straddling water and air overlaps fluid")
{
    // Any overlap counts, matching Overlaps. A player with only their feet in
    // the river is swimming.
    World world(1, 1, 1);
    Palette palette = DefaultPalette();
    palette[7] = glm::vec4(0.2f, 0.4f, 0.8f, 0.55f);
    world.SetPalette(palette);

    world.SetBlock(8, 7, 8, BlockId{7});

    // Box centre 8.5 with half-height 0.9 spans y 7.6 to 9.4, so its lower part
    // is in the water cell at y=7 and its upper part is in air.
    CHECK(VoxelCollision::OverlapsFluid(
        world, glm::vec3(8.5f, 8.5f, 8.5f), PlayerHalfExtents));
}

TEST_CASE("Water does not make a box overlap solid")
{
    // The companion to the cases above: the two queries must stay independent.
    World world(1, 1, 1);
    Palette palette = DefaultPalette();
    palette[7] = glm::vec4(0.2f, 0.4f, 0.8f, 0.55f);
    world.SetPalette(palette);

    world.SetBlock(8, 8, 8, BlockId{7});

    CHECK_FALSE(VoxelCollision::Overlaps(
        world, glm::vec3(8.5f, 8.5f, 8.5f), PlayerHalfExtents));
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64`
Expected: **compile error** — `'OverlapsFluid' is not a member of 'VoxelCollision'`.

- [ ] **Step 3: Generalise the sweep over its predicate**

In `Cubit/src/Voxel/VoxelCollision.cpp`, replace the `OverlapsSolid` helper (lines 20-37) with a predicate-driven version. `MoveAxis` calls `OverlapsSolid` at line ~73 and must keep working unchanged.

The codebase already uses this shape — `ChunkMesher`'s corner rules are templated over their cell source "so the chunk cache and a bare world share one rule rather than drifting apart as two copies". Same reasoning: two identical sweeps that answer different questions.

```cpp
    //Reports whether the box spanning these corners covers any block the
    //predicate accepts. Solid and fluid are the same sweep over the same cells;
    //only the question differs, so they share one copy of the walk.
    template <typename Accepts>
    bool OverlapsAny(const World& world, const glm::vec3& min, const glm::vec3& max,
        Accepts accepts)
    {
        const int minX = static_cast<int>(std::floor(min.x + Skin));
        const int minY = static_cast<int>(std::floor(min.y + Skin));
        const int minZ = static_cast<int>(std::floor(min.z + Skin));
        const int maxX = static_cast<int>(std::floor(max.x - Skin));
        const int maxY = static_cast<int>(std::floor(max.y - Skin));
        const int maxZ = static_cast<int>(std::floor(max.z - Skin));

        for (int z = minZ; z <= maxZ; ++z)
            for (int y = minY; y <= maxY; ++y)
                for (int x = minX; x <= maxX; ++x)
                    if (accepts(world, x, y, z))
                        return true;

        return false;
    }

    //Reports whether the box covers any solid block. Solidity rather than
    //presence: water is a block you can see but walk straight through.
    bool OverlapsSolid(const World& world, const glm::vec3& min, const glm::vec3& max)
    {
        return OverlapsAny(world, min, max,
            [](const World& w, int x, int y, int z) { return w.IsBlockSolid(x, y, z); });
    }
```

- [ ] **Step 4: Add the public query**

In `Cubit/include/Cubit/Voxel/VoxelCollision.h`, after the `Overlaps` declaration:

```cpp
    //Reports whether a box at this position overlaps any fluid block. The swim
    //rules key off the body rather than the camera, so a player wading with
    //their head clear is still walking.
    //
    //Any overlap counts, matching Overlaps: feet in the river means swimming.
    //The shipped map has no shallows, so the simpler rule costs nothing there.
    static bool OverlapsFluid(
        const World& world,
        const glm::vec3& position,
        const glm::vec3& halfExtents);
```

In `Cubit/src/Voxel/VoxelCollision.cpp`, after the `Overlaps` definition (~line 91):

```cpp
bool VoxelCollision::OverlapsFluid(
    const World& world,
    const glm::vec3& position,
    const glm::vec3& halfExtents)
{
    return OverlapsAny(world, position - halfExtents, position + halfExtents,
        [](const World& w, int x, int y, int z) { return w.IsBlockFluid(x, y, z); });
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `./bin/Debug-windows-x86_64/Tests/Tests.exe -tc="A box inside water overlaps fluid,A box in open air overlaps no fluid,A box inside stone overlaps no fluid,A box straddling water and air overlaps fluid,Water does not make a box overlap solid"`
Expected: all 5 PASS.

Then the whole suite: `./bin/Debug-windows-x86_64/Tests/Tests.exe`
Expected: all pass (189). The existing collision tests are the proof that extracting `OverlapsAny` did not change `Overlaps` or `MoveBox`.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "Add a fluid box query beside the solid one

Both are the same sweep over the same cells with a different question,
so the walk is written once and the predicate varies."
```

---

### Task 3: Water cannot be dug or placed

Three changes that together stop water being a material: the raycast learns to ignore fluid, the Sandbox asks it to, and water leaves the palette bar.

**Files:**
- Modify: `Cubit/include/Cubit/Voxel/VoxelRaycast.h` (all four doc comments plus the signature)
- Modify: `Cubit/src/Voxel/VoxelRaycast.cpp:60-108`
- Modify: `Sandbox/src/Sandbox.cpp:48-52,242-250`
- Test: `Tests/src/VoxelRaycastTests.cpp:153-183` (delete two cases), plus appends and one comment rewrite at `:294`

**Interfaces:**
- Consumes: `World::IsBlockSolid`, `World::IsBlockPresent` — both exist.
- Produces: `VoxelRaycast::Cast(const World&, const glm::vec3& origin, const glm::vec3& direction, float maxDistance, bool solidOnly = false)`. **`skipStartVoxel` ceases to exist.**

- [ ] **Step 1: Delete the two `skipStartVoxel` tests**

In `Tests/src/VoxelRaycastTests.cpp`, delete these two whole cases — the parameter they test is being removed:
- `"skipStartVoxel does not report the block the ray starts inside"` (~line 153)
- `"skipStartVoxel only skips the origin, not the block right after it"` (~line 167)

Leave `"A ray starting inside a solid block reports that block"` (~line 139) alone. It pins the default behaviour, which is unchanged.

- [ ] **Step 2: Write the failing tests**

Append to `Tests/src/VoxelRaycastTests.cpp`:

```cpp
TEST_CASE("solidOnly passes through water to the block behind it")
{
    // Water is scenery, not a material: an edit ray must reach the riverbed
    // through it rather than stopping at the surface.
    World world(1, 1, 1);
    Palette palette = DefaultPalette();
    palette[7] = glm::vec4(0.2f, 0.4f, 0.8f, 0.55f); // water
    world.SetPalette(palette);

    world.SetBlock(8, 8, 8, BlockId{7});  // water, nearer
    world.SetBlock(8, 8, 12, BlockId{1}); // opaque, further

    const VoxelRayHit hit = VoxelRaycast::Cast(
        world,
        glm::vec3(8.5f, 8.5f, 4.5f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        16.0f,
        true);

    REQUIRE(hit.Hit);
    CHECK(hit.Block == glm::ivec3(8, 8, 12));
    CHECK(hit.Normal == glm::ivec3(0, 0, -1));
}

TEST_CASE("solidOnly still reports an ordinary solid block")
{
    // The companion: ignoring fluid must not make the ray ignore terrain.
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});

    const VoxelRayHit hit = VoxelRaycast::Cast(
        world,
        glm::vec3(8.5f, 8.5f, 4.5f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        16.0f,
        true);

    REQUIRE(hit.Hit);
    CHECK(hit.Block == glm::ivec3(8, 8, 8));
}

TEST_CASE("solidOnly ignores water the ray starts inside")
{
    // The case that used to need its own flag. A player standing on the
    // riverbed has their eye inside a water cell; that cell must not be what
    // every click hits.
    World world(1, 1, 1);
    Palette palette = DefaultPalette();
    palette[7] = glm::vec4(0.2f, 0.4f, 0.8f, 0.55f);
    world.SetPalette(palette);

    world.SetBlock(8, 8, 8, BlockId{7});  // the ray starts in here
    world.SetBlock(8, 4, 8, BlockId{1});  // the bed below

    const VoxelRayHit hit = VoxelRaycast::Cast(
        world,
        glm::vec3(8.5f, 8.5f, 8.5f),
        glm::vec3(0.0f, -1.0f, 0.0f),
        16.0f,
        true);

    REQUIRE(hit.Hit);
    CHECK(hit.Block == glm::ivec3(8, 4, 8));
}
```

- [ ] **Step 3: Run to verify they fail**

Run: `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64`
Expected: it **compiles** (the 5th argument is still the old `bool skipStartVoxel`) and the three new cases FAIL — with `skipStartVoxel` semantics the first reports `(8,8,8)`, the third reports `(8,8,8)` too. The second passes already; it is a regression guard.

- [ ] **Step 4: Replace the parameter**

In `Cubit/include/Cubit/Voxel/VoxelRaycast.h`, replace lines 30-47 entirely — the whole `Cast` doc comment plus the signature. The existing `skipStartVoxel` paragraph is six lines long and all of it goes:

```cpp
    //Walks the ray voxel by voxel and reports the first present block it
    //enters. Presence, not solidity or opacity, decides a hit: a non-solid
    //block like water still stops the ray. Positions outside the world are
    //treated as air, matching World::GetBlock.
    //
    //solidOnly, when true, ignores fluid blocks entirely, which is what an
    //edit — or a future shot — wants: water is scenery you aim through, not a
    //target. It also settles the case of a ray beginning inside water, which a
    //player standing on the riverbed does every frame; that cell would
    //otherwise be reported at zero distance with no entry face to place
    //against, making it the answer to every click regardless of aim.
    static VoxelRayHit Cast(
        const World& world,
        const glm::vec3& origin,
        const glm::vec3& direction,
        float maxDistance,
        bool solidOnly = false);
```

Leave `VoxelRayHit::Normal`'s comment at lines 17-18 alone — "Zero when the ray started inside a present block and never crossed a face" still describes the default path correctly, and it names no removed flag.

In `Cubit/src/Voxel/VoxelRaycast.cpp`, change the signature's last parameter to `bool solidOnly` and replace the hit test (lines 97-100):

```cpp
        const bool blocks = solidOnly
            ? world.IsBlockSolid(voxel.x, voxel.y, voxel.z)
            : world.IsBlockPresent(voxel.x, voxel.y, voxel.z);

        if (blocks)
        {
```

Delete the `isStartVoxel` local entirely.

- [ ] **Step 5: Update the Sandbox call and the placeable list**

In `Sandbox/src/Sandbox.cpp`, replace the cast at lines 242-250:

```cpp
        // Solid only: water is scenery, so an edit ray passes through the river
        // to the bed rather than targeting the surface — or, when the player is
        // standing in it, the cell their own head occupies.
        const VoxelRayHit hit = VoxelRaycast::Cast(
            m_World,
            camera.GetPosition() - WorldOffset,
            camera.GetForwardDirection(),
            ReachDistance,
            true);
```

Leave the zero-normal right-click guard at `:261` in place. Under `solidOnly` it becomes reachable again — a player embedded in *solid* terrain still starts inside a hit block — which is exactly the case it was written for.

Then replace the placeable list at lines 48-52:

```cpp
    //Palette indices selectable with the number keys, in order. Water (7) is
    //deliberately absent: it cannot be broken, so being able to place it would
    //hand the player a block they can create and never remove.
    constexpr BlockId PlaceableBlocks[] = { 1, 2, 3, 4, 5, 6, 8 };
```

This shortens the list to seven, so key 7 now selects `Wood` and key 8 selects nothing. That is intended.

- [ ] **Step 6: Rewrite the stale comment on the water raycast test**

`Tests/src/VoxelRaycastTests.cpp:294`, the case "A ray stops at water rather than passing through it", justifies itself with water being placeable via key 7 — which Step 5 just made false. The case still earns its place: it pins the **default** behaviour. Replace its comment with:

```cpp
    // Pins the default: without solidOnly the ray reports any present block,
    // water included. Callers that want to aim through water opt in; the engine
    // does not decide that for them.
```

- [ ] **Step 7: Run the tests to verify they pass**

Run: `./bin/Debug-windows-x86_64/Tests/Tests.exe -tc="solidOnly passes through water to the block behind it,solidOnly still reports an ordinary solid block,solidOnly ignores water the ray starts inside,A ray stops at water rather than passing through it,A ray starting inside a solid block reports that block"`
Expected: all 5 PASS.

Confirm the removed flag is gone: `grep -rn "skipStartVoxel" Cubit Sandbox Tests`
Expected: **no matches.**

Then the whole suite: `./bin/Debug-windows-x86_64/Tests/Tests.exe`
Expected: all pass (190 — 189 plus 3 added minus 2 deleted).

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "Make water scenery rather than a material

The edit ray now ignores fluid instead of skipping only the voxel it
starts in, which fixes aiming through water as well as standing in it,
and replaces the narrower flag added for that case. Water leaves the
placeable list: a block that cannot be broken must not be placeable."
```

---

### Task 4: Swimming

**Files:**
- Modify: `Sandbox/src/HudLayer.h:16-25` (the `HudState` struct) and its readout, ~line 184
- Modify: `Sandbox/src/Sandbox.cpp:9-14` (includes), `:41-46` (constants), `:118-153` (`OnUpdate`)

**Interfaces:**
- Consumes: `VoxelCollision::OverlapsFluid` (Task 2), `World::IsBlockFluid` (Task 1).
- Produces: `HudState::EyeInFluid` and `HudState::BodyInFluid`, both `bool`. Tasks 5 and 6 read `EyeInFluid`.

- [ ] **Step 1: Add the published state**

In `Sandbox/src/HudLayer.h`, add two fields to `HudState` after `Grounded`:

```cpp
    //Whether the camera is inside a fluid block. Drives the underwater tint and
    //the fog; separate from BodyInFluid because a wading player has their head
    //in open air while their legs are in the river.
    bool EyeInFluid = false;

    //Whether the player's box overlaps any fluid block. Drives the swim rules.
    bool BodyInFluid = false;
```

- [ ] **Step 2: Show it on the HUD**

In `DrawReadout`, immediately after the `GND` line (~line 184), add:

```cpp
        y -= lineHeight;
        DrawText(
            std::string("WET ") +
            (m_State->EyeInFluid ? "E" : "-") +
            (m_State->BodyInFluid ? "B" : "-"),
            TextMargin,
            y);
```

This is the readout the sandbox verification in Task 7 uses to tell the two states apart.

- [ ] **Step 3: Add the constants and the include**

`Sandbox/src/Sandbox.cpp` has no `<cmath>` include and needs one for `std::floor`. Add it to the include block at lines 9-14, keeping the list alphabetical:

```cpp
#include <cmath>
```

Then after the existing `Gravity` constant (~line 43), add:

```cpp
    //Water physics. Gravity is weakened rather than cancelled, so doing nothing
    //settles the player onto the riverbed instead of leaving them hanging.
    constexpr float WaterGravity = 6.0f;
    constexpr float SinkSpeed = 1.5f;
    constexpr float SwimUpSpeed = 3.5f;
    constexpr float WaterDrag = 0.6f;
```

- [ ] **Step 4: Rewrite the movement block**

In `OnUpdate`, replace lines 120-126 (from `const float seconds` through `m_VerticalVelocity -= Gravity * seconds;`) with:

```cpp
        const float seconds = static_cast<float>(timestep.GetSeconds());
        const bool inFluid = VoxelCollision::OverlapsFluid(
            m_World, m_PlayerPosition, PlayerHalfExtents);

        glm::vec3 walk = ReadWalkInput() * WalkSpeed;

        // Space has to be tested here rather than after the jump: standing on
        // the riverbed is grounded and submerged at once, so a dry jump would
        // otherwise fire instead of a swim stroke.
        if (inFluid)
        {
            walk *= WaterDrag;

            if (Input::IsKeyPressed(KeyCode::Space))
                m_VerticalVelocity = SwimUpSpeed;

            m_VerticalVelocity -= WaterGravity * seconds;
            m_VerticalVelocity = glm::max(m_VerticalVelocity, -SinkSpeed);
        }
        else
        {
            if (m_Grounded && Input::IsKeyPressed(KeyCode::Space))
                m_VerticalVelocity = JumpSpeed;

            m_VerticalVelocity -= Gravity * seconds;
        }
```

Note `walk` changes from `const glm::vec3` to `glm::vec3`, because the drag multiplies it.

- [ ] **Step 5: Publish both flags**

In `OnUpdate`, after `m_HudState->Grounded = m_Grounded;` (~line 139), add:

```cpp
        m_HudState->BodyInFluid = inFluid;

        // The eye, not the box: the tint and the fog should come on when the
        // camera goes under, which happens later than the feet getting wet.
        const glm::vec3 eye =
            m_PlayerPosition + glm::vec3(0.0f, EyeOffset, 0.0f);
        m_HudState->EyeInFluid = m_World.IsBlockFluid(
            static_cast<int>(std::floor(eye.x)),
            static_cast<int>(std::floor(eye.y)),
            static_cast<int>(std::floor(eye.z)));
```

- [ ] **Step 6: Build and run the suite**

Run: `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64`
then `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Sandbox/Sandbox.vcxproj -p:Configuration=Debug -p:Platform=x64`

Expected: both build clean, suite still 190 passing. Swim feel is not unit tested — Task 7 verifies it on screen.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "Let the player swim rather than sink through water

Weakened gravity and a capped sink speed, with Space rising, so doing
nothing still settles you on the riverbed. The Space test moves ahead of
the jump because standing on the bed is grounded and submerged at once."
```

---

### Task 5: Distance haze

**Files:**
- Modify: `Sandbox/src/Sandbox.cpp:89-113` (both shader sources), `:41-46` (constants), `:156-172` (`OnRender`)

**Interfaces:**
- Consumes: `HudState::EyeInFluid` (Task 4).
- Produces: nothing later tasks depend on.

- [ ] **Step 1: Add the fog constants**

In `Sandbox/src/Sandbox.cpp`, after the water physics constants added in Task 4:

```cpp
    //Underwater haze. Roughly half strength at the 12-block reach distance and
    //83% at 30, which reads as murk without hiding what you are aiming at.
    const glm::vec3 FogColor{ 0.10f, 0.30f, 0.55f };
    constexpr float FogDensity = 0.06f;
```

- [ ] **Step 2: Add world position to the vertex shader**

Replace the vertex shader source (lines 89-102) with:

```cpp
        constexpr std::string_view vertexSource = R"(
            #version 330 core
            layout(location = 0) in vec3 a_Position;
            layout(location = 1) in vec4 a_Color;
            uniform mat4 u_ViewProjection;
            uniform mat4 u_Transform;
            out vec4 v_Color;
            out vec3 v_WorldPos;

            void main()
            {
                v_Color = a_Color;
                v_WorldPos = (u_Transform * vec4(a_Position, 1.0)).xyz;
                gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 1.0);
            }
        )";
```

- [ ] **Step 3: Fog the fragment shader**

Replace the fragment shader source (lines 103-112) with:

```cpp
        constexpr std::string_view fragmentSource = R"(
            #version 330 core
            layout(location = 0) out vec4 color;
            in vec4 v_Color;
            in vec3 v_WorldPos;
            uniform vec3 u_FogColor;
            uniform float u_FogDensity;
            uniform vec3 u_CameraPos;

            void main()
            {
                // Exponential, so it needs no far-plane constant and never
                // saturates abruptly. Density is zero when dry, which makes
                // this a mix against nothing rather than a branch.
                float d = length(v_WorldPos - u_CameraPos);
                float f = 1.0 - exp(-u_FogDensity * d);
                color = vec4(mix(v_Color.rgb, u_FogColor, f), v_Color.a);
            }
        )";
```

- [ ] **Step 4: Set the uniforms each frame**

In `OnRender`, between `Renderer::BeginScene(...)` and `m_WorldRenderer.Render(...)`:

```cpp
        // u_Transform already carries WorldOffset and the camera position is in
        // that same space — the invariant the transparency sort already relies
        // on — so the two can be subtracted directly.
        m_Shader->SetFloat3("u_FogColor", FogColor);
        m_Shader->SetFloat3("u_CameraPos", m_CameraController.GetCamera().GetPosition());
        m_Shader->SetFloat("u_FogDensity", m_HudState->EyeInFluid ? FogDensity : 0.0f);
```

The setters call `Bind()` themselves and uniforms are program state, so these survive the per-chunk `Submit` calls that set only `u_ViewProjection` and `u_Transform`.

- [ ] **Step 5: Build and check nothing regressed above water**

Run: `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Sandbox/Sandbox.vcxproj -p:Configuration=Debug -p:Platform=x64`
then launch `./bin/Debug-windows-x86_64/Sandbox/Sandbox.exe`, screenshot, and close via **WM_CLOSE**.

Screenshot the window found by its **GLFW30 window class**, not `MainWindowHandle` — the latter grabs a console.

Expected: the map looks exactly as before. The spawn is dry, so `u_FogDensity` is 0 and the output is unchanged. A visibly hazy or discoloured dry world means the density is not reaching zero, or `u_CameraPos` is in the wrong space.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "Haze the world into blue while the camera is underwater

Exponential distance fog on the world shader, density zero when dry so
the above-water path is a mix against nothing rather than a branch."
```

---

### Task 6: Screen wash

**Files:**
- Modify: `Sandbox/src/HudLayer.h` — constants (~line 134), constructor (~line 92), `OnRender` (~line 108), `DrawQuad` (~line 236), plus a new texture helper

**Interfaces:**
- Consumes: `HudState::EyeInFluid` (Task 4).
- Produces: nothing.

- [ ] **Step 1: Add the tint constant**

In `HudLayer`'s private constants block (~line 134, beside `TextScale`):

```cpp
    //Covers the whole screen while submerged. The fog cannot reach the sky, so
    //without this, looking up from underwater shows an untouched clear colour.
    static inline const glm::vec4 UnderwaterTint{ 0.15f, 0.40f, 0.70f, 0.45f };
```

`static inline const`, not `constexpr` — GLM's vector constructors are not reliably `constexpr` in this configuration, and `inline` is what lets a `static` data member be initialised in-class without a separate definition. The neighbouring `TextScale` and `TextMargin` are `constexpr` because `float` has no such problem.

- [ ] **Step 2: Give `DrawQuad` a tint**

`DrawQuad` currently hardcodes `u_Tint` to white. Add a defaulted parameter — the existing calls keep working untouched:

```cpp
    void DrawQuad(
        const Texture2D& texture,
        float x,
        float y,
        float width,
        float height,
        const glm::vec2& uvOffset,
        const glm::vec2& uvScale,
        const glm::vec4& tint = glm::vec4(1.0f)) const
    {
        m_Shader->SetInt("u_Texture", 0);
        m_Shader->SetFloat4("u_Tint", tint);
```

Leave the rest of the body as it is.

- [ ] **Step 3: Add a 1x1 white texture**

The overlay shader always samples a texture, so a flat colour needs a white one to multiply the tint against. Add this helper beside `CreateCrosshairTexture`:

```cpp
    //A single white pixel. The overlay shader always samples a texture, so a
    //flat tinted fill needs something neutral to multiply against.
    static std::unique_ptr<Texture2D> CreateWhiteTexture()
    {
        const std::uint8_t pixel[4] = { 255, 255, 255, 255 };
        return std::make_unique<Texture2D>(1, 1, pixel);
    }
```

Add the member beside `m_Crosshair` and `m_Font`:

```cpp
    std::unique_ptr<Texture2D> m_White;
```

and initialise it in the constructor beside the other two (~line 92):

```cpp
        m_White = CreateWhiteTexture();
```

- [ ] **Step 4: Draw the wash**

Add the draw method:

```cpp
    //Fills the screen with the underwater tint. Drawn before the crosshair and
    //the readout so those stay legible through it.
    void DrawUnderwaterWash() const
    {
        DrawQuad(
            *m_White,
            0.0f,
            0.0f,
            static_cast<float>(m_Width),
            static_cast<float>(m_Height),
            glm::vec2(0.0f),
            glm::vec2(1.0f),
            UnderwaterTint);
    }
```

and call it first in `OnRender`:

```cpp
        Renderer::SetDepthTest(false);
        Renderer::BeginScene(m_Camera);

        if (m_State->EyeInFluid)
            DrawUnderwaterWash();

        DrawCrosshair();
        DrawReadout();
```

- [ ] **Step 5: Build**

Run: `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Sandbox/Sandbox.vcxproj -p:Configuration=Debug -p:Platform=x64`
Expected: builds clean. The spawn is dry, so nothing looks different yet — Task 7 is where the wash is actually seen.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "Wash the screen blue while the camera is underwater

A tinted fullscreen quad under the HUD. It covers the sky, which the
world fog cannot reach."
```

---

### Task 7: Verify on screen and update the README

**Files:**
- Temporarily modify then revert: `Sandbox/src/Sandbox.cpp:36`
- Modify: `README.md:18-20`, and the `VoxelRaycast`/`VoxelCollision` bullet at `:63-68`

**Interfaces:**
- Consumes: everything above.
- Produces: nothing.

- [ ] **Step 1: Move the spawn into the river**

Keyboard input cannot be driven from a script here; the mouse can. So let gravity do the walking.

Record the exact current value at `Sandbox/src/Sandbox.cpp:36` — `const glm::vec3 SpawnPosition{ 80.5f, 30.0f, 112.5f };` — then **temporarily** change it to:

```cpp
    const glm::vec3 SpawnPosition{ 127.5f, 30.0f, 112.5f };
```

The river runs along z centred on the mirror plane at x=127, so this drops the player straight down the channel.

- [ ] **Step 2: Build, run, and check all five criteria**

Build the sandbox and launch `./bin/Debug-windows-x86_64/Sandbox/Sandbox.exe`. Screenshot by **GLFW30 window class**, close via **WM_CLOSE** or the log is lost.

| # | check | pass looks like |
|---|---|---|
| 1 | Settles on the bed | HUD `POS` y ≈ **9.9**, `GND 1` |
| 2 | Both wet flags set | HUD reads `WET EB` while on the bed |
| 3 | Screen is washed and hazed | Blue cast over everything, far bank fading into blue |
| 4 | Digging targets the bed | Left-click logs `Broke block at 127,8,112` — **not** y=9 or y=10, which are water |
| 5 | Swimming rises | Nothing to automate — see Step 3 |

Record the actual values. If check 4 logs a y of 9 or 10, `solidOnly` is not reaching the raycast and the task fails — report that rather than explaining it away.

- [ ] **Step 3: Confirm the swim by watching the wet flags**

Space cannot be sent from a script, so verify the swim indirectly: with the player resting on the bed, the HUD shows `WET EB`. Break the two water cells above with left-clicks (they are at y=9 and y=10 in the column) and the flags must fall to `WET --` as the water clears, with `POS` y unchanged at 9.9.

That confirms the fluid detection is live and tracks the world rather than being a constant. Note in the report that Space-to-swim itself was not exercised and needs a human at the keyboard.

- [ ] **Step 4: Restore the spawn**

Put `SpawnPosition` back to `{ 80.5f, 30.0f, 112.5f }` exactly. Confirm with `git diff Sandbox/src/Sandbox.cpp` that it is clean before committing anything. **The temporary change must not be committed.**

- [ ] **Step 5: Update the README**

Replace `README.md:18-20`:

```markdown
You load a map, walk around it under gravity, and dig into it or build on it, lit by
sky light and ambient occlusion. Water is see-through and you swim through it rather
than walking on it, with the screen washing blue and hazing out while you are under.
The current map is a 256x64x256 battlefield.
```

Replace the `VoxelRaycast`/`VoxelCollision` bullet at `README.md:63-68`:

```markdown
- `VoxelRaycast` (grid traversal, reporting the entry face) and `VoxelCollision` (a
  stepped, per-axis box move that reports which axes were blocked and whether the box
  is grounded, plus solid and fluid overlap queries). A block is *present* if it is
  there at all, *solid* if it stops you, and *fluid* if it is present but does not —
  so water is swum through, aimed through, and cannot be dug or placed
```

- [ ] **Step 6: Commit and push**

```bash
git add -A
git commit -m "Record swimming and the underwater view in the readme"
git push origin master
```

---

## Notes for the implementer

- **Two tasks have a compile error for their red phase** (1 and 2). In C++ a missing method cannot fail at runtime; a build failure naming the missing symbol is the correct red and there is no other one to reach.
- **Several tests pass on arrival by design** — "solidOnly still reports an ordinary solid block", "Water does not make a box overlap solid", "An opaque block is not fluid". They are regression guards proving the new predicate did not swallow the old behaviour. Do not manufacture failures for them.
- **Task 3 deletes two tests.** That is correct: they test a parameter being removed. Deleting a test is normally a smell, so it is called out explicitly here.
- **The dry path must stay pixel-identical.** Fog density 0 makes the mix a no-op. If the world looks different above water after Task 5, something is wrong with the uniform or the space `u_CameraPos` is in — do not "tune" the fog to compensate.
- **Debug is a ~20x multiplier and total call count dominates** (`docs/performance.md` P7). `OverlapsFluid` runs once per frame, not per cell, so it is nowhere near a hot path — do not optimise it.
