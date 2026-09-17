#include <doctest.h>

#include "Cubit/Voxel/BlockEdit.h"
#include "Cubit/Voxel/Support.h"
#include "Cubit/Voxel/VoxLoader.h"
#include "Cubit/Voxel/World.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
    //A 32 x 32 x 32 world with a solid bottom layer: the anchor everything here
    //is measured against.
    World FloorWorld()
    {
        World world(2, 2, 2);

        for (int z = 0; z < world.GetDepth(); ++z)
            for (int x = 0; x < world.GetWidth(); ++x)
                world.SetBlock(x, 0, z, BlockId{ 1 });

        return world;
    }

    void Fill(World& world, const glm::ivec3& from, const glm::ivec3& to, BlockId block)
    {
        for (int z = from.z; z <= to.z; ++z)
            for (int y = from.y; y <= to.y; ++y)
                for (int x = from.x; x <= to.x; ++x)
                    world.SetBlock(x, y, z, block);
    }

    //Empties a cell the way an edit does and reports what that left unsupported.
    std::vector<glm::ivec3> Dig(World& world, const glm::ivec3& cell,
        std::size_t maxSearch = MaxSupportSearch)
    {
        ApplyBlockEdit(world, BlockEdit{ cell, BlockId{ 0 } });

        const glm::ivec3 emptied[] = { cell };
        return FindUnsupported(world, emptied, maxSearch);
    }

    bool Holds(const std::vector<glm::ivec3>& cells, const glm::ivec3& cell)
    {
        return std::find(cells.begin(), cells.end(), cell) != cells.end();
    }
}

TEST_CASE("A pillar cut at its base comes down whole")
{
    World world = FloorWorld();
    Fill(world, glm::ivec3(16, 1, 16), glm::ivec3(16, 10, 16), BlockId{ 1 });

    const std::vector<glm::ivec3> loose = Dig(world, glm::ivec3(16, 1, 16));

    CHECK(loose.size() == 9);
    for (int y = 2; y <= 10; ++y)
        CHECK(Holds(loose, glm::ivec3(16, y, 16)));
}

TEST_CASE("Digging into solid ground leaves nothing unsupported")
{
    //The case that matters most: the world must not fall. Ten solid layers, a
    //cell taken out of the middle of them.
    World world = FloorWorld();
    Fill(world, glm::ivec3(0, 0, 0), glm::ivec3(31, 9, 31), BlockId{ 1 });

    CHECK(Dig(world, glm::ivec3(16, 5, 16)).empty());

    //And a tunnel through it, cell by cell, still holds up.
    bool anythingLoose = false;
    for (int x = 4; x <= 27; ++x)
        anythingLoose = anythingLoose || !Dig(world, glm::ivec3(x, 5, 16)).empty();

    CHECK_FALSE(anythingLoose);
}

TEST_CASE("An arch that loses one leg stands on the other")
{
    World world = FloorWorld();
    Fill(world, glm::ivec3(10, 1, 16), glm::ivec3(10, 5, 16), BlockId{ 1 });
    Fill(world, glm::ivec3(14, 1, 16), glm::ivec3(14, 5, 16), BlockId{ 1 });
    Fill(world, glm::ivec3(10, 6, 16), glm::ivec3(14, 6, 16), BlockId{ 1 });

    CHECK(Dig(world, glm::ivec3(10, 1, 16)).empty());

    //Taking the second leg's base leaves the whole arch hanging: two legs, less
    //the two cells just dug, and the five-cell span.
    const std::vector<glm::ivec3> loose = Dig(world, glm::ivec3(14, 1, 16));

    CHECK(loose.size() == 13);
    CHECK(Holds(loose, glm::ivec3(10, 5, 16)));
    CHECK(Holds(loose, glm::ivec3(12, 6, 16)));
    CHECK_FALSE(Holds(loose, glm::ivec3(10, 1, 16)));
}

TEST_CASE("A piece too big to walk is left where it is")
{
    //A slab on one column: 100 cells of slab and 18 of column.
    World world = FloorWorld();
    Fill(world, glm::ivec3(5, 1, 5), glm::ivec3(5, 19, 5), BlockId{ 1 });
    Fill(world, glm::ivec3(4, 20, 4), glm::ivec3(13, 20, 13), BlockId{ 1 });

    World tooBig = world;
    CHECK(Dig(tooBig, glm::ivec3(5, 1, 5), 50).empty());

    //The same piece, with a search allowed to finish.
    const std::vector<glm::ivec3> loose = Dig(world, glm::ivec3(5, 1, 5));
    CHECK(loose.size() == 118);
}

TEST_CASE("Removing what came loose leaves nothing else hanging")
{
    //The property that makes a chain reaction impossible: a search walks a whole
    //connected piece, so anything resting on that piece went with it.
    World world = FloorWorld();
    Fill(world, glm::ivec3(16, 1, 16), glm::ivec3(16, 10, 16), BlockId{ 1 });
    Fill(world, glm::ivec3(16, 11, 16), glm::ivec3(20, 11, 20), BlockId{ 1 });
    Fill(world, glm::ivec3(20, 12, 20), glm::ivec3(20, 14, 20), BlockId{ 1 });

    const std::vector<glm::ivec3> loose = Dig(world, glm::ivec3(16, 1, 16));
    REQUIRE_FALSE(loose.empty());

    std::vector<BlockEdit> falls;
    for (const glm::ivec3& cell : loose)
        falls.push_back(BlockEdit{ cell, BlockId{ 0 } });

    ApplyBlockEdits(world, falls);

    CHECK(FindUnsupported(world, loose).empty());
}

