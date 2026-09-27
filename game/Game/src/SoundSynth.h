#pragma once

#include "Cubit/Audio/SoundClip.h"

#include <cstdint>

//Cubit's own game: its sounds, made in code.
//
//Placeholders on purpose. There are no sound files, so there is nothing to
//license and nothing to author; the game makes every clip at startup from a
//seed, the way it makes its map. A real sound replaces one of these without
//anything that plays it noticing.
namespace CubitGame
{
    enum class Cue
    {
        Gunshot,
        Impact,
        Dig,
        Place,
        Crumble,
        Footstep,
        HitConfirm,
        KillConfirm
    };

    constexpr int CueCount = 8;

    //How many different footsteps a walk cycles through.
    constexpr int FootstepVariants = 4;

    constexpr std::uint32_t SynthRate = 48000;

    const char* CueName(Cue cue);

    //The same cue and seed always make the same samples.
    SoundClip Synthesise(Cue cue, std::uint32_t seed);
}
