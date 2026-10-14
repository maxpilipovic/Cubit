#include "CtfMode.h"

#include "Cubit/Logger.h"
#include "Cubit/Voxel/CharacterController.h"
#include "Cubit/Voxel/SpawnFinder.h"
#include "Cubit/Voxel/TerrainGen.h"

#include <algorithm>
#include <utility>

namespace CubitGame
{
    CtfMode::CtfMode(const CtfRules& rules, const glm::ivec3& mapSize, std::uint32_t seed)
        : m_Rules(rules), m_MapSize(mapSize), m_Bases(TerrainGen::FortCentres(mapSize)),
          m_Random(seed * 2654435761u + 1u)
    {
    }

    Team CtfMode::SmallerTeam()
    {
        const int blue = m_State.TeamSize(Team::Blue);
        const int green = m_State.TeamSize(Team::Green);
        if (blue != green)
            return blue < green ? Team::Blue : Team::Green;

        //xorshift: the same seed balances the same way on every build.
        m_Random ^= m_Random << 13;
        m_Random ^= m_Random >> 17;
        m_Random ^= m_Random << 5;
        return (m_Random & 1u) ? Team::Green : Team::Blue;
    }

    void CtfMode::OnJoin(GameModeHost& host, PlayerId player)
    {
        if (m_State.Players.empty())
            PlaceIntelsHome(host.Match().GetWorld());

        m_State.Players.push_back(PlayerStats{ player, SmallerTeam() });
        Spawn(host, player);
        Changed();
    }

    void CtfMode::OnLeave(GameModeHost&, PlayerId player)
    {
        std::erase_if(m_State.Players, [player](const PlayerStats& s) { return s.Player == player; });
        m_ProtectedUntil.erase(player);
        Changed();
    }

    bool CtfMode::CanDamage(const GameModeHost& host, PlayerId shooter, PlayerId victim) const
    {
        const PlayerStats* a = m_State.Find(shooter);
        const PlayerStats* b = m_State.Find(victim);
        if (a == nullptr || b == nullptr || a->Side == b->Side)
            return false;

        const auto protectedUntil = m_ProtectedUntil.find(victim);
        return protectedUntil == m_ProtectedUntil.end() || host.Tick() >= protectedUntil->second;
    }

    void CtfMode::OnKilled(GameModeHost& host, PlayerId victim, PlayerId killer)
    {
        if (m_State.Stage == Phase::Active)
        {
            if (PlayerStats* k = m_State.Find(killer)) ++k->Kills;
            if (PlayerStats* v = m_State.Find(victim)) ++v->Deaths;
        }
        DropIntelOf(host, victim);
        QueueRespawn(host, victim);
        Changed();
    }

    void CtfMode::OnCommand(GameModeHost& host, PlayerId player, std::span<const std::uint8_t> bytes)
    {
        PlayerStats* stats = m_State.Find(player);
        if (stats == nullptr || bytes.empty() || bytes[0] != SwitchTeamCommand)
            return;

        //After the move the new team has `after` and the old one `left`; the
        //switch may not leave them more than one apart. 1 v 1 gives 2 v 0:
        //refused. 2 v 1, moving off the bigger side, gives 1 v 2: allowed.
        const Team to = Other(stats->Side);
        const int after = m_State.TeamSize(to) + 1;
        const int left = m_State.TeamSize(stats->Side) - 1;
        if (after - left > 1)
            return;

        stats->Side = to;
        host.Kill(player);
        QueueRespawn(host, player);
        Changed();
    }

    void CtfMode::QueueRespawn(GameModeHost& host, PlayerId player)
    {
        if (PlayerStats* stats = m_State.Find(player))
            stats->RespawnTick = host.Tick() + m_Rules.RespawnDelayTicks;
    }

    void CtfMode::Spawn(GameModeHost& host, PlayerId player)
    {
        PlayerStats* stats = m_State.Find(player);
        if (stats == nullptr)
            return;

        const World& world = host.Match().GetWorld();
        const glm::vec3 half = CharacterConfig{}.HalfExtents;
        const glm::ivec2 base = m_Bases[IndexOf(stats->Side)];

        std::optional<glm::vec3> at = FindSpawn(world, base, half);
        if (!at.has_value())
        {
            CB_WARN("No spawn near the base at " + std::to_string(base.x) + "," + std::to_string(base.y)
                + "; trying the map centre");
            at = FindSpawn(world, glm::ivec2(m_MapSize.x / 2, m_MapSize.z / 2), half);
        }

        //Never leave a player unspawned: the top of the centre column is still
        //somewhere to stand, if a bad one.
        const glm::vec3 position = at.value_or(glm::vec3(m_MapSize.x / 2 + 0.5f,
            static_cast<float>(m_MapSize.y) + half.y, m_MapSize.z / 2 + 0.5f));

        host.Respawn(player, position);
        stats->RespawnTick = 0;
        m_ProtectedUntil[player] = host.Tick() + m_Rules.SpawnProtectionTicks;
    }

