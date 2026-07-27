#include <doctest.h>

#include "Cubit/Voxel/Chunk.h"
#include "Cubit/Voxel/SkyLight.h"
#include "Cubit/Voxel/World.h"

TEST_CASE("An open column is lit from top to bottom")
{
    World world(1, 4, 1);
    SkyLight::PropagateAll(world);

    for (int y = 0; y < world.GetHeight(); ++y)
        CHECK(world.GetSkyLight(8, y, 8) == SkyLight::Max);
}

TEST_CASE("A solid block holds no light")
{
    World world(1, 4, 1);
    world.SetBlock(8, 20, 8, BlockId{1});

    SkyLight::PropagateAll(world);

    CHECK(world.GetSkyLight(8, 20, 8) == 0);
}

TEST_CASE("A roof darkens the column beneath it")
{
    // A solid ceiling spanning the whole world, so no light can creep in from
    // the sides. Everything below it must be pitch dark.
    World world(1, 4, 1);
    const int roof = 20;

    for (int z = 0; z < world.GetDepth(); ++z)
        for (int x = 0; x < world.GetWidth(); ++x)
            world.SetBlock(x, roof, z, BlockId{1});

    SkyLight::PropagateAll(world);

    CHECK(world.GetSkyLight(8, roof + 1, 8) == SkyLight::Max);
    CHECK(world.GetSkyLight(8, roof - 1, 8) == 0);
    CHECK(world.GetSkyLight(8, 0, 8) == 0);
}

TEST_CASE("Light spreads sideways under an overhang, losing one level a step")
{
    // A roof covering everything except the x == 0 column, which stays open to
    // the sky. Light falls down that column and creeps sideways underneath.
    World world(2, 4, 1);
    const int roof = 20;

    for (int z = 0; z < world.GetDepth(); ++z)
        for (int x = 1; x < world.GetWidth(); ++x)
            world.SetBlock(x, roof, z, BlockId{1});

    SkyLight::PropagateAll(world);

    const int under = roof - 1;
    REQUIRE(world.GetSkyLight(0, under, 8) == SkyLight::Max);

    CHECK(world.GetSkyLight(1, under, 8) == SkyLight::Max - 1);
    CHECK(world.GetSkyLight(2, under, 8) == SkyLight::Max - 2);
    CHECK(world.GetSkyLight(3, under, 8) == SkyLight::Max - 3);
}

TEST_CASE("Light dies out after fifteen sideways steps")
{
    World world(2, 4, 1);
    const int roof = 20;

    for (int z = 0; z < world.GetDepth(); ++z)
        for (int x = 1; x < world.GetWidth(); ++x)
            world.SetBlock(x, roof, z, BlockId{1});

    SkyLight::PropagateAll(world);

    const int under = roof - 1;
    CHECK(world.GetSkyLight(15, under, 8) == 0);
    CHECK(world.GetSkyLight(16, under, 8) == 0);
}

TEST_CASE("Light that has already dimmed does not fall for free")
{
    // Sky light falls without attenuation only at full strength. Once it has
    // spread sideways and dimmed, dropping down must cost a level like any
    // other step, or a deep cave stays almost fully lit.
    //
    // A solid world with an L carved into it: a shaft open to the sky, and a
    // pocket hanging off its foot. The pocket's only entrance is one sideways
    // step out of the shaft, so everything below its top is reached by light
    // that has already dimmed falling — the case a free fall would mask.
    World world(1, 4, 1);

    for (int z = 0; z < world.GetDepth(); ++z)
        for (int y = 0; y < world.GetHeight(); ++y)
            for (int x = 0; x < world.GetWidth(); ++x)
                world.SetBlock(x, y, z, BlockId{1});

    const int mouth = 30;

    // The shaft, open from the sky down to the pocket.
    for (int y = world.GetHeight() - 1; y >= mouth; --y)
        world.SetBlock(0, y, 0, BlockId{0});

    // The pocket, one column over, hanging below the shaft's mouth.
    for (int y = mouth; y >= mouth - 2; --y)
        world.SetBlock(1, y, 0, BlockId{0});

    SkyLight::PropagateAll(world);

    REQUIRE(world.GetSkyLight(0, mouth, 0) == SkyLight::Max);

    CHECK(world.GetSkyLight(1, mouth, 0) == SkyLight::Max - 1);
    CHECK(world.GetSkyLight(1, mouth - 1, 0) == SkyLight::Max - 2);
    CHECK(world.GetSkyLight(1, mouth - 2, 0) == SkyLight::Max - 3);
}

TEST_CASE("Propagation is idempotent")
{
    World world(2, 2, 2);

    for (int z = 0; z < world.GetDepth(); ++z)
        for (int x = 0; x < world.GetWidth(); ++x)
            world.SetBlock(x, 10, z, BlockId{1});

    SkyLight::PropagateAll(world);
    const std::uint8_t sample = world.GetSkyLight(5, 20, 5);

    SkyLight::PropagateAll(world);

    CHECK(world.GetSkyLight(5, 20, 5) == sample);
}
