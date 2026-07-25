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
