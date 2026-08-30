# MatchState (Networking Stage 1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move the game simulation behind a `MatchState` seam that a server and a client can both call, changing no observable behaviour and adding no networking.

**Architecture:** A new `MatchState` owns the `World`, a `PlayerId → CharacterController` map, and the simulation tick, exposing one `Step(commands, seconds)`. `CharacterInput` changes from a camera-resolved world-space direction to raw local axes plus yaw and pitch, so a server can reproduce movement rather than trust a client's vector. `SandboxLayer` stops simulating and becomes a caller.

**Tech Stack:** C++20, MSVC (Visual Studio 18 / vs2026), premake5, doctest, GLM. No new dependencies in this stage.

**Spec:** `docs/superpowers/specs/2026-08-27-networking-design.md`

## Global Constraints

- **C++20**, `cppdialect "C++20"` in `premake5.lua`. `std::span` is available.
- **`Cubit/src/Voxel/` and `Cubit/include/Cubit/Voxel/` must stay GL-free.** No `glad`, `GLFW`, or `gl*`. This is what lets the simulation run headless; it is verified and must not regress.
- **Any exported class (`CB_API`) with a `std::` or `glm::` member needs the 4251 pragma guard**, matching `World.h` and `CharacterController.h`:
  ```cpp
  #ifdef _MSC_VER
  #pragma warning(push)
  #pragma warning(disable: 4251)
  #endif
  // ... declarations ...
  #ifdef _MSC_VER
  #pragma warning(pop)
  #endif
  ```
- **Premake globs expand at generation time.** After adding any new file, run `/c/dev/premake/premake5 vs2026` from the repo root. Do **not** run `GenerateProjects.bat` — it deletes `bin/` and ends in `pause`, which hangs a non-interactive shell.
- **Tests include `<doctest.h>`**, not `<doctest/doctest.h>`.
- **Build command** (repo root):
  ```bash
  MSB="/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe"
  "$MSB" Cubit.slnx -p:Configuration=Debug -p:Platform=x64 -m -v:minimal -nologo
  ```
  The suite runs as a post-build step, so a failing test fails the build.
- **Never add Claude co-author trailers or attribution to commits.**
- **Behaviour must not change.** The acceptance check is the Sandbox reporting `POS 240.5 26.9 300.5` and `FACES 1927774`, identical to before this plan.

---

## File Structure

| File | Responsibility |
|---|---|
| `Cubit/include/Cubit/Voxel/Heading.h` | **Create.** Two free functions turning a yaw into ground-plane forward/right. GL-free, header-only. |
| `Tests/src/HeadingTests.cpp` | **Create.** Pins the yaw convention against `PerspectiveCamera`. |
| `Cubit/include/Cubit/Voxel/CharacterController.h` | **Modify.** `CharacterInput` gains `Yaw`/`Pitch`; `Move` becomes local axes. |
| `Cubit/src/Voxel/CharacterController.cpp` | **Modify.** `Step` resolves heading and caps speed. |
| `Tests/src/CharacterControllerTests.cpp` | **Modify.** Migrate 16 tests to the new input shape. |
| `Cubit/include/Cubit/Voxel/MatchState.h` | **Create.** Owns world, players, tick. |
| `Cubit/src/Voxel/MatchState.cpp` | **Create.** |
| `Tests/src/MatchStateTests.cpp` | **Create.** Registry, tick, independence, determinism. |
| `Cubit/include/Cubit/Cubit.h` | **Modify.** Export the two new headers. |
| `Sandbox/src/Sandbox.cpp` | **Modify.** Becomes a client of `MatchState`. |
| `docs/engine-roadmap.md`, `docs/superpowers/specs/2026-08-27-networking-design.md` | **Modify.** Record Stage 1 done. |

---

### Task 1: Heading — one yaw convention, pinned

The camera already turns yaw into a direction. A second copy of that formula is how `-90°` meaning `-z` becomes a bug again (it already cost a session on 2026-08-16). This task writes the GL-free copy and proves it agrees with the camera before anything depends on it.

**Files:**
- Create: `Cubit/include/Cubit/Voxel/Heading.h`
- Create: `Tests/src/HeadingTests.cpp`
- Modify: `Cubit/include/Cubit/Cubit.h`

**Interfaces:**
- Consumes: nothing.
- Produces: `glm::vec3 HeadingForward(float yawDegrees)` and `glm::vec3 HeadingRight(float yawDegrees)`, both unit-length and both with `y == 0`.

- [ ] **Step 1: Write the failing test**

Create `Tests/src/HeadingTests.cpp`:

```cpp
#include <doctest.h>

#include "Cubit/Voxel/Heading.h"
#include "Cubit/Renderer/PerspectiveCamera.h"

#include <glm/glm.hpp>

TEST_CASE("Heading matches the camera's flattened forward and right")
{
    //The oracle. PerspectiveCamera owns the yaw convention the whole engine
    //renders with; Heading is a second copy of it that cannot see the camera,
    //so it is checked against the original rather than against a formula
    //rewritten from the same memory that produced it.
    //
    //Pitch is varied deliberately: the camera builds forward from yaw AND
    //pitch and the old walk code flattened the result, so if Heading (yaw
    //only) is to replace that, it has to agree at every pitch. It does,
    //because PerspectiveCameraController clamps pitch to +/-89 degrees, so
    //cos(pitch) is always positive and drops out of the normalise.
    PerspectiveCamera camera(16.0f / 9.0f);

    for (float yaw = -360.0f; yaw <= 360.0f; yaw += 7.5f)
    {
        for (float pitch : { -89.0f, -45.0f, 0.0f, 45.0f, 89.0f })
        {
            camera.SetRotation(yaw, pitch);

            glm::vec3 forward = camera.GetForwardDirection();
            forward.y = 0.0f;
            forward = glm::normalize(forward);

            glm::vec3 right = camera.GetRightDirection();
            right.y = 0.0f;
            right = glm::normalize(right);

            const glm::vec3 headingForward = HeadingForward(yaw);
            const glm::vec3 headingRight = HeadingRight(yaw);

            INFO("yaw " << yaw << " pitch " << pitch);
            CHECK(headingForward.x == doctest::Approx(forward.x).epsilon(0.0001));
            CHECK(headingForward.y == doctest::Approx(0.0f));
            CHECK(headingForward.z == doctest::Approx(forward.z).epsilon(0.0001));
            CHECK(headingRight.x == doctest::Approx(right.x).epsilon(0.0001));
            CHECK(headingRight.y == doctest::Approx(0.0f));
            CHECK(headingRight.z == doctest::Approx(right.z).epsilon(0.0001));
        }
    }
}

TEST_CASE("Heading is unit length and flat")
{
    for (float yaw = -180.0f; yaw <= 180.0f; yaw += 11.0f)
    {
        CHECK(glm::length(HeadingForward(yaw)) == doctest::Approx(1.0f));
        CHECK(glm::length(HeadingRight(yaw)) == doctest::Approx(1.0f));
        CHECK(HeadingForward(yaw).y == doctest::Approx(0.0f));
        CHECK(HeadingRight(yaw).y == doctest::Approx(0.0f));
    }
}

TEST_CASE("Yaw zero faces positive x, with right toward positive z")
{
    //Written out so the convention is legible without deriving it. Movement
    //tests elsewhere use yaw 0 and rely on exactly this.
    CHECK(HeadingForward(0.0f).x == doctest::Approx(1.0f));
    CHECK(HeadingForward(0.0f).z == doctest::Approx(0.0f));
    CHECK(HeadingRight(0.0f).x == doctest::Approx(0.0f));
    CHECK(HeadingRight(0.0f).z == doctest::Approx(1.0f));
}
```

