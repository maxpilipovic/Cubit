# Camera Aim API and Map-Aware Spawn Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a spawn choose where it stands and which way it faces, so neither value is a constant hand-picked against one specific map.

**Architecture:** Three independent additions composed by the Sandbox. `PerspectiveCameraController::SetRotation` aims the camera through the controller that owns yaw and pitch. `PerspectiveCamera::YawPitchToward` converts a from/to pair into that yaw and pitch, keeping the convention with the camera that defines it. A new `SpawnFinder` engine unit resolves a coarse `x,z` hint into a position where the player box actually fits.

**Tech Stack:** C++20, GLM, doctest, Premake 5, Visual Studio 2026 on Windows x64.

Design: [`docs/superpowers/specs/2026-08-16-camera-aim-and-spawn-design.md`](../specs/2026-08-16-camera-aim-and-spawn-design.md)

## Global Constraints

- **Build and test from Git Bash:**
  `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64`
  Use **forward slashes** in the `.vcxproj` path — backslashes fail with MSB1009.
- **Run one case:** `Tests.exe -tc="<case name>"` from `bin/Debug-windows-x86_64/Tests`.
- The Tests project runs the whole suite as a post-build step, so a failing test breaks the build.
- **New source files need Premake re-run.** `premake5.lua` globs `Cubit/src/**.cpp` and `Tests/src/**.cpp`. Run `premake5 vs2026` **directly** from the repo root. Do **not** run `GenerateProjects.bat` — it deletes `bin/` and ends in a blocking `pause`.
- Comment style: `//` with no space after the slashes in engine headers/sources, matching the surrounding files. Explain *why*, not *what*.
- Public engine symbols are exported with `CB_API`. A free function takes `CB_API` directly, as `BuildWorld` does in `VoxLoader.h:42`.
- Commit after each task. Push to `master` directly — no feature branches. **Never** add Claude co-author trailers or attribution.

---

### Task 1: Yaw/pitch setter on the camera controller

**Files:**
- Modify: `Cubit/include/Cubit/Renderer/PerspectiveCameraController.h:27-32`
- Modify: `Cubit/src/Renderer/PerspectiveCameraController.cpp:39-43`
- Create: `Tests/src/PerspectiveCameraControllerTests.cpp`

**Interfaces:**
- Consumes: existing `PerspectiveCamera::SetRotation(float, float)`, `GetForwardDirection()`.
- Produces: `void PerspectiveCameraController::SetRotation(float yaw, float pitch)`, `float GetYaw() const`, `float GetPitch() const`.

This task adds a new test file, so it includes the Premake re-run. Do that **first**, before writing the test, or the new file will not be in the project when you build.

- [ ] **Step 1: Create the empty test file and regenerate projects**

Create `Tests/src/PerspectiveCameraControllerTests.cpp` containing only:

```cpp
#include <doctest.h>

#include "Cubit/Renderer/PerspectiveCameraController.h"
#include "Cubit/Events/MouseEvent.h"
```

Then, from the repo root:

```bash
premake5 vs2026
```

- [ ] **Step 2: Write the failing tests**

Append to `Tests/src/PerspectiveCameraControllerTests.cpp`:

```cpp
TEST_CASE("Setting rotation aims the camera")
{
    PerspectiveCameraController controller(16.0f / 9.0f);

    //Yaw 0 faces +x under the camera's convention.
    controller.SetRotation(0.0f, 0.0f);

    const glm::vec3 forward = controller.GetCamera().GetForwardDirection();
    CHECK(forward.x == doctest::Approx(1.0f));
    CHECK(forward.y == doctest::Approx(0.0f));
    CHECK(forward.z == doctest::Approx(0.0f).epsilon(0.001));
    CHECK(controller.GetYaw() == doctest::Approx(0.0f));
    CHECK(controller.GetPitch() == doctest::Approx(0.0f));
}

TEST_CASE("Setting rotation clamps pitch to the mouse-look limit")
{
    PerspectiveCameraController controller(16.0f / 9.0f);

    controller.SetRotation(0.0f, 175.0f);

    CHECK(controller.GetPitch() == doctest::Approx(89.0f));
}

TEST_CASE("A mouse move continues from a rotation that was set")
{
    //The bug this guards: yaw and pitch are stored on both the controller and
    //the camera. Aiming the camera directly leaves the controller's copy stale,
    //and the next mouse move recomputes from it and snaps the view back.
    PerspectiveCameraController controller(16.0f / 9.0f);
    controller.SetRotation(0.0f, 0.0f);

    //The first move only records a reference position, so it takes two to
    //produce an offset — the same as the real input path.
    MouseMovedEvent first(100.0, 100.0);
    controller.OnEvent(first);
    MouseMovedEvent second(110.0, 100.0);
    controller.OnEvent(second);

    //10 pixels at the 0.12 sensitivity is 1.2 degrees on top of the yaw set
    //above, not 1.2 degrees on top of the -90 default.
    CHECK(controller.GetYaw() == doctest::Approx(1.2f));
}
```

