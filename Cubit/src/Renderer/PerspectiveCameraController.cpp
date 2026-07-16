#include "cub.h"

#include "Cubit/Renderer/PerspectiveCameraController.h"

#include "Cubit/Input.h"
#include "Cubit/KeyCodes.h"

#include <algorithm>

PerspectiveCameraController::PerspectiveCameraController(float aspectRatio)
    : m_AspectRatio(aspectRatio),
      m_Camera(m_FieldOfView, aspectRatio, m_NearClip, m_FarClip)
{
    UpdateCamera();
}

void PerspectiveCameraController::OnUpdate(Timestep timestep)
{
    const float distance = m_MovementSpeed * static_cast<float>(timestep.GetSeconds());
    const glm::vec3 forward = m_Camera.GetForwardDirection();
    const glm::vec3 right = m_Camera.GetRightDirection();

    if (Input::IsKeyPressed(KeyCode::W))
        m_Position += forward * distance;
    if (Input::IsKeyPressed(KeyCode::S))
        m_Position -= forward * distance;
    if (Input::IsKeyPressed(KeyCode::D))
        m_Position += right * distance;
    if (Input::IsKeyPressed(KeyCode::A))
        m_Position -= right * distance;
    if (Input::IsKeyPressed(KeyCode::Space))
        m_Position.y += distance;
    if (Input::IsKeyPressed(KeyCode::LeftShift))
        m_Position.y -= distance;

    m_Camera.SetPosition(m_Position);
}

void PerspectiveCameraController::OnEvent(Event& event)
{
    EventDispatcher dispatcher(event);
    dispatcher.Dispatch<MouseMovedEvent>(
        [this](MouseMovedEvent& mouseEvent) { return OnMouseMoved(mouseEvent); });
    dispatcher.Dispatch<WindowResizeEvent>(
        [this](WindowResizeEvent& resizeEvent) { return OnWindowResized(resizeEvent); });
}

bool PerspectiveCameraController::OnMouseMoved(MouseMovedEvent& event)
{
    const float x = static_cast<float>(event.GetX());
    const float y = static_cast<float>(event.GetY());
    if (!m_HasMousePosition)
    {
        m_LastMouseX = x;
        m_LastMouseY = y;
        m_HasMousePosition = true;
        return false;
    }

    const float xOffset = x - m_LastMouseX;
    const float yOffset = m_LastMouseY - y;
    m_LastMouseX = x;
    m_LastMouseY = y;

    m_Yaw += xOffset * m_MouseSensitivity;
    m_Pitch = std::clamp(m_Pitch + yOffset * m_MouseSensitivity, -89.0f, 89.0f);
    UpdateCamera();
    return false;
}

bool PerspectiveCameraController::OnWindowResized(WindowResizeEvent& event)
{
    if (event.GetHeight() == 0)
        return false;

    m_AspectRatio = static_cast<float>(event.GetWidth()) / static_cast<float>(event.GetHeight());
    m_Camera.SetProjection(m_FieldOfView, m_AspectRatio, m_NearClip, m_FarClip);
    return false;
}

void PerspectiveCameraController::UpdateCamera()
{
    m_Camera.SetRotation(m_Yaw, m_Pitch);
    m_Camera.SetPosition(m_Position);
}
