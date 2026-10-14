#include <doctest.h>

#include "MatchHud.h"
#include "MatchModels.h"

using namespace CubitGame;

TEST_CASE("The score line and every phase line")
{
    CtfState s;
    s.Captures = { 1, 0 };
    CHECK(ScoreLine(s) == "BLUE 1 - 0 GREEN");

    s.Stage = Phase::Warmup;
    CHECK(PhaseLine(s, 100) == "WARMUP");

    s.Stage = Phase::Countdown;
    s.StageEndTick = 1000;
    CHECK(PhaseLine(s, 1000 - 6 * 60 - 1) == "STARTS IN 7");   //rounds up

    //At and past the end tick a countdown shows zero, not a wrapped-around number.
    CHECK(PhaseLine(s, 1000) == "STARTS IN 0");
    CHECK(PhaseLine(s, 1050) == "STARTS IN 0");

    s.Stage = Phase::Active;
    s.StageEndTick = 1000 + (12 * 60 + 43) * 60;
    CHECK(PhaseLine(s, 1000) == "12:43");
    CHECK(PhaseLine(s, s.StageEndTick + 50) == "0:00");

    s.Stage = Phase::Ended;
    s.Winner = Team::Green;
    CHECK(PhaseLine(s, 0) == "GREEN WINS");
    s.Winner = Team::Blue;
    CHECK(PhaseLine(s, 0) == "BLUE WINS");
    s.Winner = Team::None;
    CHECK(PhaseLine(s, 0) == "DRAW");
}

TEST_CASE("The respawn line counts down in whole seconds and is empty when alive")
{
    PlayerStats p;
    CHECK(RespawnLine(p, 50).empty());
    p.RespawnTick = 400;
    CHECK(RespawnLine(p, 400 - 2 * 60 - 30) == "RESPAWN IN 3");

    //At and past the respawn tick the player is no longer waiting.
    CHECK(RespawnLine(p, 400).empty());
    CHECK(RespawnLine(p, 450).empty());
}

TEST_CASE("Event lines speak from the local player's side")
{
    CHECK(EventLine(MatchEvent{ 1, EventKind::Captured, Team::Blue, 3 }, Team::Blue) == "Your team captured the intel");
    CHECK(EventLine(MatchEvent{ 1, EventKind::Captured, Team::Green, 3 }, Team::Blue) == "Green captured the intel");
    CHECK(EventLine(MatchEvent{ 1, EventKind::PickedUp, Team::Blue, 3 }, Team::Blue) == "Your intel was taken");
    CHECK(EventLine(MatchEvent{ 1, EventKind::PickedUp, Team::Green, 3 }, Team::Blue) == "Your team took the Green intel");
    CHECK(EventLine(MatchEvent{ 1, EventKind::Returned, Team::Blue, 0 }, Team::Blue) == "Your intel was returned");
    CHECK(EventLine(MatchEvent{ 1, EventKind::MatchStarted, Team::None, 0 }, Team::Blue) == "The match has started");

    CHECK(EventLine(MatchEvent{ 1, EventKind::Returned, Team::Green, 0 }, Team::Blue) == "The Green intel was returned");
    CHECK(EventLine(MatchEvent{ 1, EventKind::MatchWon, Team::Blue, 0 }, Team::Blue) == "Your team won the match");
    CHECK(EventLine(MatchEvent{ 1, EventKind::MatchWon, Team::Green, 0 }, Team::Blue) == "Green won the match");
    CHECK(EventLine(MatchEvent{ 1, EventKind::MatchWon, Team::Blue, 0 }, Team::Green) == "Blue won the match");
    CHECK(EventLine(MatchEvent{ 1, EventKind::Captured, Team::Blue, 3 }, Team::Green) == "Blue captured the intel");
    CHECK(EventLine(MatchEvent{ 1, EventKind::Captured, Team::Green, 3 }, Team::Green) == "Your team captured the intel");
    CHECK(EventLine(MatchEvent{ 1, EventKind::PickedUp, Team::Blue, 3 }, Team::Green) == "Your team took the Blue intel");
    CHECK(EventLine(MatchEvent{ 1, EventKind::Draw, Team::None, 0 }, Team::Blue) == "The match is a draw");

    //A spectator has no side, so nothing is "Your".
    CHECK(EventLine(MatchEvent{ 1, EventKind::Captured, Team::Blue, 3 }, Team::None) == "Blue captured the intel");
    CHECK(EventLine(MatchEvent{ 1, EventKind::MatchWon, Team::Green, 0 }, Team::None) == "Green won the match");
    CHECK(EventLine(MatchEvent{ 1, EventKind::PickedUp, Team::Blue, 3 }, Team::None) == "The Blue intel was taken");
    CHECK(EventLine(MatchEvent{ 1, EventKind::Returned, Team::Blue, 0 }, Team::None) == "The Blue intel was returned");
}

TEST_CASE("Scoreboard rows: Blue first, then by captures and kills, local marked")
{
    CtfState s;
    s.Players = {
        PlayerStats{ 1, Team::Green, 5, 0, 0 },
        PlayerStats{ 2, Team::Blue, 1, 2, 0 },
        PlayerStats{ 3, Team::Blue, 0, 0, 1 },
    };
    const std::vector<ScoreRow> rows = ScoreboardRows(s, 2);
    REQUIRE(rows.size() == 3);
    CHECK(rows[0].Player == 3);   //Blue, one capture
    CHECK(rows[1].Player == 2);
    CHECK(rows[1].Local);
    CHECK(rows[2].Player == 1);
}

TEST_CASE("A team model changes only the torso colour")
{
    VoxModel player;
    player.Size = { 2, 2, 2 };
    player.Voxels.assign(8, 1);
    player.Colors[1] = glm::vec4(0.1f);
    player.Colors[2] = glm::vec4(0.2f);
    player.Colors[3] = glm::vec4(0.3f);

    const VoxModel blue = TeamModel(player, Team::Blue);
    CHECK(blue.Colors[1] == player.Colors[1]);
    CHECK(blue.Colors[3] == player.Colors[3]);
    CHECK(blue.Colors[2] == TeamColour(Team::Blue));
    CHECK(blue.Voxels == player.Voxels);
}

TEST_CASE("The intel is a solid cube in its team's colour")
{
    const VoxModel intel = IntelModel(Team::Green);
    CHECK(intel.Size == glm::ivec3(6, 6, 6));
    CHECK(intel.Voxels.size() == 216);
    CHECK(intel.Colors[1] == TeamColour(Team::Green));
}
