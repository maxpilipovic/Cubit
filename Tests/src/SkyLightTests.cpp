#include <doctest.h>

#include "Cubit/Voxel/Chunk.h"
#include "Cubit/Voxel/SkyLight.h"
#include "Cubit/Voxel/World.h"

#include <set>
#include <vector>

TEST_CASE("An open column is lit from top to bottom")
{
    World world(1, 4, 1);
    SkyLight::PropagateAll(world);

    for (int y = 0; y < world.GetHeight(); ++y)
        CHECK(world.GetSkyLight(8, y, 8) == SkyLight::Max);
}

TEST_CASE("A solid block holds no light")
{
    World world(1, 4, 1);
    world.SetBlock(8, 20, 8, BlockId{1});

    SkyLight::PropagateAll(world);

    CHECK(world.GetSkyLight(8, 20, 8) == 0);
}

TEST_CASE("A roof darkens the column beneath it")
{
    // A solid ceiling spanning the whole world, so no light can creep in from
    // the sides. Everything below it must be pitch dark.
    World world(1, 4, 1);
    const int roof = 20;

    for (int z = 0; z < world.GetDepth(); ++z)
        for (int x = 0; x < world.GetWidth(); ++x)
            world.SetBlock(x, roof, z, BlockId{1});

    SkyLight::PropagateAll(world);

    CHECK(world.GetSkyLight(8, roof + 1, 8) == SkyLight::Max);
    CHECK(world.GetSkyLight(8, roof - 1, 8) == 0);
    CHECK(world.GetSkyLight(8, 0, 8) == 0);
}

TEST_CASE("Light spreads sideways under an overhang, losing one level a step")
{
    // A roof covering everything except the x == 0 column, which stays open to
    // the sky. Light falls down that column and creeps sideways underneath.
    World world(2, 4, 1);
    const int roof = 20;

    for (int z = 0; z < world.GetDepth(); ++z)
        for (int x = 1; x < world.GetWidth(); ++x)
            world.SetBlock(x, roof, z, BlockId{1});

    SkyLight::PropagateAll(world);

    const int under = roof - 1;
    REQUIRE(world.GetSkyLight(0, under, 8) == SkyLight::Max);

    CHECK(world.GetSkyLight(1, under, 8) == SkyLight::Max - 1);
    CHECK(world.GetSkyLight(2, under, 8) == SkyLight::Max - 2);
    CHECK(world.GetSkyLight(3, under, 8) == SkyLight::Max - 3);
}

TEST_CASE("Light dies out after fifteen sideways steps")
{
    World world(2, 4, 1);
    const int roof = 20;

    for (int z = 0; z < world.GetDepth(); ++z)
        for (int x = 1; x < world.GetWidth(); ++x)
            world.SetBlock(x, roof, z, BlockId{1});

    SkyLight::PropagateAll(world);

    const int under = roof - 1;
    CHECK(world.GetSkyLight(15, under, 8) == 0);
    CHECK(world.GetSkyLight(16, under, 8) == 0);
}

TEST_CASE("Light that has already dimmed does not fall for free")
{
    // Sky light falls without attenuation only at full strength. Once it has
    // spread sideways and dimmed, dropping down must cost a level like any
    // other step, or a deep cave stays almost fully lit.
    //
    // A solid world with an L carved into it: a shaft open to the sky, and a
    // pocket hanging off its foot. The pocket's only entrance is one sideways
    // step out of the shaft, so everything below its top is reached by light
    // that has already dimmed falling — the case a free fall would mask.
    World world(1, 4, 1);

    for (int z = 0; z < world.GetDepth(); ++z)
        for (int y = 0; y < world.GetHeight(); ++y)
            for (int x = 0; x < world.GetWidth(); ++x)
                world.SetBlock(x, y, z, BlockId{1});

    const int mouth = 30;

    // The shaft, open from the sky down to the pocket.
    for (int y = world.GetHeight() - 1; y >= mouth; --y)
        world.SetBlock(0, y, 0, BlockId{0});

    // The pocket, one column over, hanging below the shaft's mouth.
    for (int y = mouth; y >= mouth - 2; --y)
        world.SetBlock(1, y, 0, BlockId{0});

    SkyLight::PropagateAll(world);

    REQUIRE(world.GetSkyLight(0, mouth, 0) == SkyLight::Max);

    CHECK(world.GetSkyLight(1, mouth, 0) == SkyLight::Max - 1);
    CHECK(world.GetSkyLight(1, mouth - 1, 0) == SkyLight::Max - 2);
    CHECK(world.GetSkyLight(1, mouth - 2, 0) == SkyLight::Max - 3);
}

