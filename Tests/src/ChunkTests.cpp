#include <doctest.h>

#include "Cubit/Voxel/Chunk.h"

#include <stdexcept>

TEST_CASE("A new chunk contains only air")
{
    const Chunk chunk;

    for (int z = 0; z < Chunk::Depth; ++z)
        for (int y = 0; y < Chunk::Height; ++y)
            for (int x = 0; x < Chunk::Width; ++x)
                REQUIRE(chunk.GetBlock(x, y, z) == BlockType::Air);
}

TEST_CASE("Every position stores and returns its own block")
{
    // Writes a distinct pattern so a swapped axis in the index math cannot pass.
    Chunk chunk;

    for (int z = 0; z < Chunk::Depth; ++z)
        for (int y = 0; y < Chunk::Height; ++y)
            for (int x = 0; x < Chunk::Width; ++x)
                if ((x + 2 * y + 3 * z) % 2 == 0)
                    chunk.SetBlock(x, y, z, BlockType::Solid);

    for (int z = 0; z < Chunk::Depth; ++z)
    {
        for (int y = 0; y < Chunk::Height; ++y)
        {
            for (int x = 0; x < Chunk::Width; ++x)
            {
                const bool expected = (x + 2 * y + 3 * z) % 2 == 0;
                REQUIRE(chunk.IsBlockSolid(x, y, z) == expected);
            }
        }
    }
}

TEST_CASE("Writing one block leaves its neighbours untouched")
{
    Chunk chunk;
    chunk.SetBlock(4, 5, 6, BlockType::Solid);

    CHECK(chunk.IsBlockSolid(4, 5, 6));
    CHECK_FALSE(chunk.IsBlockSolid(5, 5, 6));
    CHECK_FALSE(chunk.IsBlockSolid(4, 6, 6));
    CHECK_FALSE(chunk.IsBlockSolid(4, 5, 7));
}

TEST_CASE("Positions outside the chunk read as air")
{
    Chunk chunk;
    chunk.SetBlock(0, 0, 0, BlockType::Solid);

    CHECK(chunk.GetBlock(-1, 0, 0) == BlockType::Air);
    CHECK(chunk.GetBlock(0, -1, 0) == BlockType::Air);
    CHECK(chunk.GetBlock(0, 0, -1) == BlockType::Air);
    CHECK(chunk.GetBlock(Chunk::Width, 0, 0) == BlockType::Air);
    CHECK(chunk.GetBlock(0, Chunk::Height, 0) == BlockType::Air);
    CHECK(chunk.GetBlock(0, 0, Chunk::Depth) == BlockType::Air);
}

TEST_CASE("Writing outside the chunk is rejected")
{
    Chunk chunk;

    CHECK_THROWS_AS(chunk.SetBlock(-1, 0, 0, BlockType::Solid), std::out_of_range);
    CHECK_THROWS_AS(chunk.SetBlock(Chunk::Width, 0, 0, BlockType::Solid), std::out_of_range);
    CHECK_THROWS_AS(chunk.SetBlock(0, Chunk::Height, 0, BlockType::Solid), std::out_of_range);
    CHECK_THROWS_AS(chunk.SetBlock(0, 0, Chunk::Depth, BlockType::Solid), std::out_of_range);
}

TEST_CASE("Bounds checking accepts only interior positions")
{
    CHECK(Chunk::IsInBounds(0, 0, 0));
    CHECK(Chunk::IsInBounds(Chunk::Width - 1, Chunk::Height - 1, Chunk::Depth - 1));
    CHECK_FALSE(Chunk::IsInBounds(-1, 0, 0));
    CHECK_FALSE(Chunk::IsInBounds(0, 0, Chunk::Depth));
}
