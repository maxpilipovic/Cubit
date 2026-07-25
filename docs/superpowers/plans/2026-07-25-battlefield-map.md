# Battlefield Map Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Generate a large, symmetric, Ace-of-Spades-style battlefield map (`128×48×128`, 8×3×8 chunks) offline into a committed `.vox` file that the Sandbox loads and renders.

**Architecture:** Add a `VoxWriter` (the exact inverse of `VoxLoader`, round-trip identity) and a `TerrainGen` (config → `VoxModel`) to the Cubit library, both doctest-covered. A thin `MapGen` console tool runs them to produce `battlefield.vox`. The Sandbox loads it and spawns the player on a fort.

**Tech Stack:** C++20, GLM, doctest (Tests project, runs as a postbuild step), premake5 (`vs2026`), OpenGL. Windows/MSVC.

## Global Constraints

- C++20, MSVC, `CB_PLATFORM_WINDOWS`. Engine symbols crossing the DLL boundary are annotated `CB_API` (from `Cubit/Core.h`).
- Chunk dimensions are `16 × 16 × 16` (`Chunk::Width/Height/Depth`).
- Map is `128 (x) × 48 (y, up) × 128 (z)` in Cubit Y-up space = 8×3×8 = 192 chunks.
- `.vox` is Z-up; Cubit is Y-up. `VoxLoader` reads `cubit(x, y, z) = vox(x, z, y)` and `palette[j + 1] = RGBA[j]`. `VoxWriter` is its inverse: store Cubit voxel `(cx, cy, cz)` at vox `(cx, cz, cy)`, vox `SIZE = (Size.x, Size.z, Size.y)`, and `RGBA[j] = Colors[j + 1]`.
- Colors round-trip through byte quantization (float → 0..255 → float), so color equality is checked with `doctest::Approx` at tolerance `1.0/255`.
- **Symmetry:** all generation is driven by the folded coordinate `fx = min(x, Size.x - 1 - x)`, so the map is mirror-symmetric by construction. Forts are the one color exception: geometry comes from `fx`, but the block color is chosen by side (`x < Size.x/2` → red, else blue).
- Errors are reported by throwing (project style). Tests are doctest, one file per subject, in `Tests/src/`.
- Palette / block-id mapping (used everywhere):
  `Air`=0, `Grass`=1, `GrassDark`=2, `Dirt`=3, `Stone`=4, `StoneDark`=5, `Sand`=6, `Water`=7, `Wood`=8, `Leaves`=9, `LeavesLight`=10, `Snow`=11, `RedBase`=12, `BlueBase`=13.

### Toolchain commands (this machine)

- Regenerate projects after adding any file/project: `/c/dev/premake/premake5 vs2026` (run from repo root `C:\dev\Cubit`).
- Build: `"/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe" "C:\dev\Cubit\Cubit.slnx" //p:Configuration=Debug //p:Platform=x64 //v:minimal //nologo`
- The Tests project runs `Tests.exe` as a postbuild step, so a failing test breaks the build. Watch the `[doctest] test cases:` count rise as new test files are added — if it doesn't rise, premake wasn't regenerated and the new file isn't compiled.

---

## Task 1: `VoxWriter` — serialize a `VoxModel` to `.vox` (round-trip identity)

**Files:**
- Create: `Cubit/include/Cubit/Voxel/VoxWriter.h`
- Create: `Cubit/src/Voxel/VoxWriter.cpp`
- Test: `Tests/src/VoxWriterTests.cpp`

**Interfaces:**
- Consumes: `VoxModel`, `Palette`, `DefaultPalette()`, `VoxLoader::Parse` (existing).
- Produces: `static std::vector<std::uint8_t> VoxWriter::Write(const VoxModel& model);`

- [ ] **Step 1: Create the header `VoxWriter.h`**

```cpp
#pragma once

#include "Cubit/Core.h"
#include "Cubit/Voxel/VoxLoader.h"   // VoxModel

#include <cstdint>
#include <vector>

//Serializes a Cubit-space VoxModel into MagicaVoxel .vox bytes. The exact inverse
//of VoxLoader::Parse: Parse(Write(model)) reproduces model (colours up to byte
//quantisation).
class CB_API VoxWriter
{
public:
    static std::vector<std::uint8_t> Write(const VoxModel& model);
};
```

- [ ] **Step 2: Write the failing tests `VoxWriterTests.cpp`**

```cpp
#include <doctest.h>

#include "Cubit/Voxel/VoxWriter.h"
#include "Cubit/Voxel/VoxLoader.h"

#include <cstdint>
#include <vector>

namespace
{
    void SetId(VoxModel& m, int x, int y, int z, std::uint8_t id)
    {
        m.Voxels[static_cast<std::size_t>(x) +
            static_cast<std::size_t>(m.Size.x) *
            (static_cast<std::size_t>(y) +
             static_cast<std::size_t>(m.Size.y) * static_cast<std::size_t>(z))] = id;
    }

    VoxModel MakeModel(int sx, int sy, int sz)
    {
        VoxModel m;
        m.Size = glm::ivec3(sx, sy, sz);
        m.Voxels.assign(static_cast<std::size_t>(sx) * sy * sz, 0);
        m.Colors = DefaultPalette();
        return m;
    }
}

TEST_CASE("VoxWriter round-trips size and voxels through the loader")
{
    VoxModel m = MakeModel(3, 4, 5);
    SetId(m, 1, 2, 3, 7);
    SetId(m, 0, 0, 0, 1);
    SetId(m, 2, 3, 4, 12);

    const VoxModel r = VoxLoader::Parse(VoxWriter::Write(m));

    CHECK(r.Size == m.Size);
    for (int z = 0; z < m.Size.z; ++z)
        for (int y = 0; y < m.Size.y; ++y)
            for (int x = 0; x < m.Size.x; ++x)
                CHECK(r.At(x, y, z) == m.At(x, y, z));
}

TEST_CASE("VoxWriter round-trips custom palette colours (byte precision)")
{
    VoxModel m = MakeModel(1, 1, 1);
    m.Colors[7] = glm::vec3(60 / 255.0f, 120 / 255.0f, 200 / 255.0f);
    m.Colors[12] = glm::vec3(190 / 255.0f, 45 / 255.0f, 45 / 255.0f);
    SetId(m, 0, 0, 0, 7);

    const VoxModel r = VoxLoader::Parse(VoxWriter::Write(m));

    CHECK(r.Colors[7].x == doctest::Approx(m.Colors[7].x).epsilon(1.0 / 255));
    CHECK(r.Colors[7].y == doctest::Approx(m.Colors[7].y).epsilon(1.0 / 255));
    CHECK(r.Colors[12].z == doctest::Approx(m.Colors[12].z).epsilon(1.0 / 255));
}

TEST_CASE("VoxWriter emits an all-air model with no voxels")
{
    VoxModel m = MakeModel(2, 2, 2); // all air

    const VoxModel r = VoxLoader::Parse(VoxWriter::Write(m));

    CHECK(r.Size == glm::ivec3(2, 2, 2));
    for (int z = 0; z < 2; ++z)
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x)
                CHECK(r.At(x, y, z) == 0);
}
```