TEST_CASE("Propagation is idempotent")
{
    World world(2, 2, 2);

    for (int z = 0; z < world.GetDepth(); ++z)
        for (int x = 0; x < world.GetWidth(); ++x)
            world.SetBlock(x, 10, z, BlockId{1});

    SkyLight::PropagateAll(world);
    const std::uint8_t sample = world.GetSkyLight(5, 20, 5);

    SkyLight::PropagateAll(world);

    CHECK(world.GetSkyLight(5, 20, 5) == sample);
}

namespace
{
    //Every light value in the world, for comparing two propagations.
    std::vector<std::uint8_t> LightSnapshot(const World& world)
    {
        std::vector<std::uint8_t> values;
        values.reserve(
            static_cast<std::size_t>(world.GetWidth()) *
            world.GetHeight() * world.GetDepth());

        for (int z = 0; z < world.GetDepth(); ++z)
            for (int y = 0; y < world.GetHeight(); ++y)
                for (int x = 0; x < world.GetWidth(); ++x)
                    values.push_back(world.GetSkyLight(x, y, z));

        return values;
    }

    //A world with a roof over everything but one open shaft, so there is both
    //bright and dark space for an edit to disturb.
    World BuildRoofedWorld()
    {
        World world(3, 4, 3);
        const int roof = 20;

        for (int z = 0; z < world.GetDepth(); ++z)
            for (int x = 0; x < world.GetWidth(); ++x)
                if (x != 24 || z != 24)
                    world.SetBlock(x, roof, z, BlockId{1});

        return world;
    }
}

TEST_CASE("Breaking a roof block lets light into the space below")
{
    World world = BuildRoofedWorld();
    SkyLight::PropagateAll(world);

    const int roof = 20;
    REQUIRE(world.GetSkyLight(5, roof - 1, 5) == 0);

    world.SetBlock(5, roof, 5, BlockId{0});
    SkyLight::Repropagate(world, 5, roof, 5);

    CHECK(world.GetSkyLight(5, roof - 1, 5) == SkyLight::Max);
    CHECK(world.GetSkyLight(5, 0, 5) == SkyLight::Max);
}

TEST_CASE("Sealing a shaft takes the light back out of it")
{
    World world = BuildRoofedWorld();
    SkyLight::PropagateAll(world);

    const int roof = 20;
    REQUIRE(world.GetSkyLight(24, roof - 1, 24) == SkyLight::Max);

    world.SetBlock(24, roof, 24, BlockId{1});
    SkyLight::Repropagate(world, 24, roof, 24);

    CHECK(world.GetSkyLight(24, roof - 1, 24) == 0);
    CHECK(world.GetSkyLight(24, 0, 24) == 0);
}

