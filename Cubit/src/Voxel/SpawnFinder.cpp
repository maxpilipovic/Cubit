#include "cub.h"

#include "Cubit/Voxel/SpawnFinder.h"

#include "Cubit/Voxel/VoxelCollision.h"
#include "Cubit/Voxel/World.h"

#include <algorithm>
#include <cstdlib>

namespace
{
    //Tries one column: finds its surface and reports where a box would stand,
    //or nothing when the column cannot hold one.
    std::optional<glm::vec3> TryColumn(
        const World& world,
        int x,
        int z,
        const glm::vec3& halfExtents)
    {
        if (x < 0 || z < 0 || x >= world.GetWidth() || z >= world.GetDepth())
            return std::nullopt;

        for (int y = world.GetHeight() - 1; y >= 0; --y)
        {
            if (!world.IsBlockSolid(x, y, z))
                continue;

            //Feet exactly on the surface, so the first frame reports grounded
            //rather than spending one falling.
            //
            //Nothing can be in the way, so nothing checks for it: this is the
            //first solid block from the top, and the 0.6-wide box centred in a
            //1.0 cell never reaches a neighbouring column. A solid-overlap
            //check here would be unreachable code.
            const glm::vec3 centre(
                static_cast<float>(x) + 0.5f,
                static_cast<float>(y) + 1.0f + halfExtents.y,
                static_cast<float>(z) + 0.5f);

            //Standing on the riverbed puts the box in water, so this keeps a
            //spawn out of the river without needing a water-level constant.
            if (VoxelCollision::OverlapsFluid(world, centre, halfExtents))
                return std::nullopt;

            return centre;
        }

        //A column of pure air, or one of pure water.
        return std::nullopt;
    }
}

std::optional<glm::vec3> FindSpawn(
    const World& world,
    const glm::ivec2& hintXZ,
    const glm::vec3& halfExtents)
{
    //Rings of increasing Chebyshev distance, nearest first, each scanned in a
    //fixed order — so the answer is the closest usable column to the hint, and
    //the same one every run.
    for (int radius = 0; radius <= MaxSpawnSearchRadius; ++radius)
        for (int dz = -radius; dz <= radius; ++dz)
            for (int dx = -radius; dx <= radius; ++dx)
            {
                //Only this ring; everything inside it was tried at a smaller
                //radius.
                if (std::max(std::abs(dx), std::abs(dz)) != radius)
                    continue;

                const std::optional<glm::vec3> found =
                    TryColumn(world, hintXZ.x + dx, hintXZ.y + dz, halfExtents);

                if (found)
                    return found;
            }

    return std::nullopt;
}
