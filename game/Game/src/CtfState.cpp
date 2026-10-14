#include "CtfState.h"

#include "Cubit/Net/ByteReader.h"
#include "Cubit/Net/ByteWriter.h"

#include <algorithm>

using namespace CubitGame;

const PlayerStats* CtfState::Find(PlayerId player) const
{
    for (const PlayerStats& stats : Players)
        if (stats.Player == player)
            return &stats;
    return nullptr;
}

PlayerStats* CtfState::Find(PlayerId player)
{
    for (PlayerStats& stats : Players)
        if (stats.Player == player)
            return &stats;
    return nullptr;
}

int CtfState::TeamSize(Team team) const
{
    return static_cast<int>(std::count_if(Players.begin(), Players.end(),
        [team](const PlayerStats& stats) { return stats.Side == team; }));
}

std::vector<std::uint8_t> CubitGame::Encode(const CtfState& state)
{
    ByteWriter out;
    out.U8(CtfStateVersion);
    out.U8(static_cast<std::uint8_t>(state.Stage));
    out.U64(state.StageEndTick);
    out.U8(state.Captures[0]);
    out.U8(state.Captures[1]);
    out.U8(static_cast<std::uint8_t>(state.Winner));

    out.U16(static_cast<std::uint16_t>(state.Players.size()));
    for (const PlayerStats& stats : state.Players)
    {
        out.U16(stats.Player);
        out.U8(static_cast<std::uint8_t>(stats.Side));
        out.U16(stats.Kills);
        out.U16(stats.Deaths);
        out.U16(stats.Captures);
        out.U64(stats.RespawnTick);
    }

    for (const Intel& intel : state.Intels)
    {
        out.U8(static_cast<std::uint8_t>(intel.Status));
        out.U16(intel.Carrier);
        out.Vec3(intel.Position);
        out.Vec3(intel.HomeSpot);
    }

    out.U8(static_cast<std::uint8_t>(state.Events.size()));
    for (const MatchEvent& event : state.Events)
    {
        out.U64(event.Serial);
        out.U8(static_cast<std::uint8_t>(event.Kind));
        out.U8(static_cast<std::uint8_t>(event.Side));
        out.U16(event.Player);
    }

    return out.Bytes();
}

std::optional<CtfState> CubitGame::DecodeCtfState(std::span<const std::uint8_t> bytes)
{
    ByteReader in(bytes);
    CtfState state;

    if (in.U8() != CtfStateVersion)
        return std::nullopt;

    const std::uint8_t stage = in.U8();
    if (stage > 3)
        return std::nullopt;
    state.Stage = static_cast<Phase>(stage);
    state.StageEndTick = in.U64();
    state.Captures[0] = in.U8();
    state.Captures[1] = in.U8();

    const std::uint8_t winner = in.U8();
    if (winner > 2)
        return std::nullopt;
    state.Winner = static_cast<Team>(winner);

    //Every count below is checked against Ok() as it is used: a count off the
    //wire never backs an allocation, only a loop that stops at the first failed read.
    const std::uint16_t playerCount = in.U16();
    for (std::uint16_t i = 0; i < playerCount && in.Ok(); ++i)
    {
        PlayerStats stats;
        stats.Player = in.U16();
        const std::uint8_t side = in.U8();
        if (side > 2)
            return std::nullopt;
        stats.Side = static_cast<Team>(side);
        stats.Kills = in.U16();
        stats.Deaths = in.U16();
        stats.Captures = in.U16();
        stats.RespawnTick = in.U64();
        if (in.Ok())
            state.Players.push_back(stats);
    }

    for (Intel& intel : state.Intels)
    {
        const std::uint8_t status = in.U8();
        if (status > 2)
            return std::nullopt;
        intel.Status = static_cast<IntelStatus>(status);
        intel.Carrier = in.U16();
        intel.Position = in.Vec3();
        intel.HomeSpot = in.Vec3();
    }

    const std::uint8_t eventCount = in.U8();
    for (std::uint8_t i = 0; i < eventCount && in.Ok(); ++i)
    {
        MatchEvent event;
        event.Serial = in.U64();
        const std::uint8_t kind = in.U8();
        if (kind == 0 || kind > 6)
            return std::nullopt;
        event.Kind = static_cast<EventKind>(kind);
        const std::uint8_t side = in.U8();
        if (side > 2)
            return std::nullopt;
        event.Side = static_cast<Team>(side);
        event.Player = in.U16();
        if (in.Ok())
            state.Events.push_back(event);
    }

    if (!in.Ok() || in.Remaining() != 0)
        return std::nullopt;

    return state;
}
