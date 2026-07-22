#include <doctest.h>

#include "Cubit/Voxel/Chunk.h"
#include "Cubit/Voxel/World.h"
#include "Cubit/Voxel/VoxelRaycast.h"

namespace
{
    //Returns the centre of a block, so rays start away from face boundaries.
    glm::vec3 CentreOf(int x, int y, int z)
    {
        return glm::vec3(x + 0.5f, y + 0.5f, z + 0.5f);
    }
}

TEST_CASE("A ray through empty space hits nothing")
{
    const World world(1, 1, 1);

    const VoxelRayHit hit =
        VoxelRaycast::Cast(world, CentreOf(8, 8, 0), glm::vec3(0, 0, 1), 32.0f);

    CHECK_FALSE(hit.Hit);
}

TEST_CASE("A ray stops at the first solid block")
{
    World world(1, 1, 1);
    world.SetBlock(8, 8, 5, BlockType::Solid);
    world.SetBlock(8, 8, 9, BlockType::Solid);

    const VoxelRayHit hit =
        VoxelRaycast::Cast(world, CentreOf(8, 8, 0), glm::vec3(0, 0, 1), 32.0f);

    REQUIRE(hit.Hit);
    CHECK(hit.Block == glm::ivec3(8, 8, 5));
}

TEST_CASE("The hit normal points back along the ray")
{
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockType::Solid);

    SUBCASE("travelling +Z")
    {
        const VoxelRayHit hit =
            VoxelRaycast::Cast(world, CentreOf(8, 8, 3), glm::vec3(0, 0, 1), 32.0f);
        REQUIRE(hit.Hit);
        CHECK(hit.Normal == glm::ivec3(0, 0, -1));
    }

    SUBCASE("travelling -Z")
    {
        const VoxelRayHit hit =
            VoxelRaycast::Cast(world, CentreOf(8, 8, 13), glm::vec3(0, 0, -1), 32.0f);
        REQUIRE(hit.Hit);
        CHECK(hit.Normal == glm::ivec3(0, 0, 1));
    }

    SUBCASE("travelling +X")
    {
        const VoxelRayHit hit =
            VoxelRaycast::Cast(world, CentreOf(3, 8, 8), glm::vec3(1, 0, 0), 32.0f);
        REQUIRE(hit.Hit);
        CHECK(hit.Normal == glm::ivec3(-1, 0, 0));
    }

    SUBCASE("travelling -X")
    {
        const VoxelRayHit hit =
            VoxelRaycast::Cast(world, CentreOf(13, 8, 8), glm::vec3(-1, 0, 0), 32.0f);
        REQUIRE(hit.Hit);
        CHECK(hit.Normal == glm::ivec3(1, 0, 0));
    }

    SUBCASE("travelling +Y")
    {
        const VoxelRayHit hit =
            VoxelRaycast::Cast(world, CentreOf(8, 3, 8), glm::vec3(0, 1, 0), 32.0f);
        REQUIRE(hit.Hit);
        CHECK(hit.Normal == glm::ivec3(0, -1, 0));
    }

    SUBCASE("travelling -Y")
    {
        const VoxelRayHit hit =
            VoxelRaycast::Cast(world, CentreOf(8, 13, 8), glm::vec3(0, -1, 0), 32.0f);
        REQUIRE(hit.Hit);
        CHECK(hit.Normal == glm::ivec3(0, 1, 0));
    }
}

TEST_CASE("The block next to the hit face is always empty")
{
    // This is the position a placed block goes into, so it must be air.
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockType::Solid);
    world.SetBlock(8, 8, 9, BlockType::Solid);

    const VoxelRayHit hit =
        VoxelRaycast::Cast(world, CentreOf(8, 8, 2), glm::vec3(0, 0, 1), 32.0f);

    REQUIRE(hit.Hit);
    const glm::ivec3 placement = hit.Block + hit.Normal;

    CHECK(placement == glm::ivec3(8, 8, 7));
    CHECK_FALSE(world.IsBlockSolid(placement.x, placement.y, placement.z));
}

TEST_CASE("A ray does not reach past its maximum distance")
{
    World world(1, 1, 1);
    world.SetBlock(8, 8, 12, BlockType::Solid);

    const glm::vec3 origin = CentreOf(8, 8, 2);

    const VoxelRayHit tooShort =
        VoxelRaycast::Cast(world, origin, glm::vec3(0, 0, 1), 4.0f);
    CHECK_FALSE(tooShort.Hit);

    const VoxelRayHit longEnough =
        VoxelRaycast::Cast(world, origin, glm::vec3(0, 0, 1), 32.0f);
    CHECK(longEnough.Hit);
}

TEST_CASE("Reported distance matches the travelled length")
{
    World world(1, 1, 1);
    world.SetBlock(8, 8, 10, BlockType::Solid);

    const VoxelRayHit hit =
        VoxelRaycast::Cast(world, CentreOf(8, 8, 2), glm::vec3(0, 0, 1), 32.0f);

    // Starts at z=2.5, enters the block at its z=10 face.
    REQUIRE(hit.Hit);
    CHECK(hit.Distance == doctest::Approx(7.5f));
}

