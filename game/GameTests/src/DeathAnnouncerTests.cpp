#include <doctest.h>

#include "DeathAnnouncer.h"

namespace
{
    //A ruling the server might send. Defaults to a clean miss, so each case
    //below says only what it is actually about.
    MatchClient::ShotReport Ruling(bool killed, PlayerId victim, PlayerId shooter,
        std::uint64_t receivedAtTick)
    {
        MatchClient::ShotReport shot;
        shot.Shooter = shooter;
        shot.Victim = victim;
        shot.Killed = killed;
        shot.ReceivedAtTick = receivedAtTick;

        return shot;
    }
}

TEST_CASE("A shot that killed nobody announces nothing")
{
    CubitGame::DeathAnnouncer announcer;

    //A hit that left the victim standing, and a clean miss. Neither is a death.
    CHECK_FALSE(announcer.Observe(Ruling(false, 2, 1, 40)).has_value());
    CHECK_FALSE(announcer.Observe(Ruling(false, InvalidPlayer, 1, 41)).has_value());
}

TEST_CASE("A killing shot announces the victim and the killer")
{
    CubitGame::DeathAnnouncer announcer;

    const std::optional<CubitGame::PlayerDiedEvent> died =
        announcer.Observe(Ruling(true, 2, 1, 40));

    REQUIRE(died.has_value());
    //The victim is who died, not who fired. Getting these the wrong way round
    //would read as the killer dying, which no test of a bool would catch.
    CHECK(died->Player == 2);
    CHECK(died->Killer == 1);
}

TEST_CASE("The same ruling is announced once, however many frames it is held for")
{
    CubitGame::DeathAnnouncer announcer;

    //LastShot keeps returning this ruling while its marker is on screen. This
    //is the whole reason the announcer exists.
    const MatchClient::ShotReport shot = Ruling(true, 2, 1, 40);

    CHECK(announcer.Observe(shot).has_value());
    CHECK_FALSE(announcer.Observe(shot).has_value());
    CHECK_FALSE(announcer.Observe(shot).has_value());
}

TEST_CASE("A later death is announced, even after an earlier one")
{
    CubitGame::DeathAnnouncer announcer;

    CHECK(announcer.Observe(Ruling(true, 2, 1, 40)).has_value());

    const std::optional<CubitGame::PlayerDiedEvent> second =
        announcer.Observe(Ruling(true, 1, 2, 55));

    REQUIRE(second.has_value());
    CHECK(second->Player == 1);
    CHECK(second->Killer == 2);
}

TEST_CASE("A death on tick zero is announced")
{
    CubitGame::DeathAnnouncer announcer;

    //Tick zero is a real tick. An announcer that remembered "none yet" as zero
    //would swallow this one, and it would only ever show up as a missing kill
    //message in the first moments of a match.
    CHECK(announcer.Observe(Ruling(true, 2, 1, 0)).has_value());
}