- [ ] **Step 2: Regenerate projects and run the test to verify it fails**

```bash
/c/dev/premake/premake5 vs2026
MSB="/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe"
"$MSB" Cubit.slnx -p:Configuration=Debug -p:Platform=x64 -m -v:minimal -nologo
```
Expected: compile error, `Cannot open include file: 'Cubit/Voxel/Heading.h'`.

- [ ] **Step 3: Write the implementation**

Create `Cubit/include/Cubit/Voxel/Heading.h`:

```cpp
#pragma once

#include <glm/glm.hpp>
#include <cmath>

//Ground-plane facing derived from a yaw in degrees.
//
//The same convention PerspectiveCamera renders with — yaw 0 faces +x, and yaw
//grows toward +z — reduced to the horizontal plane and stripped of any
//dependency on the renderer, so the simulation can resolve a heading with no
//camera and no GL context. HeadingTests pins it against the camera rather than
//against a second reading of the formula.
//
//Pitch is deliberately absent. Walking speed must not change when you look up
//or down, so the walk direction was always the camera's forward flattened; and
//because pitch is clamped to +/-89 degrees, cos(pitch) is strictly positive and
//divides out of that flatten. Yaw alone is therefore exact here, not an
//approximation of it.
inline glm::vec3 HeadingForward(float yawDegrees)
{
    const float yaw = glm::radians(yawDegrees);
    return glm::vec3(std::cos(yaw), 0.0f, std::sin(yaw));
}

//Ninety degrees clockwise of HeadingForward, matching
//cross(forward, worldUp) with worldUp = +y.
inline glm::vec3 HeadingRight(float yawDegrees)
{
    const float yaw = glm::radians(yawDegrees);
    return glm::vec3(-std::sin(yaw), 0.0f, std::cos(yaw));
}
```

- [ ] **Step 4: Export it**

In `Cubit/include/Cubit/Cubit.h`, add in alphabetical position among the `Voxel/` includes (after `Chunk.h`, before `ChunkMesher.h`):

```cpp
#include "Cubit/Voxel/Heading.h"
```

Note the existing block is ordered `Block.h`, `BlockEdit.h`, `CharacterController.h`, `Chunk.h`, `ChunkMesher.h`, `VoxelCollision.h`, `VoxelRaycast.h`, `World.h`. `Heading.h` sorts after `ChunkMesher.h`.

- [ ] **Step 5: Run the tests to verify they pass**

```bash
MSB="/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe"
"$MSB" Cubit.slnx -p:Configuration=Debug -p:Platform=x64 -m -v:minimal -nologo
```
Expected: `[doctest] Status: SUCCESS!` with 3 more test cases than before (297 total).

- [ ] **Step 6: Commit**

```bash
git add Cubit/include/Cubit/Voxel/Heading.h Tests/src/HeadingTests.cpp Cubit/include/Cubit/Cubit.h
git commit -F - <<'MSG'
Give the simulation its own yaw convention, pinned to the camera's

Movement is about to be resolved from a yaw on a machine with no camera
and no GL context, which means a second copy of the formula that turns a
yaw into a direction. A second copy of that formula is exactly how -90
degrees meaning -z became a bug once already.

So the copy is checked against the original rather than against a second
reading of it: HeadingTests sweeps yaw against PerspectiveCamera's own
forward and right, at five pitches including both clamp limits.

Pitch is absent from Heading on purpose and it is exact rather than
approximate. Walk direction was always the camera's forward flattened,
and pitch is clamped to +/-89 degrees, so cos(pitch) is strictly positive
and divides out of the normalise.
MSG
```

---

### Task 2: `CharacterInput` becomes local axes plus yaw

**Files:**
- Modify: `Cubit/include/Cubit/Voxel/CharacterController.h`
- Modify: `Cubit/src/Voxel/CharacterController.cpp`
- Modify: `Tests/src/CharacterControllerTests.cpp`

**Interfaces:**
- Consumes: `HeadingForward(float)`, `HeadingRight(float)` from Task 1.
- Produces: `CharacterInput { glm::vec2 Move; float Yaw; float Pitch; bool Jump; }` where `Move.x` is strafe and `Move.y` is forward, both in character-local space.

- [ ] **Step 1: Change the input struct**

In `Cubit/include/Cubit/Voxel/CharacterController.h`, replace the `Move` member and its comment, and add the two new fields:

```cpp
struct CharacterInput
{
    //Movement in the character's own frame: x strafes right, y walks forward.
    //Local rather than world-space so that a server can resolve the direction
    //itself from Yaw below, instead of trusting a vector a client computed.
    //
    //A length above 1 is capped after resolution rather than rejected, so
    //holding two keys walks at the same speed as holding one, and a future
    //analog stick at half deflection still walks at half speed.
    glm::vec2 Move{ 0.0f };

    //Where the character is looking, in degrees, in the same convention as
    //Heading.h and PerspectiveCamera.
    float Yaw = 0.0f;

    //Carried but not consumed by movement, deliberately: looking up must not
    //change walking. Remote-character rendering needs it, and hitscan will
    //need it, and adding a field to a wire format later is worse than
    //carrying an unused one now.
    float Pitch = 0.0f;

    //Held, not tapped. Jumping and swimming up are the same request; which one
    //happens depends on whether the character is in water.
    bool Jump = false;
};
```

- [ ] **Step 2: Resolve the heading inside `Step`**

In `Cubit/src/Voxel/CharacterController.cpp`, add the include:

```cpp
#include "Cubit/Voxel/Heading.h"
```

Then replace the line `glm::vec2 walk = input.Move * m_Config.WalkSpeed;` with:

```cpp
    // Resolve the character's own axes into the world here rather than taking
    // a world-space vector from the caller: this is the step a server has to
    // be able to reproduce, and reproducing it means deriving the direction
    // from the yaw rather than trusting a direction it was handed.
    const glm::vec3 forward = HeadingForward(input.Yaw);
    const glm::vec3 right = HeadingRight(input.Yaw);
    const glm::vec3 heading = forward * input.Move.y + right * input.Move.x;

    // Cap rather than normalise, so partial deflection still walks slowly
    // while two keys at once cannot outrun one.
    const float headingLength = glm::length(heading);
    const glm::vec3 direction =
        headingLength > 1.0f ? heading / headingLength : heading;

    glm::vec2 walk =
        glm::vec2(direction.x, direction.z) * m_Config.WalkSpeed;
```

The rest of `Step` is unchanged — `walk` is still an x/z pair consumed by the same `glm::vec3(walk.x, m_VerticalVelocity, walk.y)` motion vector.

- [ ] **Step 3: Run the tests to verify they fail**

```bash
MSB="/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe"
"$MSB" Cubit.slnx -p:Configuration=Debug -p:Platform=x64 -m -v:minimal -nologo
```
Expected: the walking tests in `CharacterControllerTests.cpp` fail. `Walking(glm::vec2(1.0f, 0.0f))` previously meant "+x in the world"; it now means "strafe right at yaw 0", which is +z. Tests that assert `Position().x` grows will fail.

- [ ] **Step 4: Migrate the test helper**

In `Tests/src/CharacterControllerTests.cpp`, replace the `Walking` and `Jumping` helpers:

```cpp
    //Movement is now in the character's frame, so a test says which way it is
    //facing. Yaw 0 faces +x with right toward +z (see HeadingTests), which is
    //what the position assertions below are written against.
    CharacterInput Walking(const glm::vec2& move, float yaw = 0.0f)
    {
        CharacterInput input;
        input.Move = move;
        input.Yaw = yaw;
        return input;
    }

    CharacterInput Jumping()
    {
        CharacterInput input;
        input.Jump = true;
        return input;
    }
```

- [ ] **Step 5: Migrate the three call sites that pass a direction**

At yaw 0, forward is +x and right is +z, so an old world-space `{x, z}` becomes a local `{z, x}` — the components swap.

In `TEST_CASE("A walking character is stopped by a wall")`, change:
```cpp
        character.Step(world, Walking(glm::vec2(1.0f, 0.0f)), Step);
```
to:
```cpp
        character.Step(world, Walking(glm::vec2(0.0f, 1.0f)), Step);
```

In `TEST_CASE("A character blocked on one axis still slides along the other")`, the vector is symmetric so it is unchanged, but add a comment above it:
```cpp
        // {1,1} at yaw 0 is strafe +z and forward +x, the same diagonal into
        // the wall the world-space version described.
        character.Step(world,
            Walking(glm::normalize(glm::vec2(1.0f, 1.0f))), Step);
```

In `TEST_CASE("Water drags a swimmer's walking speed down")`, change both:
```cpp
        wet.Step(world, Walking(glm::vec2(1.0f, 0.0f)), Step);
        dry.Step(air, Walking(glm::vec2(1.0f, 0.0f)), Step);
```
to:
```cpp
        wet.Step(world, Walking(glm::vec2(0.0f, 1.0f)), Step);
        dry.Step(air, Walking(glm::vec2(0.0f, 1.0f)), Step);
```

All other tests pass `CharacterInput{}` or `Jumping()` and need no change.

- [ ] **Step 6: Add tests for the new behaviour**

Append to `Tests/src/CharacterControllerTests.cpp`:

```cpp
TEST_CASE("Yaw turns which way forward is")
{
    //The same input walks a different way when the character faces a
    //different way. This is the property that lets a server resolve movement
    //from yaw instead of trusting a direction a client sent.
    World world = FlatWorld();

    CharacterController east;
    east.Teleport(glm::vec3(16.0f, 20.0f, 16.0f));
    SettleOnGround(east, world);

    CharacterController south;
    south.Teleport(glm::vec3(16.0f, 20.0f, 16.0f));
    SettleOnGround(south, world);

    for (int i = 0; i < 30; ++i)
    {
        east.Step(world, Walking(glm::vec2(0.0f, 1.0f), 0.0f), Step);
        south.Step(world, Walking(glm::vec2(0.0f, 1.0f), 90.0f), Step);
    }

    //Yaw 0 walks +x; yaw 90 walks +z.
    CHECK(east.Position().x > 16.5f);
    CHECK(east.Position().z == doctest::Approx(16.0f).epsilon(0.01));
    CHECK(south.Position().z > 16.5f);
    CHECK(south.Position().x == doctest::Approx(16.0f).epsilon(0.01));
}

TEST_CASE("Two keys at once do not walk faster than one")
{
    //The cap. Without it, holding forward and strafe walks sqrt(2) times
    //faster, which is the oldest speed exploit there is.
    World world = FlatWorld();

    CharacterController straight;
    straight.Teleport(glm::vec3(8.0f, 20.0f, 16.0f));
    SettleOnGround(straight, world);

    CharacterController diagonal;
    diagonal.Teleport(glm::vec3(8.0f, 20.0f, 16.0f));
    SettleOnGround(diagonal, world);

    const glm::vec3 start = straight.Position();

    for (int i = 0; i < 30; ++i)
    {
        straight.Step(world, Walking(glm::vec2(0.0f, 1.0f)), Step);
        diagonal.Step(world, Walking(glm::vec2(1.0f, 1.0f)), Step);
    }

    const float straightDistance =
        glm::length(straight.Position() - start);
    const float diagonalDistance =
        glm::length(diagonal.Position() - start);

    CHECK(diagonalDistance == doctest::Approx(straightDistance).epsilon(0.01));
}

TEST_CASE("Half deflection walks at half speed")
{
    //The reason movement is capped rather than normalised: an analog stick
    //pushed halfway should walk slowly, not snap to full speed.
    World world = FlatWorld();

    CharacterController full;
    full.Teleport(glm::vec3(8.0f, 20.0f, 16.0f));
    SettleOnGround(full, world);

    CharacterController half;
    half.Teleport(glm::vec3(8.0f, 20.0f, 16.0f));
    SettleOnGround(half, world);

    const glm::vec3 start = full.Position();

    for (int i = 0; i < 30; ++i)
    {
        full.Step(world, Walking(glm::vec2(0.0f, 1.0f)), Step);
        half.Step(world, Walking(glm::vec2(0.0f, 0.5f)), Step);
    }

    const float fullDistance = glm::length(full.Position() - start);
    const float halfDistance = glm::length(half.Position() - start);

    CHECK(halfDistance == doctest::Approx(fullDistance * 0.5f).epsilon(0.02));
}
```

- [ ] **Step 7: Run the tests to verify they pass**

