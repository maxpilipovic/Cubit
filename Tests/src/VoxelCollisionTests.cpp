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
                world.SetBlock(x, 0, z, BlockId{1});
    }

    //Fills one full column layer at the given x, forming a wall.
    void AddWallAtX(World& world, int wallX)
    {
        for (int z = 0; z < Chunk::Depth; ++z)
            for (int y = 0; y < Chunk::Height; ++y)
                world.SetBlock(wallX, y, z, BlockId{1});
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
            world.SetBlock(x, 10, z, BlockId{1});

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
    world.SetBlock(5, 5, 5, BlockId{1});

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
                world.SetBlock(x, y, z, BlockId{1});

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
            world.SetBlock(x, 0, z, BlockId{1});

    const VoxelMoveResult result = VoxelCollision::MoveBox(
        world,
        glm::vec3(15.9f, 6.0f, 8.0f),
        PlayerHalfExtents,
        glm::vec3(0.0f, -10.0f, 0.0f));

    CHECK(result.Position.y == doctest::Approx(1.9f).epsilon(0.01));
    CHECK(result.BlockedY);
    CHECK(result.Grounded);
}

TEST_CASE("A falling box passes through water and lands on the bed")
{
    // The riverbed is what holds the player up, not the surface above it. Two
    // layers of water over a floor mirrors the shipped map's river depth.
    World world(1, 1, 1);
    Palette palette = DefaultPalette();
    palette[7] = glm::vec4(0.2f, 0.4f, 0.8f, 0.55f); // water
    world.SetPalette(palette);

    AddFloor(world);
    for (int z = 0; z < Chunk::Depth; ++z)
        for (int x = 0; x < Chunk::Width; ++x)
        {
            world.SetBlock(x, 1, z, BlockId{7});
            world.SetBlock(x, 2, z, BlockId{7});
        }

    const VoxelMoveResult result = VoxelCollision::MoveBox(
        world,
        glm::vec3(8.0f, 6.0f, 8.0f),
        PlayerHalfExtents,
        glm::vec3(0.0f, -10.0f, 0.0f));

    // The floor block spans y 0 to 1, so a box resting on it sits one half
    // extent above y=1 — the same result as landing on bare floor.
    CHECK(result.Position.y == doctest::Approx(1.0f + PlayerHalfExtents.y).epsilon(0.01));
    CHECK(result.Grounded);
}

TEST_CASE("A box does not overlap water it is standing in")
{
    // LiftPlayerClearOfTerrain steps the player up while Overlaps is true, so a
    // box submerged in water must read as clear or a reload would launch the
    // player out of the river.
    World world(1, 1, 1);
    Palette palette = DefaultPalette();
    palette[7] = glm::vec4(0.2f, 0.4f, 0.8f, 0.55f);
    world.SetPalette(palette);

    world.SetBlock(8, 8, 8, BlockId{7});

    CHECK_FALSE(VoxelCollision::Overlaps(
        world, glm::vec3(8.5f, 8.5f, 8.5f), PlayerHalfExtents));
}

TEST_CASE("An opaque block still stops a box after the solidity change")
{
    // The companion case: switching the predicate must not make ordinary
    // terrain stop blocking movement.
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});

    CHECK(VoxelCollision::Overlaps(
        world, glm::vec3(8.5f, 8.5f, 8.5f), PlayerHalfExtents));
}

TEST_CASE("A box inside water overlaps fluid")
{
    World world(1, 1, 1);
    Palette palette = DefaultPalette();
    palette[7] = glm::vec4(0.2f, 0.4f, 0.8f, 0.55f); // water
    world.SetPalette(palette);

    world.SetBlock(8, 8, 8, BlockId{7});

    CHECK(VoxelCollision::OverlapsFluid(
        world, glm::vec3(8.5f, 8.5f, 8.5f), PlayerHalfExtents));
}

TEST_CASE("A box in open air overlaps no fluid")
{
    const World world(1, 1, 1);

    CHECK_FALSE(VoxelCollision::OverlapsFluid(
        world, glm::vec3(8.5f, 8.5f, 8.5f), PlayerHalfExtents));
}

TEST_CASE("A box inside stone overlaps no fluid")
{
    // Solid is not fluid. Without this the predicate could be reading presence
    // and every test above would still pass.
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});

    CHECK_FALSE(VoxelCollision::OverlapsFluid(
        world, glm::vec3(8.5f, 8.5f, 8.5f), PlayerHalfExtents));
}

TEST_CASE("A box straddling water and air overlaps fluid")
{
    // Any overlap counts, matching Overlaps. A player with only their feet in
    // the river is swimming.
    World world(1, 1, 1);
    Palette palette = DefaultPalette();
    palette[7] = glm::vec4(0.2f, 0.4f, 0.8f, 0.55f);
    world.SetPalette(palette);

    world.SetBlock(8, 7, 8, BlockId{7});

    // Box centre 8.5 with half-height 0.9 spans y 7.6 to 9.4, so its lower part
    // is in the water cell at y=7 and its upper part is in air.
    CHECK(VoxelCollision::OverlapsFluid(
        world, glm::vec3(8.5f, 8.5f, 8.5f), PlayerHalfExtents));
}

TEST_CASE("Water does not make a box overlap solid")
{
    // The companion to the cases above: the two queries must stay independent.
    World world(1, 1, 1);
    Palette palette = DefaultPalette();
    palette[7] = glm::vec4(0.2f, 0.4f, 0.8f, 0.55f);
    world.SetPalette(palette);

    world.SetBlock(8, 8, 8, BlockId{7});

    CHECK_FALSE(VoxelCollision::Overlaps(
        world, glm::vec3(8.5f, 8.5f, 8.5f), PlayerHalfExtents));
}
