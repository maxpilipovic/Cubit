#include <doctest.h>

#include "Cubit/Voxel/Chunk.h"
#include "Cubit/Voxel/SkyLight.h"
#include "Cubit/Voxel/TerrainGen.h"
#include "Cubit/Voxel/VoxLoader.h"
#include "Cubit/Voxel/World.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
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
    // that has already dimmed falling â€” the case a free fall would mask.
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

    //A world with solid ground under open sky, the shape most of a real map
    //has. An edit on that surface disturbs almost nothing, which is what makes
    //it the case that separates relighting the change from relighting a box.
    World BuildOpenGroundWorld()
    {
        World world(3, 4, 3);

        for (int z = 0; z < world.GetDepth(); ++z)
            for (int y = 0; y <= 10; ++y)
                for (int x = 0; x < world.GetWidth(); ++x)
                    world.SetBlock(x, y, z, BlockId{1});

        return world;
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

TEST_CASE("Repropagation under a roof matches a full propagation")
{
    // The test that proves the incremental relight reaches far enough. Whatever
    // the edit, settling the light around it must leave the world in exactly the
    // state a from-scratch flood would have produced.
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
    // every cell from y = 0 to the roof was previously dark (unreachable â€”
    // more than Max=15 sideways steps from the world's other opening at
    // (24,24)) and is now lit. That column sits in chunk (0,0,0) for y = 0-15
    // and chunk (0,1,0) for y = 16-20, so both are dirty.
    //
    // At each lit y level below the roof, light also spreads sideways from
    // (5,5) up to 14 steps before dying out (a 15th step computes to exactly
    // 0, which is not an improvement over the existing 0 and so doesn't
    // brighten anything or get marked). Along +X that reaches x = 16-19 (still
    // z = 5, chunk z = 0) â€” into chunk (1, Â·, 0); along +Z it symmetrically
    // reaches z = 16-19 (still x = 5, chunk x = 0) â€” into chunk (0, Â·, 1). In
    // both directions the reach spans y = 0-19, so both the y = 0 chunk and
    // the y = 1 chunk pick up newly lit cells: (1,0,0), (1,1,0), (0,0,1),
    // (0,1,1).
    //
    // Reaching chunk (1,Â·,1) would need dx >= 11 (to cross into the x = 1
    // chunk) and dz >= 11 (to cross into the z = 1 chunk) simultaneously,
    // costing at least 22 steps â€” well past the 14-step budget â€” so that
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

TEST_CASE("Repropagation over open ground matches a full propagation")
{
    // The roofed world exercises light creeping under an overhang. This one
    // exercises the other rule: full-strength light falling for free down an
    // open column. Interrupting such a column is the case a relight that only
    // removes dimmer neighbours gets wrong, because the cell below a
    // free-falling one holds the *same* level, not a lower one.
    World world = BuildOpenGroundWorld();
    SkyLight::PropagateAll(world);

    const int surface = 10;
    glm::ivec3 edit{ 0, 0, 0 };
    BlockId block{ 0 };

    SUBCASE("breaking the surface block")
    {
        edit = glm::ivec3(24, surface, 24);
        block = BlockId{0};
    }

    SUBCASE("placing a block on the surface")
    {
        edit = glm::ivec3(24, surface + 1, 24);
        block = BlockId{1};
    }

    SUBCASE("placing a block in mid-air, interrupting a free-falling column")
    {
        edit = glm::ivec3(24, 30, 24);
        block = BlockId{1};
    }

    SUBCASE("placing a block in mid-air at the world edge")
    {
        edit = glm::ivec3(0, 30, 0);
        block = BlockId{1};
    }

    world.SetBlock(edit.x, edit.y, edit.z, block);
    SkyLight::Repropagate(world, edit.x, edit.y, edit.z);
    const std::vector<std::uint8_t> bounded = LightSnapshot(world);

    SkyLight::PropagateAll(world);
    const std::vector<std::uint8_t> full = LightSnapshot(world);

    CHECK(bounded == full);
}

TEST_CASE("Placing a block darkens the whole column it shades")
{
    // Stated directly rather than only through the equivalence check, so the
    // free-fall removal rule has a test that names what it is for.
    World world = BuildOpenGroundWorld();
    SkyLight::PropagateAll(world);

    REQUIRE(world.GetSkyLight(24, 20, 24) == SkyLight::Max);
    REQUIRE(world.GetSkyLight(24, 11, 24) == SkyLight::Max);

    world.SetBlock(24, 30, 24, BlockId{1});
    SkyLight::Repropagate(world, 24, 30, 24);

    // Everything under the new block loses its free fall. It is not pitch dark
    // â€” light creeps back in sideways from the open columns around it â€” but it
    // must no longer be at full strength.
    CHECK(world.GetSkyLight(24, 29, 24) < SkyLight::Max);
    CHECK(world.GetSkyLight(24, 20, 24) < SkyLight::Max);
    CHECK(world.GetSkyLight(24, 11, 24) < SkyLight::Max);

    // Above the block is untouched.
    CHECK(world.GetSkyLight(24, 31, 24) == SkyLight::Max);
}

TEST_CASE("Relighting an edit costs what the edit changed, not what it might have")
{
    // The bug this guards: relighting used to blank a fixed radius-15,
    // full-height box around every edit and flood it again from scratch, so a
    // block broken on open ground cost tens of thousands of cell visits to
    // discover that one cell had changed. Each toggle below changes a single
    // cell's light, so a hundred of them is a small amount of real work.
    World world = BuildOpenGroundWorld();
    SkyLight::PropagateAll(world);

    const int surface = 10;
    constexpr int edits = 100;

    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < edits; ++i)
    {
        world.SetBlock(24, surface, 24, i % 2 == 0 ? BlockId{0} : BlockId{1});
        SkyLight::Repropagate(world, 24, surface, 24);
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const double ms =
        std::chrono::duration<double, std::milli>(elapsed).count();

    INFO("100 single-cell edits took ", ms, " ms");

    // Deliberately loose: this is a Debug build and the point is the order of
    // magnitude, not a stopwatch. Relighting the box costs well over 100x this.
    CHECK(ms < 250.0);
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

TEST_CASE("Light passes through a non-opaque block")
{
    // A roof of transparent blocks should light the space under it just as an
    // open sky would, which is what makes a riverbed visible through water.
    World world(1, 4, 1);
    Palette palette = DefaultPalette();
    palette[2] = glm::vec4(0.2f, 0.4f, 0.8f, 0.5f); // transparent
    world.SetPalette(palette);

    const int roof = 20;
    for (int z = 0; z < world.GetDepth(); ++z)
        for (int x = 0; x < world.GetWidth(); ++x)
            world.SetBlock(x, roof, z, BlockId{2});

    SkyLight::PropagateAll(world);

    CHECK(world.GetSkyLight(8, roof, 8) == SkyLight::Max);
    CHECK(world.GetSkyLight(8, roof - 1, 8) == SkyLight::Max);
    CHECK(world.GetSkyLight(8, 0, 8) == SkyLight::Max);
}

TEST_CASE("An opaque roof still blocks light after the opacity change")
{
    // The companion to the case above: switching the predicate must not make
    // ordinary solid blocks stop casting shadow.
    World world(1, 4, 1);
    const int roof = 20;

    for (int z = 0; z < world.GetDepth(); ++z)
        for (int x = 0; x < world.GetWidth(); ++x)
            world.SetBlock(x, roof, z, BlockId{1});

    SkyLight::PropagateAll(world);

    CHECK(world.GetSkyLight(8, roof - 1, 8) == 0);
}

TEST_CASE("Placing a transparent block does not darken the column below it")
{
    // Repropagate has to ask the same question the flood asks. A transparent
    // block does not stop light, so placing one must not take back the light
    // beneath it — and because full-strength light falls for free, getting this
    // wrong darkens the whole column rather than a single cell.
    World world(1, 4, 1);
    Palette palette = DefaultPalette();
    palette[2] = glm::vec4(0.2f, 0.4f, 0.8f, 0.5f); // transparent
    world.SetPalette(palette);

    SkyLight::PropagateAll(world);

    const int placed = 20;
    REQUIRE(world.GetSkyLight(8, placed - 1, 8) == SkyLight::Max);

    world.SetBlock(8, placed, 8, BlockId{2});
    SkyLight::Repropagate(world, 8, placed, 8);

    CHECK(world.GetSkyLight(8, placed, 8) == SkyLight::Max);
    CHECK(world.GetSkyLight(8, placed - 1, 8) == SkyLight::Max);
    CHECK(world.GetSkyLight(8, 0, 8) == SkyLight::Max);
}

TEST_CASE("Placing an opaque block still darkens the column below it")
{
    // The companion case: the fix must not stop ordinary blocks casting shadow.
    World world(1, 4, 1);

    SkyLight::PropagateAll(world);

    const int placed = 20;
    world.SetBlock(8, placed, 8, BlockId{1});
    SkyLight::Repropagate(world, 8, placed, 8);

    CHECK(world.GetSkyLight(8, placed - 1, 8) < SkyLight::Max);
}

TEST_CASE("Placing a transparent block on the top layer keeps it at full strength")
{
    // The case that separates the two branches. Anywhere lower, an unflood is
    // undone by light falling back down the column for free, so the bug hides.
    // At the top there is nothing above to refill from, and only lateral
    // neighbours reach the cell — and they pay a level to get there.
    World world(1, 4, 1);
    Palette palette = DefaultPalette();
    palette[2] = glm::vec4(0.2f, 0.4f, 0.8f, 0.5f); // transparent
    world.SetPalette(palette);

    SkyLight::PropagateAll(world);

    const int top = world.GetHeight() - 1;
    world.SetBlock(8, top, 8, BlockId{2});
    SkyLight::Repropagate(world, 8, top, 8);

    CHECK(world.GetSkyLight(8, top, 8) == SkyLight::Max);
    CHECK(world.GetSkyLight(8, top - 1, 8) == SkyLight::Max);
}

namespace
{
    //A deliberately naive sky-light propagation, kept as an oracle for the
    //optimised one: seed the top layer, then spread until nothing brightens.
    //
    //This is the implementation the column scan replaced. Do not optimise it
    //and do not share code with the engine — its only job is to be obviously
    //correct, so that a disagreement means the fast path is wrong.
    void ReferencePropagate(World& world)
    {
        const int width = world.GetWidth();
        const int height = world.GetHeight();
        const int depth = world.GetDepth();

        for (int z = 0; z < depth; ++z)
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                    world.SetSkyLight(x, y, z, 0);

        //Index 5 is straight down, the one direction light travels for free.
        constexpr glm::ivec3 directions[6] =
        {
            {  1,  0,  0 }, { -1,  0,  0 },
            {  0,  0,  1 }, {  0,  0, -1 },
            {  0,  1,  0 }, {  0, -1,  0 },
        };
        constexpr int downIndex = 5;

        std::deque<glm::ivec3> queue;
        const int top = height - 1;

        for (int z = 0; z < depth; ++z)
            for (int x = 0; x < width; ++x)
                if (!world.IsBlockOpaque(x, top, z))
                {
                    world.SetSkyLight(x, top, z, SkyLight::Max);
                    queue.push_back(glm::ivec3(x, top, z));
                }

        while (!queue.empty())
        {
            const glm::ivec3 cell = queue.front();
            queue.pop_front();

            const int level = world.GetSkyLight(cell.x, cell.y, cell.z);
            if (level <= 1)
                continue;

            for (int d = 0; d < 6; ++d)
            {
                const glm::ivec3 next = cell + directions[d];

                if (!world.IsInBounds(next.x, next.y, next.z))
                    continue;
                if (world.IsBlockOpaque(next.x, next.y, next.z))
                    continue;

                const int value = (d == downIndex && level == SkyLight::Max)
                    ? SkyLight::Max
                    : level - 1;

                if (world.GetSkyLight(next.x, next.y, next.z) >= value)
                    continue;

                world.SetSkyLight(
                    next.x, next.y, next.z, static_cast<std::uint8_t>(value));
                queue.push_back(next);
            }
        }
    }

    //Builds the world twice, lights one each way, and reports the first cell
    //where they disagree.
    //
    //The position, not a bool: a whole-world equality check that fails with
    //"not equal" is undiagnosable, and a lighting disagreement is almost always
    //in one shadowed corner that you need named to find.
    void CheckMatchesReference(const std::function<World()>& build)
    {
        World expected = build();
        World actual = build();

        ReferencePropagate(expected);
        SkyLight::PropagateAll(actual);

        glm::ivec3 firstBad(-1);
        int referenceLevel = -1;
        int actualLevel = -1;

        for (int y = 0; y < expected.GetHeight() && firstBad.x < 0; ++y)
            for (int z = 0; z < expected.GetDepth() && firstBad.x < 0; ++z)
                for (int x = 0; x < expected.GetWidth(); ++x)
                {
                    const int e = expected.GetSkyLight(x, y, z);
                    const int a = actual.GetSkyLight(x, y, z);

                    if (e != a)
                    {
                        firstBad = glm::ivec3(x, y, z);
                        referenceLevel = e;
                        actualLevel = a;
                        break;
                    }
                }

        INFO("first difference at " << firstBad.x << "," << firstBad.y << ","
             << firstBad.z << " reference=" << referenceLevel
             << " actual=" << actualLevel);
        CHECK(firstBad == glm::ivec3(-1));
    }
}

TEST_CASE("Column scan matches the reference over a flat floor")
{
    CheckMatchesReference([]
    {
        World world(2, 2, 2);
        for (int z = 0; z < world.GetDepth(); ++z)
            for (int x = 0; x < world.GetWidth(); ++x)
                world.SetBlock(x, 0, z, BlockId{1});
        return world;
    });
}

TEST_CASE("Column scan matches the reference under an overhang")
{
    //The sideways-spreading case: cells under the slab are lit only by light
    //that came in from the side and dimmed on the way.
    CheckMatchesReference([]
    {
        World world(2, 2, 2);
        for (int z = 0; z < world.GetDepth(); ++z)
            for (int x = 0; x < world.GetWidth(); ++x)
                world.SetBlock(x, 0, z, BlockId{1});

        for (int z = 4; z < 28; ++z)
            for (int x = 4; x < 28; ++x)
                world.SetBlock(x, 8, z, BlockId{1});

        return world;
    });
}

TEST_CASE("Column scan matches the reference around a sealed cave")
{
    //Nothing lit borders the hollow, so it must stay dark in both.
    CheckMatchesReference([]
    {
        World world(2, 2, 2);
        for (int z = 0; z < world.GetDepth(); ++z)
            for (int y = 0; y < 12; ++y)
                for (int x = 0; x < world.GetWidth(); ++x)
                    world.SetBlock(x, y, z, BlockId{1});

        for (int z = 10; z < 20; ++z)
            for (int y = 4; y < 8; ++y)
                for (int x = 10; x < 20; ++x)
                    world.SetBlock(x, y, z, BlockId{0});

        return world;
    });
}

TEST_CASE("Column scan matches the reference with a column closed at the very top")
{
    //An empty lit range: the scan stops before writing anything, and that
    //column's neighbours have to notice it is dark.
    CheckMatchesReference([]
    {
        World world(2, 2, 2);
        const int top = world.GetHeight() - 1;

        for (int z = 0; z < world.GetDepth(); ++z)
            for (int x = 0; x < world.GetWidth(); ++x)
                world.SetBlock(x, 0, z, BlockId{1});

        world.SetBlock(5, top, 5, BlockId{1});
        return world;
    });
}

TEST_CASE("Column scan matches the reference in a world of pure air")
{
    //Every cell is Max and no cell borders darkness, so the seed set is empty.
    CheckMatchesReference([] { return World(1, 1, 1); });
}

TEST_CASE("Column scan matches the reference across staircase relief")
{
    //The case that stresses the seed y-ranges hardest: every column differs in
    //height from its neighbour, so every column contributes a seed range.
    CheckMatchesReference([]
    {
        World world(2, 2, 2);
        for (int z = 0; z < world.GetDepth(); ++z)
            for (int x = 0; x < world.GetWidth(); ++x)
                for (int y = 0; y <= x; ++y)
                    world.SetBlock(x, y, z, BlockId{1});

        return world;
    });
}

TEST_CASE("Column scan matches the reference on generated terrain")
{
    //Real structure rather than hand-built shapes: hills, a river of
    //non-opaque water, forests and forts. Smaller than the shipped map on
    //purpose — structure is what breaks this, not scale.
    CheckMatchesReference([]
    {
        TerrainConfig config;
        config.Size = glm::ivec3(128, 64, 128);
        return BuildWorld(TerrainGen::Generate(config));
    });
}

TEST_CASE("Column scan matches the reference under a roof in a middle chunk")
{
    //Three chunks tall, with the roof in the middle one, so the darkness it
    //casts starts in one chunk and has to carry down through the next. A scan
    //that walks a chunk at a time rather than a world column has to hand that
    //"already closed" state across the boundary itself; one that forgets
    //relights the bottom chunk as though it were open sky, which no
    //single-chunk-tall world would catch.
    CheckMatchesReference([]
    {
        World world(2, 3, 2);
        for (int z = 0; z < world.GetDepth(); ++z)
            for (int x = 0; x < world.GetWidth(); ++x)
                world.SetBlock(x, 0, z, BlockId{1});

        for (int z = 4; z < 28; ++z)
            for (int x = 4; x < 28; ++x)
                world.SetBlock(x, 20, z, BlockId{1});

        return world;
    });
}

TEST_CASE("Column scan matches the reference with a roof on a chunk boundary")
{
    //The roof sits at y=16 and y=32 — local y=0 of the middle and top chunks,
    //the first cell a downward chunk-major sweep looks at after crossing into
    //a new chunk. An off-by-one in where the boundary is handled shows up here
    //and nowhere else: the closing block is either double-counted or missed
    //entirely, depending on which side of the seam the sweep thinks it is on.
    CheckMatchesReference([]
    {
        World world(2, 3, 2);
        for (int z = 0; z < world.GetDepth(); ++z)
            for (int x = 0; x < world.GetWidth(); ++x)
                world.SetBlock(x, 0, z, BlockId{1});

        for (int z = 2; z < 14; ++z)
            for (int x = 2; x < 14; ++x)
                world.SetBlock(x, 16, z, BlockId{1});

        for (int z = 18; z < 30; ++z)
            for (int x = 18; x < 30; ++x)
                world.SetBlock(x, 32, z, BlockId{1});

        return world;
    });
}
