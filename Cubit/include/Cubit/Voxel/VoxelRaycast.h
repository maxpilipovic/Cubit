#pragma once

#include "Cubit/Core.h"

#include <glm/glm.hpp>

class World;

struct VoxelRayHit
{
    //True when the ray reached a present block within its maximum distance.
    bool Hit = false;

    //Coordinates of the present block the ray entered.
    glm::ivec3 Block{ 0 };

    //Face the ray entered through, pointing back along the ray. Zero when the
    //ray started inside a present block and never crossed a face.
    glm::ivec3 Normal{ 0 };

    //Distance travelled along the normalized ray direction to reach the block.
    float Distance = 0.0f;
};

class CB_API VoxelRaycast
{
public:
    VoxelRaycast() = delete;

    //Walks the ray voxel by voxel and reports the first present block it
    //enters. Presence, not solidity or opacity, decides a hit: a non-solid
    //block like water still stops the ray. Positions outside the world are
    //treated as air, matching World::GetBlock.
    //
    //solidOnly, when true, ignores fluid blocks entirely, which is what an
    //edit — or a future shot — wants: water is scenery you aim through, not a
    //target. It also settles the case of a ray beginning inside water, which a
    //player standing on the riverbed does every frame; that cell would
    //otherwise be reported at zero distance with no entry face to place
    //against, making it the answer to every click regardless of aim.
    static VoxelRayHit Cast(
        const World& world,
        const glm::vec3& origin,
        const glm::vec3& direction,
        float maxDistance,
        bool solidOnly = false);
};
