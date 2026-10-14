#pragma once

#include "Cubit/Voxel/MatchState.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <vector>

//Cubit's own game: the match as the server rules it, and as every client reads it.
namespace CubitGame
{
    enum class Team : std::uint8_t { None = 0, Blue = 1, Green = 2 };

    //0 for Blue, 1 for Green. Team::None has no index.
    inline int IndexOf(Team team) { return team == Team::Green ? 1 : 0; }
    inline Team TeamAt(int index) { return index == 1 ? Team::Green : Team::Blue; }
    inline Team Other(Team team) { return team == Team::Blue ? Team::Green : Team::Blue; }

    enum class Phase : std::uint8_t { Warmup = 0, Countdown = 1, Active = 2, Ended = 3 };

    enum class IntelStatus : std::uint8_t { Home = 0, Carried = 1, Dropped = 2 };

    enum class EventKind : std::uint8_t
    {
        MatchStarted = 1, PickedUp = 2, Captured = 3, Returned = 4, MatchWon = 5, Draw = 6
    };

    struct PlayerStats
    {
        PlayerId Player = InvalidPlayer;
        Team Side = Team::None;
        std::uint16_t Kills = 0;
        std::uint16_t Deaths = 0;
        std::uint16_t Captures = 0;

        //The server tick this player respawns at, or 0 when not waiting to.
        std::uint64_t RespawnTick = 0;
    };

    //One team's intel. `Owner` is the team it belongs to.
    struct Intel
    {
        IntelStatus Status = IntelStatus::Home;
        PlayerId Carrier = InvalidPlayer;
        glm::vec3 Position{ 0.0f };   //bottom centre, in world space
        glm::vec3 HomeSpot{ 0.0f };
    };

    struct MatchEvent
    {
        std::uint64_t Serial = 0;     //counts up from 1; NewEvents reads by it
        EventKind Kind = EventKind::MatchStarted;
        Team Side = Team::None;       //the team the event is about (who scored, whose intel)
        PlayerId Player = InvalidPlayer;
    };

    struct CtfState
    {
        Phase Stage = Phase::Warmup;
        std::uint64_t StageEndTick = 0;   //0 when the stage has no end
        std::array<std::uint8_t, 2> Captures{};
        Team Winner = Team::None;         //set in Ended; None there means a draw
        std::vector<PlayerStats> Players;
        std::array<Intel, 2> Intels{};    //indexed by IndexOf(owner)
        std::deque<MatchEvent> Events;    //oldest first

        const PlayerStats* Find(PlayerId player) const;
        PlayerStats* Find(PlayerId player);
        int TeamSize(Team team) const;
    };

    //First byte is this format's version, so an old client refuses a new layout
    //rather than misreading it.
    constexpr std::uint8_t CtfStateVersion = 1;

    std::vector<std::uint8_t> Encode(const CtfState& state);
    std::optional<CtfState> DecodeCtfState(std::span<const std::uint8_t> bytes);

    //The one command a client sends.
    constexpr std::uint8_t SwitchTeamCommand = 1;
}