- [ ] **Step 3: Regenerate projects and confirm the tests fail to link**

Run: `/c/dev/premake/premake5 vs2026`
Then build. Expected: `VoxWriterTests.cpp` fails to link because `VoxWriter::Write` has no definition. (If a definition is needed to reach the link error, add an empty `VoxWriter.cpp` with the include first.)

- [ ] **Step 4: Implement `VoxWriter.cpp`**

```cpp
#include "cub.h"

#include "Cubit/Voxel/VoxWriter.h"

#include <cstring>

namespace
{
    void PushInt(std::vector<std::uint8_t>& out, std::int32_t value)
    {
        for (int i = 0; i < 4; ++i)
            out.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF));
    }

    void PushTag(std::vector<std::uint8_t>& out, const char* tag)
    {
        out.insert(out.end(), tag, tag + 4);
    }

    //Converts a 0..1 colour channel to a 0..255 byte.
    std::uint8_t ToByte(float channel)
    {
        float c = channel;
        if (c < 0.0f) c = 0.0f;
        if (c > 1.0f) c = 1.0f;
        return static_cast<std::uint8_t>(c * 255.0f + 0.5f);
    }
}

std::vector<std::uint8_t> VoxWriter::Write(const VoxModel& model)
{
    // SIZE: vox axes are (x, z, y) relative to Cubit.
    std::vector<std::uint8_t> size;
    PushTag(size, "SIZE");
    PushInt(size, 12);
    PushInt(size, 0);
    PushInt(size, model.Size.x);
    PushInt(size, model.Size.z);
    PushInt(size, model.Size.y);

    // XYZI: sparse, non-air voxels only, stored at vox (cx, cz, cy).
    std::vector<std::uint8_t> voxelBytes;
    std::int32_t count = 0;
    for (int z = 0; z < model.Size.z; ++z)
        for (int y = 0; y < model.Size.y; ++y)
            for (int x = 0; x < model.Size.x; ++x)
            {
                const std::uint8_t id = model.At(x, y, z);
                if (id == 0)
                    continue;
                voxelBytes.push_back(static_cast<std::uint8_t>(x)); // vox x
                voxelBytes.push_back(static_cast<std::uint8_t>(z)); // vox y = cubit z
                voxelBytes.push_back(static_cast<std::uint8_t>(y)); // vox z = cubit y
                voxelBytes.push_back(id);
                ++count;
            }

    std::vector<std::uint8_t> xyzi;
    PushTag(xyzi, "XYZI");
    PushInt(xyzi, 4 + 4 * count);
    PushInt(xyzi, 0);
    PushInt(xyzi, count);
    xyzi.insert(xyzi.end(), voxelBytes.begin(), voxelBytes.end());

    // RGBA: entry j is colour index j + 1 (inverse of the loader's off-by-one).
    std::vector<std::uint8_t> rgba;
    PushTag(rgba, "RGBA");
    PushInt(rgba, 256 * 4);
    PushInt(rgba, 0);
    for (int j = 0; j < 256; ++j)
    {
        if (j < 255)
        {
            const glm::vec3& c = model.Colors[static_cast<std::size_t>(j) + 1];
            rgba.push_back(ToByte(c.r));
            rgba.push_back(ToByte(c.g));
            rgba.push_back(ToByte(c.b));
            rgba.push_back(255);
        }
        else
        {
            rgba.push_back(0); rgba.push_back(0); rgba.push_back(0); rgba.push_back(0);
        }
    }

    std::vector<std::uint8_t> bytes;
    PushTag(bytes, "VOX ");
    PushInt(bytes, 150);
    PushTag(bytes, "MAIN");
    PushInt(bytes, 0);
    PushInt(bytes, static_cast<std::int32_t>(size.size() + xyzi.size() + rgba.size()));
    bytes.insert(bytes.end(), size.begin(), size.end());
    bytes.insert(bytes.end(), xyzi.begin(), xyzi.end());
    bytes.insert(bytes.end(), rgba.begin(), rgba.end());
    return bytes;
}
```

- [ ] **Step 5: Build and confirm the tests pass**

Build. Expected: the three `VoxWriter` cases pass and the total `[doctest] test cases:` count rises by 3.

- [ ] **Step 6: Commit**

```bash
git add Cubit Tests
git commit -m "Add VoxWriter that round-trips through the loader"
```

---

## Task 2: `TerrainGen` skeleton — config, palette, base terrain

Behaviour: produce a mirror-symmetric heightmap of stone/dirt/grass columns. River, forests, and forts are declared as no-op stages here and filled in by later tasks.

