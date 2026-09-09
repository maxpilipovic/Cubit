#pragma once

#include "Cubit/Core.h"
#include "Cubit/Voxel/MatchState.h"

#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

//An axis-aligned box in world space.
struct Aabb
{
    glm::vec3 Min{ 0.0f };
    glm::vec3 Max{ 0.0f };
};

//How many past positions are kept per player.
//
//MaxRewindTicks of window plus one more, because a fractional instant needs a
//sample on either side of it to lerp between. Fifteen alone would leave the
//oldest reachable instant with nothing below it to bracket against.
constexpr std::size_t MaxHistorySamples = 16;

//How far back a shot may be resolved, in ticks. Fifteen is 250 ms at 60 Hz.
//
//This is the "shot behind cover" window: a target who reached safety within
//this many ticks can still be hit by someone whose screen had not caught up.
//Chosen to cover the 150 ms link this arc targets with room for jitter.
constexpr int MaxRewindTicks = 15;

//Where everybody has recently been, so a shot can be resolved against the world
//as the shooter saw it rather than as it is now.
//
//Holds no networking and no GL, which is the same rule that keeps MatchState in
//Voxel/ - and it is what lets this be tested with no server, no transport and
//no socket.
//
//Positions only. A hitbox is HalfExtents around a position; neither velocity nor
//grounded shapes it, and keeping a whole CharacterController here would invite
//somebody to rewind physics rather than geometry.
class CB_API HitboxHistory
{
public:
    //Appends this player's position for a tick, evicting the oldest sample once
    //the ring is full. Ticks are expected to arrive in increasing order, which
    //is what a server stepping once per tick produces.
    void Record(PlayerId player, std::uint64_t tick, const glm::vec3& position);

    //Drops everything known about a player. Called on respawn and on
    //disconnect - on respawn because a shot must never rewind across a death
    //and damage whoever now stands where the dead player did.
    void Forget(PlayerId player);

    //Rebuilds this player's box at a fractional instant, lerping between the
    //two bracketing samples exactly as MatchClient::PoseOf does when it draws
    //them.
    //
    //Returns false when there is no record of this player at that instant -
    //either they are unknown, or the instant predates their oldest sample. That
    //is deliberately not a hit of zero size: "no record" and "recorded, and the
    //ray missed" are different answers, and a caller must not confuse them.
    //
    //An instant newer than every sample holds the newest rather than
    //extrapolating, for the reason PoseOf gives: being late costs a box drawn
    //where it was; being early costs a guess that has to be taken back.
    bool BoxAt(PlayerId player, double instant, const glm::vec3& halfExtents,
        Aabb& out) const;

    //How many samples are kept for a player. For tests and diagnostics.
    std::size_t SampleCount(PlayerId player) const;

private:
    struct Sample
    {
        std::uint64_t Tick = 0;
        glm::vec3 Position{ 0.0f };
    };

    std::map<PlayerId, std::deque<Sample>> m_Samples;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
