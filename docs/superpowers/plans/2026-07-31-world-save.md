# World Save Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Write an edited `World` back out as a MagicaVoxel `.vox` file, so a map can be fixed by playing it rather than by re-tuning `TerrainGen`.

**Architecture:** Two new pieces mirroring the existing load path. `ToVoxModel(world)` is the free-function inverse of `BuildWorld(model)`; `VoxWriter::WriteFile(model, path)` is the mirror of `VoxLoader::LoadFile(path)`. The existing `VoxWriter::Write(model) -> bytes` does all the format work and is not modified. The Sandbox binds `F5` to the pair.

**Tech Stack:** C++20, MSVC, doctest, premake5, GLM.

Full design: [`docs/superpowers/specs/2026-07-31-world-save-design.md`](../specs/2026-07-31-world-save-design.md).

## Global Constraints

- **Build command** (builds `Cubit` then `Tests`, and runs the whole suite as a postbuild step):
  ```bash
  "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests\\Tests.vcxproj -p:Configuration=Debug -p:Platform=x64
  ```
- **Run one test case:** `./bin/Debug-windows-x86_64/Tests/Tests.exe -tc="<test case name>"`
- **Run the whole suite:** `./bin/Debug-windows-x86_64/Tests/Tests.exe`
- A failing test breaks the build by design — `Tests.exe` runs as a postbuild step and returns non-zero.
- **Comment style:** `//` with no space before the text, matching every existing file. Comments explain *why*, not *what*.
- **Exception convention:** all format errors are `std::runtime_error` with a message prefixed `vox: `, matching `VoxLoader`.
- **Do not** modify `VoxWriter::Write`, `VoxLoader`, or `BuildWorld`. This feature is additive.
- **Never** add Claude co-author trailers or attribution to commits.

---

### Task 1: `ToVoxModel` — the pure conversion

**Files:**
- Modify: `Cubit/include/Cubit/Voxel/VoxWriter.h`
- Modify: `Cubit/src/Voxel/VoxWriter.cpp`
- Test: `Tests/src/WorldSaveTests.cpp` (create)

**Interfaces:**
- Consumes: `World` (`GetWidth/GetHeight/GetDepth/GetBlock/GetPalette/SetBlock/SetPalette`), `VoxModel` (`Size`, `Voxels`, `Colors`, `At`), `BuildWorld`, `VoxLoader::Parse`, `VoxWriter::Write` — all existing.
- Produces: `CB_API VoxModel ToVoxModel(const World& world);` — a free function, declared in `VoxWriter.h`. Later tasks call it.

- [ ] **Step 1: Create the test file with the six conversion tests**

Create `Tests/src/WorldSaveTests.cpp`:

```cpp
#include <doctest.h>

#include "Cubit/Voxel/VoxLoader.h"
#include "Cubit/Voxel/VoxWriter.h"
#include "Cubit/Voxel/World.h"

#include <cstdint>

namespace
{
    //The first position where two worlds disagree, or (-1, -1, -1) when they
    //agree everywhere. Returning the position rather than a bool means a failure
    //message names the block that broke, and it keeps the comparison to a single
    //assertion instead of tens of thousands.
    glm::ivec3 FirstBlockMismatch(const World& left, const World& right)
    {
        for (int z = 0; z < left.GetDepth(); ++z)
            for (int y = 0; y < left.GetHeight(); ++y)
                for (int x = 0; x < left.GetWidth(); ++x)
                    if (left.GetBlock(x, y, z) != right.GetBlock(x, y, z))
                        return glm::ivec3(x, y, z);

        return glm::ivec3(-1);
    }

    //The same comparison between two models.
    glm::ivec3 FirstVoxelMismatch(const VoxModel& left, const VoxModel& right)
    {
        for (int z = 0; z < left.Size.z; ++z)
            for (int y = 0; y < left.Size.y; ++y)
                for (int x = 0; x < left.Size.x; ++x)
                    if (left.At(x, y, z) != right.At(x, y, z))
                        return glm::ivec3(x, y, z);

        return glm::ivec3(-1);
    }
}

TEST_CASE("ToVoxModel reports the world's padded block size")
{
    // A World is always whole chunks, so the saved size is the padded size, not
    // whatever size the map originally came from.
    const World world(2, 1, 3); // chunks -> 32 x 16 x 48 blocks

    CHECK(ToVoxModel(world).Size == glm::ivec3(32, 16, 48));
}

TEST_CASE("ToVoxModel places blocks at matching positions")
{
    World world(2, 1, 3);
    world.SetBlock(0, 0, 0, BlockId{1});
    world.SetBlock(17, 5, 40, BlockId{9});
    world.SetBlock(31, 15, 47, BlockId{4}); // the far corner

    const VoxModel model = ToVoxModel(world);

    CHECK(model.At(0, 0, 0) == 1);
    CHECK(model.At(17, 5, 40) == 9);
    CHECK(model.At(31, 15, 47) == 4);
}

TEST_CASE("ToVoxModel leaves untouched positions as air")
{
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});

    const VoxModel model = ToVoxModel(world);

    CHECK(model.At(8, 8, 8) == 1);
    CHECK(model.At(7, 8, 8) == 0);
    CHECK(model.At(0, 0, 0) == 0);
}

TEST_CASE("ToVoxModel carries the world's palette")
{
    World world(1, 1, 1);
    Palette palette = DefaultPalette();
    palette[4] = glm::vec3(0.2f, 0.4f, 0.6f);
    world.SetPalette(palette);

    CHECK(ToVoxModel(world).Colors[4] == glm::vec3(0.2f, 0.4f, 0.6f));
}

TEST_CASE("A world survives a round trip through BuildWorld")
{
    // The property the whole feature exists to provide. It fails loudly if
    // either half of the conversion drifts.
    World world(2, 1, 2);
    Palette palette = DefaultPalette();
    palette[6] = glm::vec3(0.1f, 0.2f, 0.3f);
    world.SetPalette(palette);

    world.SetBlock(0, 0, 0, BlockId{1});
    world.SetBlock(5, 9, 17, BlockId{6});
    world.SetBlock(31, 15, 31, BlockId{3});

    const World restored = BuildWorld(ToVoxModel(world));

    REQUIRE(restored.GetWidth() == world.GetWidth());
    REQUIRE(restored.GetHeight() == world.GetHeight());
    REQUIRE(restored.GetDepth() == world.GetDepth());
    CHECK(FirstBlockMismatch(world, restored) == glm::ivec3(-1));
    CHECK(restored.GetBlockColor(BlockId{6}) == world.GetBlockColor(BlockId{6}));
}

TEST_CASE("A world survives a round trip through .vox bytes")
{
    // Goes through Write and Parse as well, so the axis swap and the palette
    // off-by-one are exercised, not just the conversion.
    World world(1, 1, 2);
    world.SetBlock(3, 4, 20, BlockId{7});
    world.SetBlock(15, 15, 31, BlockId{2});

    const VoxModel model = ToVoxModel(world);
    const VoxModel restored = VoxLoader::Parse(VoxWriter::Write(model));

    REQUIRE(restored.Size == model.Size);
    CHECK(FirstVoxelMismatch(model, restored) == glm::ivec3(-1));
}
```

- [ ] **Step 2: Regenerate the project files so the new test file is compiled**

The `Tests` project globs `Tests/src/**.cpp`, but the glob is expanded at generation time, so a new file needs premake to run again.

Run: `premake5 vs2026`

Do **not** run `GenerateProjects.bat` — it deletes `bin/` and ends in a `pause` that blocks.

Expected: premake reports `Generating Tests/Tests.vcxproj` among others, and exits 0.

- [ ] **Step 3: Run the tests to verify they fail**

Run:
```bash
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests\\Tests.vcxproj -p:Configuration=Debug -p:Platform=x64
```

Expected: the **build fails to compile** with an error naming `ToVoxModel` as an undeclared identifier. That is the failing state for this task — the function does not exist yet.

- [ ] **Step 4: Declare `ToVoxModel` in the header**

In `Cubit/include/Cubit/Voxel/VoxWriter.h`, after the closing brace of `class CB_API VoxWriter` and before the final newline, add:

```cpp

//Converts a World back into a Cubit-space VoxModel: the inverse of BuildWorld.
//
//Writes the world's full padded size. A World is always a whole number of
//chunks, so a map whose size is not a multiple of 16 comes back larger than it
//went in — the added layers are air and cost nothing on disk, because Write
//stores voxels sparsely.
//
//Sky light is not carried. The .vox format has nowhere to put it, and
//SkyLight::PropagateAll recomputes it when the map loads.
CB_API VoxModel ToVoxModel(const World& world);
```

No new includes are needed: `VoxWriter.h` already includes `VoxLoader.h`, which includes `World.h`.

- [ ] **Step 5: Implement the conversion**

In `Cubit/src/Voxel/VoxWriter.cpp`, append at the end of the file:

```cpp

VoxModel ToVoxModel(const World& world)
{
    VoxModel model;
    model.Size = glm::ivec3(
        world.GetWidth(), world.GetHeight(), world.GetDepth());
    model.Colors = world.GetPalette();
    model.Voxels.assign(
        static_cast<std::size_t>(model.Size.x) *
        static_cast<std::size_t>(model.Size.y) *
        static_cast<std::size_t>(model.Size.z), 0);

    for (int z = 0; z < model.Size.z; ++z)
        for (int y = 0; y < model.Size.y; ++y)
            for (int x = 0; x < model.Size.x; ++x)
            {
                const BlockId id = world.GetBlock(x, y, z);
                if (id == 0)
                    continue;

                model.Voxels[static_cast<std::size_t>(x) +
                    static_cast<std::size_t>(model.Size.x) *
                    (static_cast<std::size_t>(y) +
                     static_cast<std::size_t>(model.Size.y) *
                     static_cast<std::size_t>(z))] = id;
            }

    return model;
}
```

Add `#include <cstddef>` to the include block at the top of the file if it is not already present.

- [ ] **Step 6: Run the tests to verify they pass**

Run:
```bash
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests\\Tests.vcxproj -p:Configuration=Debug -p:Platform=x64
```

Expected: build succeeds and the postbuild `Tests.exe` reports `Status: SUCCESS!` with 144 test cases (138 existing + 6 new), 0 failed.

- [ ] **Step 7: Commit**

```bash
git add Cubit/include/Cubit/Voxel/VoxWriter.h Cubit/src/Voxel/VoxWriter.cpp Tests/src/WorldSaveTests.cpp
git commit -m "Convert a world back into a vox model"
```

---

### Task 2: Reject a world too large for one `.vox` model

**Files:**
- Modify: `Cubit/include/Cubit/Voxel/VoxWriter.h`
- Modify: `Cubit/src/Voxel/VoxWriter.cpp`
- Test: `Tests/src/WorldSaveTests.cpp`

**Interfaces:**
- Consumes: `ToVoxModel` from Task 1.
- Produces: no new symbols. `ToVoxModel` now throws `std::runtime_error` when any axis exceeds 256.

**Why:** `VoxWriter::Write` stores each voxel coordinate in a single byte (`VoxWriter.cpp`, the `XYZI` loop). A world wider than 256 on an axis wraps silently and writes a file that loads back as garbage. `World` has no such limit — its chunk grid is arbitrary — so nothing stops this today.

- [ ] **Step 1: Add the two boundary tests**

Append to `Tests/src/WorldSaveTests.cpp`, and add `#include <stdexcept>` to its include block:

```cpp

TEST_CASE("A world too wide for a single .vox model is rejected")
{
    // 17 chunks on x is 272 blocks. Write stores a coordinate in one byte, so
    // without this guard the file would wrap round and load back as garbage.
    const World world(17, 1, 1);

    CHECK_THROWS_AS(ToVoxModel(world), std::runtime_error);
}

TEST_CASE("A world exactly 256 blocks on an axis is accepted")
{
    // 256 is the largest legal size, not the first illegal one: coordinates run
    // 0 to 255, which is exactly a byte.
    const World world(16, 1, 1);

    CHECK(ToVoxModel(world).Size.x == 256);
}
```

- [ ] **Step 2: Run the new tests to verify the first one fails**