**Files:**
- Create: `Cubit/include/Cubit/Voxel/TerrainGen.h`
- Create: `Cubit/src/Voxel/TerrainGen.cpp`
- Test: `Tests/src/TerrainGenTests.cpp`

**Interfaces:**
- Consumes: `VoxModel`, `Palette`, `BlockId` (existing).
- Produces:
  - `struct TerrainConfig { glm::ivec3 Size; unsigned Seed; int WaterLevel; int GroundBase; int MountainHeight; int SnowLine; float ForestDensity; };`
  - `namespace MapBlocks { constexpr BlockId Grass=1, GrassDark=2, Dirt=3, Stone=4, StoneDark=5, Sand=6, Water=7, Wood=8, Leaves=9, LeavesLight=10, Snow=11, RedBase=12, BlueBase=13; }`
  - `class TerrainGen { static VoxModel Generate(const TerrainConfig&); static Palette MapPalette(); };`

- [ ] **Step 1: Create the header `TerrainGen.h`**

```cpp
#pragma once

#include "Cubit/Core.h"
#include "Cubit/Voxel/VoxLoader.h"   // VoxModel
#include "Cubit/Voxel/Block.h"       // BlockId, Palette

#include <glm/glm.hpp>

//Tunable parameters for one generated map. Defaults describe the shipped
//128x48x128 battlefield.
struct TerrainConfig
{
    glm::ivec3 Size{ 128, 48, 128 };
    unsigned Seed = 1337;
    int WaterLevel = 10;      // y at/below which the river holds water
    int GroundBase = 13;      // baseline grass height before noise
    int MountainHeight = 30;  // extra height added toward the z flanks
    int SnowLine = 34;        // surfaces above this y become snow/dark rock
    float ForestDensity = 0.06f; // per-eligible-column tree probability
};

//Block ids the generator writes, index-aligned to MapPalette().
namespace MapBlocks
{
    constexpr BlockId Grass = 1;
    constexpr BlockId GrassDark = 2;
    constexpr BlockId Dirt = 3;
    constexpr BlockId Stone = 4;
    constexpr BlockId StoneDark = 5;
    constexpr BlockId Sand = 6;
    constexpr BlockId Water = 7;
    constexpr BlockId Wood = 8;
    constexpr BlockId Leaves = 9;
    constexpr BlockId LeavesLight = 10;
    constexpr BlockId Snow = 11;
    constexpr BlockId RedBase = 12;
    constexpr BlockId BlueBase = 13;
}

//Generates a symmetric Ace-of-Spades-style map as a Cubit-space VoxModel.
class CB_API TerrainGen
{
public:
    static VoxModel Generate(const TerrainConfig& config);

    //The natural + team-colour palette the generator installs.
    static Palette MapPalette();
};
```

- [ ] **Step 2: Write the failing tests `TerrainGenTests.cpp`**

```cpp
#include <doctest.h>

#include "Cubit/Voxel/TerrainGen.h"

namespace
{
    TerrainConfig SmallConfig()
    {
        TerrainConfig c;
        c.Size = glm::ivec3(64, 48, 64); // smaller for fast tests, still even width
        return c;
    }

    bool IsGrass(std::uint8_t id)
    {
        return id == MapBlocks::Grass || id == MapBlocks::GrassDark;
    }
}

TEST_CASE("Generated model matches the configured size")
{
    const VoxModel m = TerrainGen::Generate(SmallConfig());
    CHECK(m.Size == glm::ivec3(64, 48, 64));
}

TEST_CASE("Generation is deterministic for a fixed seed")
{
    const VoxModel a = TerrainGen::Generate(SmallConfig());
    const VoxModel b = TerrainGen::Generate(SmallConfig());
    CHECK(a.Voxels == b.Voxels);
}

TEST_CASE("Terrain is geometrically mirror-symmetric across x")
{
    const VoxModel m = TerrainGen::Generate(SmallConfig());
    for (int z = 0; z < m.Size.z; ++z)
        for (int y = 0; y < m.Size.y; ++y)
            for (int x = 0; x < m.Size.x; ++x)
                CHECK(IsSolid(m.At(x, y, z)) ==
                      IsSolid(m.At(m.Size.x - 1 - x, y, z)));
}

TEST_CASE("Every ground column has grass on its surface and solid below")
{
    const VoxModel m = TerrainGen::Generate(SmallConfig());
    // Sample a column away from the centre river line.
    const int x = 10, z = 10;
    int top = -1;
    for (int y = m.Size.y - 1; y >= 0; --y)
        if (IsSolid(m.At(x, y, z))) { top = y; break; }

    REQUIRE(top >= 0);
    CHECK(IsGrass(m.At(x, top, z)));
    CHECK(IsSolid(m.At(x, top - 1, z)));
    CHECK(m.At(x, 0, z) == MapBlocks::Stone);
}
```

- [ ] **Step 3: Regenerate projects and confirm the tests fail**

Run: `/c/dev/premake/premake5 vs2026`
Then build. Expected: link error (no `TerrainGen::Generate` / `MapPalette` definition yet).

- [ ] **Step 4: Implement `TerrainGen.cpp` (palette, noise, base terrain, stage stubs)**

