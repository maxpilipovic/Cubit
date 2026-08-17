# Sky-Light Column Scan Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Cut `SkyLight::PropagateAll` — 57% of map load time — by filling open sky columns with a downward scan instead of a breadth-first flood, without changing a single cell of the resulting light.

**Architecture:** One pass down each column writes `Max` for the open run under the sky and `0` below the first opaque block, recording each column's lowest lit y. The existing BFS then runs unchanged, seeded only where a lit column borders an unlit one. Everything is private to `SkyLight.cpp`; no public API changes.

**Tech Stack:** C++20, GLM, doctest, Premake 5, Visual Studio 2026 on Windows x64.

Design: [`docs/superpowers/specs/2026-08-16-sky-light-column-scan-design.md`](../specs/2026-08-16-sky-light-column-scan-design.md)
Measurements: [`docs/superpowers/investigations/2026-08-16-load-cost-breakdown.md`](../investigations/2026-08-16-load-cost-breakdown.md)

## This is an optimisation, not a feature — read this before Task 1

**The tests are written to pass against the *current* implementation, before any rewrite.** That is deliberate and it is not a broken TDD cycle.

The requirement here is that behaviour does **not** change, so there is no failing test to write. Instead Task 1 builds a characterisation harness — a naive reference implementation plus cell-for-cell comparisons — and proves it agrees with the code as it stands today. That step is what validates the *oracle*: if the oracle disagreed with the current implementation, the oracle would be the thing that is wrong, and you would find that out before it could mislead you about the rewrite.

Task 2 then rewrites `PropagateAll`, and every test from Task 1 must still pass, unchanged. A test you have to edit in Task 2 is a behaviour change and must be raised, not edited.

## Global Constraints

- **Build and test from Git Bash:**
  `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64`
  Use **forward slashes** in the `.vcxproj` path — backslashes fail with MSB1009.
- Swap `-p:Configuration=Release` for optimised numbers. Debug is a ~20x multiplier; **measure in Debug before claiming any win**, because cost-per-call optimisations that win in Release have measured *slower* in Debug here before.
- **Run one case:** `Tests.exe -tc="<case name>"` from `bin/Debug-windows-x86_64/Tests`.
- The Tests project runs the whole suite as a post-build step, so a failing test breaks the build.
- **No new source files in this plan**, so Premake does not need re-running. If that changes, run `premake5 vs2026` directly from the repo root — **not** `GenerateProjects.bat`, which deletes `bin/` and ends in a blocking `pause`.
- Comment style: `//` with no space after the slashes, matching the surrounding files. Explain *why*, not *what*.
- `SkyLight::Max` is 15. Down is index 5 in the direction table, and full-strength light falls **without** dimming — that rule has its own named test and must survive.
- Commit after each task. Push to `master` directly — no feature branches. **Never** add Claude co-author trailers or attribution.

## File structure

| File | Responsibility | Change |
|---|---|---|
| `Cubit/src/Voxel/SkyLight.cpp` | Sky lighting. Only `PropagateAll` and two new file-local helpers change; `Flood`, `Unflood`, `Repropagate`, `LightRecorder` are untouched. | Modify |
| `Tests/src/SkyLightTests.cpp` | Sky-light rules, plus the new reference oracle and equivalence cases. | Modify |

---

### Task 1: The reference oracle and the equivalence harness

**Files:**
- Modify: `Tests/src/SkyLightTests.cpp` (append)

**Interfaces:**
- Consumes: `SkyLight::PropagateAll`, `SkyLight::Max`, `World`, `BuildWorld`, `TerrainGen::Generate`, `TerrainConfig`.
- Produces: `ReferencePropagate(World&)` and `CheckMatchesReference(const std::function<World()>&)`, both file-local to the test translation unit. Task 2 relies on these existing and passing.

- [ ] **Step 1: Add the includes the oracle needs**

At the top of `Tests/src/SkyLightTests.cpp`, alongside the existing includes:

```cpp
#include "Cubit/Voxel/TerrainGen.h"
#include "Cubit/Voxel/VoxLoader.h"

#include <cstdint>
#include <deque>
#include <functional>
```

`<chrono>`, `<set>` and `<vector>` are already there.

- [ ] **Step 2: Add the naive reference implementation**

Append to `Tests/src/SkyLightTests.cpp`:

```cpp
namespace
{
    //A deliberately naive sky-light propagation, kept as an oracle for the
    //optimised one: seed the top layer, then spread until nothing brightens.
    //
    //This is the implementation the column scan replaced. Do not optimise it
    //and do not share code with the engine — its only job is to be obviously
    //correct, so that a disagreement means the fast path is wrong.
    void ReferencePropagate(World& world)
    {
        const int width = world.GetWidth();
        const int height = world.GetHeight();
        const int depth = world.GetDepth();

        for (int z = 0; z < depth; ++z)
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                    world.SetSkyLight(x, y, z, 0);

        //Index 5 is straight down, the one direction light travels for free.
        constexpr glm::ivec3 directions[6] =
        {
            {  1,  0,  0 }, { -1,  0,  0 },
            {  0,  0,  1 }, {  0,  0, -1 },
            {  0,  1,  0 }, {  0, -1,  0 },
        };
        constexpr int downIndex = 5;

        std::deque<glm::ivec3> queue;
        const int top = height - 1;

        for (int z = 0; z < depth; ++z)
            for (int x = 0; x < width; ++x)
                if (!world.IsBlockOpaque(x, top, z))
                {
                    world.SetSkyLight(x, top, z, SkyLight::Max);
                    queue.push_back(glm::ivec3(x, top, z));
                }

        while (!queue.empty())
        {
            const glm::ivec3 cell = queue.front();
            queue.pop_front();

            const int level = world.GetSkyLight(cell.x, cell.y, cell.z);
            if (level <= 1)
                continue;

            for (int d = 0; d < 6; ++d)
            {
                const glm::ivec3 next = cell + directions[d];

                if (!world.IsInBounds(next.x, next.y, next.z))
                    continue;
                if (world.IsBlockOpaque(next.x, next.y, next.z))
                    continue;

                const int value = (d == downIndex && level == SkyLight::Max)
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
```

- [ ] **Step 3: Add the comparison harness**

Append to `Tests/src/SkyLightTests.cpp`:

```cpp
namespace
{
    //Builds the world twice, lights one each way, and reports the first cell
    //where they disagree.
    //
    //The position, not a bool: a whole-world equality check that fails with
    //"not equal" is undiagnosable, and a lighting disagreement is almost always
    //in one shadowed corner that you need named to find.
    void CheckMatchesReference(const std::function<World()>& build)
    {
        World expected = build();
        World actual = build();

        ReferencePropagate(expected);
        SkyLight::PropagateAll(actual);

        glm::ivec3 firstBad(-1);
        int referenceLevel = -1;
        int actualLevel = -1;

        for (int y = 0; y < expected.GetHeight() && firstBad.x < 0; ++y)
            for (int z = 0; z < expected.GetDepth() && firstBad.x < 0; ++z)
                for (int x = 0; x < expected.GetWidth(); ++x)
                {
                    const int e = expected.GetSkyLight(x, y, z);
                    const int a = actual.GetSkyLight(x, y, z);

                    if (e != a)
                    {
                        firstBad = glm::ivec3(x, y, z);
                        referenceLevel = e;
                        actualLevel = a;
                        break;
                    }
                }

        INFO("first difference at " << firstBad.x << "," << firstBad.y << ","
             << firstBad.z << " reference=" << referenceLevel
             << " actual=" << actualLevel);
        CHECK(firstBad == glm::ivec3(-1));
    }
}
```

- [ ] **Step 4: Add the synthetic equivalence cases**

Append to `Tests/src/SkyLightTests.cpp`. Each targets one rule the rewrite could break:

