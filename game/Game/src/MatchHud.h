#pragma once

#include "CtfRules.h"
#include "CtfState.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

//Cubit's own game: the match as words and rows. Pure, so it needs no graphics.
namespace CubitGame
{
    namespace MatchHudDetail
    {
        //Whole seconds left, rounded up. Zero at and past the end tick, so the
        //unsigned subtraction never wraps.
        inline std::uint64_t SecondsUntil(std::uint64_t endTick, std::uint64_t serverTick)
        {
            if (serverTick >= endTick)
                return 0;
            return (endTick - serverTick + TicksPerSecond - 1) / TicksPerSecond;
        }

        inline const char* UpperName(Team team) { return team == Team::Green ? "GREEN" : "BLUE"; }
        inline const char* Name(Team team) { return team == Team::Green ? "Green" : "Blue"; }
    }

    inline std::string ScoreLine(const CtfState& state)
    {
        return "BLUE " + std::to_string(state.Captures[0]) + " - " +
            std::to_string(state.Captures[1]) + " GREEN";
    }

    inline std::string PhaseLine(const CtfState& state, std::uint64_t serverTick)
    {
        switch (state.Stage)
        {
        case Phase::Warmup:
            return "WARMUP";
        case Phase::Countdown:
            return "STARTS IN " + std::to_string(MatchHudDetail::SecondsUntil(state.StageEndTick, serverTick));
        case Phase::Active:
        {
            const std::uint64_t seconds = MatchHudDetail::SecondsUntil(state.StageEndTick, serverTick);
            const std::uint64_t rest = seconds % 60;
            return std::to_string(seconds / 60) + ":" + (rest < 10 ? "0" : "") + std::to_string(rest);
        }
        case Phase::Ended:
            if (state.Winner == Team::None)
                return "DRAW";
            return std::string(MatchHudDetail::UpperName(state.Winner)) + " WINS";
        }
        return "";
    }

    //Empty once the respawn tick is reached: the player is no longer waiting.
    inline std::string RespawnLine(const PlayerStats& player, std::uint64_t serverTick)
    {
        if (player.RespawnTick == 0 || serverTick >= player.RespawnTick)
            return "";
        return "RESPAWN IN " + std::to_string(MatchHudDetail::SecondsUntil(player.RespawnTick, serverTick));
    }

    //`local` is the viewer's team. Team::None gets the named-team wording.
    inline std::string EventLine(const MatchEvent& event, Team local)
    {
        const bool mine = local != Team::None && event.Side == local;
        const std::string side = MatchHudDetail::Name(event.Side);

        switch (event.Kind)
        {
        case EventKind::MatchStarted:
            return "The match has started";
        case EventKind::PickedUp:
            if (mine)
                return "Your intel was taken";
            if (local == Team::None)
                return "The " + side + " intel was taken";
            return "Your team took the " + side + " intel";
        case EventKind::Captured:
            return (mine ? std::string("Your team") : side) + " captured the intel";
        case EventKind::Returned:
            if (mine)
                return "Your intel was returned";
            return "The " + side + " intel was returned";
        case EventKind::MatchWon:
            return (mine ? std::string("Your team") : side) + " won the match";
        case EventKind::Draw:
            return "The match is a draw";
        }
        return "";
    }

    struct ScoreRow
    {
        PlayerId Player = InvalidPlayer;
        Team Side = Team::None;
        std::uint16_t Kills = 0;
        std::uint16_t Deaths = 0;
        std::uint16_t Captures = 0;
        bool Local = false;
    };

    //Blue first, then the most captures, then the most kills, then the lowest id.
    inline std::vector<ScoreRow> ScoreboardRows(const CtfState& state, PlayerId local)
    {
        std::vector<ScoreRow> rows;
        rows.reserve(state.Players.size());
        for (const PlayerStats& p : state.Players)
            rows.push_back(ScoreRow{ p.Player, p.Side, p.Kills, p.Deaths, p.Captures, p.Player == local });

        std::sort(rows.begin(), rows.end(), [](const ScoreRow& a, const ScoreRow& b)
        {
            if (a.Side != b.Side)
                return a.Side < b.Side;
            if (a.Captures != b.Captures)
                return a.Captures > b.Captures;
            if (a.Kills != b.Kills)
                return a.Kills > b.Kills;
            return a.Player < b.Player;
        });
        return rows;
    }
}
