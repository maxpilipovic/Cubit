#include <doctest.h>

#include "DeathAnnouncer.h"

#include <deque>
#include <vector>

namespace
{
    //A ruling the server might send. Defaults to a clean miss, so each case
    //below says only what it is actually about.
    MatchClient::ShotReport Ruling(bool killed, PlayerId victim, PlayerId shooter,
        std::uint64_t serial)
    {
        MatchClient::ShotReport shot;
        shot.Shooter = shooter;
        shot.Victim = victim;
        shot.Killed = killed;
        shot.Serial = serial;

        return shot;
    }
}

TEST_CASE("A shot that killed nobody announces nothing")
{
    CubitGame::DeathAnnouncer announcer;

    //A hit that left the victim standing, and a clean miss. Neither is a death.
    const std::deque<MatchClient::ShotReport> recent{
        Ruling(false, 2, 1, 1), Ruling(false, InvalidPlayer, 1, 2) };

    CHECK(announcer.Observe(recent).empty());
}

TEST_CASE("A killing shot announces the victim and the killer")
{
    CubitGame::DeathAnnouncer announcer;

    const std::vector<CubitGame::PlayerDiedEvent> died =
        announcer.Observe({ Ruling(true, 2, 1, 1) });

    REQUIRE(died.size() == 1);
    //The victim is who died, not who fired. Getting these the wrong way round
    //would read as the killer dying, which no test of a bool would catch.
    CHECK(died[0].Player == 2);
    CHECK(died[0].Killer == 1);
}

TEST_CASE("The same ruling is announced once, however many frames it is held for")
{
    CubitGame::DeathAnnouncer announcer;

    //RecentShots keeps returning this ruling until newer ones push it out. This
    //is the whole reason the announcer exists.
    const std::deque<MatchClient::ShotReport> recent{ Ruling(true, 2, 1, 1) };

    CHECK(announcer.Observe(recent).size() == 1);
    CHECK(announcer.Observe(recent).empty());
    CHECK(announcer.Observe(recent).empty());
}

TEST_CASE("A later death is announced, even after an earlier one")
{
    CubitGame::DeathAnnouncer announcer;

    std::deque<MatchClient::ShotReport> recent{ Ruling(true, 2, 1, 1) };
    CHECK(announcer.Observe(recent).size() == 1);

    recent.push_back(Ruling(true, 1, 2, 2));
    const std::vector<CubitGame::PlayerDiedEvent> second = announcer.Observe(recent);

    REQUIRE(second.size() == 1);
    CHECK(second[0].Player == 1);
    CHECK(second[0].Killer == 2);
}

TEST_CASE("Two deaths arriving together are both announced")
{
    //Both rulings drained in one step. The old announcer, keyed on the tick a
    //ruling arrived on and reading only the newest, announced one of them.
    CubitGame::DeathAnnouncer announcer;

    const std::vector<CubitGame::PlayerDiedEvent> died = announcer.Observe({
        Ruling(true, 2, 1, 1), Ruling(false, 3, 1, 2), Ruling(true, 3, 4, 3) });

    REQUIRE(died.size() == 2);
    CHECK(died[0].Player == 2);
    CHECK(died[1].Player == 3);
    CHECK(died[1].Killer == 4);
}
