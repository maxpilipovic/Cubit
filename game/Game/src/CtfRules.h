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

        //Metres from the intel to the nearest point of a player's box.
        float PickupRadius = 1.5f;

        //Around the carrier's own intel home spot, horizontally and vertically.
        float BaseRadius = 4.0f;

        std::size_t EventHistory = 8;
    };
}