Run:
```bash
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests\\Tests.vcxproj -p:Configuration=Debug -p:Platform=x64
```

Expected: the build fails because `Tests.exe` returns non-zero. In its output, `A world too wide for a single .vox model is rejected` FAILS with `expression didn't throw!`. `A world exactly 256 blocks on an axis is accepted` already passes.

- [ ] **Step 3: Add the guard**

In `Cubit/src/Voxel/VoxWriter.cpp`, inside the existing anonymous `namespace { ... }` at the top of the file, add after `ToByte`:

```cpp

    //The largest model a single .vox can address, matching VoxLoader's own limit
    //on the way in.
    constexpr int MaxDimension = 256;

    //Rejects a world a single .vox model cannot hold. Write stores each voxel
    //coordinate in one byte, so a larger world would wrap silently and produce a
    //file that loads back as garbage — a corrupt save is worse than a refused
    //one.
    void RequireWritableSize(const glm::ivec3& size)
    {
        const char* const axes[3] = { "x", "y", "z" };

        for (int i = 0; i < 3; ++i)
            if (size[i] > MaxDimension)
                throw std::runtime_error(
                    std::string("vox: world is too large for a single .vox model (")
                    + axes[i] + " = " + std::to_string(size[i]) + ")");
    }
```

Add `#include <stdexcept>` and `#include <string>` to the file's include block.

Then in `ToVoxModel`, call it immediately after `model.Size` is assigned and **before** `model.Voxels.assign`:

```cpp
    RequireWritableSize(model.Size);
```

- [ ] **Step 4: Document the throw in the header**

In `Cubit/include/Cubit/Voxel/VoxWriter.h`, append to the `ToVoxModel` comment block, immediately before the declaration:

```cpp
//Throws when the world is larger than 256 on any axis, which a single .vox
//model cannot address. Splitting such a world across several models is
//multi-model stitching, which does not exist yet.
```

- [ ] **Step 5: Run the tests to verify they pass**

Run:
```bash
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests\\Tests.vcxproj -p:Configuration=Debug -p:Platform=x64
```

Expected: build succeeds, `Status: SUCCESS!`, 146 test cases, 0 failed.

- [ ] **Step 6: Commit**

```bash
git add Cubit/include/Cubit/Voxel/VoxWriter.h Cubit/src/Voxel/VoxWriter.cpp Tests/src/WorldSaveTests.cpp
git commit -m "Refuse to write a world larger than a vox model can address"
```

---

### Task 3: `VoxWriter::WriteFile`

**Files:**
- Modify: `Cubit/include/Cubit/Voxel/VoxWriter.h`
- Modify: `Cubit/src/Voxel/VoxWriter.cpp`
- Test: `Tests/src/WorldSaveTests.cpp`

**Interfaces:**
- Consumes: `VoxWriter::Write`, `ToVoxModel`, `VoxLoader::LoadFile`.
- Produces: `static void VoxWriter::WriteFile(const VoxModel& model, const std::string& path);` — Task 4 calls it.

- [ ] **Step 1: Add the two file tests**

Append to `Tests/src/WorldSaveTests.cpp`, and add `#include <filesystem>` to its include block:

```cpp

TEST_CASE("WriteFile round-trips a world through disk")
{
    World world(1, 1, 1);
    world.SetBlock(2, 3, 4, BlockId{5});

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "cubit_worldsave_test.vox";

    VoxWriter::WriteFile(ToVoxModel(world), path.string());
    const VoxModel restored = VoxLoader::LoadFile(path.string());
    std::filesystem::remove(path);

    CHECK(restored.Size == glm::ivec3(16, 16, 16));
    CHECK(restored.At(2, 3, 4) == 5);
}

TEST_CASE("WriteFile throws when the path cannot be opened")
{
    const World world(1, 1, 1);
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        "cubit_no_such_directory" / "out.vox";

    CHECK_THROWS_AS(
        VoxWriter::WriteFile(ToVoxModel(world), path.string()),
        std::runtime_error);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run:
```bash
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests\\Tests.vcxproj -p:Configuration=Debug -p:Platform=x64
```

Expected: the build fails to compile — `WriteFile` is not a member of `VoxWriter`.

- [ ] **Step 3: Declare `WriteFile`**

In `Cubit/include/Cubit/Voxel/VoxWriter.h`, inside `class CB_API VoxWriter`, directly under the existing `Write` declaration:

```cpp

    //Writes a model to a .vox file, mirroring VoxLoader::LoadFile. Throws when
    //the path cannot be opened or the write fails.
    static void WriteFile(const VoxModel& model, const std::string& path);
