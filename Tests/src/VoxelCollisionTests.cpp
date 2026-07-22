#include <doctest.h>

#include "Cubit/Voxel/Chunk.h"
#include "Cubit/Voxel/World.h"
#include "Cubit/Voxel/VoxelCollision.h"

namespace
{
    //Half extents of a 0.6 x 1.8 x 0.6 character box.
    const glm::vec3 PlayerHalfExtents{ 0.3f, 0.9f, 0.3f };

    //Fills the whole y=0 layer of a single-chunk world, giving the box something
    //to stand on.
    void AddFloor(World& world)
    {
        for (int z = 0; z < Chunk::Depth; ++z)
            for (int x = 0; x < Chunk::Width; ++x)
                world.SetBlock(x, 0, z, BlockType::Solid);
    }

    //Fills one full column layer at the given x, forming a wall.
    void AddWallAtX(World& world, int wallX)
    {
        for (int z = 0; z < Chunk::Depth; ++z)
            for (int y = 0; y < Chunk::Height; ++y)
                world.SetBlock(wallX, y, z, BlockType::Solid);
    }
}

TEST_CASE("A box moves freely through empty space")
{
    const World world(1, 1, 1);

    const VoxelMoveResult result = VoxelCollision::MoveBox(
        world,
        glm::vec3(8.0f, 8.0f, 8.0f),
        PlayerHalfExtents,
        glm::vec3(1.0f, 2.0f, -1.5f));

    CHECK(result.Position.x == doctest::Approx(9.0f));
    CHECK(result.Position.y == doctest::Approx(10.0f));
    CHECK(result.Position.z == doctest::Approx(6.5f));
    CHECK_FALSE(result.BlockedX);
    CHECK_FALSE(result.BlockedY);
    CHECK_FALSE(result.BlockedZ);
    CHECK_FALSE(result.Grounded);
}

TEST_CASE("A falling box lands on top of the floor")
{
    World world(1, 1, 1);
    AddFloor(world);

    const VoxelMoveResult result = VoxelCollision::MoveBox(
        world,
        glm::vec3(8.0f, 6.0f, 8.0f),
        PlayerHalfExtents,
        glm::vec3(0.0f, -10.0f, 0.0f));

    // The floor block spans y 0 to 1, so the box centre rests one half extent
    // above the top face.
    CHECK(result.Position.y == doctest::Approx(1.9f).epsilon(0.01));
    CHECK(result.BlockedY);
    CHECK(result.Grounded);
}

TEST_CASE("A box stops against a wall it walks into")
{
    World world(1, 1, 1);
    AddWallAtX(world, 5);

    const VoxelMoveResult result = VoxelCollision::MoveBox(
        world,
        glm::vec3(2.0f, 5.0f, 8.0f),
        PlayerHalfExtents,
        glm::vec3(10.0f, 0.0f, 0.0f));

    // The wall's near face is at x=5, so the box centre stops a half extent back.
    CHECK(result.Position.x == doctest::Approx(4.7f).epsilon(0.01));
    CHECK(result.BlockedX);
}

TEST_CASE("A box stops against a wall approached from the other side")
{
    World world(1, 1, 1);
    AddWallAtX(world, 2);

    const VoxelMoveResult result = VoxelCollision::MoveBox(
        world,
        glm::vec3(8.0f, 5.0f, 8.0f),
        PlayerHalfExtents,
        glm::vec3(-10.0f, 0.0f, 0.0f));

    // The wall block spans x 2 to 3, so the box centre stops just past x=3.
    CHECK(result.Position.x == doctest::Approx(3.3f).epsilon(0.01));
    CHECK(result.BlockedX);
}

TEST_CASE("A rising box stops below a ceiling")
{
    World world(1, 1, 1);
    for (int z = 0; z < Chunk::Depth; ++z)
        for (int x = 0; x < Chunk::Width; ++x)
            world.SetBlock(x, 10, z, BlockType::Solid);

    const VoxelMoveResult result = VoxelCollision::MoveBox(
        world,
        glm::vec3(8.0f, 5.0f, 8.0f),
        PlayerHalfExtents,
        glm::vec3(0.0f, 10.0f, 0.0f));

    CHECK(result.Position.y == doctest::Approx(9.1f).epsilon(0.01));
    CHECK(result.BlockedY);
    CHECK_FALSE(result.Grounded);
}

TEST_CASE("A box blocked on one axis still slides along the others")
{
    World world(1, 1, 1);
    AddWallAtX(world, 5);

    const VoxelMoveResult result = VoxelCollision::MoveBox(
        world,
        glm::vec3(2.0f, 5.0f, 8.0f),
        PlayerHalfExtents,
        glm::vec3(10.0f, 0.0f, 2.0f));

    CHECK(result.Position.x == doctest::Approx(4.7f).epsilon(0.01));
    CHECK(result.Position.z == doctest::Approx(10.0f));
    CHECK(result.BlockedX);
    CHECK_FALSE(result.BlockedZ);
}