```bash
MSB="/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe"
"$MSB" Cubit.slnx -p:Configuration=Debug -p:Platform=x64 -m -v:minimal -nologo
```
Expected: `SUCCESS!`, 300 test cases.

Note: the Sandbox will fail to compile at this point because `ReadWalkInput` still returns a world-space vector. That is expected and is fixed in Task 5. If the build stops before running tests, build only the Tests project:
```bash
"$MSB" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64 -m -v:minimal -nologo
```

- [ ] **Step 8: Commit**

```bash
git add Cubit/include/Cubit/Voxel/CharacterController.h Cubit/src/Voxel/CharacterController.cpp Tests/src/CharacterControllerTests.cpp
git commit -F - <<'MSG'
Move character input into the character's own frame

Movement arrived as a world-space direction the caller had already
resolved against the camera. A server cannot reproduce that step, only
trust its result - and it learns nothing about where the player is
looking, which remote-character rendering needs now and hitscan needs
later.

Move is now local - x strafes, y walks forward - alongside the yaw it is
resolved against, and Step does the resolving. Pitch rides along unused
on purpose: looking up must not change walking speed, but a wire format
gains a field far more cheaply now than later.

Speed is capped after resolution rather than the input normalised before
it, which fixes one bug and avoids another. Holding forward and strafe
no longer walks sqrt(2) times faster, and a half-deflected analog stick
still walks at half speed instead of snapping to full.

Three call sites in the tests swapped components: at yaw 0 forward is +x
and right is +z, so an old world {x,z} is a local {z,x}.
MSG
```

---

### Task 3: `MatchState` — world and player registry

**Files:**
- Create: `Cubit/include/Cubit/Voxel/MatchState.h`
- Create: `Cubit/src/Voxel/MatchState.cpp`
- Create: `Tests/src/MatchStateTests.cpp`
- Modify: `Cubit/include/Cubit/Cubit.h`

**Interfaces:**
- Consumes: `World`, `CharacterController`, `CharacterConfig`.
- Produces: `PlayerId` (alias for `std::uint16_t`), `InvalidPlayer`, `PlayerCommand`, and `MatchState` with `AddPlayer`, `RemovePlayer`, `HasPlayer`, `Player`, `TeleportPlayer`, `ReplaceWorld`, `GetWorld`, `Tick`. `Step` arrives in Task 4.

- [ ] **Step 1: Write the failing test**

Create `Tests/src/MatchStateTests.cpp`:

```cpp
#include <doctest.h>

#include "Cubit/Voxel/MatchState.h"
#include "Cubit/Voxel/World.h"

#include <glm/glm.hpp>

namespace
{
    //A world with a solid floor at y=0 and air above it.
    World FlatWorld()
    {
        World world(2, 2, 2);

        for (int z = 0; z < world.GetDepth(); ++z)
            for (int x = 0; x < world.GetWidth(); ++x)
                world.SetBlock(x, 0, z, BlockId{ 1 });

        return world;
    }
}

TEST_CASE("A new match has no players and a zero tick")
{
    MatchState match(FlatWorld());

    CHECK(match.Tick() == 0);
    CHECK_FALSE(match.HasPlayer(1));
}

TEST_CASE("Players get distinct ids and spawn where they are put")
{
    MatchState match(FlatWorld());

    const PlayerId first = match.AddPlayer(glm::vec3(4.0f, 10.0f, 5.0f));
    const PlayerId second = match.AddPlayer(glm::vec3(9.0f, 11.0f, 3.0f));

    CHECK(first != second);
    CHECK(first != InvalidPlayer);
    CHECK(second != InvalidPlayer);

    CHECK(match.HasPlayer(first));
    CHECK(match.HasPlayer(second));

    CHECK(match.Player(first).Position() == glm::vec3(4.0f, 10.0f, 5.0f));
    CHECK(match.Player(second).Position() == glm::vec3(9.0f, 11.0f, 3.0f));
}

TEST_CASE("A spawned player has nothing to interpolate through")
{
    //AddPlayer teleports rather than assigning, so the first frame after a
    //join does not smear the character in from wherever the default was.
    MatchState match(FlatWorld());
    const PlayerId player = match.AddPlayer(glm::vec3(4.0f, 10.0f, 5.0f));

    CHECK(match.Player(player).Position()
        == match.Player(player).PreviousPosition());
}

TEST_CASE("A removed player is gone and its id is not reused")
{
    //Not reused because an id identifies a participant across a match: a
    //stale packet naming a departed player must not be applied to whoever
    //joined next.
    MatchState match(FlatWorld());

    const PlayerId first = match.AddPlayer(glm::vec3(4.0f, 10.0f, 5.0f));
    match.RemovePlayer(first);

    CHECK_FALSE(match.HasPlayer(first));

    const PlayerId second = match.AddPlayer(glm::vec3(4.0f, 10.0f, 5.0f));
    CHECK(second != first);
}

TEST_CASE("Removing a player who is not there is not an error")
{
    //Once a removal can arrive from a socket, a duplicate disconnect is
    //malformed input rather than a caller bug.
    MatchState match(FlatWorld());

    match.RemovePlayer(77);
    CHECK_FALSE(match.HasPlayer(77));
}

TEST_CASE("Asking for a player who is not there throws")
{
    //The opposite choice from RemovePlayer, deliberately: there is no honest
    //CharacterController to return, and handing back a default one would
    //read as a character standing at the origin.
    MatchState match(FlatWorld());

    CHECK_THROWS(match.Player(5));
}

TEST_CASE("Teleporting a player moves both of its positions")
{
    MatchState match(FlatWorld());
    const PlayerId player = match.AddPlayer(glm::vec3(4.0f, 10.0f, 5.0f));

    match.TeleportPlayer(player, glm::vec3(20.0f, 6.0f, 21.0f));

    CHECK(match.Player(player).Position() == glm::vec3(20.0f, 6.0f, 21.0f));
    CHECK(match.Player(player).PreviousPosition()
        == glm::vec3(20.0f, 6.0f, 21.0f));
}

TEST_CASE("Replacing the world keeps the players and the tick")
{
    //What F9 does: reload the terrain being worked on, leave the player where
    //they were standing.
    MatchState match(FlatWorld());
    const PlayerId player = match.AddPlayer(glm::vec3(4.0f, 10.0f, 5.0f));

    World replacement(2, 2, 2);
    replacement.SetBlock(1, 1, 1, BlockId{ 3 });

    match.ReplaceWorld(std::move(replacement));

    CHECK(match.HasPlayer(player));
    CHECK(match.Player(player).Position() == glm::vec3(4.0f, 10.0f, 5.0f));
    CHECK(match.GetWorld().GetBlock(1, 1, 1) == BlockId{ 3 });
}
```

- [ ] **Step 2: Regenerate and run to verify it fails**