```

Add `#include <string>` to the header's include block.

- [ ] **Step 4: Implement `WriteFile`**

In `Cubit/src/Voxel/VoxWriter.cpp`, add after `VoxWriter::Write` and before `ToVoxModel`:

```cpp

void VoxWriter::WriteFile(const VoxModel& model, const std::string& path)
{
    const std::vector<std::uint8_t> bytes = Write(model);

    std::ofstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("vox: cannot open file for writing: " + path);

    file.write(reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));

    //A stream that failed mid-write leaves a truncated file behind, which would
    //otherwise be discovered only when someone tried to load it.
    if (!file)
        throw std::runtime_error("vox: failed writing file: " + path);
}
```

Add `#include <fstream>` to the file's include block.

- [ ] **Step 5: Run the tests to verify they pass**

Run:
```bash
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests\\Tests.vcxproj -p:Configuration=Debug -p:Platform=x64
```

Expected: build succeeds, `Status: SUCCESS!`, 148 test cases, 0 failed.

- [ ] **Step 6: Commit**

```bash
git add Cubit/include/Cubit/Voxel/VoxWriter.h Cubit/src/Voxel/VoxWriter.cpp Tests/src/WorldSaveTests.cpp
git commit -m "Write a vox model to a file"
```

---

### Task 4: Save from the Sandbox with F5

**Files:**
- Modify: `Sandbox/src/Sandbox.cpp`

**Interfaces:**
- Consumes: `ToVoxModel`, `VoxWriter::WriteFile` from Tasks 1–3; `KeyCode::F5`; `CB_INFO` / `CB_ERROR`.
- Produces: nothing other tasks depend on. This is the last task.

**Why a distinct file:** the `assets` directory beside the executable is a build artifact — `premake5.lua` re-copies it on every `Sandbox` build — so saving over `battlefield.vox` there would be silently undone by the next build. `saved.vox` is promoted into `Sandbox/assets` by hand when it is worth keeping.

This task has no unit test: it is GLFW and file-path wiring, which the project verifies by running the sandbox.

- [ ] **Step 1: Add the include and the save path**

In `Sandbox/src/Sandbox.cpp`, add to the include block at the top, after the existing `#include "Cubit/Voxel/VoxLoader.h"`:

```cpp
#include "Cubit/Voxel/VoxWriter.h"
```

and add to the standard includes:

```cpp
#include <exception>
#include <filesystem>
```

Then in the anonymous `namespace { ... }`, after `PlaceableBlockCount`, add:

```cpp

    //Where F5 writes the edited world, relative to the executable. Deliberately
    //not the map that was loaded: the assets directory beside the exe is a build
    //artifact that the next Sandbox build overwrites, so a save written over
    //battlefield.vox there would vanish without warning. Promoting a save into
    //Sandbox/assets stays a deliberate copy.
    constexpr const char* SavePath = "assets/maps/saved.vox";
```

- [ ] **Step 2: Add the save method**

In `SandboxLayer`, in the `private:` section directly above `OnKeyPressed`, add:

```cpp
    //Writes the edited world beside the executable and logs where it went.
    void SaveWorld() const
    {
        // This runs inside a GLFW key callback, which is C code, and throwing
        // across a C frame is undefined. A failed save has to end here, as a log
        // line rather than a crash.
        try
        {
            VoxWriter::WriteFile(ToVoxModel(m_World), SavePath);

            CB_INFO("Saved world to " +
                std::filesystem::absolute(SavePath).string());
        }
        catch (const std::exception& error)
        {
            CB_ERROR(std::string("Could not save world: ") + error.what());
        }
    }
```

- [ ] **Step 3: Bind F5**

