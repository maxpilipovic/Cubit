#pragma once

#include <glm/glm.hpp>
#include <cmath>

//Ground-plane facing derived from a yaw in degrees.
//
//The same convention PerspectiveCamera renders with — yaw 0 faces +x, and yaw
//grows toward +z — reduced to the horizontal plane and stripped of any
//dependency on the renderer, so the simulation can resolve a heading with no
//camera and no GL context. HeadingTests pins it against the camera rather than
//against a second reading of the formula.
//
//Pitch is deliberately absent. Walking speed must not change when you look up
//or down, so the walk direction was always the camera's forward flattened; and
//because pitch is clamped to +/-89 degrees, cos(pitch) is strictly positive and
//divides out of that flatten. Yaw alone is therefore exact here, not an
//approximation of it.
inline glm::vec3 HeadingForward(float yawDegrees)
{
    const float yaw = glm::radians(yawDegrees);
    return glm::vec3(std::cos(yaw), 0.0f, std::sin(yaw));
}

//Ninety degrees clockwise of HeadingForward, matching
//cross(forward, worldUp) with worldUp = +y.
inline glm::vec3 HeadingRight(float yawDegrees)
{
    const float yaw = glm::radians(yawDegrees);
    return glm::vec3(-std::sin(yaw), 0.0f, std::cos(yaw));
}

//Full 3D aim direction from a yaw and a pitch, both in degrees.
//
//The pitched sibling of HeadingForward, and the note above about pitch being
//deliberately absent still stands for everything it was written about: WALKING
//must not change when you look up. Aiming is the case that reasoning does not
//cover - a shot goes where you are looking, including up and down.
//
//Identical to PerspectiveCamera::RecalculateViewMatrix's forward vector, and
//HeadingTests pins the two together rather than reading the formula twice. If
//they ever drift, every shot lands somewhere other than the crosshair while
//every unit test still passes.
inline glm::vec3 AimDirection(float yawDegrees, float pitchDegrees)
{
    const float yaw = glm::radians(yawDegrees);
    const float pitch = glm::radians(pitchDegrees);

    return glm::normalize(glm::vec3(
        std::cos(yaw) * std::cos(pitch),
        std::sin(pitch),
        std::sin(yaw) * std::cos(pitch)));
}
