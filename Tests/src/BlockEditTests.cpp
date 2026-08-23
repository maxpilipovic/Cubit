#include <doctest.h>

#include "Cubit/Voxel/BlockEdit.h"
#include "Cubit/Voxel/SkyLight.h"
#include "Cubit/Voxel/World.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace
{
    //A 32x32x32 world whose lower ten layers are solid, with a sealed air
    //chamber buried inside it. The chamber is what makes the light round-trip
    //test meaningful: breaking its roof floods it, and undoing that must empty
    //it again.
    World MakeChamberWorld()
    {
        World world(2, 2, 2);

        for (int z = 0; z < world.GetDepth(); ++z)
            for (int x = 0; x < world.GetWidth(); ++x)
                for (int y = 0; y <= 9; ++y)
                    world.SetBlock(x, y, z, BlockId{1});

        // Hollow out y=5..8 in the middle, leaving y=9 as an intact roof.
        for (int z = 12; z <= 20; ++z)
            for (int x = 12; x <= 20; ++x)
                for (int y = 5; y <= 8; ++y)
                    world.SetBlock(x, y, z, BlockId{0});

        return world;
    }

    //Every sky-light value in the world, in z, y, x order.
    std::vector<std::uint8_t> SnapshotLight(const World& world)
    {
        std::vector<std::uint8_t> light;
        light.reserve(static_cast<std::size_t>(world.GetWidth()) *
            world.GetHeight() * world.GetDepth());

        for (int z = 0; z < world.GetDepth(); ++z)
            for (int y = 0; y < world.GetHeight(); ++y)
                for (int x = 0; x < world.GetWidth(); ++x)
                    light.push_back(world.GetSkyLight(x, y, z));

        return light;
    }

    //The first cell whose light differs from the snapshot, or nothing.
    //
    //A position rather than a bool: a lighting disagreement hides in one corner
    //out of tens of thousands, and "not equal" is useless for finding it.
    std::optional<glm::ivec3> FirstLightDifference(
        const World& world,
        const std::vector<std::uint8_t>& before)
    {
        std::size_t index = 0;
        for (int z = 0; z < world.GetDepth(); ++z)
            for (int y = 0; y < world.GetHeight(); ++y)
                for (int x = 0; x < world.GetWidth(); ++x)
                {
                    if (before[index++] != world.GetSkyLight(x, y, z))
                        return glm::ivec3(x, y, z);
                }

        return std::nullopt;
    }

    std::string Describe(const std::optional<glm::ivec3>& cell)
    {
        if (!cell)
            return "none";

        return std::to_string(cell->x) + "," +
            std::to_string(cell->y) + "," +
            std::to_string(cell->z);
    }
}

TEST_CASE("Applying an edit changes the block")
{
    World world(1, 1, 1);

    const std::optional<BlockEdit> inverse =
        ApplyBlockEdit(world, BlockEdit{ glm::ivec3(4, 5, 6), BlockId{7} });

    REQUIRE(inverse.has_value());
    CHECK(world.GetBlock(4, 5, 6) == BlockId{7});
}

TEST_CASE("The inverse carries the position and the previous block")
{
    World world(1, 1, 1);
    world.SetBlock(4, 5, 6, BlockId{3});

    const std::optional<BlockEdit> inverse =
        ApplyBlockEdit(world, BlockEdit{ glm::ivec3(4, 5, 6), BlockId{7} });

    REQUIRE(inverse.has_value());
    CHECK(inverse->Position == glm::ivec3(4, 5, 6));
    CHECK(inverse->Block == BlockId{3});
}

TEST_CASE("Applying the inverse restores the previous block")
{
    World world(1, 1, 1);
    world.SetBlock(4, 5, 6, BlockId{3});

    const std::optional<BlockEdit> inverse =
        ApplyBlockEdit(world, BlockEdit{ glm::ivec3(4, 5, 6), BlockId{7} });
    REQUIRE(inverse.has_value());

    ApplyBlockEdit(world, *inverse);

    CHECK(world.GetBlock(4, 5, 6) == BlockId{3});
}

TEST_CASE("An out-of-range position is rejected rather than thrown")
{
    // The divergence from World::SetBlock, which throws. An edit arriving from a
    // file or a socket is input, not a bug in the caller.
    World world(1, 1, 1);

    std::optional<BlockEdit> inverse;
    CHECK_NOTHROW(
        inverse = ApplyBlockEdit(world, BlockEdit{ glm::ivec3(-1, 0, 0), BlockId{7} }));
    CHECK_FALSE(inverse.has_value());

    CHECK_NOTHROW(
        inverse = ApplyBlockEdit(
            world,
            BlockEdit{ glm::ivec3(world.GetWidth(), 0, 0), BlockId{7} }));
    CHECK_FALSE(inverse.has_value());
}

