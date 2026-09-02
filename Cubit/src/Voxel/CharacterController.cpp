#include "cub.h"

#include "Cubit/Voxel/CharacterController.h"

#include "Cubit/Voxel/Heading.h"
#include "Cubit/Voxel/VoxelCollision.h"
#include "Cubit/Voxel/World.h"

#include <cmath>

void CharacterController::Step(
    const World& world, const CharacterInput& input, float seconds)
{
    // The step is about to overwrite the position rendering interpolates from,
    // so keep it first.
    m_PreviousPosition = m_Position;

    m_BodyInFluid =
        VoxelCollision::OverlapsFluid(world, m_Position, m_Config.HalfExtents);

    // Resolve the character's own axes into the world here rather than taking
    // a world-space vector from the caller: this is the step a server has to
    // be able to reproduce, and reproducing it means deriving the direction
    // from the yaw rather than trusting a direction it was handed.
    const glm::vec3 forward = HeadingForward(input.Yaw);
    const glm::vec3 right = HeadingRight(input.Yaw);
    const glm::vec3 heading = forward * input.Move.y + right * input.Move.x;

    // Cap rather than normalise, so partial deflection still walks slowly
    // while two keys at once cannot outrun one.
    const float headingLength = glm::length(heading);
    const glm::vec3 direction =
        headingLength > 1.0f ? heading / headingLength : heading;

    glm::vec2 walk =
        glm::vec2(direction.x, direction.z) * m_Config.WalkSpeed;

    // Jump has to be tested inside this branch rather than after it: standing
    // on the riverbed is grounded and submerged at once, so a dry jump would
    // otherwise fire instead of a swim stroke.
    if (m_BodyInFluid)
    {
        walk *= m_Config.WaterDrag;

        // Only while the head is under. The box still counts as wet with the
        // whole body clear of the surface, so stroking on that alone thrusts
        // the character out of the river and buzzes them above it.
        if (input.Jump && IsEyeInFluid(world, m_Position))
            m_VerticalVelocity = m_Config.SwimUpSpeed;

        m_VerticalVelocity -= m_Config.WaterGravity * seconds;
        m_VerticalVelocity = glm::max(m_VerticalVelocity, -m_Config.SinkSpeed);
    }
    else
    {
        if (m_Grounded && input.Jump)
            m_VerticalVelocity = m_Config.JumpSpeed;

        m_VerticalVelocity -= m_Config.Gravity * seconds;
    }

    // The position is in world coordinates, so collision runs against the whole
    // world and the box can cross chunk boundaries.
    const VoxelMoveResult move = VoxelCollision::MoveBox(
        world,
        m_Position,
        m_Config.HalfExtents,
        glm::vec3(walk.x, m_VerticalVelocity, walk.y) * seconds);

    m_Position = move.Position;
    m_Grounded = move.Grounded;

    // The eye, not the box: the tint and the fog should come on when the camera
    // goes under, which happens later than the feet getting wet. Taken after
    // the move so a renderer reading it is not a frame stale.
    m_EyeInFluid = IsEyeInFluid(world, m_Position);

    // Landing or hitting a ceiling ends vertical motion.
    if (move.BlockedY)
        m_VerticalVelocity = 0.0f;
}

void CharacterController::Teleport(const glm::vec3& position)
{
    m_Position = position;
    m_PreviousPosition = position;
}

void CharacterController::SetState(const glm::vec3& position,
    const glm::vec3& previousPosition, float verticalVelocity, bool grounded)
{
    m_Position = position;
    m_PreviousPosition = previousPosition;
    m_VerticalVelocity = verticalVelocity;
    m_Grounded = grounded;
}

glm::vec3 CharacterController::InterpolatedPosition(float alpha) const
{
    return glm::mix(m_PreviousPosition, m_Position, alpha);
}

glm::vec3 CharacterController::InterpolatedEye(float alpha) const
{
    return InterpolatedPosition(alpha) +
        glm::vec3(0.0f, m_Config.EyeOffset, 0.0f);
}

bool CharacterController::IsEyeInFluid(
    const World& world, const glm::vec3& position) const
{
    const glm::vec3 eye =
        position + glm::vec3(0.0f, m_Config.EyeOffset, 0.0f);

    return world.IsBlockFluid(
        static_cast<int>(std::floor(eye.x)),
        static_cast<int>(std::floor(eye.y)),
        static_cast<int>(std::floor(eye.z)));
}
