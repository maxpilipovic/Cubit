#pragma once

#include "Cubit/Core.h"
#include "Cubit/Timestep.h"

//Turns a variable wall-clock frame delta into a whole number of fixed
//simulation steps plus the fraction left over, so the simulation advances at a
//rate that does not depend on how long a frame took to draw.
class CB_API FrameClock
{
public:
    //Seconds each fixed simulation step advances the world by.
    static constexpr double FixedStepSeconds = 1.0 / 60.0;

    //Most steps one frame may run. Past this the surplus is discarded rather
    //than repaid, so a long stall skips wall-clock time instead of draining in
    //slow motion across the frames that follow.
    static constexpr int MaxTicksPerFrame = 5;

    //Absorbs one frame's wall-clock delta and returns how many fixed steps to
    //run before rendering.
    int Advance(double frameSeconds);

    //How far the accumulator sits between the last completed step and the next,
    //in [0, 1]. Rendering interpolates by this.
    float Alpha() const;

    //Whole steps the most recent Advance owed but did not run, because they were
    //past MaxTicksPerFrame. Zero for any frame inside the cap. A renderer can
    //ignore it; a server cannot, since every one is a tick its clients sent an
    //input for.
    int DiscardedTicks() const { return m_Discarded; }

    //The duration every fixed step advances the simulation by.
    static constexpr Timestep Step() { return Timestep(FixedStepSeconds); }

private:
    double m_Accumulator = 0.0;
    int m_Discarded = 0;
};
