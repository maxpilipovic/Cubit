#include "cub.h"

#include "Cubit/Voxel/BlockEdit.h"

#include "Cubit/Voxel/SkyLight.h"
#include "Cubit/Voxel/World.h"

std::optional<BlockEdit> ApplyBlockEdit(World& world, const BlockEdit& edit)
{
    const glm::ivec3& at = edit.Position;

    if (!world.IsInBounds(at.x, at.y, at.z))
        return std::nullopt;

    const BlockId previous = world.GetBlock(at.x, at.y, at.z);
    if (previous == edit.Block)
        return std::nullopt;

    world.SetBlock(at.x, at.y, at.z, edit.Block);

    // Relight before returning, so an applied edit always leaves the world
    // consistent. A caller that had to remember this separately would produce
    // wrong light, which reads as a lighting bug rather than a missing call.
    SkyLight::Repropagate(world, at.x, at.y, at.z);

    return BlockEdit{ at, previous };
}
