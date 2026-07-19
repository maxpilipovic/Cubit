#pragma once

#include "Cubit/Core.h"

#include <glm/glm.hpp>

class Chunk;

struct VoxelMoveResult
{
    //Centre of the box after the move was resolved against solid blocks.
    glm::vec3 Position{ 0.0f };

    //Set when the box was stopped while moving along that axis.
    bool BlockedX = false;
    bool BlockedY = false;
    bool BlockedZ = false;

    //Set when downward motion was stopped, meaning the box is standing on
    //something solid.
    bool Grounded = false;
};

class CB_API VoxelCollision
{
public:
    VoxelCollision() = delete;

    //Moves an axis-aligned box through the chunk and stops it against solid
    //blocks. Each axis is resolved separately so a box blocked on one axis still
    //slides along the others. Long moves are split into small steps so a box
    //cannot pass through a block it should have hit.
    static VoxelMoveResult MoveBox(
        const Chunk& chunk,
        const glm::vec3& position,
        const glm::vec3& halfExtents,
        const glm::vec3& motion);

    //Reports whether a box at this position overlaps any solid block.
    static bool Overlaps(
        const Chunk& chunk,
        const glm::vec3& position,
        const glm::vec3& halfExtents);
};