```cpp
TEST_CASE("Column scan matches the reference over a flat floor")
{
    CheckMatchesReference([]
    {
        World world(2, 2, 2);
        for (int z = 0; z < world.GetDepth(); ++z)
            for (int x = 0; x < world.GetWidth(); ++x)
                world.SetBlock(x, 0, z, BlockId{1});
        return world;
    });
}

TEST_CASE("Column scan matches the reference under an overhang")
{
    //The sideways-spreading case: cells under the slab are lit only by light
    //that came in from the side and dimmed on the way.
    CheckMatchesReference([]
    {
        World world(2, 2, 2);
        for (int z = 0; z < world.GetDepth(); ++z)
            for (int x = 0; x < world.GetWidth(); ++x)
                world.SetBlock(x, 0, z, BlockId{1});

        for (int z = 4; z < 28; ++z)
            for (int x = 4; x < 28; ++x)
                world.SetBlock(x, 8, z, BlockId{1});

        return world;
    });
}

TEST_CASE("Column scan matches the reference around a sealed cave")
{
    //Nothing lit borders the hollow, so it must stay dark in both.
    CheckMatchesReference([]
    {
        World world(2, 2, 2);
        for (int z = 0; z < world.GetDepth(); ++z)
            for (int y = 0; y < 12; ++y)
                for (int x = 0; x < world.GetWidth(); ++x)
                    world.SetBlock(x, y, z, BlockId{1});

        for (int z = 10; z < 20; ++z)
            for (int y = 4; y < 8; ++y)
                for (int x = 10; x < 20; ++x)
                    world.SetBlock(x, y, z, BlockId{0});

        return world;
    });
}

TEST_CASE("Column scan matches the reference with a column closed at the very top")
{
    //An empty lit range: the scan stops before writing anything, and that
    //column's neighbours have to notice it is dark.
    CheckMatchesReference([]
    {
        World world(2, 2, 2);
        const int top = world.GetHeight() - 1;

        for (int z = 0; z < world.GetDepth(); ++z)
            for (int x = 0; x < world.GetWidth(); ++x)
                world.SetBlock(x, 0, z, BlockId{1});

        world.SetBlock(5, top, 5, BlockId{1});
        return world;
    });
}

TEST_CASE("Column scan matches the reference in a world of pure air")
{
    //Every cell is Max and no cell borders darkness, so the seed set is empty.
    CheckMatchesReference([] { return World(1, 1, 1); });
}

TEST_CASE("Column scan matches the reference across staircase relief")
{
    //The case that stresses the seed y-ranges hardest: every column differs in
    //height from its neighbour, so every column contributes a seed range.
    CheckMatchesReference([]
    {
        World world(2, 2, 2);
        for (int z = 0; z < world.GetDepth(); ++z)
            for (int x = 0; x < world.GetWidth(); ++x)
                for (int y = 0; y <= x; ++y)
                    world.SetBlock(x, y, z, BlockId{1});

        return world;
    });
}

TEST_CASE("Column scan matches the reference on generated terrain")
{
    //Real structure rather than hand-built shapes: hills, a river of
    //non-opaque water, forests and forts. Smaller than the shipped map on
    //purpose — structure is what breaks this, not scale.
    CheckMatchesReference([]
    {
        TerrainConfig config;
        config.Size = glm::ivec3(128, 64, 128);
        return BuildWorld(TerrainGen::Generate(config));
    });
}
```

- [ ] **Step 5: Run the suite — these must PASS against the current implementation**

```bash
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64
```

Expected: **PASS**, all seven new cases green, suite total up by 7.

This is the point of the task. `PropagateAll` has not changed yet, so the oracle is being compared against the very algorithm it was copied from — agreement proves the oracle and the harness are trustworthy. **If any case fails here, stop.** The oracle or the harness is wrong, not the engine, and carrying that into Task 2 would send you hunting a bug that does not exist.

The generated-terrain case adds roughly 2–3 s to a Debug run. That is the price of realistic structure, and far below the ~19 s a 512-map comparison would cost on every build.

- [ ] **Step 6: Commit**

```bash
git add Tests/src/SkyLightTests.cpp
git commit -m "Add a reference oracle for sky-light propagation"
```

---

### Task 2: Replace the open-column flood with a downward scan

**Files:**
- Modify: `Cubit/src/Voxel/SkyLight.cpp` — includes, a new file-local constant, and the body of `PropagateAll` (currently lines ~180–203)

**Interfaces:**
- Consumes: `Flood(World&, std::deque<glm::ivec3>&, LightRecorder*)` — unchanged, still called with a `nullptr` recorder. The oracle and harness from Task 1.
- Produces: no signature change. `SkyLight::PropagateAll(World&)` keeps its declaration in `Cubit/include/Cubit/Voxel/SkyLight.h` exactly as it is.

- [ ] **Step 1: Add the includes and the horizontal step table**

In `Cubit/src/Voxel/SkyLight.cpp`, extend the include block:

```cpp
#include <glm/glm.hpp>
#include <algorithm>
#include <cstddef>
#include <deque>
#include <map>
#include <utility>
#include <vector>
```

