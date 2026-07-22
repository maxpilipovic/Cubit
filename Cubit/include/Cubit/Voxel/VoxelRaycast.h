#pragma once

#include "Cubit/Core.h"

#include <glm/glm.hpp>

class World;

struct VoxelRayHit
{
    //True when the ray reached a solid block within its maximum distance.
    bool Hit = false;

    //Coordinates of the solid block the ray entered.
    glm::ivec3 Block{ 0 };

    //Face the ray entered through, pointing back along the ray. Zero when the
    //ray started inside a solid block and never crossed a face.
    glm::ivec3 Normal{ 0 };

    //Distance travelled along the normalized ray direction to reach the block.
    float Distance = 0.0f;
};

class CB_API VoxelRaycast
{
public:
    VoxelRaycast() = delete;

    //Walks the ray voxel by voxel and reports the first solid block it enters.
    //Positions outside the world are treated as air, matching World::GetBlock.
    static VoxelRayHit Cast(
        const World& world,
        const glm::vec3& origin,
        const glm::vec3& direction,
        float maxDistance);
};