TEST_CASE("Setting a block to what it already is changes nothing")
{
    // Rejecting no-ops is what keeps an undo stack meaningful: an inverse that
    // does nothing when popped makes the user press undo twice for one change.
    World world(1, 1, 1);
    world.SetBlock(4, 5, 6, BlockId{3});

    const std::optional<BlockEdit> inverse =
        ApplyBlockEdit(world, BlockEdit{ glm::ivec3(4, 5, 6), BlockId{3} });

    CHECK_FALSE(inverse.has_value());
    CHECK(world.GetBlock(4, 5, 6) == BlockId{3});
}

TEST_CASE("Applying marks the containing chunk dirty")
{
    World world(3, 3, 3);
    world.ClearDirty();

    ApplyBlockEdit(world, BlockEdit{ glm::ivec3(20, 20, 20), BlockId{1} });

    CHECK(world.DirtyChunks().count(glm::ivec3(1, 1, 1)) == 1);
}

TEST_CASE("An edit on a chunk face marks the neighbour too")
{
    World world(3, 3, 3);
    world.ClearDirty();

    // x=16 is the first block of chunk 1, so chunk 0 borders it.
    ApplyBlockEdit(world, BlockEdit{ glm::ivec3(16, 20, 20), BlockId{1} });

    CHECK(world.DirtyChunks().count(glm::ivec3(1, 1, 1)) == 1);
    CHECK(world.DirtyChunks().count(glm::ivec3(0, 1, 1)) == 1);
}

TEST_CASE("A sequence of edits undone in reverse restores every block")
{
    World world(1, 1, 1);

    std::vector<BlockEdit> undo;
    for (int i = 0; i < 8; ++i)
    {
        const std::optional<BlockEdit> inverse = ApplyBlockEdit(
            world,
            BlockEdit{ glm::ivec3(i, i + 1, i + 2), static_cast<BlockId>(i + 1) });
        REQUIRE(inverse.has_value());
        undo.push_back(*inverse);
    }

    while (!undo.empty())
    {
        ApplyBlockEdit(world, undo.back());
        undo.pop_back();
    }

    for (int i = 0; i < 8; ++i)
        CHECK(world.GetBlock(i, i + 1, i + 2) == BlockId{0});
}

TEST_CASE("The light comparison reports the first differing cell")
{
    // Guards the guard. The round-trip case below is only evidence if this
    // comparison can itself fail, and the obvious mutation of ApplyBlockEdit —
    // deleting the relight — trips an earlier assertion without ever reaching
    // it. So corrupt a known cell of the snapshot and require that exact
    // coordinate back.
    World world = MakeChamberWorld();
    SkyLight::PropagateAll(world);

    std::vector<std::uint8_t> before = SnapshotLight(world);

    // SnapshotLight walks z outermost, then y, then x.
    const glm::ivec3 cell(14, 7, 19);
    const std::size_t index =
        (static_cast<std::size_t>(cell.z) * world.GetHeight() + cell.y) *
            world.GetWidth() + cell.x;
    before[index] = static_cast<std::uint8_t>(before[index] + 1);

    const std::optional<glm::ivec3> difference = FirstLightDifference(world, before);

    REQUIRE(difference.has_value());
    CHECK(*difference == cell);
}

TEST_CASE("An edit and its inverse leave every sky-light value unchanged")
{
    // The property that carries the design. A Repropagate that floods the
    // chamber correctly but fails to un-flood it on the way back passes every
    // other case in this file.
    World world = MakeChamberWorld();
    SkyLight::PropagateAll(world);

    const std::vector<std::uint8_t> before = SnapshotLight(world);

    // The chamber is sealed, so it starts dark.
    REQUIRE(world.GetSkyLight(16, 8, 16) == 0);

    const std::optional<BlockEdit> inverse =
        ApplyBlockEdit(world, BlockEdit{ glm::ivec3(16, 9, 16), BlockId{0} });
    REQUIRE(inverse.has_value());

    // Breaking the roof must actually flood it, or the round trip below proves
    // nothing.
    REQUIRE(world.GetSkyLight(16, 8, 16) > 0);

    ApplyBlockEdit(world, *inverse);

    const std::optional<glm::ivec3> difference = FirstLightDifference(world, before);
    INFO("first differing sky-light cell: ", Describe(difference));
    CHECK_FALSE(difference.has_value());
}