```cpp
#include "cub.h"

#include "Cubit/Voxel/TerrainGen.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace
{
    using namespace MapBlocks;

    std::size_t Index(const VoxModel& m, int x, int y, int z)
    {
        return static_cast<std::size_t>(x) +
            static_cast<std::size_t>(m.Size.x) *
            (static_cast<std::size_t>(y) +
             static_cast<std::size_t>(m.Size.y) * static_cast<std::size_t>(z));
    }

    void Set(VoxModel& m, int x, int y, int z, BlockId id)
    {
        if (x < 0 || x >= m.Size.x || y < 0 || y >= m.Size.y || z < 0 || z >= m.Size.z)
            return;
        m.Voxels[Index(m, x, y, z)] = id;
    }

    BlockId Get(const VoxModel& m, int x, int y, int z)
    {
        if (x < 0 || x >= m.Size.x || y < 0 || y >= m.Size.y || z < 0 || z >= m.Size.z)
            return 0;
        return m.Voxels[Index(m, x, y, z)];
    }

    //Folds x across the mirror plane so all generation is symmetric by construction.
    int Fold(const TerrainConfig& c, int x) { return std::min(x, c.Size.x - 1 - x); }

    std::uint32_t Hash(int x, int z, unsigned seed)
    {
        std::uint32_t h = seed + 374761393u;
        h += static_cast<std::uint32_t>(x) * 3266489917u;
        h ^= h >> 15; h *= 2246822519u;
        h += static_cast<std::uint32_t>(z) * 668265263u;
        h ^= h >> 13; h *= 3266489917u;
        h ^= h >> 16;
        return h;
    }

    float ValueAt(int xi, int zi, unsigned seed)
    {
        return (Hash(xi, zi, seed) & 0xFFFF) / 65535.0f; // [0,1]
    }

    float SmoothNoise(float x, float z, unsigned seed)
    {
        const int x0 = static_cast<int>(std::floor(x));
        const int z0 = static_cast<int>(std::floor(z));
        float tx = x - x0, tz = z - z0;
        tx = tx * tx * (3.0f - 2.0f * tx);
        tz = tz * tz * (3.0f - 2.0f * tz);
        const float v00 = ValueAt(x0, z0, seed), v10 = ValueAt(x0 + 1, z0, seed);
        const float v01 = ValueAt(x0, z0 + 1, seed), v11 = ValueAt(x0 + 1, z0 + 1, seed);
        const float a = v00 + (v10 - v00) * tx;
        const float b = v01 + (v11 - v01) * tx;
        return a + (b - a) * tz; // [0,1]
    }

    float Fbm(float x, float z, unsigned seed)
    {
        float sum = 0, amp = 0.5f, freq = 1.0f, norm = 0;
        for (int o = 0; o < 4; ++o)
        {
            sum += amp * SmoothNoise(x * freq, z * freq, seed + o * 101u);
            norm += amp; amp *= 0.5f; freq *= 2.0f;
        }
        return sum / norm; // [0,1]
    }

    //Final surface height of a column, symmetric in x. Base terrain only here;
    //Task 3 adds the mountain ridge.
    int SurfaceHeight(const TerrainConfig& c, int x, int z)
    {
        const int fx = Fold(c, x);
        const float hills = Fbm(fx * 0.06f, z * 0.06f, c.Seed);
        int h = c.GroundBase + static_cast<int>(std::lround(hills * 8.0f - 2.0f));
        return std::clamp(h, 1, c.Size.y - 1);
    }

    //Fills one column with stone/dirt/grass up to its surface. Task 3 adds snow.
    void FillColumn(VoxModel& m, const TerrainConfig& c, int x, int z)
    {
        const int top = SurfaceHeight(c, x, z);
        const bool dark = (Hash(Fold(c, x), z, c.Seed + 5u) & 1u) != 0;
        for (int y = 0; y < top; ++y)
        {
            BlockId id;
            if (y == top - 1)      id = dark ? GrassDark : Grass;
            else if (y >= top - 4) id = Dirt;
            else                   id = Stone;
            Set(m, x, y, z, id);
        }
    }

    void ApplyTerrain(VoxModel& m, const TerrainConfig& c)
    {
        for (int z = 0; z < c.Size.z; ++z)
            for (int x = 0; x < c.Size.x; ++x)
                FillColumn(m, c, x, z);
    }

    // Filled in by later tasks. No-ops for now.
    void CarveRiver(VoxModel&, const TerrainConfig&) {}
    void PlantForests(VoxModel&, const TerrainConfig&) {}
    void BuildForts(VoxModel&, const TerrainConfig&) {}
}

Palette TerrainGen::MapPalette()
{
    Palette p = DefaultPalette();
    p[Grass]       = glm::vec3(70, 145, 55) / 255.0f;
    p[GrassDark]   = glm::vec3(55, 115, 45) / 255.0f;
    p[Dirt]        = glm::vec3(120, 85, 50) / 255.0f;
    p[Stone]       = glm::vec3(130, 130, 135) / 255.0f;
    p[StoneDark]   = glm::vec3(80, 80, 85) / 255.0f;
    p[Sand]        = glm::vec3(210, 195, 140) / 255.0f;
    p[Water]       = glm::vec3(55, 110, 200) / 255.0f;
    p[Wood]        = glm::vec3(95, 65, 40) / 255.0f;
    p[Leaves]      = glm::vec3(45, 110, 45) / 255.0f;
    p[LeavesLight] = glm::vec3(70, 140, 60) / 255.0f;
    p[Snow]        = glm::vec3(235, 240, 245) / 255.0f;
    p[RedBase]     = glm::vec3(190, 45, 45) / 255.0f;
    p[BlueBase]    = glm::vec3(55, 80, 200) / 255.0f;
    return p;
}

VoxModel TerrainGen::Generate(const TerrainConfig& config)
{
    VoxModel m;
    m.Size = config.Size;
    m.Colors = MapPalette();
    m.Voxels.assign(
        static_cast<std::size_t>(config.Size.x) *
        static_cast<std::size_t>(config.Size.y) *
        static_cast<std::size_t>(config.Size.z), 0);

    ApplyTerrain(m, config);
    CarveRiver(m, config);
    PlantForests(m, config);
    BuildForts(m, config);
    return m;
}
```

- [ ] **Step 5: Build and confirm the tests pass**

Build. Expected: all four `TerrainGen` cases pass; the case count rises by 4.

- [ ] **Step 6: Commit**

```bash
git add Cubit Tests
git commit -m "Add TerrainGen with symmetric base terrain"
```

---

## Task 3: Mountains and snow

