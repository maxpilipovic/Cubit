#pragma once

#include <cstddef>
#include <cstdint>

//Cubit's own game: the capture-the-intel numbers. Tuning, not logic.
namespace CubitGame
{
    constexpr std::uint64_t TicksPerSecond = 60;

    struct CtfRules
    {
        int CapturesToWin = 3;
        std::uint64_t TimeLimitTicks = 20 * 60 * TicksPerSecond;
        std::uint64_t CountdownTicks = 10 * TicksPerSecond;
        std::uint64_t EndScreenTicks = 15 * TicksPerSecond;
        std::uint64_t RespawnDelayTicks = 5 * TicksPerSecond;
        std::uint64_t SpawnProtectionTicks = 2 * TicksPerSecond;
        std::uint64_t DroppedReturnTicks = 30 * TicksPerSecond;

        //How long everyone stays dead when the match starts or resets, before
        //they appear at base. A respawn on the tick of the kill shows no client
        //a dead snapshot, so each counts the move home as a correction. It has
        //to outlast a round trip plus the server's input queue: inputs sent
        //before a client saw itself dead reach the server after the respawn
        //and must find the player still dead. And the one dead snapshot is on
        //the unreliable channel, so several of them are needed, not one.
        std::uint64_t ResetHoldTicks = 1 * TicksPerSecond;

        //Metres from the intel to the nearest point of a player's box.
        float PickupRadius = 1.5f;

        //Around the carrier's own intel home spot, horizontally and vertically.
        float BaseRadius = 4.0f;

        std::size_t EventHistory = 8;
    };
}
