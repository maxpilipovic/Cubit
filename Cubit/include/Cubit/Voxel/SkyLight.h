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

    //Settles the light around an edit already made at this position, and marks
    //every chunk whose light actually changed dirty. Works outward from the
    //edit and stops where the light stops moving, so the cost follows what the
    //edit disturbed rather than the size of the region it might have.
    //
    //Call it after the block has been set: a cell that has just been filled in
    //still holds the light it had while it was open, which is what tells this
    //how much light the new block cut off.
    static void Repropagate(World& world, int x, int y, int z);
};
