#pragma once

#include <cstdint>
#include <vector>

//A sound, as samples. The engine ships none: the game makes its own and the
//harness makes a test tone, both in code, the way the debug font is.
//
//Mono, because every sound is either positioned in the world - where the
//spatialiser makes the stereo - or a UI sound, where mono centred is right.
struct SoundClip
{
    //One sample per frame, in -1..1.
    std::vector<float> Samples;
    std::uint32_t SampleRate = 48000;

    float Seconds() const
    {
        return SampleRate == 0 ? 0.0f
            : static_cast<float>(Samples.size()) / static_cast<float>(SampleRate);
    }
};
