#include "cub.h"

#include "Cubit/Voxel/VoxelRaycast.h"

#include "Cubit/Voxel/Chunk.h"

#include <cmath>
#include <limits>

namespace
{
    constexpr float Infinity = std::numeric_limits<float>::infinity();

    //Returns the direction to step along one axis, or zero when the ray is
    //parallel to that axis.
    int StepFor(float direction)
    {
        if (direction > 0.0f)
            return 1;
        if (direction < 0.0f)
            return -1;

        return 0;
    }

    //Returns the distance along the ray to the first voxel boundary on one axis.
    float FirstBoundary(float origin, float direction, int voxel)
    {
        if (direction == 0.0f)
            return Infinity;

        const float boundary = direction > 0.0f
            ? static_cast<float>(voxel + 1)
            : static_cast<float>(voxel);

        return (boundary - origin) / direction;
    }

    //Returns the distance along the ray needed to cross one voxel on one axis.
    float BoundarySpacing(float direction)
    {
        if (direction == 0.0f)
            return Infinity;

        return std::abs(1.0f / direction);
    }

    //Returns the axis whose next boundary is nearest along the ray.
    int NearestAxis(const glm::vec3& nextBoundary)
    {
        if (nextBoundary.x < nextBoundary.y && nextBoundary.x < nextBoundary.z)
            return 0;
        if (nextBoundary.y < nextBoundary.z)
            return 1;

        return 2;
    }
}

VoxelRayHit VoxelRaycast::Cast(
    const Chunk& chunk,
    const glm::vec3& origin,
    const glm::vec3& direction,
    float maxDistance)
{
    VoxelRayHit result;

    const float length = glm::length(direction);
    if (length <= 0.0f)
        return result;

    const glm::vec3 ray = direction / length;

    glm::ivec3 voxel{
        static_cast<int>(std::floor(origin.x)),
        static_cast<int>(std::floor(origin.y)),
        static_cast<int>(std::floor(origin.z)) };

    const glm::ivec3 step{ StepFor(ray.x), StepFor(ray.y), StepFor(ray.z) };

    glm::vec3 nextBoundary{
        FirstBoundary(origin.x, ray.x, voxel.x),
        FirstBoundary(origin.y, ray.y, voxel.y),
        FirstBoundary(origin.z, ray.z, voxel.z) };

    const glm::vec3 boundarySpacing{
        BoundarySpacing(ray.x),
        BoundarySpacing(ray.y),
        BoundarySpacing(ray.z) };

    glm::ivec3 normal{ 0 };
    float travelled = 0.0f;

    while (travelled <= maxDistance)
    {
        if (chunk.IsBlockSolid(voxel.x, voxel.y, voxel.z))
        {
            result.Hit = true;
            result.Block = voxel;
            result.Normal = normal;
            result.Distance = travelled;

            return result;
        }

        const int axis = NearestAxis(nextBoundary);

        travelled = nextBoundary[axis];
        if (travelled > maxDistance)
            break;

        voxel[axis] += step[axis];
        nextBoundary[axis] += boundarySpacing[axis];

        normal = glm::ivec3{ 0 };
        normal[axis] = -step[axis];
    }

    return result;
}