    void CtfMode::DropIntelOf(GameModeHost&, PlayerId)
    {
        //Filled in with the intel itself.
    }

    void CtfMode::Record(EventKind kind, Team side, PlayerId player)
    {
        const std::uint64_t serial = m_State.Events.empty() ? 1 : m_State.Events.back().Serial + 1;
        m_State.Events.push_back(MatchEvent{ serial, kind, side, player });
        while (m_State.Events.size() > m_Rules.EventHistory)
            m_State.Events.pop_front();
        Changed();
    }

    void CtfMode::PlaceIntelsHome(const World& world)
    {
        for (int i = 0; i < 2; ++i)
        {
            const glm::ivec2 base = m_Bases[i];
            int top = 0;
            for (int y = world.GetHeight() - 1; y >= 0; --y)
                if (world.IsBlockSolid(base.x, y, base.y)) { top = y + 1; break; }

            Intel& intel = m_State.Intels[i];
            intel.HomeSpot = glm::vec3(base.x + 0.5f, static_cast<float>(top), base.y + 0.5f);
            intel.Position = intel.HomeSpot;
            intel.Status = IntelStatus::Home;
            intel.Carrier = InvalidPlayer;
        }
    }

    void CtfMode::StartMatch(GameModeHost& host)
    {
        m_State.Stage = Phase::Active;
        m_State.StageEndTick = host.Tick() + m_Rules.TimeLimitTicks;
        m_State.Captures = { 0, 0 };
        m_State.Winner = Team::None;
        for (PlayerStats& stats : m_State.Players)
            stats.Kills = stats.Deaths = stats.Captures = 0;
        PlaceIntelsHome(host.Match().GetWorld());
        Record(EventKind::MatchStarted, Team::None, InvalidPlayer);
    }

    void CtfMode::EndMatch(GameModeHost& host, Team winner)
    {
        m_State.Stage = Phase::Ended;
        m_State.StageEndTick = host.Tick() + m_Rules.EndScreenTicks;
        m_State.Winner = winner;
        Record(winner == Team::None ? EventKind::Draw : EventKind::MatchWon, winner, InvalidPlayer);
    }

    void CtfMode::ResetMatch(GameModeHost& host)
    {
        host.ResetWorld();
        m_State.Captures = { 0, 0 };
        m_State.Winner = Team::None;
        for (PlayerStats& stats : m_State.Players)
        {
            stats.Kills = stats.Deaths = stats.Captures = 0;
            host.Kill(stats.Player);
            Spawn(host, stats.Player);
        }
        PlaceIntelsHome(host.Match().GetWorld());
        m_State.Stage = Phase::Warmup;
        m_State.StageEndTick = 0;
        Changed();
    }

    void CtfMode::Step(GameModeHost& host)
    {
        const std::uint64_t now = host.Tick();
        const bool bothTeams = m_State.TeamSize(Team::Blue) > 0 && m_State.TeamSize(Team::Green) > 0;

        switch (m_State.Stage)
        {
        case Phase::Warmup:
            if (bothTeams)
            {
                m_State.Stage = Phase::Countdown;
                m_State.StageEndTick = now + m_Rules.CountdownTicks;
                Changed();
            }
            break;

        case Phase::Countdown:
            if (!bothTeams)
            {
                m_State.Stage = Phase::Warmup;
                m_State.StageEndTick = 0;
                Changed();
            }
            else if (now >= m_State.StageEndTick)
                StartMatch(host);
            break;

        case Phase::Active:
            if (now >= m_State.StageEndTick)
            {
                const auto [blue, green] = std::pair{ m_State.Captures[0], m_State.Captures[1] };
                EndMatch(host, blue == green ? Team::None : (blue > green ? Team::Blue : Team::Green));
            }
            break;

        case Phase::Ended:
            if (now >= m_State.StageEndTick)
                ResetMatch(host);
            break;
        }

        for (PlayerStats& stats : m_State.Players)
        {
            if (stats.RespawnTick != 0 && host.Tick() >= stats.RespawnTick)
            {
                Spawn(host, stats.Player);
                Changed();
            }
        }
    }

    bool CtfMode::TakeStateChanged()
    {
        const bool was = m_Changed;
        m_Changed = false;
        return was;
    }

    std::vector<std::uint8_t> CtfMode::EncodeState() const
    {
        return Encode(m_State);
    }
}
