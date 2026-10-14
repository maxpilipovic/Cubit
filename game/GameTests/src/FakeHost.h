#pragma once

#include "Cubit/Net/GameMode.h"
#include "Cubit/Voxel/MatchState.h"

#include <glm/glm.hpp>

#include <utility>

//A GameModeHost with no network: a real MatchState, a tick the test sets, and
//a count of the calls that have no state of their own to check.
class FakeHost final : public GameModeHost
{
public:
    explicit FakeHost(World world) : m_Match(std::move(world)) {}

    std::uint64_t Now = 1;
    int Resets = 0;

    std::uint64_t Tick() const override { return Now; }
    const MatchState& Match() const override { return m_Match; }
    bool IsAlive(PlayerId player) const override { return m_Match.IsAlive(player); }

    void Respawn(PlayerId player, const glm::vec3& position) override
    {
        m_Match.TeleportPlayer(player, position);
        m_Match.SetAlive(player, true);
    }

    void Kill(PlayerId player) override { m_Match.SetAlive(player, false); }
    void ResetWorld() override { ++Resets; }

    //What the server does on a join: add the player dead, then ask the mode.
    PlayerId Join(GameMode& mode)
    {
        const PlayerId player = m_Match.AddPlayer(glm::vec3(0.0f));
        m_Match.SetAlive(player, false);
        mode.OnJoin(*this, player);
        return player;
    }

    void Leave(GameMode& mode, PlayerId player)
    {
        mode.OnLeave(*this, player);
        m_Match.RemovePlayer(player);
    }

    //What the server does on a ruled kill.
    void RuleKill(GameMode& mode, PlayerId victim, PlayerId killer)
    {
        m_Match.SetAlive(victim, false);
        mode.OnKilled(*this, victim, killer);
    }

    void Place(PlayerId player, const glm::vec3& centre) { m_Match.TeleportPlayer(player, centre); }

    void Run(GameMode& mode, std::uint64_t ticks)
    {
        for (std::uint64_t i = 0; i < ticks; ++i)
        {
            ++Now;
            mode.Step(*this);
        }
    }

    World& Terrain() { return m_Match.GetWorld(); }

private:
    MatchState m_Match;
};
