#pragma once

#include "Cubit/Core.h"

class CB_API Timestep
{
public:
    explicit constexpr Timestep(double seconds = 0.0)
        : m_Seconds(seconds)
    {
    }

    [[nodiscard]] constexpr double GetSeconds() const { return m_Seconds; }
    [[nodiscard]] constexpr double GetMilliseconds() const { return m_Seconds * 1000.0; }

private:
    double m_Seconds;
};