TEST_CASE("A ray starting inside a solid block reports that block")
{
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockType::Solid);

    const VoxelRayHit hit =
        VoxelRaycast::Cast(world, CentreOf(8, 8, 8), glm::vec3(0, 0, 1), 32.0f);

    REQUIRE(hit.Hit);
    CHECK(hit.Block == glm::ivec3(8, 8, 8));
    CHECK(hit.Distance == doctest::Approx(0.0f));
    CHECK(hit.Normal == glm::ivec3(0, 0, 0));
}

TEST_CASE("An unnormalized direction gives the same result")
{
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockType::Solid);

    const glm::vec3 origin = CentreOf(8, 8, 2);
    const VoxelRayHit unit =
        VoxelRaycast::Cast(world, origin, glm::vec3(0, 0, 1), 32.0f);
    const VoxelRayHit scaled =
        VoxelRaycast::Cast(world, origin, glm::vec3(0, 0, 7.3f), 32.0f);

    REQUIRE(unit.Hit);
    REQUIRE(scaled.Hit);
    CHECK(unit.Block == scaled.Block);
    CHECK(unit.Normal == scaled.Normal);
    CHECK(unit.Distance == doctest::Approx(scaled.Distance));
}

TEST_CASE("A zero direction cannot hit anything")
{
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockType::Solid);

    const VoxelRayHit hit =
        VoxelRaycast::Cast(world, CentreOf(8, 8, 2), glm::vec3(0, 0, 0), 32.0f);

    CHECK_FALSE(hit.Hit);
}

TEST_CASE("A diagonal ray enters through the nearer face")
{
    // The ray rises into y=9 first, passing through the empty block at (8,9,8),
    // and only then crosses x into the target. So it enters through the -X face
    // even though the y step happened earlier.
    World world(1, 1, 1);
    world.SetBlock(9, 9, 8, BlockType::Solid);

    const VoxelRayHit hit = VoxelRaycast::Cast(
        world,
        glm::vec3(8.5f, 8.9f, 8.5f),
        glm::vec3(1.0f, 0.4f, 0.0f),
        32.0f);

    REQUIRE(hit.Hit);
    CHECK(hit.Block == glm::ivec3(9, 9, 8));
    CHECK(hit.Normal == glm::ivec3(-1, 0, 0));
}

TEST_CASE("A ray from outside the world reaches blocks inside it")
{
    World world(1, 1, 1);
    world.SetBlock(0, 8, 8, BlockType::Solid);

    const VoxelRayHit hit = VoxelRaycast::Cast(
        world,
        glm::vec3(-5.5f, 8.5f, 8.5f),
        glm::vec3(1, 0, 0),
        32.0f);

    REQUIRE(hit.Hit);
    CHECK(hit.Block == glm::ivec3(0, 8, 8));
    CHECK(hit.Normal == glm::ivec3(-1, 0, 0));
}

TEST_CASE("A ray crosses a chunk boundary to reach a block in the next chunk")
{
    // The block sits at world x=20, inside chunk 1. The ray starts in chunk 0 and
    // must walk across the seam at x=16 to reach it. A single chunk could not even
    // name block 20, so this only works in world coordinates.
    World world(2, 1, 1);
    world.SetBlock(20, 8, 8, BlockType::Solid);

    const VoxelRayHit hit =
        VoxelRaycast::Cast(world, CentreOf(2, 8, 8), glm::vec3(1, 0, 0), 32.0f);

    REQUIRE(hit.Hit);
    CHECK(hit.Block == glm::ivec3(20, 8, 8));
    CHECK(hit.Normal == glm::ivec3(-1, 0, 0));
}

TEST_CASE("Rays never report a block they did not enter")
{
    // Sweeps many directions and confirms every reported hit is genuinely solid
    // and that the face in front of it is genuinely air.
    World world(1, 1, 1);
    for (int z = 0; z < Chunk::Depth; ++z)
        for (int x = 0; x < Chunk::Width; ++x)
            for (int y = 0; y < 4; ++y)
                world.SetBlock(x, y, z, BlockType::Solid);

    const glm::vec3 origin(8.5f, 12.5f, 8.5f);

    for (int i = 0; i < 64; ++i)
    {
        const float angle = static_cast<float>(i) * 0.1f;
        const glm::vec3 direction(std::cos(angle), -1.0f, std::sin(angle));

        const VoxelRayHit hit =
            VoxelRaycast::Cast(world, origin, direction, 64.0f);

        if (!hit.Hit)
            continue;

        REQUIRE(world.IsBlockSolid(hit.Block.x, hit.Block.y, hit.Block.z));

        const glm::ivec3 placement = hit.Block + hit.Normal;
        REQUIRE_FALSE(world.IsBlockSolid(placement.x, placement.y, placement.z));
    }
}
