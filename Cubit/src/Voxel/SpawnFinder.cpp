#include "cub.h"

#include "Cubit/Voxel/SpawnFinder.h"

#include "Cubit/Voxel/VoxelCollision.h"
#include "Cubit/Voxel/World.h"

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
    return TryColumn(world, hintXZ.x, hintXZ.y, halfExtents);
}
