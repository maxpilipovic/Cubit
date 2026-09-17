#include "cub.h"

#include "Cubit/Voxel/BlockEdit.h"

#include "Cubit/Voxel/SkyLight.h"
#include "Cubit/Voxel/World.h"

#include <algorithm>

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

std::vector<BlockEdit> ApplyBlockEdits(World& world, std::span<const BlockEdit> edits)
{
    std::vector<BlockEdit> undo;
    undo.reserve(edits.size());

    // One relight per changed cell, the same as applying them one at a time.
    // A single relight over the whole batch would be cheaper, and is only worth
    // its risk if a batch's cost is measured to matter.
    for (const BlockEdit& edit : edits)
    {
        if (const std::optional<BlockEdit> inverse = ApplyBlockEdit(world, edit))
            undo.push_back(*inverse);
    }

    // Newest first, so a cell named twice unwinds through its middle value back
    // to the one it started with.
    std::reverse(undo.begin(), undo.end());
    return undo;
}
