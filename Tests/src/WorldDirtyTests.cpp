#include <doctest.h>

#include "Cubit/Voxel/World.h"

namespace
{
    // A 3x3x3 chunk world (48 blocks per axis) so a centre chunk has a
    // neighbour on every side. Chunk (1,1,1) spans world blocks 16..31.
    World MakeWorld()
    {
        return World(3, 3, 3);
    }
}

TEST_CASE("A new world starts with every chunk dirty")
{
    const World world = MakeWorld();

    CHECK(world.DirtyChunks().size() == 27);
}

TEST_CASE("ClearDirty empties the dirty set")
{
    World world = MakeWorld();

    world.ClearDirty();

    CHECK(world.DirtyChunks().empty());
}

TEST_CASE("An interior edit marks exactly one chunk")
{
    World world = MakeWorld();
    world.ClearDirty();

    // Local (8,8,8) inside chunk (1,1,1): touches no boundary.
    world.SetBlock(24, 24, 24, BlockType::Solid);

    CHECK(world.DirtyChunks().size() == 1);
    CHECK(world.DirtyChunks().count(glm::ivec3(1, 1, 1)) == 1);
}

TEST_CASE("A face edit marks the chunk and one neighbour")
{
    World world = MakeWorld();
    world.ClearDirty();

    // Local (0,8,8) inside chunk (1,1,1): touches the -X boundary.
    world.SetBlock(16, 24, 24, BlockType::Solid);

    CHECK(world.DirtyChunks().size() == 2);
    CHECK(world.DirtyChunks().count(glm::ivec3(1, 1, 1)) == 1);
    CHECK(world.DirtyChunks().count(glm::ivec3(0, 1, 1)) == 1);
}

TEST_CASE("An edge edit marks the chunk and two neighbours")
{
    World world = MakeWorld();
    world.ClearDirty();

    // Local (0,0,8) inside chunk (1,1,1): touches -X and -Y boundaries.
    world.SetBlock(16, 16, 24, BlockType::Solid);

    CHECK(world.DirtyChunks().size() == 3);
}

TEST_CASE("A corner edit marks the chunk and three neighbours")
{
    World world = MakeWorld();
    world.ClearDirty();

    // Local (0,0,0) inside chunk (1,1,1): touches -X, -Y and -Z boundaries.
    world.SetBlock(16, 16, 16, BlockType::Solid);

    CHECK(world.DirtyChunks().size() == 4);
}

TEST_CASE("A boundary edit at the world edge marks no out-of-bounds neighbour")
{
    World world = MakeWorld();
    world.ClearDirty();

    // Local (0,8,8) inside chunk (0,1,1): the -X neighbour would be (-1,1,1),
    // which is outside the world and must not be marked.
    world.SetBlock(0, 24, 24, BlockType::Solid);

    CHECK(world.DirtyChunks().size() == 1);
    CHECK(world.DirtyChunks().count(glm::ivec3(0, 1, 1)) == 1);
}

TEST_CASE("Editing the same chunk twice leaves one dirty entry")
{
    World world = MakeWorld();
    world.ClearDirty();

    world.SetBlock(24, 24, 24, BlockType::Solid);
    world.SetBlock(25, 25, 25, BlockType::Solid);

    CHECK(world.DirtyChunks().size() == 1);
}
