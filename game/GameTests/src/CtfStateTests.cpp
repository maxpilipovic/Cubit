#include <doctest.h>

#include "CtfState.h"

using namespace CubitGame;

namespace
{
    CtfState Sample()
    {
        CtfState state;
        state.Stage = Phase::Active;
        state.StageEndTick = 123456;
        state.Captures = { 2, 1 };
        state.Winner = Team::None;
        state.Players.push_back(PlayerStats{ 3, Team::Blue, 4, 1, 2, 0 });
        state.Players.push_back(PlayerStats{ 7, Team::Green, 0, 5, 0, 999 });
        state.Intels[0] = Intel{ IntelStatus::Carried, 7, glm::vec3(1.5f, 2.0f, 3.5f), glm::vec3(8.5f, 20.0f, 256.5f) };
        state.Intels[1] = Intel{ IntelStatus::Dropped, InvalidPlayer, glm::vec3(100.5f, 12.0f, 40.5f), glm::vec3(503.5f, 20.0f, 256.5f) };
        state.Events.push_back(MatchEvent{ 11, EventKind::Captured, Team::Blue, 3 });
        state.Events.push_back(MatchEvent{ 12, EventKind::PickedUp, Team::Blue, 7 });
        return state;
    }
}

TEST_CASE("The match state round-trips every field")
{
    const CtfState sent = Sample();
    const std::optional<CtfState> back = DecodeCtfState(Encode(sent));
    REQUIRE(back.has_value());

    CHECK(back->Stage == sent.Stage);
    CHECK(back->StageEndTick == sent.StageEndTick);
    CHECK(back->Captures == sent.Captures);
    CHECK(back->Winner == sent.Winner);
    REQUIRE(back->Players.size() == 2);
    CHECK(back->Players[1].Player == 7);
    CHECK(back->Players[1].Side == Team::Green);
    CHECK(back->Players[1].Deaths == 5);
    CHECK(back->Players[1].RespawnTick == 999);
    CHECK(back->Intels[0].Status == IntelStatus::Carried);
    CHECK(back->Intels[0].Carrier == 7);
    CHECK(back->Intels[1].Position == sent.Intels[1].Position);
    CHECK(back->Intels[1].HomeSpot == sent.Intels[1].HomeSpot);
    REQUIRE(back->Events.size() == 2);
    CHECK(back->Events[0].Serial == 11);
    CHECK(back->Events[0].Kind == EventKind::Captured);
    CHECK(back->Events[1].Player == 7);
}

TEST_CASE("A different version, a short buffer, or an unknown enum is refused")
{
    std::vector<std::uint8_t> bytes = Encode(Sample());

    std::vector<std::uint8_t> wrongVersion = bytes;
    wrongVersion[0] = CtfStateVersion + 1;
    CHECK_FALSE(DecodeCtfState(wrongVersion).has_value());

    for (std::size_t length = 0; length < bytes.size(); ++length)
        CHECK_FALSE(DecodeCtfState(std::span(bytes.data(), length)).has_value());

    std::vector<std::uint8_t> badPhase = bytes;
    badPhase[1] = 9;   //the phase byte follows the version
    CHECK_FALSE(DecodeCtfState(badPhase).has_value());
}

TEST_CASE("A full match's state stays well under the wire's cap")
{
    CtfState state = Sample();
    state.Players.clear();
    for (PlayerId p = 1; p <= 32; ++p)
        state.Players.push_back(PlayerStats{ p, Team::Blue, 999, 999, 99, 1 });
    for (std::uint64_t s = 1; s <= 8; ++s)
        state.Events.push_back(MatchEvent{ s, EventKind::PickedUp, Team::Green, 1 });

    CHECK(Encode(state).size() < 4096);
}

TEST_CASE("Finding a player and counting a team")
{
    CtfState state = Sample();
    REQUIRE(state.Find(3) != nullptr);
    CHECK(state.Find(3)->Kills == 4);
    CHECK(state.Find(99) == nullptr);
    CHECK(state.TeamSize(Team::Blue) == 1);
    CHECK(state.TeamSize(Team::Green) == 1);
}
