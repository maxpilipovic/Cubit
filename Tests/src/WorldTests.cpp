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
                REQUIRE(world.GetBlock(x, y, z) == BlockId{0});
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
                    world.SetBlock(x, y, z, BlockId{2});

    for (int z = 0; z < world.GetDepth(); ++z)
        for (int y = 0; y < world.GetHeight(); ++y)
            for (int x = 0; x < world.GetWidth(); ++x)
                REQUIRE(world.IsBlockPresent(x, y, z) == expected(x, y, z));
}

TEST_CASE("Writing a block does not disturb the neighbouring chunk")
{
    World world(2, 1, 1);

    // Either side of the boundary between chunk 0 and chunk 1.
    world.SetBlock(Chunk::Width - 1, 4, 4, BlockId{5});

    CHECK(world.GetBlock(Chunk::Width - 1, 4, 4) == BlockId{5});
    CHECK(world.GetBlock(Chunk::Width, 4, 4) == BlockId{0});

    world.SetBlock(Chunk::Width, 4, 4, BlockId{6});

    CHECK(world.GetBlock(Chunk::Width - 1, 4, 4) == BlockId{5});
    CHECK(world.GetBlock(Chunk::Width, 4, 4) == BlockId{6});
}

TEST_CASE("Positions outside the world read as air")
{
    World world(1, 1, 1);
    world.SetBlock(0, 0, 0, BlockId{1});

    CHECK(world.GetBlock(-1, 0, 0) == BlockId{0});
    CHECK(world.GetBlock(0, -1, 0) == BlockId{0});
    CHECK(world.GetBlock(0, 0, -1) == BlockId{0});
    CHECK(world.GetBlock(world.GetWidth(), 0, 0) == BlockId{0});
    CHECK(world.GetBlock(0, world.GetHeight(), 0) == BlockId{0});
    CHECK(world.GetBlock(0, 0, world.GetDepth()) == BlockId{0});
}

TEST_CASE("Writing outside the world is rejected")
{
    World world(2, 2, 2);

    CHECK_THROWS_AS(world.SetBlock(-1, 0, 0, BlockId{1}), std::out_of_range);
    CHECK_THROWS_AS(
        world.SetBlock(world.GetWidth(), 0, 0, BlockId{1}), std::out_of_range);
    CHECK_THROWS_AS(
        world.SetBlock(0, world.GetHeight(), 0, BlockId{1}), std::out_of_range);
    CHECK_THROWS_AS(
        world.SetBlock(0, 0, world.GetDepth(), BlockId{1}), std::out_of_range);
}

