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
    //skipStartVoxel, when true, does not report the voxel the ray starts
    //inside (the one with no crossed face yet, i.e. a zero Normal) and keeps
    //walking past it instead. A caller whose origin can sit inside a
    //non-solid block — a player wading in water, say — has no entry face
    //there to aim through or place against, so that voxel is never a useful
    //hit for editing. Blocks the ray reaches after crossing a real face are
    //reported as usual, including further non-solid ones.
    static VoxelRayHit Cast(
        const World& world,
        const glm::vec3& origin,
        const glm::vec3& direction,
        float maxDistance,
        bool skipStartVoxel = false);
};
