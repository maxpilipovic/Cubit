#include <doctest.h>

#include "Cubit/Voxel/SpawnFinder.h"
#include "Cubit/Voxel/World.h"

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