Add the flank ridge to `SurfaceHeight` and snow/dark-rock to `FillColumn`.

**Files:**
- Modify: `Cubit/src/Voxel/TerrainGen.cpp`
- Test: `Tests/src/TerrainGenTests.cpp`

**Interfaces:**
- Consumes: existing `SurfaceHeight`, `FillColumn`.
- Produces: no new public symbols; behaviour change only.

- [ ] **Step 1: Add the failing tests (append to `TerrainGenTests.cpp`)**

```cpp
TEST_CASE("Flanks rise into mountains higher than the central lanes")
{
    const VoxModel m = TerrainGen::Generate(SmallConfig());

    auto topAt = [&](int x, int z)
    {
        for (int y = m.Size.y - 1; y >= 0; --y)
            if (IsSolid(m.At(x, y, z))) return y;
        return -1;
    };

    // Average surface height near a z-edge (flank) vs. mid-z (lane), same x band.
    int flank = 0, lane = 0, n = 0;
    for (int x = 4; x < 20; ++x, ++n)
    {
        flank += topAt(x, 1);
        lane  += topAt(x, m.Size.z / 2);
    }
    CHECK(flank > lane); // flanks are taller
}

TEST_CASE("Snow only appears at or above the snow line")
{
    const VoxModel m = TerrainGen::Generate(SmallConfig());
    const TerrainConfig c = SmallConfig();
    for (int z = 0; z < m.Size.z; ++z)
        for (int y = 0; y < c.SnowLine; ++y)
            for (int x = 0; x < m.Size.x; ++x)
                CHECK(m.At(x, y, z) != MapBlocks::Snow);
}
```

- [ ] **Step 2: Build and confirm the new tests fail**

Build. Expected: "Flanks rise into mountains" fails (flanks not yet taller than lanes); the snow test passes trivially (no snow exists yet). This still counts as red for the feature.

- [ ] **Step 3: Extend `SurfaceHeight` with the mountain ridge**

Replace the `SurfaceHeight` function body with:

```cpp
    int SurfaceHeight(const TerrainConfig& c, int x, int z)
    {
        const int fx = Fold(c, x);
        const float hills = Fbm(fx * 0.06f, z * 0.06f, c.Seed);
        int h = c.GroundBase + static_cast<int>(std::lround(hills * 8.0f - 2.0f));

        // Ridge rising toward the z=0 and z=max flanks (0 at mid-z, 1 at edges).
        const float zc = (c.Size.z > 1) ? z / static_cast<float>(c.Size.z - 1) : 0.5f;
        const float edge = std::pow(std::abs(zc - 0.5f) * 2.0f, 2.0f);
        const float ridge = Fbm(fx * 0.03f, z * 0.03f, c.Seed + 7u);
        h += static_cast<int>(std::lround(c.MountainHeight * edge * ridge));

        return std::clamp(h, 1, c.Size.y - 1);
    }
```

- [ ] **Step 4: Extend `FillColumn` with snow and exposed dark rock**

Replace the `FillColumn` function body with:

```cpp
    void FillColumn(VoxModel& m, const TerrainConfig& c, int x, int z)
    {
        const int top = SurfaceHeight(c, x, z);
        const bool dark = (Hash(Fold(c, x), z, c.Seed + 5u) & 1u) != 0;
        const bool snowy = (top - 1) >= c.SnowLine;
        for (int y = 0; y < top; ++y)
        {
            BlockId id;
            if (y == top - 1)
                id = snowy ? Snow : (dark ? GrassDark : Grass);
            else if (y >= top - 4)
                id = snowy ? StoneDark : Dirt;
            else
                id = Stone;
            Set(m, x, y, z, id);
        }
    }
```

- [ ] **Step 5: Build and confirm all tests pass**

Build. Expected: mountains and snow cases pass; mirror-symmetry case still passes.

- [ ] **Step 6: Commit**

```bash
git add Cubit Tests
git commit -m "Add flank mountains and snow caps to TerrainGen"
```

---

## Task 4: River

Carve a symmetric central channel with water and sand banks.

**Files:**
- Modify: `Cubit/src/Voxel/TerrainGen.cpp`
- Test: `Tests/src/TerrainGenTests.cpp`

**Interfaces:**
- Consumes: existing `Set`, `Get`, `SurfaceHeight`, `SmoothNoise`, `Fold`.
- Produces: implemented `CarveRiver`.

- [ ] **Step 1: Add the failing tests (append to `TerrainGenTests.cpp`)**

```cpp
TEST_CASE("A river of water sits on the centre line and never above water level")
{
    const VoxModel m = TerrainGen::Generate(SmallConfig());
    const TerrainConfig c = SmallConfig();
    const int cx = c.Size.x / 2;

    bool waterAtCentre = false;
    for (int z = 0; z < m.Size.z; ++z)
        for (int y = 0; y <= c.WaterLevel; ++y)
            if (m.At(cx, y, z) == MapBlocks::Water ||
                m.At(cx - 1, y, z) == MapBlocks::Water)
                waterAtCentre = true;
    CHECK(waterAtCentre);

    // No water above the water level anywhere.
    for (int z = 0; z < m.Size.z; ++z)
        for (int y = c.WaterLevel + 1; y < m.Size.y; ++y)
            for (int x = 0; x < m.Size.x; ++x)
                CHECK(m.At(x, y, z) != MapBlocks::Water);
}

TEST_CASE("The river has sand somewhere on its banks")
{
    const VoxModel m = TerrainGen::Generate(SmallConfig());
    bool sand = false;
    for (std::size_t i = 0; i < m.Voxels.size() && !sand; ++i)
        if (m.Voxels[i] == MapBlocks::Sand) sand = true;
    CHECK(sand);
}
```

- [ ] **Step 2: Build and confirm the new tests fail**

Build. Expected: both new cases fail (no water/sand yet).

- [ ] **Step 3: Implement `CarveRiver`**

