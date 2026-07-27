#include <doctest.h>

#include "Cubit/Voxel/Chunk.h"

#include <stdexcept>

TEST_CASE("A new chunk contains only air")
{
    const Chunk chunk;

    for (int z = 0; z < Chunk::Depth; ++z)
        for (int y = 0; y < Chunk::Height; ++y)
            for (int x = 0; x < Chunk::Width; ++x)
                REQUIRE(chunk.GetBlock(x, y, z) == BlockId{0});
}

TEST_CASE("Every position stores and returns its own block")
{
    // Writes a distinct pattern so a swapped axis in the index math cannot pass.
    Chunk chunk;

    for (int z = 0; z < Chunk::Depth; ++z)
        for (int y = 0; y < Chunk::Height; ++y)
            for (int x = 0; x < Chunk::Width; ++x)
                if ((x + 2 * y + 3 * z) % 2 == 0)
                    chunk.SetBlock(x, y, z, BlockId{1});

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
    chunk.SetBlock(4, 5, 6, BlockId{1});

    CHECK(chunk.IsBlockSolid(4, 5, 6));
    CHECK_FALSE(chunk.IsBlockSolid(5, 5, 6));
    CHECK_FALSE(chunk.IsBlockSolid(4, 6, 6));
    CHECK_FALSE(chunk.IsBlockSolid(4, 5, 7));
}

TEST_CASE("Positions outside the chunk read as air")
{
    Chunk chunk;
    chunk.SetBlock(0, 0, 0, BlockId{1});

    CHECK(chunk.GetBlock(-1, 0, 0) == BlockId{0});
    CHECK(chunk.GetBlock(0, -1, 0) == BlockId{0});
    CHECK(chunk.GetBlock(0, 0, -1) == BlockId{0});
    CHECK(chunk.GetBlock(Chunk::Width, 0, 0) == BlockId{0});
    CHECK(chunk.GetBlock(0, Chunk::Height, 0) == BlockId{0});
    CHECK(chunk.GetBlock(0, 0, Chunk::Depth) == BlockId{0});
}

TEST_CASE("Writing outside the chunk is rejected")
{
    Chunk chunk;

    CHECK_THROWS_AS(chunk.SetBlock(-1, 0, 0, BlockId{1}), std::out_of_range);
    CHECK_THROWS_AS(chunk.SetBlock(Chunk::Width, 0, 0, BlockId{1}), std::out_of_range);
    CHECK_THROWS_AS(chunk.SetBlock(0, Chunk::Height, 0, BlockId{1}), std::out_of_range);
    CHECK_THROWS_AS(chunk.SetBlock(0, 0, Chunk::Depth, BlockId{1}), std::out_of_range);
}

TEST_CASE("Bounds checking accepts only interior positions")
{
    CHECK(Chunk::IsInBounds(0, 0, 0));
    CHECK(Chunk::IsInBounds(Chunk::Width - 1, Chunk::Height - 1, Chunk::Depth - 1));
    CHECK_FALSE(Chunk::IsInBounds(-1, 0, 0));
    CHECK_FALSE(Chunk::IsInBounds(0, 0, Chunk::Depth));
}

TEST_CASE("A new chunk starts with no sky light")
{
    const Chunk chunk;

    CHECK(chunk.GetSkyLight(0, 0, 0) == 0);
    CHECK(chunk.GetSkyLight(8, 8, 8) == 0);
}

TEST_CASE("Sky light can be set and read back")
{
    Chunk chunk;
    chunk.SetSkyLight(3, 4, 5, 12);

    CHECK(chunk.GetSkyLight(3, 4, 5) == 12);

    // Setting one cell must not disturb its neighbours.
    CHECK(chunk.GetSkyLight(4, 4, 5) == 0);
    CHECK(chunk.GetSkyLight(3, 5, 5) == 0);
}

TEST_CASE("Sky light outside a chunk reads as open sky")
{
    const Chunk chunk;

    CHECK(chunk.GetSkyLight(-1, 0, 0) == 15);
    CHECK(chunk.GetSkyLight(0, Chunk::Height, 0) == 15);
    CHECK(chunk.GetSkyLight(0, 0, Chunk::Depth) == 15);
}