TEST_CASE("Water holds nothing up and never comes loose")
{
    World world = FloorWorld();
    Palette palette = DefaultPalette();
    palette[7] = glm::vec4(0.2f, 0.4f, 0.8f, 0.55f);
    world.SetPalette(palette);

    //A column of water under a solid block, and the block's only solid support
    //beside it.
    Fill(world, glm::ivec3(8, 1, 8), glm::ivec3(8, 6, 8), BlockId{ 7 });
    world.SetBlock(8, 7, 8, BlockId{ 1 });
    Fill(world, glm::ivec3(9, 1, 8), glm::ivec3(9, 7, 8), BlockId{ 1 });

    //Cutting the solid column leaves the block above the water hanging: the
    //water beneath it is not holding it up.
    const std::vector<glm::ivec3> loose = Dig(world, glm::ivec3(9, 1, 8));

    CHECK(Holds(loose, glm::ivec3(8, 7, 8)));
    CHECK_FALSE(Holds(loose, glm::ivec3(8, 6, 8)));
    for (const glm::ivec3& cell : loose)
        CHECK(world.IsBlockSolid(cell.x, cell.y, cell.z));
}

TEST_CASE("Emptying nothing asks nothing")
{
    World world = FloorWorld();
    Fill(world, glm::ivec3(16, 1, 16), glm::ivec3(16, 10, 16), BlockId{ 1 });

    CHECK(FindUnsupported(world, {}).empty());

    //A cell that is still solid - a placement, say - unsettles nothing either.
    const glm::ivec3 filled[] = { glm::ivec3(16, 5, 16) };
    CHECK(FindUnsupported(world, filled).empty());
}

TEST_CASE("A cell on the bottom layer is its own anchor")
{
    //Digging beside a block that sits on the floor must not lift it: y = 0 is
    //the anchor, so a lone floor block is held up by being there at all.
    World world = FloorWorld();

    CHECK(Dig(world, glm::ivec3(16, 0, 16)).empty());
    CHECK(world.IsBlockSolid(17, 0, 16));
}

TEST_CASE("What the support search costs, measured")
{
    //Not a gate: the numbers behind MaxSupportSearch. Three cases - a dig into
    //the shipped map's terrain, a crater's worth of emptied cells, and the worst
    //case, a search that runs to the cap and gives up.
    std::filesystem::path path;
    for (const char* candidate : {
            "game/assets/maps/battlefield512.vox",
            "../game/assets/maps/battlefield512.vox" })
        if (std::filesystem::exists(candidate))
        {
            path = candidate;
            break;
        }

    REQUIRE_FALSE(path.empty());

    World world = BuildWorld(VoxLoader::LoadFile(path.string()));

    const int cx = world.GetWidth() / 2;
    const int cz = world.GetDepth() / 2;
    int top = world.GetHeight() - 1;
    while (top > 0 && !world.IsBlockOpaque(cx, top, cz))
        --top;

    const auto milliseconds = [](auto from, auto to)
    {
        return std::chrono::duration<double, std::milli>(to - from).count();
    };

    //One cell out of a hillside: the search reaches y = 0 and stops.
    {
        const glm::ivec3 cell(cx, top, cz);
        ApplyBlockEdit(world, BlockEdit{ cell, BlockId{0} });

        const glm::ivec3 emptied[] = { cell };
        const auto start = std::chrono::steady_clock::now();
        const std::vector<glm::ivec3> loose = FindUnsupported(world, emptied);
        const auto end = std::chrono::steady_clock::now();

        MESSAGE("one cell out of terrain: ", loose.size(), " loose, ",
            milliseconds(start, end), " ms");
        CHECK(loose.empty());
    }

    //A radius-3 crater: every emptied cell asks, and they share their answers.
    {
        std::vector<BlockEdit> ball;
        std::vector<glm::ivec3> emptied;
        for (int dz = -3; dz <= 3; ++dz)
            for (int dy = -3; dy <= 3; ++dy)
                for (int dx = -3; dx <= 3; ++dx)
                    if (dx * dx + dy * dy + dz * dz <= 9)
                    {
                        const glm::ivec3 cell(cx + 8 + dx, top + dy, cz + dx);
                        ball.push_back(BlockEdit{ cell, BlockId{0} });
                        emptied.push_back(cell);
                    }

        ApplyBlockEdits(world, ball);

        const auto start = std::chrono::steady_clock::now();
        const std::vector<glm::ivec3> loose = FindUnsupported(world, emptied);
        const auto end = std::chrono::steady_clock::now();

        MESSAGE("a radius-3 crater (", emptied.size(), " cells emptied): ", loose.size(),
            " loose, ", milliseconds(start, end), " ms");
    }

    //The worst case: a slab far bigger than the cap, cut from its one support,
    //so the search walks MaxSupportSearch cells and only then gives up.
    {
        World slab(4, 4, 4);
        for (int z = 0; z < slab.GetDepth(); ++z)
            for (int x = 0; x < slab.GetWidth(); ++x)
                slab.SetBlock(x, 0, z, BlockId{1});

        for (int y = 1; y <= 19; ++y)
            slab.SetBlock(5, y, 5, BlockId{1});

        for (int z = 0; z < 60; ++z)
            for (int x = 0; x < 60; ++x)
                slab.SetBlock(x + 2, 20, z + 2, BlockId{1});

        const glm::ivec3 cell(5, 1, 5);
        ApplyBlockEdit(slab, BlockEdit{ cell, BlockId{0} });

        const glm::ivec3 emptied[] = { cell };
        const auto start = std::chrono::steady_clock::now();
        const std::vector<glm::ivec3> loose = FindUnsupported(slab, emptied);
        const auto end = std::chrono::steady_clock::now();

        MESSAGE("a 3,600-cell slab against a ", MaxSupportSearch, "-cell cap: ",
            loose.size(), " loose, ", milliseconds(start, end), " ms");
    }
}