- [ ] **Step 3: Run the tests to verify they fail**

```bash
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64
```

Expected: compile error — `SetRotation`, `GetYaw`, `GetPitch` are not members of `PerspectiveCameraController`.

- [ ] **Step 4: Add the declarations**

In `PerspectiveCameraController.h`, after the existing `SetPosition` declaration:

```cpp
    //Aims the camera. Pitch is clamped to the same range mouse-look uses.
    //
    //Goes through the controller rather than the camera because both hold a
    //copy of yaw and pitch: aiming the camera directly works until the next
    //mouse move recomputes from the controller's stale copy and snaps back.
    void SetRotation(float yaw, float pitch);

    float GetYaw() const { return m_Yaw; }
    float GetPitch() const { return m_Pitch; }
```

- [ ] **Step 5: Add the definition**

In `PerspectiveCameraController.cpp`, after `SetPosition`:

```cpp
void PerspectiveCameraController::SetRotation(float yaw, float pitch)
{
    m_Yaw = yaw;
    m_Pitch = std::clamp(pitch, -89.0f, 89.0f);
    UpdateCamera();
}
```

`<algorithm>` is already included at the top of the file for the mouse path's `std::clamp`.

- [ ] **Step 6: Run the tests to verify they pass**

```bash
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64
```

Expected: PASS, and the whole suite still green.

- [ ] **Step 7: Commit**

```bash
git add Cubit/include/Cubit/Renderer/PerspectiveCameraController.h Cubit/src/Renderer/PerspectiveCameraController.cpp Tests/src/PerspectiveCameraControllerTests.cpp Tests/Tests.vcxproj
git commit -m "Let a caller aim the perspective camera controller"
```

---

### Task 2: Yaw and pitch toward a point

**Files:**
- Modify: `Cubit/include/Cubit/Renderer/PerspectiveCamera.h:23`
- Modify: `Cubit/src/Renderer/PerspectiveCamera.cpp:40-45`
- Create: `Tests/src/PerspectiveCameraTests.cpp`

**Interfaces:**
- Consumes: nothing from Task 1.
- Produces: `static glm::vec2 PerspectiveCamera::YawPitchToward(const glm::vec3& from, const glm::vec3& to)` — returns `{yaw, pitch}` in degrees.

Another new test file, so this task also re-runs Premake first.

- [ ] **Step 1: Create the empty test file and regenerate projects**

Create `Tests/src/PerspectiveCameraTests.cpp` containing only:

```cpp
#include <doctest.h>

#include "Cubit/Renderer/PerspectiveCamera.h"

#include <glm/glm.hpp>
```

Then, from the repo root:

```bash
premake5 vs2026
```

- [ ] **Step 2: Write the failing tests**

Append to `Tests/src/PerspectiveCameraTests.cpp`:

```cpp
TEST_CASE("Yaw and pitch toward a point match the camera's convention")
{
    //-90 degrees faces -z, which is the camera's default facing.
    const glm::vec2 backward =
        PerspectiveCamera::YawPitchToward(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    CHECK(backward.x == doctest::Approx(-90.0f));
    CHECK(backward.y == doctest::Approx(0.0f));

    //0 degrees faces +x.
    const glm::vec2 right =
        PerspectiveCamera::YawPitchToward(glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    CHECK(right.x == doctest::Approx(0.0f));
    CHECK(right.y == doctest::Approx(0.0f));
}

TEST_CASE("Yaw and pitch toward a point carry elevation")
{
    //Level with the target horizontally, one unit up: 45 degrees of pitch.
    const glm::vec2 up =
        PerspectiveCamera::YawPitchToward(glm::vec3(0.0f), glm::vec3(1.0f, 1.0f, 0.0f));

    CHECK(up.x == doctest::Approx(0.0f));
    CHECK(up.y == doctest::Approx(45.0f));
}

TEST_CASE("A camera aimed toward a point looks at it")
{
    //The strongest check: round-trip the result back through the camera and
    //confirm the forward vector really points at the target. A sign or axis
    //error survives the angle checks above but fails here.
    const glm::vec3 eye(3.0f, 10.0f, -4.0f);
    const glm::vec3 target(70.0f, 2.0f, 55.0f);

    const glm::vec2 rotation = PerspectiveCamera::YawPitchToward(eye, target);

    PerspectiveCamera camera(60.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
    camera.SetPosition(eye);
    camera.SetRotation(rotation.x, rotation.y);

    const glm::vec3 expected = glm::normalize(target - eye);
    const glm::vec3 actual = camera.GetForwardDirection();

    CHECK(glm::dot(expected, actual) == doctest::Approx(1.0f).epsilon(0.0001));
}

TEST_CASE("Yaw and pitch toward the camera's own position fall back to the default")
{
    //No direction to derive. Returning the default beats returning a NaN that
    //would silently poison the view matrix.
    const glm::vec2 rotation =
        PerspectiveCamera::YawPitchToward(glm::vec3(5.0f), glm::vec3(5.0f));

    CHECK(rotation.x == doctest::Approx(-90.0f));
    CHECK(rotation.y == doctest::Approx(0.0f));
}
```