Replace the `CarveRiver` no-op with:

```cpp
    void CarveRiver(VoxModel& m, const TerrainConfig& c)
    {
        const int bed = c.WaterLevel - 2;               // river bottom
        for (int z = 0; z < c.Size.z; ++z)
        {
            // Half-width waves along z (symmetric: centreline stays on the mirror plane).
            const float w = SmoothNoise(z * 0.10f, 0.0f, c.Seed + 31u);
            const int halfWidth = 3 + static_cast<int>(std::lround(w * 3.0f)); // 3..6

            for (int x = 0; x < c.Size.x; ++x)
            {
                // Distance from the mirror plane, symmetric in x: 0 at the centre columns.
                const int dist = (c.Size.x - 1) / 2 - Fold(c, x);

                if (dist <= halfWidth)
                {
                    // Inside the channel: clear above the bed, fill water to water level.
                    for (int y = bed + 1; y < c.Size.y; ++y)
                        Set(m, x, y, z, 0);
                    for (int y = bed + 1; y <= c.WaterLevel; ++y)
                        Set(m, x, y, z, MapBlocks::Water);
                    if (Get(m, x, bed, z) == 0)
                        Set(m, x, bed, z, MapBlocks::Stone); // guarantee a floor
                }
                else if (dist <= halfWidth + 2)
                {
                    // Bank: turn the existing surface block to sand.
                    const int top = SurfaceHeight(c, x, z) - 1;
                    if (IsSolid(Get(m, x, top, z)))
                        Set(m, x, top, z, MapBlocks::Sand);
                }
            }
        }
    }
```

- [ ] **Step 4: Build and confirm all tests pass**

Build. Expected: river water, no-water-above-level, and sand cases pass; mirror symmetry still passes.

- [ ] **Step 5: Commit**

```bash
git add Cubit Tests
git commit -m "Carve a symmetric central river with sand banks"
```

---

## Task 5: Forests

Scatter symmetric trees on grass, away from the river.

**Files:**
- Modify: `Cubit/src/Voxel/TerrainGen.cpp`
- Test: `Tests/src/TerrainGenTests.cpp`

**Interfaces:**
- Consumes: existing `Set`, `Get`, `SurfaceHeight`, `Hash`, `Fold`.
- Produces: implemented `PlantForests`.

- [ ] **Step 1: Add the failing tests (append to `TerrainGenTests.cpp`)**

```cpp
TEST_CASE("Forests place tree trunks whose base rests on grass")
{
    const VoxModel m = TerrainGen::Generate(SmallConfig());

    int trunks = 0;
    for (int z = 0; z < m.Size.z; ++z)
        for (int y = 1; y < m.Size.y; ++y)
            for (int x = 0; x < m.Size.x; ++x)
                if (m.At(x, y, z) == MapBlocks::Wood &&
                    m.At(x, y - 1, z) != MapBlocks::Wood)
                {
                    // This is a trunk base: the block under it must be grass.
                    const std::uint8_t below = m.At(x, y - 1, z);
                    CHECK((below == MapBlocks::Grass || below == MapBlocks::GrassDark));
                    ++trunks;
                }
    CHECK(trunks > 0); // at least some trees exist
}

TEST_CASE("Leaves exist above trunks")
{
    const VoxModel m = TerrainGen::Generate(SmallConfig());
    bool leaves = false;
    for (std::size_t i = 0; i < m.Voxels.size() && !leaves; ++i)
        if (m.Voxels[i] == MapBlocks::Leaves || m.Voxels[i] == MapBlocks::LeavesLight)
            leaves = true;
    CHECK(leaves);
}
```

- [ ] **Step 2: Build and confirm the new tests fail**

Build. Expected: both cases fail (no wood/leaves yet).

- [ ] **Step 3: Implement `PlantForests`**

Replace the `PlantForests` no-op with:

```cpp
    void PlaceTree(VoxModel& m, const TerrainConfig& c, int x, int baseY, int z)
    {
        const std::uint32_t r = Hash(Fold(c, x), z, c.Seed + 71u);
        const int trunkH = 4 + static_cast<int>(r % 4u); // 4..7
        for (int i = 0; i < trunkH; ++i)
            Set(m, x, baseY + i, z, MapBlocks::Wood);

        const int cy = baseY + trunkH;      // canopy centre
        const int rad = 2;
        for (int dz = -rad; dz <= rad; ++dz)
            for (int dy = -rad; dy <= rad; ++dy)
                for (int dx = -rad; dx <= rad; ++dx)
                {
                    if (dx * dx + dy * dy + dz * dz > rad * rad + 1)
                        continue;
                    const bool light = ((dx + dy + dz) & 1) != 0;
                    // Do not overwrite the trunk column below the canopy centre.
                    if (dx == 0 && dz == 0 && dy < 0)
                        continue;
                    Set(m, x + dx, cy + dy, z + dz,
                        light ? MapBlocks::LeavesLight : MapBlocks::Leaves);
                }
    }

    void PlantForests(VoxModel& m, const TerrainConfig& c)
    {
        for (int z = 2; z < c.Size.z - 2; ++z)
            for (int x = 2; x < c.Size.x - 2; ++x)
            {
                const int top = SurfaceHeight(c, x, z) - 1; // surface block y
                const std::uint8_t surface = Get(m, x, top, z);
                if (surface != MapBlocks::Grass && surface != MapBlocks::GrassDark)
                    continue;                              // only on grass
                if (top <= c.WaterLevel)
                    continue;                              // not in wet lowland
                const float roll = (Hash(Fold(c, x), z, c.Seed + 91u) & 0xFFFF) / 65535.0f;
                if (roll < c.ForestDensity)
                    PlaceTree(m, c, x, top + 1, z);
            }
    }
```

- [ ] **Step 4: Build and confirm all tests pass**

Build. Expected: trunk-on-grass and leaves cases pass; mirror symmetry still passes (trees are placed from the folded coordinate, so both halves match).

