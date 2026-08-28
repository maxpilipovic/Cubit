#pragma once

#include "Cubit/Core.h"

#include <glm/glm.hpp>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

class World;

//What a character is being asked to do this step.
//
//A value rather than something the controller reads for itself. Polling the
//keyboard from inside the step would tie simulation to a focused window: it
//could not be unit tested, it could not be replayed, and it could not run on a
//machine with no window at all. Handing the intent in costs one struct and
//buys all three.
struct CharacterInput
{
    //Ground-plane movement, already resolved against whatever the character is
    //facing. Length is expected to be 0 or 1; anything longer walks faster, so
    //the caller normalises rather than this clamping and hiding the mistake.
    //x is right, y is forward.
    glm::vec2 Move{ 0.0f };

    //Held, not tapped. Jumping and swimming up are the same request; which one
    //happens depends on whether the character is in water.
    bool Jump = false;
};

//How a character moves. One instance today, on the player.
//
//Named fields rather than constants in a source file because a game will want
//a heavier character or a faster one before it wants anything else here, and
//because a test that needs a fall to take a predictable number of steps has to
//be able to say so.
struct CharacterConfig
{
    //Half extents of a 0.6 x 1.8 x 0.6 collision box.
    glm::vec3 HalfExtents{ 0.3f, 0.9f, 0.3f };

    //Eye height above the centre of the box.
    float EyeOffset = 0.7f;

    float WalkSpeed = 5.0f;
    float JumpSpeed = 9.0f;
    float Gravity = 24.0f;

    //Water physics. Gravity is weakened rather than cancelled, so doing nothing
    //settles the character onto the riverbed instead of leaving them hanging.
    float WaterGravity = 6.0f;
    float SinkSpeed = 1.5f;
    float SwimUpSpeed = 3.5f;
    float WaterDrag = 0.6f;
};

//A box that walks, falls and swims through a voxel world.
//
//Holds no camera, reads no input and makes no GL calls, so it runs anywhere a
//World does — including a test, and including a machine with no window.
class CB_API CharacterController
{
public:
    CharacterController() = default;
    explicit CharacterController(const CharacterConfig& config)
        : m_Config(config) {}

    //Advances one fixed step. Call it with a fixed timestep: the jump height
    //and the fall curve are only stable if the step is.
    void Step(const World& world, const CharacterInput& input, float seconds);

    //Moves the character without interpolating through the space in between.
    //
    //Writes both positions together, so the lerp that rendering does next is a
    //no-op rather than a smear across the map. That pairing is the whole reason
    //the previous position lives in here: kept beside the current one it cannot
    //be left stale by a caller who moved one and forgot the other.
    //
    //Deliberately does not reset the vertical velocity — that stays the
    //caller's business. A respawn wants it cleared; a teleport that keeps a
    //falling character falling might not.
    void Teleport(const glm::vec3& position);

    //Centre of the collision box at the end of the last step.
    const glm::vec3& Position() const { return m_Position; }

    //Where the box stood at the end of the step before that. Rendering
    //interpolates between the two, so motion stays smooth when frames and steps
    //do not line up.
    const glm::vec3& PreviousPosition() const { return m_PreviousPosition; }

    //Where the box was `alpha` of the way through the step just taken.
    glm::vec3 InterpolatedPosition(float alpha) const;

    //Eye position for a given interpolation point, which is what a camera
    //wants. Separate from InterpolatedPosition because the offset belongs to
    //the character rather than to whoever is drawing it.
    glm::vec3 InterpolatedEye(float alpha) const;

    //Set when the last step ended with downward motion stopped.
    bool Grounded() const { return m_Grounded; }

    //Set when any part of the box overlapped a fluid block during the last
    //step. Feet in the river counts.
    bool BodyInFluid() const { return m_BodyInFluid; }

    //Set when the eye specifically was in a fluid block. Later than
    //BodyInFluid: a body that still counts as wet can have its head well clear
    //of the surface, and the underwater tint should follow the head.
    bool EyeInFluid() const { return m_EyeInFluid; }

    float VerticalVelocity() const { return m_VerticalVelocity; }
    void SetVerticalVelocity(float velocity) { m_VerticalVelocity = velocity; }

    const CharacterConfig& Config() const { return m_Config; }

private:
    //Reports whether the eye sits in a fluid block for a given box position.
    bool IsEyeInFluid(const World& world, const glm::vec3& position) const;

    CharacterConfig m_Config;

    glm::vec3 m_Position{ 0.0f };
    glm::vec3 m_PreviousPosition{ 0.0f };

    float m_VerticalVelocity = 0.0f;
    bool m_Grounded = false;
    bool m_BodyInFluid = false;
    bool m_EyeInFluid = false;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