In `SandboxLayer::OnKeyPressed`, after the `if (event.IsRepeat()) return false;` guard and before the `const int key = ...` line, add:

```cpp
        if (event.GetKeyCode() == KeyCode::F5)
        {
            SaveWorld();
            return true;
        }

```

- [ ] **Step 4: Build the Sandbox**

Run:
```bash
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Sandbox\\Sandbox.vcxproj -p:Configuration=Debug -p:Platform=x64
```

Expected: build succeeds with no warnings from `Sandbox.cpp`.

- [ ] **Step 5: Verify in the running sandbox**

Run the sandbox from its target directory so `assets/...` resolves:

```bash
cd bin/Debug-windows-x86_64/Sandbox && ./Sandbox.exe
```

In the window: break a couple of blocks with left click, place a couple with right click, then press `F5`. Close the window via its close button (`WM_CLOSE`) rather than killing the process, or the log is lost.

Expected: the log contains `Saved world to C:\dev\Cubit\bin\Debug-windows-x86_64\Sandbox\assets\maps\saved.vox`, and that file exists with a non-zero size.

- [ ] **Step 6: Verify the save actually round-trips**

```bash
cp bin/Debug-windows-x86_64/Sandbox/assets/maps/saved.vox Sandbox/assets/maps/battlefield.vox
```

Rebuild and run the Sandbox again. Expected: the blocks you broke are still missing and the ones you placed are still there.

Then restore the original map so the edit is not committed:

```bash
git checkout Sandbox/assets/maps/battlefield.vox
```

- [ ] **Step 7: Commit**

```bash
git add Sandbox/src/Sandbox.cpp
git commit -m "Save the edited world from the sandbox with F5"
```

- [ ] **Step 8: Update the docs**

In `docs/engine-roadmap.md`, mark item 7 under *Format / smaller gaps* as done, matching how items 1, 2 and 4 are already struck through:

```markdown
7. ~~**World persistence**~~ **DONE 2026-07-31** — `ToVoxModel` plus
   `VoxWriter::WriteFile` write an edited world back to `.vox`; the Sandbox binds
   it to `F5`.
```

And in the same file's *"finish the engine" arc*, amend item 3 so only transparency remains open:

```markdown
3. **Transparency** (real water). World save landed 2026-07-31. ← next.
```

In `README.md`, under *What's next*, remove `and **saving an edited world** back to
`.vox`, which is nearly free now that `VoxWriter` exists` from the first bullet, leaving
transparency as the item. Add to the *Content pipeline* list in *What works*:

```markdown
- `ToVoxModel` and `VoxWriter::WriteFile` save an edited world back out, so a map
  can be fixed by playing it — the sandbox binds this to `F5`
```

```bash
git add docs/engine-roadmap.md README.md
git commit -m "Record world save in the roadmap and readme"
```

---

## Self-Review

**Spec coverage:**

| Spec section | Task |
|---|---|
| `ToVoxModel` (size, voxels, colors, no sky light) | 1 |
| Padding decision (save padded world as-is) | 1 (test: padded block size) |
| 256-per-axis guard | 2 |
| `VoxWriter::WriteFile` | 3 |
| Error handling — oversized throws | 2 |
| Error handling — unopenable path throws | 3 |
| Error handling — Sandbox catches across the GLFW callback | 4 (Step 2) |
| Sandbox `F5` to `assets/maps/saved.vox`, absolute path logged | 4 |
| Tests: size, blocks, air, palette, both round-trips, oversize | 1, 2 |
| Tests: WriteFile write-then-load, unopenable path | 3 |
| Rendering verification | 4 (Steps 5–6) |
| Build wiring — premake regeneration for the new test file | 1 (Step 2) |

No gaps.

**Placeholder scan:** every code step contains the actual code. No "TBD", no "add error handling", no "similar to Task N".

**Type consistency:** `ToVoxModel(const World&) -> VoxModel` and
`VoxWriter::WriteFile(const VoxModel&, const std::string&) -> void` are used with those
exact signatures in Tasks 1–4. `SavePath` is `constexpr const char*` and is passed where
a `const std::string&` is expected, which converts implicitly.