- [ ] **Step 5: Commit**

```bash
git add Cubit Tests
git commit -m "Scatter symmetric forests across the terrain"
```

---

## Task 6: Forts

Build two symmetric ramparts, red on the left, blue on the right.

**Files:**
- Modify: `Cubit/src/Voxel/TerrainGen.cpp`
- Test: `Tests/src/TerrainGenTests.cpp`

**Interfaces:**
- Consumes: existing `Set`, `SurfaceHeight`, `Fold`.
- Produces: implemented `BuildForts`.

- [ ] **Step 1: Add the failing tests (append to `TerrainGenTests.cpp`)**

```cpp
TEST_CASE("Forts are team-coloured: red only on the left half, blue only on the right")
{
    const VoxModel m = TerrainGen::Generate(SmallConfig());
    const int half = m.Size.x / 2;

    int red = 0, blue = 0;
    for (int z = 0; z < m.Size.z; ++z)
        for (int y = 0; y < m.Size.y; ++y)
            for (int x = 0; x < m.Size.x; ++x)
            {
                const std::uint8_t id = m.At(x, y, z);
                if (id == MapBlocks::RedBase)
                {
                    CHECK(x < half);
                    ++red;
                }
                else if (id == MapBlocks::BlueBase)
                {
                    CHECK(x >= half);
                    ++blue;
                }
            }
    CHECK(red > 0);
    CHECK(blue > 0);
    CHECK(red == blue); // symmetric forts
}

TEST_CASE("Colours mirror across x everywhere except the team-coloured forts")
{
    const VoxModel m = TerrainGen::Generate(SmallConfig());
    for (int z = 0; z < m.Size.z; ++z)
        for (int y = 0; y < m.Size.y; ++y)
            for (int x = 0; x < m.Size.x; ++x)
            {
                const std::uint8_t a = m.At(x, y, z);
                const std::uint8_t b = m.At(m.Size.x - 1 - x, y, z);
                const bool teamA = (a == MapBlocks::RedBase || a == MapBlocks::BlueBase);
                const bool teamB = (b == MapBlocks::RedBase || b == MapBlocks::BlueBase);
                if (teamA || teamB)
                    continue; // forts are intentionally recoloured by side
                CHECK(a == b);
            }
}
```

- [ ] **Step 2: Build and confirm the new test fails**

Build. Expected: fails (no fort blocks yet).

- [ ] **Step 3: Implement `BuildForts`**

Replace the `BuildForts` no-op with:

```cpp
    void BuildForts(VoxModel& m, const TerrainConfig& c)
    {
        const int half = c.Size.x / 2;
        const int cz = c.Size.z / 2;         // fort centre in z
        const int fx = 8;                    // fort centre distance from the x edge
        const int radius = 5;                // half-footprint
        const int wallH = 5;

        // Two mirrored footprints: left (x≈fx) and right (x≈Size.x-1-fx).
        const int centres[2] = { fx, c.Size.x - 1 - fx };
        for (int cxi : centres)
        {
            for (int z = cz - radius; z <= cz + radius; ++z)
                for (int x = cxi - radius; x <= cxi + radius; ++x)
                {
                    if (x < 0 || x >= c.Size.x || z < 0 || z >= c.Size.z)
                        continue;
                    const int ground = SurfaceHeight(c, x, z); // floor sits here
                    const BlockId team = (x < half) ? MapBlocks::RedBase
                                                    : MapBlocks::BlueBase;

                    // Floor slab.
                    Set(m, x, ground, z, team);

                    // Perimeter walls, with an opening on the field-facing side.
                    const bool edge =
                        x == cxi - radius || x == cxi + radius ||
                        z == cz - radius || z == cz + radius;
                    const bool opening =
                        (z == cz) && ((x < half) ? (x == cxi + radius)
                                                  : (x == cxi - radius));
                    if (edge && !opening)
                        for (int i = 1; i <= wallH; ++i)
                            Set(m, x, ground + i, z, team);
                }
        }
    }
```

- [ ] **Step 4: Build and confirm all tests pass**

Build. Expected: the fort test passes (`red == blue`, red left / blue right); every earlier `TerrainGen` case still passes.

- [ ] **Step 5: Commit**

```bash
git add Cubit Tests
git commit -m "Build two team-coloured forts"
```

---

## Task 7: `MapGen` tool and the committed `battlefield.vox`

Add the console project, generate the asset, and pin it with a parse test.

**Files:**
- Create: `MapGen/src/MapGen.cpp`
- Create: `Sandbox/assets/maps/battlefield.vox` (generated, committed)
- Modify: `premake5.lua`
- Test: `Tests/src/TerrainGenTests.cpp`

**Interfaces:**
- Consumes: `TerrainGen::Generate`, `VoxWriter::Write`, `VoxLoader::LoadFile`.

- [ ] **Step 1: Write `MapGen/src/MapGen.cpp`**

```cpp
#include "Cubit/Voxel/TerrainGen.h"
#include "Cubit/Voxel/VoxWriter.h"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

//Offline tool: generate the battlefield map and write it as .vox. Pass an output
//path as argv[1], e.g. "Sandbox/assets/maps/battlefield.vox".
int main(int argc, char** argv)
{
    const std::string out = (argc > 1) ? argv[1] : "battlefield.vox";

    const TerrainConfig config;
    const VoxModel model = TerrainGen::Generate(config);
    const std::vector<std::uint8_t> bytes = VoxWriter::Write(model);

    std::ofstream file(out, std::ios::binary);
    if (!file)
    {
        std::cerr << "Cannot open output: " << out << "\n";
        return 1;
    }
    file.write(reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));

    std::cout << "Wrote " << out << " (" << bytes.size() << " bytes)\n";
    return 0;
}
```

- [ ] **Step 2: Add the `MapGen` project to `premake5.lua`**

After the `project "Sandbox"` block (before `group "Tests"`), add:

```lua
project "MapGen"
    location "MapGen"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

    files
    {
        "MapGen/src/**.h",
        "MapGen/src/**.cpp"
    }

    includedirs
    {
        "Cubit/include",
        "vendor/GLM"
    }

    links
    {
        "Cubit"
    }

    defines
    {
        "CB_PLATFORM_WINDOWS"
    }

    filter "system:windows"
        systemversion "latest"

        postbuildcommands
        {
            ("{COPY} ../bin/" .. outputdir .. "/Cubit/Cubit.dll ../bin/" .. outputdir .. "/MapGen")
        }

    filter "configurations:Debug"
        defines "CB_DEBUG"
        symbols "On"

    filter "configurations:Release"
        defines "CB_RELEASE"
        optimize "On"

    filter "configurations:Dist"
        defines "CB_DIST"
        optimize "On"
```

- [ ] **Step 3: Regenerate and build**

Run: `/c/dev/premake/premake5 vs2026`
Then build. Expected: `MapGen.exe` links (it needs `Cubit.dll`, copied beside it by the postbuild). All existing tests still pass.

- [ ] **Step 4: Run `MapGen` to produce the committed asset**

From the repo root `C:\dev\Cubit`:
```
./bin/Debug-windows-x86_64/MapGen/MapGen.exe Sandbox/assets/maps/battlefield.vox
```
Expected: prints `Wrote Sandbox/assets/maps/battlefield.vox (... bytes)` (on the order of 1–1.5 MB).

- [ ] **Step 5: Add a parse test pinning the committed asset**

Append to `Tests/src/TerrainGenTests.cpp`:

```cpp
#include "Cubit/Voxel/VoxLoader.h"
#include <filesystem>

TEST_CASE("The committed battlefield map loads at the expected size")
{
    const std::filesystem::path path = "Sandbox/assets/maps/battlefield.vox";
    if (!std::filesystem::exists(path))
        return; // asset not reachable from this working directory; skip

    const VoxModel m = VoxLoader::LoadFile(path.string());
    CHECK(m.Size == glm::ivec3(128, 48, 128));
}
```

- [ ] **Step 6: Build and confirm tests pass**

Build. Expected: green (the new case passes when the asset is reachable, otherwise skips).

- [ ] **Step 7: Commit**

```bash
git add MapGen premake5.lua Sandbox/assets/maps/battlefield.vox Tests
git commit -m "Generate the battlefield map with a MapGen tool"
```

---

## Task 8: Load the battlefield map in the Sandbox

Point the Sandbox at the new map and spawn on Fort A.

**Files:**
- Modify: `Sandbox/src/Sandbox.cpp`

**Interfaces:**
- Consumes: `VoxLoader::LoadFile`, `BuildWorld` (existing).

- [ ] **Step 1: Switch the loaded map and set a fort-top spawn**

In `Sandbox/src/Sandbox.cpp`, change the map load line in the `SandboxLayer` constructor from:

```cpp
        m_World = BuildWorld(VoxLoader::LoadFile("assets/maps/starter.vox"));
```
to:
```cpp
        m_World = BuildWorld(VoxLoader::LoadFile("assets/maps/battlefield.vox"));
```

Update the spawn/offset constants near the top of the anonymous namespace. Replace the existing `WorldOffset`, `SpawnPosition`, and `FallResetHeight` definitions with:

```cpp
    //Centre the 128x48x128 map roughly on the origin for the view.
    const glm::vec3 WorldOffset{ -64.0f, -24.0f, -64.0f };

    //Spawn on top of Fort A (x≈8, z≈64), looking down the field toward Fort B.
    const glm::vec3 SpawnPosition{ 8.5f, 26.0f, 64.5f };

    //Below the map floor: a fallen player is returned to spawn.
    constexpr float FallResetHeight = -8.0f;
```

(Leave `ReachDistance`, `PlayerHalfExtents`, `EyeOffset`, the speeds, and everything else unchanged.)

- [ ] **Step 2: Build and run the test suite**

Build. Expected: compiles; all tests still pass (this task is Sandbox-only, no engine test changes).

- [ ] **Step 3: Verify rendering**

Run the Sandbox with its working directory at the target dir so `assets/` resolves (the premake `debugdir` + `{COPYDIR} assets` postbuild from the prior slice already copy `battlefield.vox` beside the exe). Screenshot the window, then close it via `PostMessage(hwnd, WM_CLOSE=0x0010, 0, 0)` (project convention — do not `Stop-Process`, or the buffered stdout log is lost). Do **not** foreground the cursor-capturing window, or stray clicks edit the map.

Confirm the frame reads as a symmetric battlefield: a central river, snow-capped mountains framing the flanks, scattered forests, and two forts (red near spawn, blue across the field). Warn the user first — running grabs the mouse cursor for a few seconds.

- [ ] **Step 4: Commit**

```bash
git add Sandbox
git commit -m "Load the battlefield map in the Sandbox"
```

---

## Notes for the implementer

- The premake `files` globs pick up new `Cubit/src/**.cpp` and `Tests/src/**.cpp` automatically, but you MUST regenerate (`premake5 vs2026`) after adding files or the new `MapGen` project, or they silently won't compile. Watch the `[doctest] test cases:` count rise to confirm new test files are included.
- Returning `VoxModel` / `std::vector` / `Palette` by value across the DLL boundary matches the existing `VoxLoader` pattern — no special-member work needed.
- Tests use a 64×64×64 config for speed; the shipped map is 128×48×128 via `TerrainConfig` defaults. Keep test widths even so the mirror math (`Size.x/2`, `Size.x-1-x`) stays clean.
- If a build's postbuild `{COPYDIR}`/`{COPY}` token misbehaves, the goal is only that `Cubit.dll` sits beside `MapGen.exe` and `assets/` sits beside `Sandbox.exe`; fall back to an explicit copy if needed.
```