- [ ] **Step 3: Run the tests to verify they fail**

```bash
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64
```

Expected: compile error — `YawPitchToward` is not a member of `PerspectiveCamera`.

- [ ] **Step 4: Add the declaration**

In `PerspectiveCamera.h`, after the `SetRotation` declaration:

```cpp
    //Returns the yaw and pitch, in degrees, that face from one point toward
    //another.
    //
    //Lives here because this is where the convention lives: it inverts the
    //forward vector RecalculateViewMatrix builds, which is the reason -90
    //degrees means -z. A caller doing its own atan2 would be duplicating a
    //constant it does not own.
    static glm::vec2 YawPitchToward(const glm::vec3& from, const glm::vec3& to);
```

- [ ] **Step 5: Add the definition**

In `PerspectiveCamera.cpp`, after `SetRotation`:

```cpp
glm::vec2 PerspectiveCamera::YawPitchToward(const glm::vec3& from, const glm::vec3& to)
{
    const glm::vec3 direction = to - from;

    //Two identical points give no direction. The default facing is a usable
    //answer; a NaN would spread silently through the view matrix.
    if (direction == glm::vec3(0.0f))
        return glm::vec2(-90.0f, 0.0f);

    const float horizontal = glm::length(glm::vec2(direction.x, direction.z));

    return glm::vec2(
        glm::degrees(std::atan2(direction.z, direction.x)),
        glm::degrees(std::atan2(direction.y, horizontal)));
}
```

`<cmath>` is already included at the top of the file.

- [ ] **Step 6: Run the tests to verify they pass**

```bash
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64
```

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add Cubit/include/Cubit/Renderer/PerspectiveCamera.h Cubit/src/Renderer/PerspectiveCamera.cpp Tests/src/PerspectiveCameraTests.cpp Tests/Tests.vcxproj
git commit -m "Derive yaw and pitch from a point to look at"
```

---

### Task 3: `SpawnFinder` — the per-column rule

**Files:**
- Create: `Cubit/include/Cubit/Voxel/SpawnFinder.h`
- Create: `Cubit/src/Voxel/SpawnFinder.cpp`
- Create: `Tests/src/SpawnFinderTests.cpp`

**Interfaces:**
- Consumes: `World::GetWidth/GetHeight/GetDepth`, `World::IsBlockSolid`, `VoxelCollision::OverlapsFluid`.
- Produces: `CB_API std::optional<glm::vec3> FindSpawn(const World& world, const glm::ivec2& hintXZ, const glm::vec3& halfExtents)` and `constexpr int MaxSpawnSearchRadius = 64`.

This task builds the whole public signature but only the **hint column** behaviour — the spiral arrives in Task 4. Three new files, so Premake runs first again.

- [ ] **Step 1: Create the files and regenerate projects**

Create `Cubit/include/Cubit/Voxel/SpawnFinder.h`:

```cpp
#pragma once

#include "Cubit/Core.h"

#include <glm/glm.hpp>
#include <optional>

class World;

//How far the search spirals out from the hint before giving up, in columns.
//Wide enough to cross any feature on the shipped maps, bounded so a hint that
//can never work fails loudly instead of quietly scanning the whole world.
constexpr int MaxSpawnSearchRadius = 64;

//Finds somewhere a player box can stand near a hinted column, and returns the
//centre of the box there — the same point VoxelMoveResult reports, so the
//result assigns straight into a position collision already understands.
//
//Takes only an x and a z: the ground decides the height. Returns nothing when
//no column within MaxSpawnSearchRadius works.
CB_API std::optional<glm::vec3> FindSpawn(
    const World& world,
    const glm::ivec2& hintXZ,
    const glm::vec3& halfExtents);
