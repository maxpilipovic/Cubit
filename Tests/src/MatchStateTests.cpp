#include <doctest.h>

#include "Cubit/Voxel/MatchState.h"
#include "Cubit/Voxel/World.h"

#include <glm/glm.hpp>

namespace
{
    //A world with a solid floor at y=0 and air above it.
    World FlatWorld()
    {
        World world(2, 2, 2);

        for (int z = 0; z < world.GetDepth(); ++z)
            for (int x = 0; x < world.GetWidth(); ++x)
                world.SetBlock(x, 0, z, BlockId{ 1 });

        return world;
    }
}

TEST_CASE("A new match has no players and a zero tick")
{
    MatchState match(FlatWorld());

    CHECK(match.Tick() == 0);
    CHECK_FALSE(match.HasPlayer(1));
}

TEST_CASE("Players get distinct ids and spawn where they are put")
{
    MatchState match(FlatWorld());

    const PlayerId first = match.AddPlayer(glm::vec3(4.0f, 10.0f, 5.0f));
    const PlayerId second = match.AddPlayer(glm::vec3(9.0f, 11.0f, 3.0f));

    CHECK(first != second);
    CHECK(first != InvalidPlayer);
    CHECK(second != InvalidPlayer);

    CHECK(match.HasPlayer(first));
    CHECK(match.HasPlayer(second));

    CHECK(match.Player(first).Position() == glm::vec3(4.0f, 10.0f, 5.0f));
    CHECK(match.Player(second).Position() == glm::vec3(9.0f, 11.0f, 3.0f));
}

TEST_CASE("A spawned player has nothing to interpolate through")
{
    //AddPlayer teleports rather than assigning, so the first frame after a
    //join does not smear the character in from wherever the default was.
    MatchState match(FlatWorld());
    const PlayerId player = match.AddPlayer(glm::vec3(4.0f, 10.0f, 5.0f));

    CHECK(match.Player(player).Position()
        == match.Player(player).PreviousPosition());
}

TEST_CASE("A removed player is gone and its id is not reused")
{
    //Not reused because an id identifies a participant across a match: a
    //stale packet naming a departed player must not be applied to whoever
    //joined next.
    MatchState match(FlatWorld());

    const PlayerId first = match.AddPlayer(glm::vec3(4.0f, 10.0f, 5.0f));
    match.RemovePlayer(first);

    CHECK_FALSE(match.HasPlayer(first));

    const PlayerId second = match.AddPlayer(glm::vec3(4.0f, 10.0f, 5.0f));
    CHECK(second != first);
}

TEST_CASE("Removing a player who is not there is not an error")
{
    //Once a removal can arrive from a socket, a duplicate disconnect is
    //malformed input rather than a caller bug.
    MatchState match(FlatWorld());

    match.RemovePlayer(77);
    CHECK_FALSE(match.HasPlayer(77));
}

TEST_CASE("Asking for a player who is not there throws")
{
    //The opposite choice from RemovePlayer, deliberately: there is no honest
    //CharacterController to return, and handing back a default one would
    //read as a character standing at the origin.
    MatchState match(FlatWorld());

    CHECK_THROWS(match.Player(5));
}

TEST_CASE("Teleporting a player moves both of its positions")
{
    MatchState match(FlatWorld());
    const PlayerId player = match.AddPlayer(glm::vec3(4.0f, 10.0f, 5.0f));

    match.TeleportPlayer(player, glm::vec3(20.0f, 6.0f, 21.0f));

    CHECK(match.Player(player).Position() == glm::vec3(20.0f, 6.0f, 21.0f));
    CHECK(match.Player(player).PreviousPosition()
        == glm::vec3(20.0f, 6.0f, 21.0f));
}

TEST_CASE("Replacing the world keeps the players and the tick")
{
    //What F9 does: reload the terrain being worked on, leave the player where
    //they were standing.
    MatchState match(FlatWorld());
    const PlayerId player = match.AddPlayer(glm::vec3(4.0f, 10.0f, 5.0f));

    World replacement(2, 2, 2);
    replacement.SetBlock(1, 1, 1, BlockId{ 3 });

    match.ReplaceWorld(std::move(replacement));

    CHECK(match.HasPlayer(player));
    CHECK(match.Player(player).Position() == glm::vec3(4.0f, 10.0f, 5.0f));
    CHECK(match.GetWorld().GetBlock(1, 1, 1) == BlockId{ 3 });
}

namespace
{
    constexpr float StepSeconds = 1.0f / 60.0f;

    CharacterInput WalkForward()
    {
        CharacterInput input;
        input.Move = glm::vec2(0.0f, 1.0f);
        input.Yaw = 0.0f;
        return input;
    }
}

TEST_CASE("The tick advances once per step regardless of commands")
{
    MatchState match(FlatWorld());

    match.Step({}, StepSeconds);
    CHECK(match.Tick() == 1);

    const PlayerId player = match.AddPlayer(glm::vec3(16.0f, 10.0f, 16.0f));
    const PlayerCommand commands[] = { { player, WalkForward() } };

    match.Step(commands, StepSeconds);
    CHECK(match.Tick() == 2);
}

TEST_CASE("Players step independently")
{
    MatchState match(FlatWorld());

    const PlayerId walker = match.AddPlayer(glm::vec3(8.0f, 10.0f, 8.0f));
    const PlayerId idler = match.AddPlayer(glm::vec3(20.0f, 10.0f, 20.0f));

    const glm::vec3 idlerStart = match.Player(idler).Position();
    const PlayerCommand commands[] = { { walker, WalkForward() } };

    for (int i = 0; i < 60; ++i)
        match.Step(commands, StepSeconds);

    //The walker moved along x; the idler only fell.
    CHECK(match.Player(walker).Position().x > 8.5f);
    CHECK(match.Player(idler).Position().x == doctest::Approx(idlerStart.x));
    CHECK(match.Player(idler).Position().z == doctest::Approx(idlerStart.z));
}

TEST_CASE("A player with no command still falls")
{
    //The normal case on a real connection, not an edge one: a dropped or late
    //packet must leave a character under gravity rather than frozen mid-air.
    MatchState match(FlatWorld());
    const PlayerId player = match.AddPlayer(glm::vec3(16.0f, 20.0f, 16.0f));

    const float start = match.Player(player).Position().y;

    for (int i = 0; i < 30; ++i)
        match.Step({}, StepSeconds);

    CHECK(match.Player(player).Position().y < start - 0.5f);
}

TEST_CASE("A command naming an absent player is ignored")
{
    //Once commands arrive off a socket, a stale id is malformed input rather
    //than a caller bug, so this must not throw and must not invent a player.
    MatchState match(FlatWorld());

    const PlayerCommand commands[] = { { 999, WalkForward() } };

    CHECK_NOTHROW(match.Step(commands, StepSeconds));
    CHECK_FALSE(match.HasPlayer(999));
    CHECK(match.Tick() == 1);
}
