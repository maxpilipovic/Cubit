#pragma once

#include "Cubit/Core.h"

#include <glm/glm.hpp>
#include <optional>

class World;

//How far the search spirals out from the hint before giving up, in columns.
//Wide enough to cross any feature on the shipped maps, bounded so a hint that
//can never work fails loudly instead of quietly scanning the whole world.
constexpr int MaxSpawnSearchRadius = 64;

//Finds somewhere a player box can stand near a hinted column, and returns the
//centre of the box there — the same point VoxelMoveResult reports, so the
//result assigns straight into a position collision already understands.
//
//Takes only an x and a z: the ground decides the height. Returns nothing when
//no column within MaxSpawnSearchRadius works.
CB_API std::optional<glm::vec3> FindSpawn(
    const World& world,
    const glm::ivec2& hintXZ,
    const glm::vec3& halfExtents);
