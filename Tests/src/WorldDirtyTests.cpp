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
    world.SetBlock(24, 24, 24, BlockId{1});

    CHECK(world.DirtyChunks().size() == 1);
    CHECK(world.DirtyChunks().count(glm::ivec3(1, 1, 1)) == 1);
}

TEST_CASE("A face edit marks the chunk and one neighbour")
{
    World world = MakeWorld();
    world.ClearDirty();

    // Local (0,8,8) inside chunk (1,1,1): touches the -X boundary.
    world.SetBlock(16, 24, 24, BlockId{1});

    CHECK(world.DirtyChunks().size() == 2);
    CHECK(world.DirtyChunks().count(glm::ivec3(1, 1, 1)) == 1);
    CHECK(world.DirtyChunks().count(glm::ivec3(0, 1, 1)) == 1);
}

TEST_CASE("An edge edit marks the chunk and three neighbours, including the diagonal")
{
    World world = MakeWorld();
    world.ClearDirty();

    // Local (0,0,8) inside chunk (1,1,1): touches -X and -Y boundaries, so all
    // four combinations of stepping -1/0 on X and Y are marked (the chunk
    // itself, the two face neighbours, and the edge-diagonal neighbour), with
    // Z fixed since the position isn't on a Z boundary.
    world.SetBlock(16, 16, 24, BlockId{1});

    CHECK(world.DirtyChunks().size() == 4);
    CHECK(world.DirtyChunks().count(glm::ivec3(1, 1, 1)) == 1);
    CHECK(world.DirtyChunks().count(glm::ivec3(0, 1, 1)) == 1);
    CHECK(world.DirtyChunks().count(glm::ivec3(1, 0, 1)) == 1);
    CHECK(world.DirtyChunks().count(glm::ivec3(0, 0, 1)) == 1);
}

TEST_CASE("A corner edit marks the chunk and all seven diagonal neighbours")
{
    World world = MakeWorld();
    world.ClearDirty();

    // Local (0,0,0) inside chunk (1,1,1): touches -X, -Y and -Z boundaries, so
    // every combination of stepping -1/0 on all three axes is marked — the
    // chunk itself plus all seven neighbours sharing a face, edge, or corner
    // with it.
    world.SetBlock(16, 16, 16, BlockId{1});

    CHECK(world.DirtyChunks().size() == 8);
    for (int dz = 0; dz <= 1; ++dz)
        for (int dy = 0; dy <= 1; ++dy)
            for (int dx = 0; dx <= 1; ++dx)
                CHECK(world.DirtyChunks().count(glm::ivec3(dx, dy, dz)) == 1);
}

TEST_CASE("A boundary edit at the world edge marks no out-of-bounds neighbour")
{
    World world = MakeWorld();
    world.ClearDirty();

    // Local (0,8,8) inside chunk (0,1,1): the -X neighbour would be (-1,1,1),
    // which is outside the world and must not be marked.
    world.SetBlock(0, 24, 24, BlockId{1});

    CHECK(world.DirtyChunks().size() == 1);
    CHECK(world.DirtyChunks().count(glm::ivec3(0, 1, 1)) == 1);
}

TEST_CASE("Editing the same chunk twice leaves one dirty entry")
{
    World world = MakeWorld();
    world.ClearDirty();

    world.SetBlock(24, 24, 24, BlockId{1});
    world.SetBlock(25, 25, 25, BlockId{1});

    CHECK(world.DirtyChunks().size() == 1);
}

TEST_CASE("A chunk-corner edit marks the diagonal chunk, not just face neighbours")
{
    // The mesher samples the diagonal cell across a face corner, so a chunk's
    // mesh depends on its edge- and corner-diagonal neighbours too, not only
    // the ones it shares a face with. A block at a chunk's far corner must
    // mark the diagonal chunk on the other side of that corner.
    World world(2, 2, 2);
    world.ClearDirty();

    // World-space (15,15,15) is chunk (0,0,0)'s local (15,15,15): its extreme
    // corner, diagonally touching chunk (1,1,1).
    world.SetBlock(15, 15, 15, BlockId{1});

    CHECK(world.DirtyChunks().count(glm::ivec3(0, 0, 0)) == 1);
    CHECK(world.DirtyChunks().count(glm::ivec3(1, 1, 1)) == 1);
    CHECK(world.DirtyChunks().size() == 8);
}
