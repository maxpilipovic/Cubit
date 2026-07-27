#pragma once

#include "Cubit/Core.h"

#include <cstdint>

class World;

//Sky lighting for a world: light falls from the open sky, spreads through air,
//and dims with distance. Pure computation over a World's light grid — it makes
//no GL calls and knows nothing about the renderer.
class CB_API SkyLight
{
public:
    SkyLight() = delete;

    //The brightness of open sky, and the maximum any cell can hold.
    static constexpr std::uint8_t Max = 15;

    //Floods sky light through the whole world, discarding whatever was there.
    //Called once after a map loads. Marks nothing dirty: a freshly loaded world
    //already has every chunk dirty.
    static void PropagateAll(World& world);
};
