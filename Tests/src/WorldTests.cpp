#include <doctest.h>

#include "Cubit/Voxel/World.h"

#include <stdexcept>

TEST_CASE("A world reports its size in blocks")
{
    const World world(2, 3, 4);

    CHECK(world.GetChunksX() == 2);
    CHECK(world.GetChunksY() == 3);
    CHECK(world.GetChunksZ() == 4);
    CHECK(world.GetWidth() == 2 * Chunk::Width);
    CHECK(world.GetHeight() == 3 * Chunk::Height);
    CHECK(world.GetDepth() == 4 * Chunk::Depth);
}

TEST_CASE("A world must have at least one chunk on every axis")
{
    CHECK_THROWS_AS(World(0, 1, 1), std::invalid_argument);
    CHECK_THROWS_AS(World(1, -1, 1), std::invalid_argument);
    CHECK_THROWS_AS(World(1, 1, 0), std::invalid_argument);
}

TEST_CASE("A new world contains only air")
{
    const World world(2, 2, 2);

    for (int z = 0; z < world.GetDepth(); ++z)
        for (int y = 0; y < world.GetHeight(); ++y)
            for (int x = 0; x < world.GetWidth(); ++x)
                REQUIRE(world.GetBlock(x, y, z) == BlockType::Air);
}

TEST_CASE("Every world position stores its own block")
{
    // Spans several chunks, so a coordinate mistake shows up as two positions
    // sharing storage rather than as an obvious crash.
    World world(2, 2, 2);

    const auto expected = [](int x, int y, int z)
    {
        return (x * 7 + y * 13 + z * 23) % 3 == 0;
    };

    for (int z = 0; z < world.GetDepth(); ++z)
        for (int y = 0; y < world.GetHeight(); ++y)
            for (int x = 0; x < world.GetWidth(); ++x)
                if (expected(x, y, z))
                    world.SetBlock(x, y, z, BlockType::Red);

    for (int z = 0; z < world.GetDepth(); ++z)
        for (int y = 0; y < world.GetHeight(); ++y)
            for (int x = 0; x < world.GetWidth(); ++x)
                REQUIRE(world.IsBlockSolid(x, y, z) == expected(x, y, z));
}

TEST_CASE("Writing a block does not disturb the neighbouring chunk")
{
    World world(2, 1, 1);

    // Either side of the boundary between chunk 0 and chunk 1.
    world.SetBlock(Chunk::Width - 1, 4, 4, BlockType::Green);

    CHECK(world.GetBlock(Chunk::Width - 1, 4, 4) == BlockType::Green);
    CHECK(world.GetBlock(Chunk::Width, 4, 4) == BlockType::Air);

    world.SetBlock(Chunk::Width, 4, 4, BlockType::Blue);

    CHECK(world.GetBlock(Chunk::Width - 1, 4, 4) == BlockType::Green);
    CHECK(world.GetBlock(Chunk::Width, 4, 4) == BlockType::Blue);
}

TEST_CASE("Positions outside the world read as air")
{
    World world(1, 1, 1);
    world.SetBlock(0, 0, 0, BlockType::Solid);

    CHECK(world.GetBlock(-1, 0, 0) == BlockType::Air);
    CHECK(world.GetBlock(0, -1, 0) == BlockType::Air);
    CHECK(world.GetBlock(0, 0, -1) == BlockType::Air);
    CHECK(world.GetBlock(world.GetWidth(), 0, 0) == BlockType::Air);
    CHECK(world.GetBlock(0, world.GetHeight(), 0) == BlockType::Air);
    CHECK(world.GetBlock(0, 0, world.GetDepth()) == BlockType::Air);
}

TEST_CASE("Writing outside the world is rejected")
{
    World world(2, 2, 2);

    CHECK_THROWS_AS(world.SetBlock(-1, 0, 0, BlockType::Solid), std::out_of_range);
    CHECK_THROWS_AS(
        world.SetBlock(world.GetWidth(), 0, 0, BlockType::Solid), std::out_of_range);
    CHECK_THROWS_AS(
        world.SetBlock(0, world.GetHeight(), 0, BlockType::Solid), std::out_of_range);
    CHECK_THROWS_AS(
        world.SetBlock(0, 0, world.GetDepth(), BlockType::Solid), std::out_of_range);
}

TEST_CASE("A block written through the world is visible in its chunk")
{
    // Confirms the world and the chunk agree on where a block lives, which is
    // what meshing depends on.
    World world(2, 2, 2);

    const int worldX = Chunk::Width + 3;
    const int worldY = Chunk::Height + 5;
    const int worldZ = Chunk::Depth + 7;
    world.SetBlock(worldX, worldY, worldZ, BlockType::Yellow);

    const Chunk& chunk = world.GetChunk(1, 1, 1);

    CHECK(chunk.GetBlock(3, 5, 7) == BlockType::Yellow);
}

TEST_CASE("Chunk origins map back to world coordinates")
{
    CHECK(World::GetChunkOrigin(0, 0, 0) == glm::ivec3(0, 0, 0));
    CHECK(World::GetChunkOrigin(1, 0, 0) == glm::ivec3(Chunk::Width, 0, 0));
    CHECK(World::GetChunkOrigin(0, 2, 3) ==
        glm::ivec3(0, 2 * Chunk::Height, 3 * Chunk::Depth));
}

TEST_CASE("Chunk bounds cover exactly the grid")
{
    const World world(2, 3, 4);

    CHECK(world.IsChunkInBounds(0, 0, 0));
    CHECK(world.IsChunkInBounds(1, 2, 3));
    CHECK_FALSE(world.IsChunkInBounds(2, 0, 0));
    CHECK_FALSE(world.IsChunkInBounds(0, 3, 0));
    CHECK_FALSE(world.IsChunkInBounds(0, 0, 4));
    CHECK_FALSE(world.IsChunkInBounds(-1, 0, 0));

    CHECK_THROWS_AS(world.GetChunk(2, 0, 0), std::out_of_range);
}