```

Create `Cubit/src/Voxel/SpawnFinder.cpp`:

```cpp
#include "cub.h"

#include "Cubit/Voxel/SpawnFinder.h"

#include "Cubit/Voxel/VoxelCollision.h"
#include "Cubit/Voxel/World.h"
```

Create `Tests/src/SpawnFinderTests.cpp`:

```cpp
#include <doctest.h>

#include "Cubit/Voxel/Chunk.h"
#include "Cubit/Voxel/SpawnFinder.h"
#include "Cubit/Voxel/World.h"
```

Then, from the repo root:

```bash
premake5 vs2026
```

- [ ] **Step 2: Write the failing tests**

Append to `Tests/src/SpawnFinderTests.cpp`:

```cpp
namespace
{
    //Half extents of the 0.6 x 1.8 x 0.6 player box the Sandbox uses.
    const glm::vec3 PlayerHalfExtents{ 0.3f, 0.9f, 0.3f };

    //Fills the whole y=0 layer, giving every column something to stand on.
    void AddFloor(World& world)
    {
        for (int z = 0; z < world.GetDepth(); ++z)
            for (int x = 0; x < world.GetWidth(); ++x)
                world.SetBlock(x, 0, z, BlockId{1});
    }
}

TEST_CASE("A spawn stands on the floor at the hinted column")
{
    World world(1, 1, 1);
    AddFloor(world);

    const std::optional<glm::vec3> spawn =
        FindSpawn(world, glm::ivec2(8, 5), PlayerHalfExtents);

    REQUIRE(spawn.has_value());

    //Centred in the column, feet exactly on the floor's top face at y=1.
    CHECK(spawn->x == doctest::Approx(8.5f));
    CHECK(spawn->y == doctest::Approx(1.9f));
    CHECK(spawn->z == doctest::Approx(5.5f));
}

TEST_CASE("A spawn lands on top of a hill rather than inside it")
{
    //The exact failure this unit exists to prevent: a hand-picked constant that
    //ends up buried renders a black screen, which reads as a rendering bug.
    World world(1, 1, 1);
    AddFloor(world);
    for (int y = 1; y <= 6; ++y)
        world.SetBlock(8, y, 5, BlockId{1});

    const std::optional<glm::vec3> spawn =
        FindSpawn(world, glm::ivec2(8, 5), PlayerHalfExtents);

    REQUIRE(spawn.has_value());
    CHECK(spawn->y == doctest::Approx(7.9f));
}

TEST_CASE("An empty world has nowhere to spawn")
{
    const World world(1, 1, 1);

    CHECK_FALSE(FindSpawn(world, glm::ivec2(8, 8), PlayerHalfExtents).has_value());
}
```

- [ ] **Step 3: Run the tests to verify they fail**

```bash
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64
```

Expected: link error — `FindSpawn` is declared but has no definition.

- [ ] **Step 4: Implement the column rule**

Append to `Cubit/src/Voxel/SpawnFinder.cpp`:

```cpp
namespace
{
    //Tries one column: finds its surface and reports where a box would stand,
    //or nothing when the column cannot hold one.
    std::optional<glm::vec3> TryColumn(
        const World& world,
        int x,
        int z,
        const glm::vec3& halfExtents)
    {
        if (x < 0 || z < 0 || x >= world.GetWidth() || z >= world.GetDepth())
            return std::nullopt;

        for (int y = world.GetHeight() - 1; y >= 0; --y)
        {
            if (!world.IsBlockSolid(x, y, z))
                continue;

            //Feet exactly on the surface, so the first frame reports grounded
            //rather than spending one falling.
            //
            //Nothing can be in the way, so nothing checks for it: this is the
            //first solid block from the top, and the 0.6-wide box centred in a
            //1.0 cell never reaches a neighbouring column. A solid-overlap
            //check here would be unreachable code.
            const glm::vec3 centre(
                static_cast<float>(x) + 0.5f,
                static_cast<float>(y) + 1.0f + halfExtents.y,
                static_cast<float>(z) + 0.5f);

            //Standing on the riverbed puts the box in water, so this keeps a
            //spawn out of the river without needing a water-level constant.
            if (VoxelCollision::OverlapsFluid(world, centre, halfExtents))
                return std::nullopt;

            return centre;
        }

        //A column of pure air, or one of pure water.
        return std::nullopt;
    }
}

