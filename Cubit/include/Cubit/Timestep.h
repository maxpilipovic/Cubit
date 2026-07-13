#pragma once

#include "Cubit/Core.h"

class CB_API Timestep
{
public:
    //Stores a frame duration expressed in seconds.
    explicit constexpr Timestep(double seconds = 0.0)
        : m_Seconds(seconds)
    {
    }

    //Returns the frame duration in seconds.
    constexpr double GetSeconds() const { return m_Seconds; }

    //Returns the frame duration in milliseconds.
    constexpr double GetMilliseconds() const { return m_Seconds * 1000.0; }

private:
    double m_Seconds;
};