TEST_CASE("A fast box cannot pass through a thin wall")
{
    // Without stepping the move, a single large jump would land past the wall
    // and report no collision at all.
    World world(1, 1, 1);
    AddWallAtX(world, 5);

    const VoxelMoveResult result = VoxelCollision::MoveBox(
        world,
        glm::vec3(1.0f, 5.0f, 8.0f),
        PlayerHalfExtents,
        glm::vec3(60.0f, 0.0f, 0.0f));

    CHECK(result.Position.x == doctest::Approx(4.7f).epsilon(0.01));
    CHECK(result.BlockedX);
}

TEST_CASE("A box fits through a gap exactly one block wide")
{
    World world(1, 1, 1);
    AddWallAtX(world, 4);
    AddWallAtX(world, 6);

    const VoxelMoveResult result = VoxelCollision::MoveBox(
        world,
        glm::vec3(5.5f, 5.0f, 2.0f),
        PlayerHalfExtents,
        glm::vec3(0.0f, 0.0f, 6.0f));

    CHECK(result.Position.z == doctest::Approx(8.0f));
    CHECK_FALSE(result.BlockedZ);
}

TEST_CASE("A resting box does not sink or drift")
{
    World world(1, 1, 1);
    AddFloor(world);

    VoxelMoveResult result;
    result.Position = glm::vec3(8.0f, 1.9f, 8.0f);

    // Apply many frames of gravity, as the character controller will.
    for (int frame = 0; frame < 240; ++frame)
    {
        result = VoxelCollision::MoveBox(
            world,
            result.Position,
            PlayerHalfExtents,
            glm::vec3(0.0f, -0.16f, 0.0f));
    }

    CHECK(result.Position.y == doctest::Approx(1.9f).epsilon(0.01));
    CHECK(result.Grounded);
}

TEST_CASE("Zero motion leaves the box where it was")
{
    World world(1, 1, 1);
    AddFloor(world);

    const glm::vec3 start(8.0f, 1.9f, 8.0f);
    const VoxelMoveResult result =
        VoxelCollision::MoveBox(world, start, PlayerHalfExtents, glm::vec3(0.0f));

    CHECK(result.Position.x == doctest::Approx(start.x));
    CHECK(result.Position.y == doctest::Approx(start.y));
    CHECK(result.Position.z == doctest::Approx(start.z));
}

TEST_CASE("Overlap reports whether a box intersects solid blocks")
{
    World world(1, 1, 1);
    world.SetBlock(5, 5, 5, BlockType::Solid);

    // Centred inside the block.
    CHECK(VoxelCollision::Overlaps(
        world, glm::vec3(5.5f, 5.5f, 5.5f), glm::vec3(0.3f)));

    // Well clear of it.
    CHECK_FALSE(VoxelCollision::Overlaps(
        world, glm::vec3(8.5f, 8.5f, 8.5f), glm::vec3(0.3f)));

    // Resting exactly on the block's top face counts as clear, not overlapping.
    CHECK_FALSE(VoxelCollision::Overlaps(
        world, glm::vec3(5.5f, 6.9f, 5.5f), PlayerHalfExtents));
}

TEST_CASE("A box never ends a move inside a solid block")
{
    // Sweeps motions in many directions against terrain and checks the result
    // is always a legal position.
    World world(1, 1, 1);
    for (int z = 0; z < Chunk::Depth; ++z)
        for (int x = 0; x < Chunk::Width; ++x)
            for (int y = 0; y < 5; ++y)
                world.SetBlock(x, y, z, BlockType::Solid);

    for (int i = 0; i < 48; ++i)
    {
        const float angle = static_cast<float>(i) * 0.13f;
        const glm::vec3 motion(
            std::cos(angle) * 4.0f,
            -3.0f,
            std::sin(angle) * 4.0f);

        const VoxelMoveResult result = VoxelCollision::MoveBox(
            world,
            glm::vec3(8.0f, 7.0f, 8.0f),
            PlayerHalfExtents,
            motion);

        REQUIRE_FALSE(
            VoxelCollision::Overlaps(world, result.Position, PlayerHalfExtents));
    }
}

TEST_CASE("A box lands on a floor block in the neighbouring chunk")
{
    // The box's centre sits in chunk 0 but its width spills across the seam at
    // x=16 into chunk 1, where the only floor is. A single chunk could not see
    // that floor, so the box would fall through; the world sees it and the box
    // lands. This is the case that makes collision need world coordinates.
    World world(2, 1, 1);
    for (int z = 0; z < Chunk::Depth; ++z)
        for (int x = Chunk::Width; x < 2 * Chunk::Width; ++x)
            world.SetBlock(x, 0, z, BlockType::Solid);

    const VoxelMoveResult result = VoxelCollision::MoveBox(
        world,
        glm::vec3(15.9f, 6.0f, 8.0f),
        PlayerHalfExtents,
        glm::vec3(0.0f, -10.0f, 0.0f));

    CHECK(result.Position.y == doctest::Approx(1.9f).epsilon(0.01));
    CHECK(result.BlockedY);
    CHECK(result.Grounded);
}
