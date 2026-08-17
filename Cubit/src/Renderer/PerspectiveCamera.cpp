#include "cub.h"

#include "Cubit/Renderer/PerspectiveCamera.h"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace
{
    constexpr glm::vec3 WorldUp{ 0.0f, 1.0f, 0.0f };
}

PerspectiveCamera::PerspectiveCamera(
    float fieldOfView,
    float aspectRatio,
    float nearClip,
    float farClip)
{
    SetProjection(fieldOfView, aspectRatio, nearClip, farClip);
    RecalculateViewMatrix();
}

void PerspectiveCamera::SetProjection(
    float fieldOfView,
    float aspectRatio,
    float nearClip,
    float farClip)
{
    m_ProjectionMatrix = glm::perspective(
        glm::radians(fieldOfView), aspectRatio, nearClip, farClip);
    m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix;
}

void PerspectiveCamera::SetPosition(const glm::vec3& position)
{
    m_Position = position;
    RecalculateViewMatrix();
}

void PerspectiveCamera::SetRotation(float yaw, float pitch)
{
    m_Yaw = yaw;
    m_Pitch = pitch;
    RecalculateViewMatrix();
}

glm::vec2 PerspectiveCamera::YawPitchToward(const glm::vec3& from, const glm::vec3& to)
{
    const glm::vec3 direction = to - from;

    //Two identical points give no direction. The default facing is a usable
    //answer; a NaN would spread silently through the view matrix.
    if (direction == glm::vec3(0.0f))
        return glm::vec2(-90.0f, 0.0f);

    const float horizontal = glm::length(glm::vec2(direction.x, direction.z));

    return glm::vec2(
        glm::degrees(std::atan2(direction.z, direction.x)),
        glm::degrees(std::atan2(direction.y, horizontal)));
}

glm::vec3 PerspectiveCamera::GetRightDirection() const
{
    return glm::normalize(glm::cross(m_ForwardDirection, WorldUp));
}

void PerspectiveCamera::RecalculateViewMatrix()
{
    const float yaw = glm::radians(m_Yaw);
    const float pitch = glm::radians(m_Pitch);
    m_ForwardDirection = glm::normalize(glm::vec3(
        std::cos(yaw) * std::cos(pitch),
        std::sin(pitch),
        std::sin(yaw) * std::cos(pitch)));
    m_ViewMatrix = glm::lookAt(m_Position, m_Position + m_ForwardDirection, WorldUp);
    m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix;
}