TEST_CASE("Bounded repropagation matches a full propagation")
{
    // The test that proves the box is big enough. Whatever the edit, relighting
    // only the box must leave the world in exactly the state a from-scratch
    // flood would have produced.
    World world = BuildRoofedWorld();
    SkyLight::PropagateAll(world);

    const int roof = 20;
    glm::ivec3 edit{ 0, 0, 0 };
    BlockId block{ 0 };

    SUBCASE("opening a hole in the roof")
    {
        edit = glm::ivec3(5, roof, 5);
        block = BlockId{0};
    }

    SUBCASE("sealing the open shaft")
    {
        edit = glm::ivec3(24, roof, 24);
        block = BlockId{1};
    }

    SUBCASE("opening a hole beside the shaft")
    {
        edit = glm::ivec3(23, roof, 24);
        block = BlockId{0};
    }

    SUBCASE("placing a block in open air under the shaft")
    {
        edit = glm::ivec3(24, roof - 5, 24);
        block = BlockId{1};
    }

    SUBCASE("opening a hole at the world edge")
    {
        edit = glm::ivec3(0, roof, 0);
        block = BlockId{0};
    }

    SUBCASE("opening a hole at the far world edge")
    {
        // Drives the box against the upper clamp: minX/minZ stay interior
        // while maxX/maxZ are pinned to the world's last column, the mirror
        // of the (0,0) case above.
        edit = glm::ivec3(world.GetWidth() - 1, roof, world.GetDepth() - 1);
        block = BlockId{0};
    }

    world.SetBlock(edit.x, edit.y, edit.z, block);
    SkyLight::Repropagate(world, edit.x, edit.y, edit.z);
    const std::vector<std::uint8_t> bounded = LightSnapshot(world);

    SkyLight::PropagateAll(world);
    const std::vector<std::uint8_t> full = LightSnapshot(world);

    CHECK(bounded == full);
}

TEST_CASE("Repropagation marks the chunks whose light changed")
{
    World world = BuildRoofedWorld();
    SkyLight::PropagateAll(world);
    world.ClearDirty();

    const int roof = 20;
    world.SetBlock(5, roof, 5, BlockId{0});
    world.ClearDirty(); // Ignore the block edit's own marking.

    SkyLight::Repropagate(world, 5, roof, 5);

    // The exact set of chunks whose light changed, derived from the geometry:
    //
    // Opening (5, roof, 5) lets full-strength light free-fall straight down
    // that whole column (nothing else blocks below the single-layer roof), so
    // every cell from y = 0 to the roof was previously dark (unreachable —
    // more than Max=15 sideways steps from the world's other opening at
    // (24,24)) and is now lit. That column sits in chunk (0,0,0) for y = 0-15
    // and chunk (0,1,0) for y = 16-20, so both are dirty.
    //
    // At each lit y level below the roof, light also spreads sideways from
    // (5,5) up to 14 steps before dying out (a 15th step computes to exactly
    // 0, which is not an improvement over the existing 0 and so doesn't
    // brighten anything or get marked). Along +X that reaches x = 16-19 (still
    // z = 5, chunk z = 0) — into chunk (1, ·, 0); along +Z it symmetrically
    // reaches z = 16-19 (still x = 5, chunk x = 0) — into chunk (0, ·, 1). In
    // both directions the reach spans y = 0-19, so both the y = 0 chunk and
    // the y = 1 chunk pick up newly lit cells: (1,0,0), (1,1,0), (0,0,1),
    // (0,1,1).
    //
    // Reaching chunk (1,·,1) would need dx >= 11 (to cross into the x = 1
    // chunk) and dz >= 11 (to cross into the z = 1 chunk) simultaneously,
    // costing at least 22 steps — well past the 14-step budget — so that
    // chunk is never touched by this edit, however close it sits to the
    // world's other, unrelated shaft at (24,24). No y-chunk above 1 is
    // touched either: nothing above the roof changes.
    //
    // That is six chunks total, confirmed by an instrumented run of the fixed
    // code rather than assumed from it: (0,0,0), (0,0,1), (0,1,0), (0,1,1),
    // (1,0,0), (1,1,0).
    const std::set<glm::ivec3, IVec3Less> expected =
    {
        glm::ivec3(0, 0, 0), glm::ivec3(0, 0, 1),
        glm::ivec3(0, 1, 0), glm::ivec3(0, 1, 1),
        glm::ivec3(1, 0, 0), glm::ivec3(1, 1, 0),
    };

    CHECK(world.DirtyChunks() == expected);
}

TEST_CASE("Repropagation after a no-op edit marks nothing dirty")
{
    World world = BuildRoofedWorld();
    SkyLight::PropagateAll(world);
    world.ClearDirty();

    // Reflooding without any preceding edit changes no light at all, so a
    // repeat propagation is idempotent and marks nothing dirty.
    const int roof = 20;
    SkyLight::Repropagate(world, 5, roof, 5);

    CHECK(world.DirtyChunks().empty());
}
