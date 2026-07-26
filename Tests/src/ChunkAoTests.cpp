#include <doctest.h>

#include "Cubit/Voxel/Chunk.h"
#include "Cubit/Voxel/ChunkMesher.h"
#include "Cubit/Voxel/World.h"

namespace
{
    //The cell just above a block at (8, 8, 8): the air side of its top face.
    constexpr glm::ivec3 AirAboveBlock{ 8, 9, 8 };

    //The two tangent offsets picking out one corner of that top face, pointing
    //toward +X and +Z.
    constexpr glm::ivec3 SideX{ 1, 0, 0 };
    constexpr glm::ivec3 SideZ{ 0, 0, 1 };
}

TEST_CASE("An unobstructed corner is fully open")
{
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});

    CHECK(ChunkMesher::CornerAoLevel(world, AirAboveBlock, SideX, SideZ) == 3);
}

TEST_CASE("A block diagonally across a corner occludes it by one")
{
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});

    // Diagonally opposite the corner: touches it, but neither side.
    world.SetBlock(9, 9, 9, BlockId{1});

    CHECK(ChunkMesher::CornerAoLevel(world, AirAboveBlock, SideX, SideZ) == 2);
}

TEST_CASE("A block on one side of a corner occludes it by one")
{
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});
    world.SetBlock(9, 9, 8, BlockId{1});

    CHECK(ChunkMesher::CornerAoLevel(world, AirAboveBlock, SideX, SideZ) == 2);
}

TEST_CASE("A corner with one side and the diagonal filled is occluded by two")
{
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});
    world.SetBlock(9, 9, 8, BlockId{1});
    world.SetBlock(9, 9, 9, BlockId{1});

    CHECK(ChunkMesher::CornerAoLevel(world, AirAboveBlock, SideX, SideZ) == 1);
}

TEST_CASE("Both sides filled pins a corner fully dark regardless of the diagonal")
{
    // Two walls meeting at a right angle. The diagonal behind them cannot make
    // the corner any lighter, and must not be allowed to.
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});
    world.SetBlock(9, 9, 8, BlockId{1});
    world.SetBlock(8, 9, 9, BlockId{1});

    CHECK(ChunkMesher::CornerAoLevel(world, AirAboveBlock, SideX, SideZ) == 0);

    world.SetBlock(9, 9, 9, BlockId{1});
    CHECK(ChunkMesher::CornerAoLevel(world, AirAboveBlock, SideX, SideZ) == 0);
}

TEST_CASE("Occlusion outside the world reads as open sky")
{
    // A block on the world's top face has nothing above or beside it, so its
    // top corners must be fully open rather than dark.
    World world(1, 1, 1);
    const int top = world.GetHeight() - 1;
    world.SetBlock(8, top, 8, BlockId{1});

    const glm::ivec3 air{ 8, top + 1, 8 };
    CHECK(ChunkMesher::CornerAoLevel(world, air, SideX, SideZ) == 3);
}

TEST_CASE("The occlusion shade table darkens monotonically")
{
    CHECK(ChunkMesher::AoShade[3] == doctest::Approx(1.00f));
    CHECK(ChunkMesher::AoShade[2] < ChunkMesher::AoShade[3]);
    CHECK(ChunkMesher::AoShade[1] < ChunkMesher::AoShade[2]);
    CHECK(ChunkMesher::AoShade[0] < ChunkMesher::AoShade[1]);
    CHECK(ChunkMesher::AoShade[0] > 0.0f);
}