std::optional<glm::vec3> FindSpawn(
    const World& world,
    const glm::ivec2& hintXZ,
    const glm::vec3& halfExtents)
{
    return TryColumn(world, hintXZ.x, hintXZ.y, halfExtents);
}
```

Only the first solid block from the top is considered. A column whose surface fails either overlap check is rejected outright rather than searched further down — everything below that surface is under it, which is a cave, not open ground.

- [ ] **Step 5: Run the tests to verify they pass**

```bash
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add Cubit/include/Cubit/Voxel/SpawnFinder.h Cubit/src/Voxel/SpawnFinder.cpp Tests/src/SpawnFinderTests.cpp Cubit/Cubit.vcxproj Cubit/Cubit.vcxproj.filters Tests/Tests.vcxproj
git commit -m "Find a spawn on the surface of a hinted column"
```

---

### Task 4: `SpawnFinder` — the outward spiral

**Files:**
- Modify: `Cubit/src/Voxel/SpawnFinder.cpp` (the body of `FindSpawn`)
- Modify: `Tests/src/SpawnFinderTests.cpp` (append cases)

**Interfaces:**
- Consumes: `TryColumn` from Task 3.
- Produces: no signature change. `FindSpawn` now searches outward instead of testing one column.

- [ ] **Step 1: Write the failing tests**

Append to `Tests/src/SpawnFinderTests.cpp`:

```cpp
TEST_CASE("A spawn spirals off water onto the bank")
{
    //Water is present but not solid, so a box on the riverbed overlaps it.
    World world(1, 1, 1);
    Palette palette{};
    palette[1] = glm::vec4(0.4f, 0.8f, 0.3f, 1.0f);   //opaque ground
    palette[7] = glm::vec4(0.2f, 0.4f, 0.8f, 0.55f);  //water
    world.SetPalette(palette);

    AddFloor(world);
    //A 3x3 pool of water around the hint, deep enough to cover the box.
    for (int z = 4; z <= 6; ++z)
        for (int x = 7; x <= 9; ++x)
            for (int y = 1; y <= 3; ++y)
                world.SetBlock(x, y, z, BlockId{7});

    const std::optional<glm::vec3> spawn =
        FindSpawn(world, glm::ivec2(8, 5), PlayerHalfExtents);

    REQUIRE(spawn.has_value());

    //Somewhere outside the pool, still on the floor.
    const bool insidePool =
        spawn->x > 7.0f && spawn->x < 10.0f && spawn->z > 4.0f && spawn->z < 7.0f;
    CHECK_FALSE(insidePool);
    CHECK(spawn->y == doctest::Approx(1.9f));
}

TEST_CASE("A spawn stands on a floating lid rather than under it")
{
    //Pins the canopy behaviour deliberately: the scan takes the topmost solid
    //block, so a lid is a surface, not an obstacle. The same property is why
    //the column rule needs no solid-overlap check.
    World world(1, 1, 1);
    AddFloor(world);
    world.SetBlock(8, 4, 5, BlockId{1});

    const std::optional<glm::vec3> spawn =
        FindSpawn(world, glm::ivec2(8, 5), PlayerHalfExtents);

    REQUIRE(spawn.has_value());
    CHECK(spawn->x == doctest::Approx(8.5f));
    CHECK(spawn->z == doctest::Approx(5.5f));
    CHECK(spawn->y == doctest::Approx(5.9f));
}

TEST_CASE("A world of nothing but water has nowhere to spawn")
{
    //A different path from the empty world: blocks are present throughout, but
    //none of them is solid, so no column ever finds a surface.
    World world(1, 1, 1);
    Palette palette{};
    palette[7] = glm::vec4(0.2f, 0.4f, 0.8f, 0.55f);
    world.SetPalette(palette);

    for (int z = 0; z < world.GetDepth(); ++z)
        for (int x = 0; x < world.GetWidth(); ++x)
            for (int y = 0; y < 4; ++y)
                world.SetBlock(x, y, z, BlockId{7});

    CHECK_FALSE(FindSpawn(world, glm::ivec2(8, 5), PlayerHalfExtents).has_value());
}

TEST_CASE("A column solid to the top of the world is stood on, not rejected")
{
    //The box ends up partly above the world, which is open air and therefore
    //fine. Worth pinning: "solid to the sky" sounds like a rejection and is not.
    World world(1, 1, 1);
    AddFloor(world);
    for (int y = 1; y < world.GetHeight(); ++y)
        world.SetBlock(8, y, 5, BlockId{1});

    const std::optional<glm::vec3> spawn =
        FindSpawn(world, glm::ivec2(8, 5), PlayerHalfExtents);

    REQUIRE(spawn.has_value());
    CHECK(spawn->x == doctest::Approx(8.5f));
    CHECK(spawn->z == doctest::Approx(5.5f));
    CHECK(spawn->y == doctest::Approx(static_cast<float>(world.GetHeight()) + 0.9f));
}

