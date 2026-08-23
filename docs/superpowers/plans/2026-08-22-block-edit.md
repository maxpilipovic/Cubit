# BlockEdit Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make a block edit a value that can be applied, reversed, and later sent or replayed, instead of four steps the Sandbox performs inline.

**Architecture:** `BlockEdit` is a position and a block id — an intent. `ApplyBlockEdit(World&, const BlockEdit&)` applies it, relights the affected region, and returns the edit that would undo it, or `std::nullopt` if nothing changed. The Sandbox routes its one edit site through it and keeps a stack of the returned inverses, which `U` pops.

**Tech Stack:** C++20, GLM, doctest, premake5 (vs2026), OpenGL/GLFW for the Sandbox only.

**Spec:** `docs/superpowers/specs/2026-08-22-block-edit-design.md`

## Global Constraints

- **Build (Tests):** `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64` — FORWARD slashes in the `.vcxproj` path, or MSBuild fails with MSB1009.
- **Build (Sandbox):** same command with `Sandbox/Sandbox.vcxproj`.
- The suite runs as a post-build step, so its stdout appears in the build log. A failing test breaks the build. The suite currently has **252 cases**.
- Run a single case: `bin/Debug-windows-x86_64/Tests/Tests.exe -tc="<case name>"`.
- **`ChunkMesherTests.cpp:323` asserts `ms < 50.0` on a Debug wall clock and is known flaky under load** — it failed about half of full-suite runs on 2026-08-22 and was proved pre-existing. A failure there in the 50–70 ms band is machine load, not your regression. Re-run before investigating; treat any *other* failure as real.
- **Create every new source file BEFORE running `premake5 vs2026`, and regenerate once.** premake expands its `files` globs (`Cubit/src/**.cpp`, `Cubit/include/**.h`, `Tests/src/**.cpp`) at generation time, so a file created afterwards is invisible to the build — and the resulting link error is indistinguishable from a genuine TDD red state. Do NOT run `GenerateProjects.bat`: it deletes `bin/` and ends in a blocking `pause`.
- Every `.cpp` under `Cubit/src/` must begin with `#include "cub.h"` — it is the precompiled header (`pchheader "cub.h"`).
- Public engine classes carry `CB_API` (from `Cubit/Core.h`). A free function that must be callable from the Sandbox needs `CB_API` on its declaration.
- Comment style: `//` with NO space before the text for doc comments on declarations; `// ` WITH a space for prose inside function bodies.
- `Cubit/src/Voxel/` includes no OpenGL headers, and that is load-bearing for an eventual headless server. Nothing added there may include GL.
- Commit after each task, with **one short imperative subject line, a blank line, then a short prose paragraph saying why**. Wrapped prose, not bullets. Push to `master` directly — no feature branches. **Never add a Co-Authored-By trailer or any assistant attribution.**

---

### Task 1: `BlockEdit` and `ApplyBlockEdit`

The whole seam, under test, before anything consumes it.

**Files:**
- Create: `Cubit/include/Cubit/Voxel/BlockEdit.h`
- Create: `Cubit/src/Voxel/BlockEdit.cpp`
- Test: `Tests/src/BlockEditTests.cpp`

**Interfaces:**
- Consumes: `World` (`Cubit/Voxel/World.h`) — `GetBlock`, `SetBlock`, `IsInBounds`; `SkyLight::Repropagate(World&, int, int, int)` (`Cubit/Voxel/SkyLight.h`); `BlockId` (`Cubit/Voxel/Block.h`).
- Produces: `struct BlockEdit { glm::ivec3 Position; BlockId Block; };` and `std::optional<BlockEdit> ApplyBlockEdit(World& world, const BlockEdit& edit);`. Task 2 calls both.

- [ ] **Step 1: Create the header**

Create `Cubit/include/Cubit/Voxel/BlockEdit.h`:

```cpp
#pragma once

#include "Cubit/Core.h"
#include "Cubit/Voxel/Block.h"

#include <glm/glm.hpp>

#include <optional>

class World;

//One block changing at one position.
//
//An intent, not a record: it says what the world should become, not what it was.
//A client sending an edit cannot honestly report the previous block — it only
//believes it knows — so the previous value comes back from applying instead.
struct BlockEdit
{
    glm::ivec3 Position{ 0 };
    BlockId Block = 0;
};

//Applies an edit and returns the edit that would undo it, or nothing if the
//world did not change.
//
//Relights the affected region as part of applying, so an edit is one operation
//that leaves the world consistent rather than a ritual every caller has to know
//the second half of.
//
//An out-of-range position returns nothing rather than throwing, unlike
//World::SetBlock: once an edit is data that can arrive from a file or a socket,
//a bad coordinate is malformed input rather than a bug in the caller.
CB_API std::optional<BlockEdit> ApplyBlockEdit(World& world, const BlockEdit& edit);
```

- [ ] **Step 2: Create the implementation as a stub**

Create `Cubit/src/Voxel/BlockEdit.cpp` with the real signature and an empty body. It exists now, before the premake run, so one regeneration covers all three new files — and so the red state is failing assertions rather than a link error, which would look identical to a build misconfiguration:

```cpp
#include "cub.h"

#include "Cubit/Voxel/BlockEdit.h"

#include "Cubit/Voxel/SkyLight.h"
#include "Cubit/Voxel/World.h"

std::optional<BlockEdit> ApplyBlockEdit(World& world, const BlockEdit& edit)
{
    (void)world;
    (void)edit;
    return std::nullopt;
}
```

- [ ] **Step 3: Create the test file**

Create `Tests/src/BlockEditTests.cpp`:

```cpp
#include <doctest.h>

#include "Cubit/Voxel/BlockEdit.h"
#include "Cubit/Voxel/SkyLight.h"
#include "Cubit/Voxel/World.h"

#include <optional>
#include <string>
#include <vector>

namespace
{
    //A 32x32x32 world whose lower ten layers are solid, with a sealed air
    //chamber buried inside it. The chamber is what makes the light round-trip
    //test meaningful: breaking its roof floods it, and undoing that must empty
    //it again.
    World MakeChamberWorld()
    {
        World world(2, 2, 2);

        for (int z = 0; z < world.GetDepth(); ++z)
            for (int x = 0; x < world.GetWidth(); ++x)
                for (int y = 0; y <= 9; ++y)
                    world.SetBlock(x, y, z, BlockId{1});

        // Hollow out y=5..8 in the middle, leaving y=9 as an intact roof.
        for (int z = 12; z <= 20; ++z)
            for (int x = 12; x <= 20; ++x)
                for (int y = 5; y <= 8; ++y)
                    world.SetBlock(x, y, z, BlockId{0});

        return world;
    }

    //Every sky-light value in the world, in z, y, x order.
    std::vector<std::uint8_t> SnapshotLight(const World& world)
    {
        std::vector<std::uint8_t> light;
        light.reserve(static_cast<std::size_t>(world.GetWidth()) *
            world.GetHeight() * world.GetDepth());

        for (int z = 0; z < world.GetDepth(); ++z)
            for (int y = 0; y < world.GetHeight(); ++y)
                for (int x = 0; x < world.GetWidth(); ++x)
                    light.push_back(world.GetSkyLight(x, y, z));

        return light;
    }

    //The first cell whose light differs from the snapshot, or nothing.
    //
    //A position rather than a bool: a lighting disagreement hides in one corner
    //out of tens of thousands, and "not equal" is useless for finding it.
    std::optional<glm::ivec3> FirstLightDifference(
        const World& world,
        const std::vector<std::uint8_t>& before)
    {
        std::size_t index = 0;
        for (int z = 0; z < world.GetDepth(); ++z)
            for (int y = 0; y < world.GetHeight(); ++y)
                for (int x = 0; x < world.GetWidth(); ++x)
                {
                    if (before[index++] != world.GetSkyLight(x, y, z))
                        return glm::ivec3(x, y, z);
                }

        return std::nullopt;
    }

    std::string Describe(const std::optional<glm::ivec3>& cell)
    {
        if (!cell)
            return "none";

        return std::to_string(cell->x) + "," +
            std::to_string(cell->y) + "," +
            std::to_string(cell->z);
    }
}

TEST_CASE("Applying an edit changes the block")
{
    World world(1, 1, 1);

    const std::optional<BlockEdit> inverse =
        ApplyBlockEdit(world, BlockEdit{ glm::ivec3(4, 5, 6), BlockId{7} });

    REQUIRE(inverse.has_value());
    CHECK(world.GetBlock(4, 5, 6) == BlockId{7});
}

TEST_CASE("The inverse carries the position and the previous block")
{
    World world(1, 1, 1);
    world.SetBlock(4, 5, 6, BlockId{3});

    const std::optional<BlockEdit> inverse =
        ApplyBlockEdit(world, BlockEdit{ glm::ivec3(4, 5, 6), BlockId{7} });

    REQUIRE(inverse.has_value());
    CHECK(inverse->Position == glm::ivec3(4, 5, 6));
    CHECK(inverse->Block == BlockId{3});
}

TEST_CASE("Applying the inverse restores the previous block")
{
    World world(1, 1, 1);
    world.SetBlock(4, 5, 6, BlockId{3});

    const std::optional<BlockEdit> inverse =
        ApplyBlockEdit(world, BlockEdit{ glm::ivec3(4, 5, 6), BlockId{7} });
    REQUIRE(inverse.has_value());

    ApplyBlockEdit(world, *inverse);

    CHECK(world.GetBlock(4, 5, 6) == BlockId{3});
}

TEST_CASE("An out-of-range position is rejected rather than thrown")
{
    // The divergence from World::SetBlock, which throws. An edit arriving from a
    // file or a socket is input, not a bug in the caller.
    World world(1, 1, 1);

    std::optional<BlockEdit> inverse;
    CHECK_NOTHROW(
        inverse = ApplyBlockEdit(world, BlockEdit{ glm::ivec3(-1, 0, 0), BlockId{7} }));
    CHECK_FALSE(inverse.has_value());

    CHECK_NOTHROW(
        inverse = ApplyBlockEdit(
            world,
            BlockEdit{ glm::ivec3(world.GetWidth(), 0, 0), BlockId{7} }));
    CHECK_FALSE(inverse.has_value());
}

TEST_CASE("Setting a block to what it already is changes nothing")
{
    // Rejecting no-ops is what keeps an undo stack meaningful: an inverse that
    // does nothing when popped makes the user press undo twice for one change.
    World world(1, 1, 1);
    world.SetBlock(4, 5, 6, BlockId{3});

    const std::optional<BlockEdit> inverse =
        ApplyBlockEdit(world, BlockEdit{ glm::ivec3(4, 5, 6), BlockId{3} });

    CHECK_FALSE(inverse.has_value());
    CHECK(world.GetBlock(4, 5, 6) == BlockId{3});
}

TEST_CASE("Applying marks the containing chunk dirty")
{
    World world(3, 3, 3);
    world.ClearDirty();

    ApplyBlockEdit(world, BlockEdit{ glm::ivec3(20, 20, 20), BlockId{1} });

    CHECK(world.DirtyChunks().count(glm::ivec3(1, 1, 1)) == 1);
}

TEST_CASE("An edit on a chunk face marks the neighbour too")
{
    World world(3, 3, 3);
    world.ClearDirty();

    // x=16 is the first block of chunk 1, so chunk 0 borders it.
    ApplyBlockEdit(world, BlockEdit{ glm::ivec3(16, 20, 20), BlockId{1} });

    CHECK(world.DirtyChunks().count(glm::ivec3(1, 1, 1)) == 1);
    CHECK(world.DirtyChunks().count(glm::ivec3(0, 1, 1)) == 1);
}

TEST_CASE("A sequence of edits undone in reverse restores every block")
{
    World world(1, 1, 1);

    std::vector<BlockEdit> undo;
    for (int i = 0; i < 8; ++i)
    {
        const std::optional<BlockEdit> inverse = ApplyBlockEdit(
            world,
            BlockEdit{ glm::ivec3(i, i + 1, i + 2), static_cast<BlockId>(i + 1) });
        REQUIRE(inverse.has_value());
        undo.push_back(*inverse);
    }

    while (!undo.empty())
    {
        ApplyBlockEdit(world, undo.back());
        undo.pop_back();
    }

    for (int i = 0; i < 8; ++i)
        CHECK(world.GetBlock(i, i + 1, i + 2) == BlockId{0});
}

TEST_CASE("An edit and its inverse leave every sky-light value unchanged")
{
    // The property that carries the design. A Repropagate that floods the
    // chamber correctly but fails to un-flood it on the way back passes every
    // other case in this file.
    World world = MakeChamberWorld();
    SkyLight::PropagateAll(world);

    const std::vector<std::uint8_t> before = SnapshotLight(world);

    // The chamber is sealed, so it starts dark.
    REQUIRE(world.GetSkyLight(16, 8, 16) == 0);

    const std::optional<BlockEdit> inverse =
        ApplyBlockEdit(world, BlockEdit{ glm::ivec3(16, 9, 16), BlockId{0} });
    REQUIRE(inverse.has_value());

    // Breaking the roof must actually flood it, or the round trip below proves
    // nothing.
    REQUIRE(world.GetSkyLight(16, 8, 16) > 0);

    ApplyBlockEdit(world, *inverse);

    const std::optional<glm::ivec3> difference = FirstLightDifference(world, before);
    INFO("first differing sky-light cell: ", Describe(difference));
    CHECK_FALSE(difference.has_value());
}
```