Then, inside the existing anonymous `namespace`, just after the `DownIndex` constant:

```cpp
    //The four directions a column can lose light to. Vertical neighbours need
    //no entry: below a lit cell is another lit cell or the opaque block the
    //scan stopped on, and above the topmost is outside the world.
    constexpr glm::ivec2 HorizontalSteps[4] =
    {
        {  1,  0 }, { -1,  0 }, {  0,  1 }, {  0, -1 },
    };
```

- [ ] **Step 2: Replace the body of `PropagateAll`**

Replace the whole of `SkyLight::PropagateAll` with:

```cpp
void SkyLight::PropagateAll(World& world)
{
    const int width = world.GetWidth();
    const int height = world.GetHeight();
    const int depth = world.GetDepth();

    //The lowest cell in each column that sky light reaches directly, or height
    //when the column is closed at the very top and reaches nothing.
    std::vector<int> skyBottom(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(depth),
        height);

    //One pass down each column writes every cell exactly once: Max for the
    //open run under the sky, 0 for everything from the first opaque block
    //down. That covers what a separate blanket clear used to do, which is why
    //there no longer is one.
    //
    //This replaces the bulk of the flood rather than speeding it up. Light
    //falls without dimming, so an open column is a straight run of Max that a
    //breadth-first search discovers one queue entry and six neighbour tests at
    //a time -- 89% of the writes it was making, for a result a downward walk
    //already knows.
    for (int z = 0; z < depth; ++z)
    {
        for (int x = 0; x < width; ++x)
        {
            int y = height - 1;

            for (; y >= 0 && !world.IsBlockOpaque(x, y, z); --y)
                world.SetSkyLight(x, y, z, Max);

            skyBottom[static_cast<std::size_t>(x) +
                static_cast<std::size_t>(width) * static_cast<std::size_t>(z)] =
                y + 1;

            for (; y >= 0; --y)
                world.SetSkyLight(x, y, z, 0);
        }
    }

    //Seed only the lit cells that border an unlit one. A lit cell whose every
    //neighbour is lit or opaque can brighten nothing, so queueing it would
    //cost six neighbour tests to discover it has nothing to do.
    std::deque<glm::ivec3> queue;

    for (int z = 0; z < depth; ++z)
    {
        for (int x = 0; x < width; ++x)
        {
            const int lit = skyBottom[static_cast<std::size_t>(x) +
                static_cast<std::size_t>(width) * static_cast<std::size_t>(z)];

            //How far down a neighbouring column stays dark. Taking the deepest
            //of the four gives the union of their ranges in one span: every
            //range starts at this column's own lit floor, so they nest.
            int deepest = lit;

            for (const glm::ivec2& step : HorizontalSteps)
            {
                const int nx = x + step.x;
                const int nz = z + step.y;

                //Outside the world contributes no darkness, matching the
                //bounds check the flood itself makes.
                if (nx < 0 || nz < 0 || nx >= width || nz >= depth)
                    continue;

                deepest = std::max(deepest,
                    skyBottom[static_cast<std::size_t>(nx) +
                        static_cast<std::size_t>(width) *
                        static_cast<std::size_t>(nz)]);
            }

            for (int y = lit; y < deepest; ++y)
                queue.push_back(glm::ivec3(x, y, z));
        }
    }

    Flood(world, queue, nullptr);
}
```

- [ ] **Step 3: Run the suite — every Task 1 case must still pass, unedited**

```bash
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64
```

Expected: PASS, same case count as the end of Task 1.

If a case fails, the `INFO` line names the first differing cell and both levels — read it before changing anything. **Do not edit a Task 1 test to make it pass.** Those tests define the requirement that behaviour is unchanged; editing one is a behaviour change and needs raising, not absorbing.

- [ ] **Step 4: Commit**

```bash
git add Cubit/src/Voxel/SkyLight.cpp
git commit -m "Fill open sky columns with a downward scan"
```

---

### Task 3: Measure the result, and check it once against the shipped map

**Files:**
- Modify (temporarily, reverted within this task): `Tests/src/SkyLightTests.cpp`

**Interfaces:**
- Consumes: `ReferencePropagate`, `CheckMatchesReference` from Task 1; the rewritten `PropagateAll` from Task 2.
- Produces: the before/after numbers Task 4 records. Nothing is committed from this task.