```bash
/c/dev/premake/premake5 vs2026
MSB="/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe"
"$MSB" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64 -m -v:minimal -nologo
```
Expected: `Cannot open include file: 'Cubit/Voxel/MatchState.h'`.

- [ ] **Step 3: Write the header**

Create `Cubit/include/Cubit/Voxel/MatchState.h`:

```cpp
#pragma once

#include "Cubit/Core.h"
#include "Cubit/Voxel/CharacterController.h"
#include "Cubit/Voxel/World.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <map>
#include <span>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

//Identifies one participant for the length of a match. Never reused, so a
//stale message naming someone who left is not applied to whoever joined next.
using PlayerId = std::uint16_t;

//Ids start at 1, so a zeroed or default-constructed id names nobody rather
//than naming the first player.
constexpr PlayerId InvalidPlayer = 0;

//One player's intent for one step.
struct PlayerCommand
{
    PlayerId Player = InvalidPlayer;
    CharacterInput Input;
};

//The whole simulated state of a match: the world, everyone in it, and how far
//it has been stepped.
//
//The point of this type is that there is exactly one implementation of a step
//and both a server and a client call it. If they ever step differently,
//reconciliation fights the player forever, and the symptom looks like a
//network fault rather than a simulation one.
//
//Deliberately not an entity system. There is one kind of actor, and an
//abstraction over one kind is a guess - the same reason a general entity
//system is still not built. See
//docs/superpowers/specs/2026-08-27-networking-design.md.
class CB_API MatchState
{
public:
    explicit MatchState(World world);

    //Adds a player standing at this position and returns their id.
    PlayerId AddPlayer(const glm::vec3& spawn);

    //Removes a player. Removing one who is not present does nothing.
    void RemovePlayer(PlayerId player);

    bool HasPlayer(PlayerId player) const;

    //Advances every present player by one fixed step, then increments the
    //tick. Commands naming absent players are ignored; see the note in
    //MatchState.cpp for why that is not an error.
    void Step(std::span<const PlayerCommand> commands, float seconds);

    //How many steps this match has taken. The authoritative clock a server
    //and a client agree on; it lives here rather than on FrameClock because
    //it is simulation state, not wall-clock state.
    std::uint64_t Tick() const { return m_Tick; }

    //Throws when the player is not present: there is no honest character to
    //return, and a default-constructed one reads as somebody standing at the
    //origin.
    const CharacterController& Player(PlayerId player) const;

    //Moves a player without interpolating through the space in between.
    void TeleportPlayer(PlayerId player, const glm::vec3& position);

    //Swaps the terrain, keeping every player and the tick. What reloading a
    //map mid-session does.
    void ReplaceWorld(World world);

    World& GetWorld() { return m_World; }
    const World& GetWorld() const { return m_World; }

private:
    World m_World;

    //Ordered rather than hashed, and that is load-bearing: iteration order is
    //part of what makes a step reproducible, and an unordered container makes
    //no promise about it across builds or insertion histories.
    std::map<PlayerId, CharacterController> m_Players;

    PlayerId m_NextPlayer = 1;
    std::uint64_t m_Tick = 0;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
```

- [ ] **Step 4: Write the implementation**

Create `Cubit/src/Voxel/MatchState.cpp`:

```cpp
#include "cub.h"

#include "Cubit/Voxel/MatchState.h"

#include <stdexcept>
#include <utility>

MatchState::MatchState(World world)
    : m_World(std::move(world))
{
}

PlayerId MatchState::AddPlayer(const glm::vec3& spawn)
{
    const PlayerId player = m_NextPlayer++;

    CharacterController character;

    // Teleport rather than assign, so both positions are written and the
    // first frame after a join does not interpolate the character in from
    // wherever a default-constructed one happened to be.
    character.Teleport(spawn);

    m_Players.emplace(player, character);
    return player;
}

void MatchState::RemovePlayer(PlayerId player)
{
    m_Players.erase(player);
}

bool MatchState::HasPlayer(PlayerId player) const
{
    return m_Players.find(player) != m_Players.end();
}

const CharacterController& MatchState::Player(PlayerId player) const
{
    const auto found = m_Players.find(player);

    if (found == m_Players.end())
        throw std::out_of_range("No such player in this match");

    return found->second;
}

void MatchState::TeleportPlayer(PlayerId player, const glm::vec3& position)
{
    const auto found = m_Players.find(player);

    if (found == m_Players.end())
        throw std::out_of_range("No such player in this match");

    found->second.Teleport(position);
}

void MatchState::ReplaceWorld(World world)
{
    m_World = std::move(world);
}
```

`Step` is deliberately not implemented yet — Task 4 adds it. Comment out or omit its declaration if the linker complains; it is only referenced from Task 4 onward.

- [ ] **Step 5: Export it**

In `Cubit/include/Cubit/Cubit.h`, add after `Cubit/Voxel/Heading.h`:

```cpp
#include "Cubit/Voxel/MatchState.h"
```

- [ ] **Step 6: Run the tests to verify they pass**

```bash
/c/dev/premake/premake5 vs2026
MSB="/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe"
"$MSB" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64 -m -v:minimal -nologo
```
Expected: `SUCCESS!`, 308 test cases.

- [ ] **Step 7: Commit**

```bash
git add Cubit/include/Cubit/Voxel/MatchState.h Cubit/src/Voxel/MatchState.cpp Tests/src/MatchStateTests.cpp Cubit/include/Cubit/Cubit.h
git commit -F - <<'MSG'
Give a match a world, a roster and a tick

The first half of the seam a server and a client will both step. This
commit is the state: the world, a player registry, and the tick that
counts how far the match has been advanced. Stepping arrives next.

The tick lives here rather than on FrameClock because it is simulation
state, not wall-clock state - the server's authoritative tick is the
match's tick, and a clock that also had one would be a second answer to
the same question.

Players are keyed in an ordered map, which is load-bearing rather than
incidental: iteration order is part of what makes a step reproducible,
and an unordered container promises nothing about it.

Ids are never reused, so a stale message naming someone who left cannot
be applied to whoever joined after them. Removing an absent player is
ignored while asking for one throws, which looks inconsistent and is
not: a duplicate disconnect off a socket is malformed input, but there
is no honest character to hand back from Player(), and a default one
would read as somebody standing at the origin.
MSG
```

---

### Task 4: `MatchState::Step` and the determinism guarantee

**Files:**
- Modify: `Cubit/src/Voxel/MatchState.cpp`
- Modify: `Tests/src/MatchStateTests.cpp`

**Interfaces:**
- Consumes: everything from Task 3, plus `CharacterController::Step` from Task 2.
- Produces: `void MatchState::Step(std::span<const PlayerCommand>, float seconds)`.

- [ ] **Step 1: Write the failing tests**

Append to `Tests/src/MatchStateTests.cpp`:

```cpp
namespace
{
    constexpr float StepSeconds = 1.0f / 60.0f;

    CharacterInput WalkForward()
    {
        CharacterInput input;
        input.Move = glm::vec2(0.0f, 1.0f);
        input.Yaw = 0.0f;
        return input;
    }
}

TEST_CASE("The tick advances once per step regardless of commands")
{
    MatchState match(FlatWorld());

    match.Step({}, StepSeconds);
    CHECK(match.Tick() == 1);

    const PlayerId player = match.AddPlayer(glm::vec3(16.0f, 10.0f, 16.0f));
    const PlayerCommand commands[] = { { player, WalkForward() } };

    match.Step(commands, StepSeconds);
    CHECK(match.Tick() == 2);
}

TEST_CASE("Players step independently")
{
    MatchState match(FlatWorld());

    const PlayerId walker = match.AddPlayer(glm::vec3(8.0f, 10.0f, 8.0f));
    const PlayerId idler = match.AddPlayer(glm::vec3(20.0f, 10.0f, 20.0f));

    const glm::vec3 idlerStart = match.Player(idler).Position();
    const PlayerCommand commands[] = { { walker, WalkForward() } };

    for (int i = 0; i < 60; ++i)
        match.Step(commands, StepSeconds);

    //The walker moved along x; the idler only fell.
    CHECK(match.Player(walker).Position().x > 8.5f);
    CHECK(match.Player(idler).Position().x == doctest::Approx(idlerStart.x));
    CHECK(match.Player(idler).Position().z == doctest::Approx(idlerStart.z));
}

TEST_CASE("A command naming an absent player is ignored")
{
    //Once commands arrive off a socket, a stale id is malformed input rather
    //than a caller bug, so this must not throw and must not invent a player.
    MatchState match(FlatWorld());

    const PlayerCommand commands[] = { { 999, WalkForward() } };

    CHECK_NOTHROW(match.Step(commands, StepSeconds));
    CHECK_FALSE(match.HasPlayer(999));
    CHECK(match.Tick() == 1);
}

TEST_CASE("Identical inputs produce bit-exact identical state")
{
    //The property every later networking stage rests on. Reconciliation means
    //replaying inputs from an authoritative state and expecting the same
    //answer the server got; if the same inputs can produce different results,
    //that correction never converges and the player is yanked about forever.
    //
    //Bit-exact rather than approximate on purpose. Within one binary, float
    //operations are reproducible, so any drift at all is a real defect -
    //an epsilon here would hide exactly what this test exists to catch.
    MatchState left(FlatWorld());
    MatchState right(FlatWorld());

    const PlayerId leftA = left.AddPlayer(glm::vec3(8.0f, 12.0f, 8.0f));
    const PlayerId leftB = left.AddPlayer(glm::vec3(20.0f, 14.0f, 9.0f));
    const PlayerId rightA = right.AddPlayer(glm::vec3(8.0f, 12.0f, 8.0f));
    const PlayerId rightB = right.AddPlayer(glm::vec3(20.0f, 14.0f, 9.0f));

    REQUIRE(leftA == rightA);
    REQUIRE(leftB == rightB);

    for (int i = 0; i < 240; ++i)
    {
        //Varying, so this exercises turning, jumping and idling rather than
        //one straight line where a whole class of drift would not show.
        CharacterInput a;
        a.Move = glm::vec2(0.0f, 1.0f);
        a.Yaw = static_cast<float>(i) * 3.0f;
        a.Jump = (i % 17) == 0;

        CharacterInput b;
        b.Move = glm::vec2(1.0f, static_cast<float>(i % 5) * 0.25f);
        b.Yaw = 45.0f - static_cast<float>(i);
        b.Jump = (i % 23) == 0;

        const PlayerCommand commands[] = { { leftA, a }, { leftB, b } };

        left.Step(commands, StepSeconds);
        right.Step(commands, StepSeconds);
    }

    CHECK(left.Tick() == right.Tick());
    CHECK(left.Player(leftA).Position() == right.Player(rightA).Position());
    CHECK(left.Player(leftB).Position() == right.Player(rightB).Position());
    CHECK(left.Player(leftA).VerticalVelocity()
        == right.Player(rightA).VerticalVelocity());
    CHECK(left.Player(leftB).VerticalVelocity()
        == right.Player(rightB).VerticalVelocity());
}

TEST_CASE("Command order does not change the result")
{
    //Characters do not collide with each other, so a step must not depend on
    //which order the server happened to read packets in. If this ever fails,
    //players have started interacting and the fix is an explicit ordering
    //rule, not a reshuffle.
    MatchState forward(FlatWorld());
    MatchState reversed(FlatWorld());

    const PlayerId fa = forward.AddPlayer(glm::vec3(8.0f, 12.0f, 8.0f));
    const PlayerId fb = forward.AddPlayer(glm::vec3(9.0f, 12.0f, 8.0f));
    const PlayerId ra = reversed.AddPlayer(glm::vec3(8.0f, 12.0f, 8.0f));
    const PlayerId rb = reversed.AddPlayer(glm::vec3(9.0f, 12.0f, 8.0f));

    for (int i = 0; i < 60; ++i)
    {
        const PlayerCommand ordered[] = {
            { fa, WalkForward() }, { fb, WalkForward() } };
        const PlayerCommand backward[] = {
            { rb, WalkForward() }, { ra, WalkForward() } };

        forward.Step(ordered, StepSeconds);
        reversed.Step(backward, StepSeconds);
    }

    CHECK(forward.Player(fa).Position() == reversed.Player(ra).Position());
    CHECK(forward.Player(fb).Position() == reversed.Player(rb).Position());
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
MSB="/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe"
"$MSB" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64 -m -v:minimal -nologo
```
Expected: unresolved external symbol `MatchState::Step`.

- [ ] **Step 3: Implement `Step`**

Add to `Cubit/src/Voxel/MatchState.cpp`:

```cpp
void MatchState::Step(std::span<const PlayerCommand> commands, float seconds)
{
    // Everyone gets a step, including players nobody sent a command for this
    // tick: a dropped or late packet must leave a character falling under
    // gravity rather than frozen in the air.
    for (auto& entry : m_Players)
    {
        const PlayerId player = entry.first;

        CharacterInput input;

        for (const PlayerCommand& command : commands)
        {
            if (command.Player == player)
            {
                input = command.Input;
                break;
            }
        }

        entry.second.Step(m_World, input, seconds);
    }

    // A command naming a player who is not here is ignored rather than
    // rejected. Once these arrive off a socket a stale id is malformed input,
    // not a caller bug - the same reasoning that makes ApplyBlockEdit return
    // nullopt for an out-of-range position instead of throwing.

    ++m_Tick;
}
```