- [ ] **Step 4: Regenerate the projects**

Run from the repo root: `premake5 vs2026`

Expected: premake prints the generated projects and exits 0. All three new files existed before this ran.

- [ ] **Step 5: Build and confirm the tests fail**

Run: `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64`

Expected: the build compiles, then the post-build test run FAILS. Against a stub that always returns `nullopt` and changes nothing, the two rejection cases ("An out-of-range position is rejected rather than thrown" and "Setting a block to what it already is changes nothing") pass legitimately, and the other seven fail. Record the actual output and say so if the split differs.

- [ ] **Step 6: Write the implementation**

Replace the stub body in `Cubit/src/Voxel/BlockEdit.cpp`:

```cpp
std::optional<BlockEdit> ApplyBlockEdit(World& world, const BlockEdit& edit)
{
    const glm::ivec3& at = edit.Position;

    if (!world.IsInBounds(at.x, at.y, at.z))
        return std::nullopt;

    const BlockId previous = world.GetBlock(at.x, at.y, at.z);
    if (previous == edit.Block)
        return std::nullopt;

    world.SetBlock(at.x, at.y, at.z, edit.Block);

    // Relight before returning, so an applied edit always leaves the world
    // consistent. A caller that had to remember this separately would produce
    // wrong light, which reads as a lighting bug rather than a missing call.
    SkyLight::Repropagate(world, at.x, at.y, at.z);

    return BlockEdit{ at, previous };
}
```

- [ ] **Step 7: Build and confirm the tests pass**

Run the Tests build command again.

Expected: green, 261 cases (252 + 9). If `ChunkMesherTests.cpp:323` fails in the 50–70 ms band, that is the known flaky timing assertion — re-run before investigating.

- [ ] **Step 8: Prove the light round-trip test can fail**

Temporarily delete the `SkyLight::Repropagate(...)` line from `ApplyBlockEdit` and rebuild.

Expected: "An edit and its inverse leave every sky-light value unchanged" FAILS, naming a differing cell inside the chamber, while "Applying the inverse restores the previous block" still PASSES — proving the light test catches what the block-level tests cannot. Restore the line and rebuild to green. Record both outputs; a test you have not seen fail is not evidence.

- [ ] **Step 9: Commit**

```bash
git add Cubit/include/Cubit/Voxel/BlockEdit.h Cubit/src/Voxel/BlockEdit.cpp Tests/src/BlockEditTests.cpp
```

