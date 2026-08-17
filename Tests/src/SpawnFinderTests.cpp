#include <doctest.h>

#include "Cubit/Voxel/SpawnFinder.h"
#include "Cubit/Voxel/VoxLoader.h"
#include "Cubit/Voxel/VoxelCollision.h"
#include "Cubit/Voxel/World.h"

#include <cmath>
#include <filesystem>

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
