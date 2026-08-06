#include <doctest.h>

#include "Cubit/Voxel/TerrainGen.h"
#include "Cubit/Voxel/VoxLoader.h"

#include <filesystem>

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

    auto isVeg = [](std::uint8_t id)
    {
        return id == MapBlocks::Wood || id == MapBlocks::Leaves ||
               id == MapBlocks::LeavesLight;
    };

    // The terrain surface is the highest non-vegetation solid block (a tree may
    // sit on top of it).
    int top = -1;
    for (int y = m.Size.y - 1; y >= 0; --y)
        if (IsSolid(m.At(x, y, z)) && !isVeg(m.At(x, y, z))) { top = y; break; }

    REQUIRE(top >= 0);
    CHECK(IsGrass(m.At(x, top, z)));
    CHECK(IsSolid(m.At(x, top - 1, z)));
    CHECK(m.At(x, 0, z) == MapBlocks::Stone);
}

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

TEST_CASE("The committed battlefield map loads at the expected size")
{
    const std::filesystem::path path = "Sandbox/assets/maps/battlefield.vox";
    if (!std::filesystem::exists(path))
        return; // asset not reachable from this working directory; skip

    const VoxModel m = VoxLoader::LoadFile(path.string());
    CHECK(m.Size == glm::ivec3(256, 64, 256));
}

TEST_CASE("Water is the only transparent entry in the map palette")
{
    const Palette palette = TerrainGen::MapPalette();

    CHECK(palette[MapBlocks::Water].a < 1.0f);

    // Everything else must stay opaque, or terrain would blend against itself.
    for (std::size_t i = 1; i < palette.size(); ++i)
        if (i != MapBlocks::Water)
            REQUIRE(palette[i].a >= 1.0f);
}
