#pragma once

#include "Cubit/Core.h"
#include "Cubit/Voxel/HitboxHistory.h"
#include "Cubit/Voxel/MatchState.h"

#include <glm/glm.hpp>
#include <span>

class World;

//One player a shot might hit, at the position the shooter saw them.
struct ShotCandidate
{
    PlayerId Player = InvalidPlayer;
    Aabb Box;
};

//What a shot hit.
struct ShotResult
{
    //InvalidPlayer when the shot hit terrain or nothing at all. The impact
    //point is meaningful either way, which is why a miss is not signalled by a
    //flag: something is always drawn at the end of the ray.
    PlayerId Victim = InvalidPlayer;

    glm::vec3 Impact{ 0.0f };
    float Distance = 0.0f;
};

//Resolves one hitscan shot against player boxes and terrain, nearest first.
//
//Pure: it reads a World and a list of boxes and returns an answer. No history,
//no clock, no network. Whoever calls it decides WHICH boxes - that is where
//rewinding lives, and it is also where the shooter is left out of their own
//shot's candidates.
//
//Terrain takes ties, so a player standing flush against a wall cannot be shot
//through it.
//
//Water is aimed through rather than stopped at, matching the editing ray:
//VoxelRaycast::Cast is called with solidOnly, whose own comment has anticipated
//"a future shot" since it was added.
CB_API ShotResult ResolveShot(
    const World& world,
    std::span<const ShotCandidate> candidates,
    const glm::vec3& origin,
    const glm::vec3& direction,
    float maxDistance);