Both checks here are one-offs. Neither belongs in the permanent suite: the oracle over a 512 map costs ~19 s on every build, and a timing probe makes the suite non-deterministic.

- [ ] **Step 1: Append the one-off probe**

Append to `Tests/src/SkyLightTests.cpp`:

```cpp
// ===== TEMPORARY: one-off verification and measurement. REVERT IN STEP 4 =====
#include <filesystem>
#include <iostream>

TEST_CASE("ONE-OFF: column scan matches the reference on battlefield512")
{
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

    const VoxModel model = VoxLoader::LoadFile(path.string());
    CheckMatchesReference([&model] { return BuildWorld(model); });
}

TEST_CASE("ONE-OFF: time PropagateAll on battlefield512")
{
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

    World world = BuildWorld(VoxLoader::LoadFile(path.string()));

    const auto start = std::chrono::steady_clock::now();
    SkyLight::PropagateAll(world);
    const double ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();

    std::cout << "\n=== PropagateAll after column scan: " << ms
              << " ms ===\n" << std::endl;
}
```

- [ ] **Step 2: Run in Debug and record both results**

```bash
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64
```

Expected: the equivalence case PASSES — this is the real proof, 16.8M cells of the shipped map agreeing cell for cell. Write down the timing line.

Baseline to beat: **19,578 ms** total `PropagateAll` in Debug, of which 18,736 ms was the flood.

- [ ] **Step 3: Run in Release and record**

```bash
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Release -p:Platform=x64
```

Baseline to beat: **1,003 ms**. Record the number.

Debug is the one that decides whether this shipped a win — it is what gets run day to day, and this project has reverted three optimisations that looked good in Release and measured slower in Debug.

- [ ] **Step 4: Revert the probe and confirm the tree is clean**

```bash
git checkout -- Tests/src/SkyLightTests.cpp
git status --short
```

Expected: no output. `git checkout` restores the file to its Task 1 state, removing both one-off cases.

- [ ] **Step 5: Rebuild to confirm the reverted tree is green**

```bash
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64
```

Expected: PASS, back to the Task 1 case count.

---

### Task 4: Record the outcome

**Files:**
- Modify: `docs/performance.md` — the P8 section
- Modify: `docs/superpowers/investigations/2026-08-16-load-cost-breakdown.md` — add an outcome section
- Modify: `docs/engine-roadmap.md` — the P8 paragraph in the arc section

**Interfaces:** none.

- [ ] **Step 1: Update P8 in `docs/performance.md`**

Add a before/after table using the Debug and Release numbers measured in Task 3, against the recorded baselines (Debug: `PropagateAll` 19,578 ms, flood 18,736 ms, total load 33,079 ms. Release: `PropagateAll` 1,003 ms, total load 1,631 ms).

Change the P8 **Status** line from `open — measured 2026-08-16, design approved` to `DONE 2026-08-16` with the new figure, and update the summary-table row at the bottom of the file to match.

State plainly whether the remaining cost makes **option C — flat-indexing the BFS** worth doing next, based on the measured number rather than on expectation. The spec deliberately left C open for exactly this decision.

- [ ] **Step 2: Add an outcome section to the investigation**

Append a short "What the fix achieved" section to
`docs/superpowers/investigations/2026-08-16-load-cost-breakdown.md` with the same
before/after numbers, so the measurement write-up ends with its own conclusion rather
than trailing off at the diagnosis.

- [ ] **Step 3: Update the roadmap**

In `docs/engine-roadmap.md`, update the P8 paragraph in the arc section: the flood rewrite is done, with the new number, and say what is now the largest remaining piece of load cost — on the Task 3 evidence, likely `parse` + `BuildWorld` at 8.5 s Debug, which has never been optimised.

- [ ] **Step 4: Commit and push**

```bash
git add docs/
git commit -m "Record the sky-light column scan result"
git push origin master
```

---

## Follow-ups this plan deliberately leaves alone

- **Option C, flat-indexing the flood** — still on the table, decided on Task 3's numbers rather than re-derived from scratch. See the spec's options section.
- **`parse` + `BuildWorld`**, 8.5 s of Debug load (26%) and never costed before this investigation.
- **Threading anything.** The measurements do not support it as the next move.
- **`Repropagate`.** The edit path is 0.03 ms and is not touched here.
