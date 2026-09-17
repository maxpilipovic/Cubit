#include "cub.h"

#include "Cubit/Voxel/EditRules.h"

#include "Cubit/Voxel/CharacterController.h"
#include "Cubit/Voxel/World.h"

bool IsCellWithinReach(const glm::vec3& eye, const glm::ivec3& cell, float reach)
{
    const glm::vec3 min(cell);
    const glm::vec3 nearest = glm::clamp(eye, min, min + glm::vec3(1.0f));
    return glm::distance(eye, nearest) <= reach;
}

bool BoxOverlapsCell(const glm::vec3& centre, const glm::vec3& halfExtents,
    const glm::ivec3& cell)
{
    const glm::vec3 boxMin = centre - halfExtents;
    const glm::vec3 boxMax = centre + halfExtents;
    const glm::vec3 cellMin(cell);
    const glm::vec3 cellMax = cellMin + glm::vec3(1.0f);

    //Strict on every axis: a box resting exactly on a cell's top face does not
    //overlap it.
    return boxMin.x < cellMax.x && boxMax.x > cellMin.x
        && boxMin.y < cellMax.y && boxMax.y > cellMin.y
        && boxMin.z < cellMax.z && boxMax.z > cellMin.z;
}

bool IsEditLegal(const MatchState& match, PlayerId editor, const BlockEdit& edit,
    OtherPlayers others, const MatchRules& rules)
{
    if (!match.HasPlayer(editor))
        return false;

    const World& world = match.GetWorld();
    const glm::ivec3& at = edit.Position;

    //ApplyBlockEdit's own conditions, checked here so an edit that would do
    //nothing is refused rather than reported as accepted.
    if (!world.IsInBounds(at.x, at.y, at.z) || world.GetBlock(at.x, at.y, at.z) == edit.Block)
        return false;

    const CharacterController& character = match.Player(editor);
    const glm::vec3 eye = character.Position() + glm::vec3(0.0f, character.Config().EyeOffset, 0.0f);

    if (!IsCellWithinReach(eye, at, rules.ReachDistance))
        return false;

    //Breaking never traps anybody.
    if (edit.Block == BlockId{ 0 })
        return true;

    for (const auto& [player, body] : match.Players())
    {
        if (player != editor && others == OtherPlayers::Ignore)
            continue;

        if (BoxOverlapsCell(body.Position(), body.Config().HalfExtents, at))
            return false;
    }

    return true;
}
