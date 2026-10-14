#pragma once

#include "CtfRules.h"
#include "CtfState.h"

#include "Cubit/Net/GameMode.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <map>

namespace CubitGame
{
    class CtfMode final : public GameMode
    {
    public:
        CtfMode(const CtfRules& rules, const glm::ivec3& mapSize, std::uint32_t seed);

        void OnJoin(GameModeHost& host, PlayerId player) override;
        void OnLeave(GameModeHost& host, PlayerId player) override;
        bool CanDamage(const GameModeHost& host, PlayerId shooter, PlayerId victim) const override;
        void OnKilled(GameModeHost& host, PlayerId victim, PlayerId killer) override;
        void OnCommand(GameModeHost& host, PlayerId player, std::span<const std::uint8_t> bytes) override;
        void Step(GameModeHost& host) override;
        bool TakeStateChanged() override;
        std::vector<std::uint8_t> EncodeState() const override;

        const CtfState& State() const { return m_State; }

    private:
        Team SmallerTeam();
        void Spawn(GameModeHost& host, PlayerId player);
        void QueueRespawn(GameModeHost& host, PlayerId player);
        void StartMatch(GameModeHost& host);
        void EndMatch(GameModeHost& host, Team winner);
        void ResetMatch(GameModeHost& host);
        void Record(EventKind kind, Team side, PlayerId player);
        void PlaceIntelsHome(const World& world);
        void DropIntelOf(GameModeHost& host, PlayerId player);
        void Changed() { m_Changed = true; }

        CtfRules m_Rules;
        glm::ivec3 m_MapSize;
        std::array<glm::ivec2, 2> m_Bases;
        std::uint32_t m_Random;
        CtfState m_State;
        bool m_Changed = false;

        //Server-only: never sent.
        std::map<PlayerId, std::uint64_t> m_ProtectedUntil;
    };
}