Note the linear scan over `commands`: with two players it is faster than building a lookup, and the container is a `span` the caller owns. If a match ever holds enough players for this to matter, that is the moment to change it, and the determinism tests pin the behaviour while it changes.

- [ ] **Step 4: Run the tests to verify they pass**

```bash
MSB="/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe"
"$MSB" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64 -m -v:minimal -nologo
```
Expected: `SUCCESS!`, 313 test cases.

- [ ] **Step 5: Prove the determinism test can fail**

An assertion that cannot fail proves nothing — the `BlockEdit` review caught exactly this once already. Temporarily break reproducibility by making the step order depend on insertion rather than key. In `MatchState.h`, change `std::map` to `std::unordered_map` and add `#include <unordered_map>`, then rebuild and run.

Expected: it may or may not fail, because `unordered_map` over two small integer keys often iterates in insertion order anyway. If it does **not** fail, use this stronger mutation instead — in `Step`, scale the timestep by the player id:

```cpp
        entry.second.Step(m_World, input, seconds * (1.0f + player * 1e-7f));
```

Expected: "Identical inputs produce bit-exact identical state" fails on a position comparison.

**Revert the mutation** (restore `std::map` and the unmodified `Step`), rebuild, and confirm `SUCCESS!` before continuing.

- [ ] **Step 6: Commit**

```bash
git add Cubit/src/Voxel/MatchState.cpp Tests/src/MatchStateTests.cpp
git commit -F - <<'MSG'
Step a whole match, reproducibly

The other half of the seam. Every present player advances one fixed step
and the tick increments once, whatever the commands say.

Players without a command this tick still step. A dropped or late packet
has to leave a character falling under gravity, not frozen in the air,
and that is the normal case on a real connection rather than an edge one.

Two tests carry the property the rest of the networking arc depends on.
Identical inputs must produce bit-exact identical state, because
reconciliation replays inputs from an authoritative state and expects
the server's answer - if the same inputs can diverge, the correction
never converges and the player is yanked about forever. Exact rather
than approximate comparison is the point: within one binary float
operations are reproducible, so any drift at all is a defect and an
epsilon would hide precisely what the test exists to catch.

And command order must not change the result, since a server cannot
control which order packets arrive in. Both were mutation-tested by
scaling the timestep per player id and watching the first fail.
MSG
```

---

### Task 5: The Sandbox becomes a client

**Files:**
- Modify: `Sandbox/src/Sandbox.cpp`

**Interfaces:**
- Consumes: `MatchState`, `PlayerCommand`, `PlayerId`, the new `CharacterInput`.
- Produces: nothing later tasks depend on.

- [ ] **Step 1: Replace the world and player members**

In the member block at the bottom of `SandboxLayer`, replace:

```cpp
    World m_World{ 1, 1, 1 };
```
and
```cpp
    CharacterController m_Player;
```
with:

```cpp
    //The simulation. One player today; the type is the seam a server will
    //step identically, which is why the Sandbox goes through it rather than
    //owning a world and a character directly.
    MatchState m_Match{ World(1, 1, 1) };
    PlayerId m_LocalPlayer = InvalidPlayer;
```

- [ ] **Step 2: Add accessors so the rest of the layer reads naturally**

Add to the `private:` section of `SandboxLayer`:

```cpp
    //Shorthands, because the layer reads the world and its own character on
    //nearly every line and m_Match.GetWorld() everywhere obscures them.
    World& World_() { return m_Match.GetWorld(); }
    const World& World_() const { return m_Match.GetWorld(); }
    const CharacterController& Player_() const
    {
        return m_Match.Player(m_LocalPlayer);
    }
```

- [ ] **Step 3: Replace every `m_World` and `m_Player` use**

```bash
sed -i 's/\bm_World\b/World_()/g; s/\bm_Player\./Player_()./g' Sandbox/src/Sandbox.cpp
```

Then fix the four places that need more than a rename:

`m_Player.Teleport(m_Spawn)` in the constructor becomes, after `LoadWorld` returns:
```cpp
        m_LocalPlayer = m_Match.AddPlayer(m_Spawn);
```

`Player_().SetVerticalVelocity(0.0f)` will not compile — `Player_()` is const. In `LoadWorld` and in the fall-reset, use:
```cpp
        m_Match.TeleportPlayer(m_LocalPlayer, m_Spawn);
```
and delete the `SetVerticalVelocity(0.0f)` lines for now; Step 6 restores that behaviour.

`m_Player.Teleport(lifted)` in `LiftPlayerClearOfTerrain` becomes:
```cpp
        m_Match.TeleportPlayer(m_LocalPlayer, lifted);
```

- [ ] **Step 4: Restore velocity clearing**

`MatchState` has no velocity setter and should not grow one for this — clearing fall speed on a respawn is a Sandbox rule. Add to `MatchState.h` in the public section:

```cpp
    //Non-const access to a player, for callers that own game rules the match
    //itself does not - respawning, and clearing fall speed with it.
    CharacterController& PlayerForWrite(PlayerId player);
```

and to `MatchState.cpp`:

```cpp
CharacterController& MatchState::PlayerForWrite(PlayerId player)
{
    const auto found = m_Players.find(player);

    if (found == m_Players.end())
        throw std::out_of_range("No such player in this match");

    return found->second;
}
```

Then in the Sandbox, both respawn sites become:

```cpp
        m_Match.TeleportPlayer(m_LocalPlayer, m_Spawn);
        m_Match.PlayerForWrite(m_LocalPlayer).SetVerticalVelocity(0.0f);
```

- [ ] **Step 5: Rewrite `ReadWalkInput` to return local axes**

Replace the whole function with:

```cpp
    //Returns held movement keys in the character's own frame: x strafes, y
    //walks forward. No camera maths here any more - the simulation resolves
    //the direction from the yaw, which is what lets a server reproduce the
    //step rather than trust a vector this machine computed.
    glm::vec2 ReadWalkInput() const
    {
        glm::vec2 move{ 0.0f };

        if (Input::IsKeyPressed(KeyCode::W))
            move.y += 1.0f;
        if (Input::IsKeyPressed(KeyCode::S))
            move.y -= 1.0f;
        if (Input::IsKeyPressed(KeyCode::D))
            move.x += 1.0f;
        if (Input::IsKeyPressed(KeyCode::A))
            move.x -= 1.0f;

        return move;
    }
```

Note there is no normalisation: `CharacterController::Step` caps the resolved vector, so pressing W and D is capped rather than scaled here.

- [ ] **Step 6: Rewrite `OnFixedUpdate`**