Commit with subject `Make a block edit an applicable, reversible value` and a prose body explaining that an edit was previously four steps inline in a Sandbox handler, that the inverse is the return value rather than a second type, and that applying relights so no caller has to know the second half of the ritual.

---

### Task 2: Route the Sandbox through it, and add undo

**Files:**
- Modify: `Sandbox/src/Sandbox.cpp` — `OnMouseButtonPressed`, `OnKeyPressed`, `LoadWorld`, members, includes
- Modify: `Cubit/include/Cubit/Cubit.h` — export `BlockEdit.h`

**Interfaces:**
- Consumes: `BlockEdit`, `ApplyBlockEdit` from Task 1. `KeyCode::U` (`Cubit/KeyCodes.h:46`).
- Produces: `SandboxLayer::m_Undo` (`std::vector<BlockEdit>`), which Task 3 reads the size of.

No unit test: `SandboxLayer` needs a GL context and is untested by the suite, which the spec records as deliberate. The behaviour it relies on is covered by Task 1.

- [ ] **Step 1: Add the members**

In `Sandbox/src/Sandbox.cpp`, beside `int m_StepsThisFrame = 0;` in the member block:

```cpp
    //Inverses of applied edits, newest last. Capped so a long session cannot
    //creep; the oldest entries are the least likely to be wanted back.
    static constexpr std::size_t MaxUndoDepth = 256;
    std::vector<BlockEdit> m_Undo;
```

Two includes are needed and neither is present:

- `Sandbox.cpp` includes `<optional>` (line 15) but **not** `<vector>`. Add `#include <vector>` to the standard-library block, keeping its alphabetical order.
- `Cubit/include/Cubit/Cubit.h` exports `Block.h`, `Chunk.h`, `ChunkMesher.h`, `VoxelCollision.h`, `VoxelRaycast.h` and `World.h`, but not `BlockEdit.h`. Add `#include "Cubit/Voxel/BlockEdit.h"` to that block, alphabetically after `Block.h`. `BlockEdit` is core public voxel API alongside `World` and `Block`, and the previous branch's review flagged exactly this kind of umbrella omission as an inconsistency worth closing. With that added, `Sandbox.cpp` needs no new Cubit include — it already includes `Cubit/Cubit.h` at line 1.

That makes `Cubit/include/Cubit/Cubit.h` a fourth file this task modifies.

- [ ] **Step 2: Route the edit through `ApplyBlockEdit`**

In `OnMouseButtonPressed`, replace everything from the `if (!m_World.IsInBounds(...))` guard through the `CB_INFO(...)` logging with:

```cpp
        const BlockEdit edit{
            target,
            button == MouseCode::Left ? BlockId{0} : m_PlaceBlock };

        // Bounds and relighting both belong to ApplyBlockEdit now: an edit is
        // one operation, not a sequence a caller has to remember the rest of.
        const std::optional<BlockEdit> inverse = ApplyBlockEdit(m_World, edit);
        if (!inverse)
            return false;

        m_Undo.push_back(*inverse);
        if (m_Undo.size() > MaxUndoDepth)
            m_Undo.erase(m_Undo.begin());

        CB_INFO(
            std::string(button == MouseCode::Left ? "Broke" : "Placed") +
            " block at " + std::to_string(target.x) + "," +
            std::to_string(target.y) + "," + std::to_string(target.z));

        return true;
```

The raycast, the two target rules, and the `hit.Normal == glm::ivec3(0)` place guard above this all stay exactly as they are. The `SkyLight::Repropagate` call and its comment are deleted — that work now happens inside `ApplyBlockEdit`.

Check whether `SkyLight` is still used elsewhere in `Sandbox.cpp` before removing its include: `LoadWorld` calls `SkyLight::PropagateAll`, so the include stays.

- [ ] **Step 3: Bind `U` to undo**

In `OnKeyPressed`, after the `F9` block and before the digit handling:

```cpp
        if (event.GetKeyCode() == KeyCode::U)
        {
            UndoLastEdit();
            return true;
        }
```

