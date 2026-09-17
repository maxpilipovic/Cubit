#pragma once

#include <cstdint>
#include <string>

//How the game was launched. Default-constructed means single-player.
struct GameOptions
{
    bool Connect = false;
    std::string Host = "127.0.0.1";
    std::uint16_t Port = 27015;

    //Round-trip milliseconds. Halved into NetworkSim's one-way latency.
    double LatencyRtt = 0.0;
    float Loss = 0.0f;
};

namespace CubitGame
{
    //The map a single-player session loads, resolved against the working
    //directory - the executable's own, where the build puts a copy of the
    //game's assets.
    constexpr const char* MapPath = "assets/maps/battlefield512.vox";
}