```cpp
    void OnFixedUpdate(Timestep timestep) override
    {
        ++m_StepsThisFrame;

        CharacterInput input;
        input.Move = ReadWalkInput();
        input.Yaw = m_CameraController.GetYaw();
        input.Pitch = m_CameraController.GetPitch();
        input.Jump = Input::IsKeyPressed(KeyCode::Space);

        const PlayerCommand commands[] = { { m_LocalPlayer, input } };
        m_Match.Step(commands, static_cast<float>(timestep.GetSeconds()));

        m_HudState->PlayerPosition = Player_().Position();
        m_HudState->Grounded = Player_().Grounded();
        m_HudState->BodyInFluid = Player_().BodyInFluid();
        m_HudState->EyeInFluid = Player_().EyeInFluid();

        // Falling off the edge of the map is a Sandbox rule rather than
        // character physics, so it stays here.
        if (Player_().Position().y < FallResetHeight)
        {
            m_Match.TeleportPlayer(m_LocalPlayer, m_Spawn);
            m_Match.PlayerForWrite(m_LocalPlayer).SetVerticalVelocity(0.0f);
        }
    }
```

- [ ] **Step 7: Point `LoadWorld` at `ReplaceWorld`**

`LoadWorld` currently assigns to `m_World`. Find the assignment (it builds a `World` from `BuildWorld(...)`) and change it to build a local `World` and then:

```cpp
        m_Match.ReplaceWorld(std::move(loaded));
```

The constructor path runs `LoadWorld` **before** `AddPlayer`, so on first construction there is no player to preserve and `ReplaceWorld` is simply how the world arrives.

- [ ] **Step 8: Build and run the tests**

```bash
MSB="/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe"
"$MSB" Cubit.slnx -p:Configuration=Debug -p:Platform=x64 -m -v:minimal -nologo
```
Expected: `SUCCESS!`, 313 test cases, and `Sandbox.exe` links.

- [ ] **Step 9: Verify behaviour is unchanged on screen**

Run the Sandbox from its own directory (the map path is relative), wait ~45 s for meshing, and read the HUD.

```bash
cd bin/Debug-windows-x86_64/Sandbox && ./Sandbox.exe
```

Expected, identical to before this plan:
- `POS 240.5 26.9 300.5`
- `FACES 1927774`
- `PEND NG 0` once meshing finishes

Then walk with WASD and confirm the direction matches where you are looking, jump with Space, and swim in the river. **Movement direction is the thing most likely to be wrong** — a sign error in `HeadingRight` inverts strafing, which Task 1's tests would catch, but confirm by hand anyway.

- [ ] **Step 10: Commit**

```bash
git add Sandbox/src/Sandbox.cpp Cubit/include/Cubit/Voxel/MatchState.h Cubit/src/Voxel/MatchState.cpp
git commit -F - <<'MSG'
Make the Sandbox a client of the match rather than the simulation

SandboxLayer owned a World and a CharacterController and stepped them
itself. It now owns a MatchState and calls it, which is the whole point
of the stage: one implementation of a step, and the Sandbox is simply
its first caller. A server will be the second.

ReadWalkInput stops doing camera maths and reports held keys in the
character's own frame; the yaw it is resolved against travels with it.
That moves the resolution to the one place a server can also run.

Nothing observable changes - same spawn at POS 240.5 26.9 300.5, same
FACES 1927774.
MSG
```

---

### Task 6: Record Stage 1 as done

**Files:**
- Modify: `docs/superpowers/specs/2026-08-27-networking-design.md`
- Modify: `docs/engine-roadmap.md`

- [ ] **Step 1: Mark the stage in the spec**

Change the status line at the top of the spec from:
```markdown
_Written 2026-08-27. Status: Stage 1 designed, Stages 2 and 3 shaped only._
```
to:
```markdown
_Written 2026-08-27. Status: **Stage 1 shipped 2026-08-30.** Stages 2 and 3 shaped only._
```

Add at the end of the Stage 1 section:

```markdown
### Shipped 2026-08-30

`MatchState` owns the world, an ordered player registry and the tick;
`CharacterInput` carries local axes plus yaw and pitch; `SandboxLayer` is a
caller rather than a simulator. Behaviour is unchanged — same spawn, same face
count.

Two things this stage added that the design did not call for. Movement is now
**capped after resolution rather than normalised before it**, which fixes
diagonal walking being `sqrt(2)` times faster and leaves room for analog input.
And `Heading.h` exists as a separate GL-free unit pinned against
`PerspectiveCamera`, rather than the resolution living inline in the
controller, because the yaw convention is the thing most likely to be
duplicated wrongly.
```

- [ ] **Step 2: Note it on the roadmap**

In `docs/engine-roadmap.md`, under the entity bullet's "Half done 2026-08-27" paragraph, append:

```markdown
  **Stage 1 of the networking arc shipped 2026-08-30**, adding `MatchState` above
  the controller: it owns the world, the roster and the tick, and both a server
  and a client will step it. Still not an entity system, and still deliberately
  so — one kind of actor.
```

- [ ] **Step 3: Commit and push**

```bash
git add docs/
git commit -F - <<'MSG'
Record networking stage 1 as shipped

Marks the stage in the spec and notes on the roadmap that MatchState now
sits above the character controller. Records the two things the stage
added that the design did not ask for: capping movement after resolution
rather than normalising before it, and Heading as its own unit pinned
against the camera.
MSG
git push origin master
```

---

## Self-Review

**Spec coverage.** Every Stage 1 item in the spec maps to a task: `MatchState` type (Tasks 3, 4), `CharacterInput` reshape (Task 2), `SandboxLayer` as caller (Task 5), determinism test (Task 4), yaw-convention test (Task 1), tick behaviour (Task 4), absent-player handling (Tasks 3, 4), `ReplaceWorld` (Task 3), existing test migration (Task 2). Stages 2 and 3 are deliberately not covered — they get their own specs.

**Deviation from the spec, recorded deliberately.** The spec's `MatchState` sketch had no `PlayerForWrite`. It is added in Task 5 Step 4 because respawning must clear fall speed and that is a Sandbox rule the match should not own. The alternative — a `MatchState::SetPlayerVelocity` — would put a game rule in the simulation type. Worth flagging at review.

**Type consistency.** `PlayerId`, `InvalidPlayer`, `PlayerCommand`, `MatchState::Step`, `Player`, `PlayerForWrite`, `TeleportPlayer`, `ReplaceWorld`, `GetWorld`, `Tick`, `HeadingForward`, `HeadingRight`, `CharacterInput::{Move,Yaw,Pitch,Jump}` are spelled identically everywhere they appear.

**Test-count arithmetic.** 294 before → 297 after Task 1 (+3) → 300 after Task 2 (+3) → 308 after Task 3 (+8) → 313 after Task 4 (+5).

**Known risk.** Task 5 Step 3 uses `sed` for a bulk rename. Read the diff before committing; `Player_().` will not match `m_Player{` style uses if any remain.