And add the method beside `OnMouseButtonPressed`:

```cpp
    //Reverses the most recent edit.
    //
    //The entry is popped whether or not applying it changes anything: an
    //inverse that comes back empty describes a cell some later edit has already
    //overwritten, so keeping it would stall the stack on the same dead entry
    //every press. Applying an inverse is itself an edit, but its own inverse is
    //deliberately not pushed — that would make U alternate between two states
    //instead of walking back through history.
    void UndoLastEdit()
    {
        if (m_Undo.empty())
            return;

        const BlockEdit inverse = m_Undo.back();
        m_Undo.pop_back();
        ApplyBlockEdit(m_World, inverse);
    }
```

- [ ] **Step 4: Clear the stack on reload**

`LoadWorld` replaces the world from disk, after which every inverse describes a state that no longer exists — popping one would write a block that was never there.

In `LoadWorld`, beside the existing `m_VerticalVelocity = 0.0f;`:

```cpp
        // The stack describes a world that no longer exists.
        m_Undo.clear();
```

- [ ] **Step 5: Build both projects**

Run the Sandbox build command, then the Tests build command.

Expected: both succeed. Tests stays at 261 — this task adds none.

- [ ] **Step 6: Commit**

```bash
git add Sandbox/src/Sandbox.cpp Cubit/include/Cubit/Cubit.h
```

Commit with subject `Undo the last block edit with U` and a prose body explaining that the Sandbox's edit now goes through `ApplyBlockEdit`, that the stack holds the inverses it returns, and that a reload clears the stack because those inverses describe a world that is gone.

---

### Task 3: Show the undo depth on the HUD

Keyboard input cannot be delivered to the window by a screenshot script, so a readout is the only way this feature is visible on screen at all.

**Files:**
- Modify: `Sandbox/src/DebugFont.h` — `Order` (line ~24) and the `Glyphs` table (the `T` row is currently last)
- Modify: `Sandbox/src/HudLayer.h` — `HudState`, and `DrawReadout`
- Modify: `Sandbox/src/Sandbox.cpp` — `OnFrameUpdate`

**Interfaces:**
- Consumes: `SandboxLayer::m_Undo` from Task 2.
- Produces: `HudState::UndoDepth` (`std::size_t`).

- [ ] **Step 1: Teach the debug font the letter U**

`DebugFont::Order` and the `Glyphs` table are positionally matched, and `IndexOf` falls back to a blank glyph for absent characters — so a length mismatch or a misordered insert draws the wrong glyph for every character after the insertion point, with no error and no failing test. Append `U` to the END of both.

In `Sandbox/src/DebugFont.h`, line ~24:

```cpp
    constexpr std::string_view Order = "0123456789-.: ACDEFGNOPSTU";
```

Append to the `Glyphs` table after the `T` row, adding a comma to the `T` line:

```cpp
        { "#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.." }, // T
        { "#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###." }  // U
```

- [ ] **Step 2: Add the field**

In `Sandbox/src/HudLayer.h`, inside `HudState` after `StepsPerFrame`:

```cpp
    //Edits that can still be undone.
    std::size_t UndoDepth = 0;
```

- [ ] **Step 3: Draw it**

In `DrawReadout`, between the `STEPS` line and the `FPS` line:

```cpp
        y -= lineHeight;
        DrawText("UNDO " + std::to_string(m_State->UndoDepth), TextMargin, y);
```

- [ ] **Step 4: Publish it**

In `Sandbox/src/Sandbox.cpp`, in `OnFrameUpdate`, beside the existing `StepsPerFrame` write:

```cpp
        m_HudState->UndoDepth = m_Undo.size();
```

- [ ] **Step 5: Verify the font table by counting**

Before building, confirm the number of characters in `Order` equals the number of rows in `Glyphs`, and that `U` occupies the same index in both. State both counts explicitly in your report — `Order` should be 26 characters with `U` at index 25.

