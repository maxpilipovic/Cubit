#include "cub.h"

#include "Cubit/Voxel/VoxelCollision.h"

#include "Cubit/Voxel/Chunk.h"

#include <algorithm>
#include <cmath>

namespace
{
    //Keeps a resolved box just clear of the block it stopped against, so the
    //next move does not start already overlapping.
    constexpr float Skin = 1.0e-4f;

    //Largest distance moved before the box is tested again. Keeping this under
    //one block means a move can never skip over a block it should have hit.
    constexpr float MaxStep = 0.25f;

    //Reports whether the box spanning these corners covers any solid block.
    bool OverlapsSolid(const Chunk& chunk, const glm::vec3& min, const glm::vec3& max)
    {
        const int minX = static_cast<int>(std::floor(min.x + Skin));
        const int minY = static_cast<int>(std::floor(min.y + Skin));
        const int minZ = static_cast<int>(std::floor(min.z + Skin));
        const int maxX = static_cast<int>(std::floor(max.x - Skin));
        const int maxY = static_cast<int>(std::floor(max.y - Skin));
        const int maxZ = static_cast<int>(std::floor(max.z - Skin));

        for (int z = minZ; z <= maxZ; ++z)
            for (int y = minY; y <= maxY; ++y)
                for (int x = minX; x <= maxX; ++x)
                    if (chunk.IsBlockSolid(x, y, z))
                        return true;

        return false;
    }

    //Records that the box was stopped while moving along one axis.
    void MarkBlocked(VoxelMoveResult& result, int axis, float delta)
    {
        switch (axis)
        {
            case 0:
                result.BlockedX = true;
                break;
            case 1:
                result.BlockedY = true;
                if (delta < 0.0f)
                    result.Grounded = true;
                break;
            default:
                result.BlockedZ = true;
                break;
        }
    }

    //Moves the box along one axis and pushes it back out of anything it entered.
    void MoveAxis(
        const Chunk& chunk,
        VoxelMoveResult& result,
        const glm::vec3& halfExtents,
        int axis,
        float delta)
    {
        if (delta == 0.0f)
            return;

        result.Position[axis] += delta;

        const glm::vec3 min = result.Position - halfExtents;
        const glm::vec3 max = result.Position + halfExtents;
        if (!OverlapsSolid(chunk, min, max))
            return;

        //Snap back to the face of the block row that was entered.
        result.Position[axis] = delta > 0.0f
            ? std::floor(max[axis]) - halfExtents[axis] - Skin
            : std::ceil(min[axis]) + halfExtents[axis] + Skin;

        MarkBlocked(result, axis, delta);
    }
}

bool VoxelCollision::Overlaps(
    const Chunk& chunk,
    const glm::vec3& position,
    const glm::vec3& halfExtents)
{
    return OverlapsSolid(chunk, position - halfExtents, position + halfExtents);
}

VoxelMoveResult VoxelCollision::MoveBox(
    const Chunk& chunk,
    const glm::vec3& position,
    const glm::vec3& halfExtents,
    const glm::vec3& motion)
{
    VoxelMoveResult result;
    result.Position = position;

    const float longest = std::max({
        std::abs(motion.x),
        std::abs(motion.y),
        std::abs(motion.z) });

    const int steps = std::max(
        1,
        static_cast<int>(std::ceil(longest / MaxStep)));

    const glm::vec3 stepMotion = motion / static_cast<float>(steps);

    for (int step = 0; step < steps; ++step)
    {
        MoveAxis(chunk, result, halfExtents, 0, stepMotion.x);
        MoveAxis(chunk, result, halfExtents, 1, stepMotion.y);
        MoveAxis(chunk, result, halfExtents, 2, stepMotion.z);
    }

    return result;
}
