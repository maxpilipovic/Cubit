#include <doctest.h>

#include "Cubit/Voxel/EditRules.h"
#include "Cubit/Voxel/MatchState.h"
#include "Cubit/Voxel/World.h"

#include <glm/glm.hpp>
#include <utility>

namespace
{
    World FloorWorld()
    {
        World world(2, 2, 2);

        for (int z = 0; z < world.GetDepth(); ++z)
            for (int x = 0; x < world.GetWidth(); ++x)
                world.SetBlock(x, 0, z, BlockId{ 1 });

        return world;
    }
}

TEST_CASE("Reach includes a cell whose nearest point is exactly ReachDistance away")
{
    //Nearest point of cell (12,0,0) to this eye is (12, 0.5, 0.5): exactly 12.
    const glm::vec3 eye(0.0f, 0.5f, 0.5f);
    CHECK(IsCellWithinReach(eye, glm::ivec3(12, 0, 0)));

    //One cell further is 13 away.
    CHECK_FALSE(IsCellWithinReach(eye, glm::ivec3(13, 0, 0)));
}

TEST_CASE("Reach measures to the nearest point of the cell, not its centre")
{
    //Centre of cell (12,0,0) is 12.5 away; its near face is 12.
    const glm::vec3 eye(0.0f, 0.5f, 0.5f);
    CHECK(glm::distance(eye, glm::vec3(12.5f, 0.5f, 0.5f)) > ReachDistance);
    CHECK(IsCellWithinReach(eye, glm::ivec3(12, 0, 0)));
}

TEST_CASE("A box overlapping a cell overlaps, and one only touching it does not")
{
    //Half extent 1 on y puts the box's bottom at exactly y = 1.0, the top of
    //cell (8,0,8). Values chosen to be exact in binary floating point.
    const glm::vec3 half(0.25f, 1.0f, 0.25f);
    const glm::vec3 standing(8.5f, 2.0f, 8.5f);

    CHECK_FALSE(BoxOverlapsCell(standing, half, glm::ivec3(8, 0, 8)));
    CHECK(BoxOverlapsCell(standing, half, glm::ivec3(8, 1, 8)));
    CHECK_FALSE(BoxOverlapsCell(standing, half, glm::ivec3(9, 1, 8)));
}

TEST_CASE("An edit is legal in reach, and illegal when it changes nothing or leaves the world")
{
    MatchState match(FloorWorld());
    const PlayerId editor = match.AddPlayer(glm::vec3(8.5f, 2.0f, 8.5f));

    CHECK(IsEditLegal(match, editor, BlockEdit{ glm::ivec3(10, 0, 8), BlockId{ 0 } }, OtherPlayers::Check));
    CHECK_FALSE(IsEditLegal(match, editor, BlockEdit{ glm::ivec3(10, 0, 8), BlockId{ 1 } }, OtherPlayers::Check));
    CHECK_FALSE(IsEditLegal(match, editor, BlockEdit{ glm::ivec3(10, -1, 8), BlockId{ 0 } }, OtherPlayers::Check));
    CHECK_FALSE(IsEditLegal(match, editor, BlockEdit{ glm::ivec3(31, 0, 31), BlockId{ 0 } }, OtherPlayers::Check));
    CHECK_FALSE(IsEditLegal(match, PlayerId{ 99 }, BlockEdit{ glm::ivec3(10, 0, 8), BlockId{ 0 } }, OtherPlayers::Check));
}

TEST_CASE("A placement may not overlap the editor, and may fill the cell under their feet")
{
    MatchState match(FloorWorld());

    //Airborne, feet at y = 2.1: cell (8,1,8) spans y 1..2 and is clear of the box.
    const PlayerId editor = match.AddPlayer(glm::vec3(8.5f, 3.0f, 8.5f));

    CHECK(IsEditLegal(match, editor, BlockEdit{ glm::ivec3(8, 1, 8), BlockId{ 2 } }, OtherPlayers::Check));
    CHECK_FALSE(IsEditLegal(match, editor, BlockEdit{ glm::ivec3(8, 2, 8), BlockId{ 2 } }, OtherPlayers::Check));
}

TEST_CASE("A placement into another player is checked only when asked to, and breaking never is")
{
    MatchState match(FloorWorld());
    const PlayerId editor = match.AddPlayer(glm::vec3(8.5f, 2.0f, 8.5f));
    const PlayerId other = match.AddPlayer(glm::vec3(11.5f, 2.0f, 8.5f));
    REQUIRE(other != editor);

    const BlockEdit intoOther{ glm::ivec3(11, 1, 8), BlockId{ 2 } };
    CHECK_FALSE(IsEditLegal(match, editor, intoOther, OtherPlayers::Check));
    CHECK(IsEditLegal(match, editor, intoOther, OtherPlayers::Ignore));

    //The floor under the other player: breaking has no overlap rule.
    CHECK(IsEditLegal(match, editor, BlockEdit{ glm::ivec3(11, 0, 8), BlockId{ 0 } }, OtherPlayers::Check));
}