Then confirm every character of the label `"UNDO "` exists in `Order`: `U`, `N`, `D`, `O`, and space. A label containing an absent letter renders as invisible gaps, which is the exact failure this glyph addition exists to avoid.

- [ ] **Step 6: Build both projects**

Run the Sandbox build command, then the Tests build command.

Expected: both succeed, tests stay at 261.

- [ ] **Step 7: Commit**

```bash
git add Sandbox/src/DebugFont.h Sandbox/src/HudLayer.h Sandbox/src/Sandbox.cpp
```

Commit with subject `Show the undo depth on the HUD` and a prose body explaining that a key binding cannot be exercised by a screenshot script, so the readout is the only on-screen evidence undo exists, and that the font needed a `U` glyph.

---

### Task 4: Verify on screen and record the result

**Files:**
- Modify: `docs/engine-roadmap.md` — the "Worth doing before gameplay" item 3

- [ ] **Step 1: Run the Sandbox and screenshot it**

Launch from `bin/Debug-windows-x86_64/Sandbox` — the map path is relative and a load failure throws out of the constructor, producing an "abort() has been called" dialog over a blank white window that looks exactly like a render bug. Use PowerShell `Start-Process`, not `cmd /c start /b`, which has died silently here before. Redirect stdout to a file; the logger is fully buffered, so force-killing discards every line.

Allow ~45 seconds for the 512×64×512 map to load and mesh. Screenshot the **GLFW30** window, not the process's `MainWindowHandle`, which returns the black console: use `EnumWindows` + `GetWindowThreadProcessId` for the pid and pick the handle whose class is not `*ConsoleWindow*`. Minimise the console with `ShowWindow(h, 6)` and set the GL window topmost via `SetWindowPos(h, (IntPtr)(-1), 0, 0, 0, 0, 0x0003)`. Capture with `Graphics.CopyFromScreen`, then close with `PostMessage(hwnd, WM_CLOSE, 0, 0)` — never `Stop-Process`.

Read the screenshot with the Read tool.

- [ ] **Step 2: Check the readout**

- `UNDO 0` appears in the HUD, with all four letters solid. A blank where `U` should be means the font insert is wrong.
- The rest of the readout is unchanged: `POS`, `GND`, `OCEAN`, `FACES`, `DRAWN`, `PENDING`, `STEPS`, `UNDO`, `FPS`. Note that `DRAWN` and `PENDING` legitimately render with gaps — the font lacks `R`, `W` and `I` — which is pre-existing and cosmetic.
- Report the log tail, including any line beginning `OpenGL:`.

Pressing `U` cannot be scripted (`glfwGetKey` needs real window focus), so the undo behaviour itself is not verifiable here — Task 1's tests are the evidence. Do not report undo as verified on screen.

- [ ] **Step 3: Update the roadmap**

In `docs/engine-roadmap.md`, under "Beyond the voxel engine — gaps found 2026-08-20" → "Worth doing before gameplay", mark item 3 (**"A `BlockEdit` value type"**) done in the style the items above it use: strike the heading, append **DONE 2026-08-22**, then prose.

Match the document's voice — flowing prose with em-dashes, candid. Cover: that an edit is now data with the inverse as the return value of applying it rather than a second type; that applying relights so no caller knows a half-ritual; that an out-of-range position returns rather than throwing, because an edit from a file or socket is input; that rejecting no-ops is what keeps undo meaningful; and that the Sandbox proves the seam with an undo stack on `U`.

Note what stayed out of scope: redo, edit recording for demos, publishing on the `EventBus`, and tick numbering — `FrameClock` counts steps per frame, not total ticks, so a tick field would have nothing honest to fill it.

**With this item done, all three "worth doing before gameplay" entries are closed.** Say so, and say what that means: what remains on that list is the "let the game pull these" set, which deliberately waits for a game to ask.

Do not rewrite other parts of the document.

- [ ] **Step 4: Commit**

```bash
git add docs/engine-roadmap.md
```

Commit with subject `Record BlockEdit as shipped` and a prose body noting that this closes the last of the three items the roadmap called worth doing before gameplay.
