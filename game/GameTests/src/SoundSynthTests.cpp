#include <doctest.h>

#include "SoundSynth.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace
{
    float Peak(const SoundClip& clip)
    {
        float peak = 0.0f;
        for (float sample : clip.Samples)
            peak = std::max(peak, std::abs(sample));
        return peak;
    }
}

TEST_CASE("Every cue synthesises an audible clip within range")
{
    for (int i = 0; i < CubitGame::CueCount; ++i)
    {
        const auto cue = static_cast<CubitGame::Cue>(i);
        CAPTURE(std::string(CubitGame::CueName(cue)));

        const SoundClip clip = CubitGame::Synthesise(cue, 1);

        CHECK(clip.SampleRate == CubitGame::SynthRate);
        CHECK(clip.Seconds() > 0.02f);
        CHECK(clip.Seconds() < 1.0f);

        //Loud enough to hear, and never past full scale - a sample over 1 is
        //clipped by the device into a crackle.
        CHECK(Peak(clip) > 0.2f);
        CHECK(Peak(clip) <= 1.0f);

        //Faded out, so it does not end on a click.
        CHECK(std::abs(clip.Samples.back()) < 0.01f);

        for (float sample : clip.Samples)
            REQUIRE(std::isfinite(sample));
    }
}

TEST_CASE("The same seed makes the same sound")
{
    for (int i = 0; i < CubitGame::CueCount; ++i)
    {
        const auto cue = static_cast<CubitGame::Cue>(i);
        CAPTURE(std::string(CubitGame::CueName(cue)));

        CHECK(CubitGame::Synthesise(cue, 7).Samples == CubitGame::Synthesise(cue, 7).Samples);
    }
}

TEST_CASE("Footsteps from different seeds differ, so a walk is not one sound repeated")
{
    const SoundClip a = CubitGame::Synthesise(CubitGame::Cue::Footstep, 1);
    const SoundClip b = CubitGame::Synthesise(CubitGame::Cue::Footstep, 2);

    CHECK(a.Samples != b.Samples);
}

TEST_CASE("A gunshot is the loudest and longest-carrying of the short sounds")
{
    //Not a matter of taste: the gunshot is the cue a player must pick out of
    //everything else, so it may not come out quieter than a footstep.
    const float gunshot = Peak(CubitGame::Synthesise(CubitGame::Cue::Gunshot, 1));

    CHECK(gunshot > Peak(CubitGame::Synthesise(CubitGame::Cue::Footstep, 1)));
    CHECK(gunshot > Peak(CubitGame::Synthesise(CubitGame::Cue::Dig, 1)));
    CHECK(gunshot > Peak(CubitGame::Synthesise(CubitGame::Cue::HitConfirm, 1)));
}