TEST_CASE("A block written through the world is visible in its chunk")
{
    // Confirms the world and the chunk agree on where a block lives, which is
    // what meshing depends on.
    World world(2, 2, 2);

    const int worldX = Chunk::Width + 3;
    const int worldY = Chunk::Height + 5;
    const int worldZ = Chunk::Depth + 7;
    world.SetBlock(worldX, worldY, worldZ, BlockId{4});

    const Chunk& chunk = world.GetChunk(1, 1, 1);

    CHECK(chunk.GetBlock(3, 5, 7) == BlockId{4});
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

TEST_CASE("A new world colours blocks with the default palette")
{
    const World world(1, 1, 1);

    CHECK(world.GetBlockColor(BlockId{5}) == glm::vec4(0.35f, 0.75f, 0.35f, 1.0f));
    CHECK(world.GetBlockColor(BlockId{2}) == glm::vec4(0.85f, 0.25f, 0.25f, 1.0f));
}

TEST_CASE("Setting a palette changes reported block colours")
{
    World world(1, 1, 1);

    Palette palette{};
    palette[3] = glm::vec4(0.1f, 0.2f, 0.3f, 1.0f);
    world.SetPalette(palette);

    CHECK(world.GetBlockColor(BlockId{3}) == glm::vec4(0.1f, 0.2f, 0.3f, 1.0f));
}

TEST_CASE("World sky light round-trips through the chunk grid")
{
    World world(2, 2, 2);
    world.SetSkyLight(20, 3, 5, 9);

    CHECK(world.GetSkyLight(20, 3, 5) == 9);
    CHECK(world.GetSkyLight(21, 3, 5) == 0);
}

TEST_CASE("Sky light outside the world reads as open sky")
{
    const World world(1, 1, 1);

    CHECK(world.GetSkyLight(-1, 0, 0) == 15);
    CHECK(world.GetSkyLight(0, world.GetHeight(), 0) == 15);
    CHECK(world.GetSkyLight(0, 0, world.GetDepth()) == 15);
}

TEST_CASE("Sky light crosses a chunk boundary independently")
{
    // The last column of chunk 0 and the first of chunk 1 are neighbours in
    // world space but live in different chunks, so this catches an accessor
    // that forgets to convert to chunk-local coordinates.
    World world(2, 1, 1);
    world.SetSkyLight(Chunk::Width - 1, 0, 0, 7);
    world.SetSkyLight(Chunk::Width, 0, 0, 4);

    CHECK(world.GetSkyLight(Chunk::Width - 1, 0, 0) == 7);
    CHECK(world.GetSkyLight(Chunk::Width, 0, 0) == 4);
}

TEST_CASE("Marking a position dirty also marks the chunk across a shared face")
{
    World world(2, 1, 1);
    world.ClearDirty();

    world.MarkChunkDirtyAt(Chunk::Width - 1, 0, 0);

    CHECK(world.DirtyChunks().count(glm::ivec3(0, 0, 0)) == 1);
    CHECK(world.DirtyChunks().count(glm::ivec3(1, 0, 0)) == 1);
}

TEST_CASE("A block is opaque when its palette alpha is full")
{
    World world(1, 1, 1);
    Palette palette = DefaultPalette();
    palette[4] = glm::vec4(0.2f, 0.4f, 0.6f, 1.0f);
    palette[5] = glm::vec4(0.2f, 0.4f, 0.6f, 0.5f);
    world.SetPalette(palette);

    CHECK(world.IsIdOpaque(BlockId{4}));
    CHECK_FALSE(world.IsIdOpaque(BlockId{5}));
}

TEST_CASE("Air is never opaque")
{
    const World world(1, 1, 1);

    CHECK_FALSE(world.IsIdOpaque(BlockId{0}));
}

TEST_CASE("Block opacity follows the block at a position")
{
    World world(1, 1, 1);
    Palette palette = DefaultPalette();
    palette[5] = glm::vec4(0.2f, 0.4f, 0.6f, 0.5f);
    world.SetPalette(palette);

    world.SetBlock(8, 8, 8, BlockId{1});
    world.SetBlock(9, 8, 8, BlockId{5});

    CHECK(world.IsBlockOpaque(8, 8, 8));
    CHECK_FALSE(world.IsBlockOpaque(9, 8, 8));
    CHECK_FALSE(world.IsBlockOpaque(0, 0, 0)); // air
}

TEST_CASE("Positions outside the world are not opaque")
{
    // Matching GetBlock returning air and GetSkyLight returning open sky: the
    // space around a map must not read as a wall.
    const World world(1, 1, 1);

    CHECK_FALSE(world.IsBlockOpaque(-1, 0, 0));
    CHECK_FALSE(world.IsBlockOpaque(0, world.GetHeight(), 0));
}

TEST_CASE("Replacing the palette updates opacity")
{
    // The opacity table is derived, so it must not go stale when a map installs
    // its own palette.
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{4});
    REQUIRE(world.IsBlockOpaque(8, 8, 8));

    Palette palette = DefaultPalette();
    palette[4] = glm::vec4(0.2f, 0.4f, 0.6f, 0.25f);
    world.SetPalette(palette);

    CHECK_FALSE(world.IsBlockOpaque(8, 8, 8));
}

TEST_CASE("A block is solid when its palette alpha is full")
{
    // Solidity is derived from alpha exactly as opacity is, so a transparent
    // block is something you can walk through as well as see through.
    World world(1, 1, 1);
    Palette palette = DefaultPalette();
    palette[4] = glm::vec4(0.2f, 0.4f, 0.6f, 1.0f);
    palette[5] = glm::vec4(0.2f, 0.4f, 0.6f, 0.5f);
    world.SetPalette(palette);

    CHECK(world.IsIdSolid(BlockId{4}));
    CHECK_FALSE(world.IsIdSolid(BlockId{5}));
}

TEST_CASE("Air is never solid")
{
    const World world(1, 1, 1);

    CHECK_FALSE(world.IsIdSolid(BlockId{0}));
}

TEST_CASE("A transparent block is present but not solid")
{
    // The whole point of the phase: presence and solidity are different
    // questions, and water is the block that answers them differently.
    World world(1, 1, 1);
    Palette palette = DefaultPalette();
    palette[5] = glm::vec4(0.2f, 0.4f, 0.6f, 0.5f);
    world.SetPalette(palette);

    world.SetBlock(8, 8, 8, BlockId{5});

    CHECK(world.IsBlockPresent(8, 8, 8));
    CHECK_FALSE(world.IsBlockSolid(8, 8, 8));
}

TEST_CASE("Positions outside the world are not solid")
{
    // Matching GetBlock returning air: the space around a map must not read as
    // a wall the player can stand on.
    const World world(1, 1, 1);

    CHECK_FALSE(world.IsBlockSolid(-1, 0, 0));
    CHECK_FALSE(world.IsBlockSolid(0, world.GetHeight(), 0));
}

TEST_CASE("Replacing the palette updates solidity")
{
    // The table is derived, so it must not go stale when a map installs its own
    // palette — a world loaded from a map has to know its water is walkable.
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{4});
    REQUIRE(world.IsBlockSolid(8, 8, 8));

    Palette palette = DefaultPalette();
    palette[4] = glm::vec4(0.2f, 0.4f, 0.6f, 0.25f);
    world.SetPalette(palette);

    CHECK_FALSE(world.IsBlockSolid(8, 8, 8));
}
