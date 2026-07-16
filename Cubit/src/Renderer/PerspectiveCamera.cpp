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
