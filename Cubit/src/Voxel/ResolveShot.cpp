#include "cub.h"

#include "Cubit/Voxel/ResolveShot.h"

#include "Cubit/Voxel/VoxelRaycast.h"
#include "Cubit/Voxel/World.h"

#include <utility>

namespace
{
    //Slab method. Returns false when the ray misses, or when the box is
    //entirely behind the origin.
    //
    //A ray starting INSIDE the box returns a distance of 0, which is correct
    //here: point blank is a hit, not a miss.
    bool RayHitsBox(const glm::vec3& origin, const glm::vec3& direction,
        const Aabb& box, float maxDistance, float& distance)
    {
        //Not `near` and `far`: <windows.h> defines both as empty macros, and a
        //translation unit that ever pulls it in would see these vanish.
        float latestEnter = 0.0f;
        float earliestExit = maxDistance;

        for (int axis = 0; axis < 3; ++axis)
        {
            //Parallel to this pair of slabs: a miss unless the origin already
            //lies between them. Testing the reciprocal instead would divide by
            //zero and produce infinities that compare in surprising ways.
            if (glm::abs(direction[axis]) < 1e-8f)
            {
                if (origin[axis] < box.Min[axis] || origin[axis] > box.Max[axis])
                    return false;

                continue;
            }

            const float inverse = 1.0f / direction[axis];
            float enter = (box.Min[axis] - origin[axis]) * inverse;
            float exit = (box.Max[axis] - origin[axis]) * inverse;

            if (enter > exit)
                std::swap(enter, exit);

            latestEnter = glm::max(latestEnter, enter);
            earliestExit = glm::min(earliestExit, exit);

            if (latestEnter > earliestExit)
                return false;
        }

        distance = latestEnter;
        return true;
    }
}

ShotResult ResolveShot(
    const World& world,
    std::span<const ShotCandidate> candidates,
    const glm::vec3& origin,
    const glm::vec3& direction,
    float maxDistance)
{
    const glm::vec3 aim = glm::normalize(direction);

    PlayerId victim = InvalidPlayer;
    float best = maxDistance;

    for (const ShotCandidate& candidate : candidates)
    {
        float distance = 0.0f;
        if (!RayHitsBox(origin, aim, candidate.Box, maxDistance, distance))
            continue;

        if (distance >= best)
            continue;

        best = distance;
        victim = candidate.Player;
    }

    //Solid only: water is scenery you aim through, not a target.
    const VoxelRayHit terrain = VoxelRaycast::Cast(world, origin, aim, maxDistance, true);

    //<= rather than <, so terrain takes ties: a player flush against a wall is
    //never shot through it.
    if (terrain.Hit && terrain.Distance <= best)
    {
        ShotResult result;
        result.Victim = InvalidPlayer;
        result.Distance = terrain.Distance;
        result.Impact = origin + aim * terrain.Distance;
        return result;
    }

    ShotResult result;
    result.Victim = victim;
    result.Distance = best;
    result.Impact = origin + aim * best;
    return result;
}
