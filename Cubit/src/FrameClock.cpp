#include "cub.h"

#include "Cubit/FrameClock.h"

int FrameClock::Advance(double frameSeconds)
{
    // A backwards or absent delta owes nothing. Subtracting it would hand back
    // time the simulation has already run.
    if (frameSeconds > 0.0)
        m_Accumulator += frameSeconds;

    int ticks = 0;
    while (m_Accumulator >= FixedStepSeconds && ticks < MaxTicksPerFrame)
    {
        m_Accumulator -= FixedStepSeconds;
        ++ticks;
    }

    // The cap stopped the loop with whole steps still owed. Drop them: repaying
    // them over the following frames would turn one stall into a longer, slower
    // one. Tested against the remainder rather than against the tick count, so a
    // frame that happens to owe exactly the cap keeps its legitimate fraction.
    if (m_Accumulator >= FixedStepSeconds)
        m_Accumulator = 0.0;

    return ticks;
}

float FrameClock::Alpha() const
{
    return static_cast<float>(m_Accumulator / FixedStepSeconds);
}