TEST_CASE("A hint outside the world walks back into it")
{
    World world(1, 1, 1);
    AddFloor(world);

    const std::optional<glm::vec3> spawn =
        FindSpawn(world, glm::ivec2(-3, 5), PlayerHalfExtents);

    REQUIRE(spawn.has_value());
    CHECK(spawn->x == doctest::Approx(0.5f));
    CHECK(spawn->y == doctest::Approx(1.9f));
}

TEST_CASE("The same world and hint always give the same spawn")
{
    //The ring scan order is fixed so a spawn is reproducible: an intermittent
    //spawn would make every downstream failure intermittent too.
    World world(1, 1, 1);
    AddFloor(world);
    world.SetBlock(8, 1, 5, BlockId{1});

    const std::optional<glm::vec3> first =
        FindSpawn(world, glm::ivec2(8, 5), PlayerHalfExtents);
    const std::optional<glm::vec3> second =
        FindSpawn(world, glm::ivec2(8, 5), PlayerHalfExtents);

    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(*first == *second);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64
```

Expected: FAIL — the water-pool and out-of-bounds cases fail their `REQUIRE(spawn.has_value())`, because `FindSpawn` still tests only the hint column. Those two are the cases that genuinely need the spiral; the floating-lid, solid-to-the-top and all-water cases already pass, and are here to pin behaviour that reads like it ought to be a rejection and is not.

- [ ] **Step 3: Replace the body of `FindSpawn` with the spiral**

In `Cubit/src/Voxel/SpawnFinder.cpp`, replace the `FindSpawn` definition with:

```cpp
std::optional<glm::vec3> FindSpawn(
    const World& world,
    const glm::ivec2& hintXZ,
    const glm::vec3& halfExtents)
{
    //Rings of increasing Chebyshev distance, nearest first, each scanned in a
    //fixed order — so the answer is the closest usable column to the hint, and
    //the same one every run.
    for (int radius = 0; radius <= MaxSpawnSearchRadius; ++radius)
        for (int dz = -radius; dz <= radius; ++dz)
            for (int dx = -radius; dx <= radius; ++dx)
            {
                //Only this ring; everything inside it was tried at a smaller
                //radius.
                if (std::max(std::abs(dx), std::abs(dz)) != radius)
                    continue;

                const std::optional<glm::vec3> found =
                    TryColumn(world, hintXZ.x + dx, hintXZ.y + dz, halfExtents);

                if (found)
                    return found;
            }

    return std::nullopt;
}
```

Add `#include <algorithm>` and `#include <cstdlib>` to the file's includes for `std::max` and `std::abs`.

- [ ] **Step 4: Run the tests to verify they pass**

```bash
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64
```

Expected: PASS, including the Task 3 cases — a usable hint column is radius 0 and still wins.

- [ ] **Step 5: Commit**

```bash
git add Cubit/src/Voxel/SpawnFinder.cpp Tests/src/SpawnFinderTests.cpp
git commit -m "Spiral outward when the hinted column cannot hold a spawn"
```

---

### Task 5: Prove the spawn against the shipped 512 map

**Files:**
- Modify: `Tests/src/SpawnFinderTests.cpp` (append one case)

**Interfaces:**
- Consumes: `FindSpawn`, `VoxLoader::LoadFile`, `BuildWorld`.
- Produces: nothing new.

Synthetic worlds prove the rules; this proves them against the real terrain the Sandbox will use.

- [ ] **Step 1: Write the failing test**

Add these includes at the top of `Tests/src/SpawnFinderTests.cpp`:

```cpp
#include "Cubit/Voxel/VoxLoader.h"
#include "Cubit/Voxel/VoxelCollision.h"

#include <cmath>
#include <filesystem>
```

Append the case:

```cpp
TEST_CASE("A spawn on the shipped 512 battlefield is standable")
{
    //The suite runs from the repo root by hand and from Tests/ as a build step,
    //so try both. REQUIRE rather than an early return: a path-guarded test that
    //silently skips looks green while proving nothing.
    std::filesystem::path path;
    for (const char* candidate : {
            "Sandbox/assets/maps/battlefield512.vox",
            "../Sandbox/assets/maps/battlefield512.vox" })
        if (std::filesystem::exists(candidate))
        {
            path = candidate;
            break;
        }

    REQUIRE_FALSE(path.empty());

    const World world = BuildWorld(VoxLoader::LoadFile(path.string()));

    const glm::ivec2 hint(world.GetWidth() / 2, world.GetDepth() / 2);
    const std::optional<glm::vec3> spawn = FindSpawn(world, hint, PlayerHalfExtents);

    REQUIRE(spawn.has_value());

    //Not in the river. No solid-overlap check: it cannot fail by construction,
    //and a check that cannot fail is worse than no check — it reads as proof.
    CHECK_FALSE(VoxelCollision::OverlapsFluid(world, *spawn, PlayerHalfExtents));

    //Standing on something, not hovering.
    const int belowY = static_cast<int>(std::floor(spawn->y - PlayerHalfExtents.y - 0.5f));
    CHECK(world.IsBlockSolid(
        static_cast<int>(std::floor(spawn->x)),
        belowY,
        static_cast<int>(std::floor(spawn->z))));
}
```

- [ ] **Step 2: Run the test**

```bash
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64
```

Expected: PASS on the first run — the implementation is already complete, and this case exists to confirm the rules hold on real terrain rather than to drive new code. If it fails, the failure is real: read which check failed before changing anything.

Note the hint is the map centre, which on the battlefield is the **river**. A pass therefore also demonstrates the spiral doing its job, not just the column rule.

- [ ] **Step 3: Commit**

```bash
git add Tests/src/SpawnFinderTests.cpp
git commit -m "Check a spawn on the 512 battlefield is standable"
```

---

### Task 6: Wire the Sandbox to the resolved spawn and facing

**Files:**
- Modify: `Sandbox/src/Sandbox.cpp:34-43` (the constant), `:215`, `:382-395` (`LoadWorld`), `:417`, `:492`, and the constructor at `:96-111`

**Interfaces:**
- Consumes: `FindSpawn`, `PerspectiveCamera::YawPitchToward`, `PerspectiveCameraController::SetRotation`.
- Produces: nothing — this is the top of the stack.

- [ ] **Step 1: Add the include**

At the top of `Sandbox/src/Sandbox.cpp`, with the other Cubit includes:

```cpp
#include "Cubit/Voxel/SpawnFinder.h"
```

- [ ] **Step 2: Replace the spawn constant with a hint**

Replace the `SpawnPosition` constant and its comment (`Sandbox.cpp:34-43`) with:

```cpp
    //Roughly where to start. Only a column: the height, and whether this exact
    //column is usable at all, are resolved against the loaded map. A hint over
    //a hill or the river moves to the nearest spot that can hold the player
    //rather than burying the camera in terrain — which used to render as a
    //black screen and read as a rendering bug.
    const glm::ivec2 SpawnHintXZ{ 240, 300 };
```

- [ ] **Step 3: Add the resolved spawn member and its resolver**

Change the member at `Sandbox.cpp:492` from `glm::vec3 m_PlayerPosition{ SpawnPosition };` to:

```cpp
    glm::vec3 m_Spawn{ 0.0f };
    glm::vec3 m_PlayerPosition{ 0.0f };
```

Add this private method beside `LiftPlayerClearOfTerrain`:

```cpp
    //Resolves the spawn hint against the loaded map.
    void ResolveSpawn()
    {
        const std::optional<glm::vec3> found =
            FindSpawn(m_World, SpawnHintXZ, PlayerHalfExtents);

        if (found)
        {
            m_Spawn = *found;
            return;
        }

        //Nothing standable within the search radius. Drop in from above the
        //hint and say so: the whole point is that a bad spawn stops being a
        //silent black screen.
        CB_ERROR(
            "No spawn found within " + std::to_string(MaxSpawnSearchRadius) +
            " columns of " + std::to_string(SpawnHintXZ.x) + "," +
            std::to_string(SpawnHintXZ.y) + " — dropping in from above");

        m_Spawn = glm::vec3(
            static_cast<float>(SpawnHintXZ.x) + 0.5f,
            static_cast<float>(m_World.GetHeight()) - PlayerHalfExtents.y,
            static_cast<float>(SpawnHintXZ.y) + 0.5f);
    }
```

Add `#include <optional>` to the file's standard includes.

- [ ] **Step 4: Resolve the spawn inside `LoadWorld`**

In `LoadWorld` (`Sandbox.cpp:382-395`), insert `ResolveSpawn();` between `SkyLight::PropagateAll(m_World);` and `LiftPlayerClearOfTerrain();`:

```cpp
        //Light has to exist before anything meshes, or the first frames bake
        //a fully dark world into their vertex colours.
        SkyLight::PropagateAll(m_World);

        //Before the lift, not after: the lift's last-resort fallback is the
        //spawn, so it has to be valid for the world just loaded.
        ResolveSpawn();

        LiftPlayerClearOfTerrain();
```

- [ ] **Step 5: Point the two remaining uses at the resolved spawn**

At `Sandbox.cpp:215` (the fall-out-of-the-world reset) and `Sandbox.cpp:417` (the lift's fallback), replace `SpawnPosition` with `m_Spawn`.

- [ ] **Step 6: Place and aim the player in the constructor**

In the `SandboxLayer` constructor, after the existing `LoadWorld(MapPath);`:

```cpp
        //LoadWorld resolves the spawn but deliberately does not teleport to it:
        //F9 reloads mid-session and should leave the player where they were
        //working. Starting fresh is the one time it should.
        m_PlayerPosition = m_Spawn;

        //Face the middle of the map, level with the eye. Aiming at the literal
        //centre of the world box would tilt the view into the ground.
        const glm::vec3 eye = m_PlayerPosition + glm::vec3(0.0f, EyeOffset, 0.0f);
        const glm::vec3 target(
            static_cast<float>(m_World.GetWidth()) * 0.5f,
            eye.y,
            static_cast<float>(m_World.GetDepth()) * 0.5f);

        const glm::vec2 rotation = PerspectiveCamera::YawPitchToward(eye, target);
        m_CameraController.SetRotation(rotation.x, rotation.y);

        UpdateCameraPosition();
```

- [ ] **Step 7: Build the Sandbox**

```bash
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Sandbox/Sandbox.vcxproj -p:Configuration=Debug -p:Platform=x64
```

Expected: builds clean, and the Tests post-build step stays green.

- [ ] **Step 8: Commit**

```bash
git add Sandbox/src/Sandbox.cpp
git commit -m "Resolve the sandbox spawn and facing from the loaded map"
```

---

### Task 7: Verify on screen and update the docs

**Files:**
- Modify: `docs/engine-roadmap.md` (item 8, the arc summary, and the spawn follow-up)
- Modify: `README.md:180-182` (the camera-setter line under smaller gaps)

**Interfaces:** none.

Rendering is not unit tested in this project — it is verified by running the sandbox and looking at the result.

- [ ] **Step 1: Run the Sandbox and screenshot it**

Launch `bin/Debug-windows-x86_64/Sandbox/Sandbox.exe`. Grab the **GLFW30** window rather than the process's `MainWindowHandle`. Close it with `WM_CLOSE` — killing the process loses the log.

Expect: a lit view across the battlefield, not a black screen. Give the mesher time — a 512 map takes tens of seconds in a Debug build before it is fully meshed, so early frames legitimately show terrain still filling in.

- [ ] **Step 2: Check the HUD against the resolved spawn**

Confirm on the screenshot:
- `POS` matches the spawn the log resolved.
- `POS` y is **not drifting upward** across frames. Upward drift is collision pushing the player out of solid ground — the tell for a spawn still buried.
- The view is level and looks across the map, not into a hillside or straight at the ground.

- [ ] **Step 3: Update the roadmap**

In `docs/engine-roadmap.md`:
- Mark item 8 **Camera aim API** as `**DONE 2026-08-16**`, describing `SetRotation` on the controller, `YawPitchToward` on the camera, and `FindSpawn`.
- Update the "the one remaining engine gap is the camera aim API" sentence in the arc section — the engine gap list is now empty and what remains is threading (P8) and gameplay.
- Update the **"Nothing about spawning is map-aware"** follow-up: the hard-coded point is now a hint resolved against the map. What remains deliberately undone is that the *hint* is still authored by hand, and that `TerrainGen`'s absolute-sized features still do not scale.
- Change `_Last updated:_` to `2026-08-16`.

- [ ] **Step 4: Update the README**

In `README.md`, remove "a yaw/pitch setter on the camera controller so a spawn can choose its facing" from the smaller-gaps list at `:180-182`, since it is now done.

Leave the rest of the README's "What's next" section alone. It is stale in other ways — it still lists multi-model stitching as upcoming and describes the map as 256×64×256 — but that predates this work and fixing it here would mix two changes in one commit.

- [ ] **Step 5: Commit and push**

```bash
git add docs/engine-roadmap.md README.md
git commit -m "Record the camera aim API and map-aware spawn as shipped"
git push origin master
```

---

## Follow-ups this plan deliberately leaves alone

- **The README's "What's next" section is stale beyond the one line touched here** — it still lists multi-model stitching as upcoming and calls the map 256×64×256. Worth its own pass.
- **`TerrainGen` sizes features in absolute blocks**, so the forts are two 10-block specks 496 apart on a 512 map. A gameplay question, not a storage one.
- **P8: threading the load** — `SkyLight::PropagateAll` plus meshing on one thread is why a 512 map visibly builds itself for the first half minute in a Debug build.
