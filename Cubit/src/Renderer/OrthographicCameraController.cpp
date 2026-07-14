#include "cub.h"

#include "Cubit/Renderer/OrthographicCameraController.h"

#include "Cubit/Input.h"
#include "Cubit/KeyCodes.h"

#include <algorithm>
#include <cmath>

OrthographicCameraController::OrthographicCameraController(float aspectRatio, bool enableRotation)
    : m_AspectRatio(aspectRatio),
      m_Camera(-aspectRatio, aspectRatio, -1.0f, 1.0f),
      m_RotationEnabled(enableRotation)
{
}

void OrthographicCameraController::OnUpdate(Timestep timestep)
{
    const float seconds = static_cast<float>(timestep.GetSeconds());
    const float radians = glm::radians(m_CameraRotation);
    const float distance = m_CameraTranslationSpeed * seconds;

    if (Input::IsKeyPressed(KeyCode::A))
    {
        m_CameraPosition.x -= std::cos(radians) * distance;
        m_CameraPosition.y -= std::sin(radians) * distance;
    }
    if (Input::IsKeyPressed(KeyCode::D))
    {
        m_CameraPosition.x += std::cos(radians) * distance;
        m_CameraPosition.y += std::sin(radians) * distance;
    }
    if (Input::IsKeyPressed(KeyCode::W))
    {
        m_CameraPosition.x -= std::sin(radians) * distance;
        m_CameraPosition.y += std::cos(radians) * distance;
    }
    if (Input::IsKeyPressed(KeyCode::S))
    {
        m_CameraPosition.x += std::sin(radians) * distance;
        m_CameraPosition.y -= std::cos(radians) * distance;
    }

    if (m_RotationEnabled)
    {
        if (Input::IsKeyPressed(KeyCode::Q))
            m_CameraRotation += m_CameraRotationSpeed * seconds;
        if (Input::IsKeyPressed(KeyCode::E))
            m_CameraRotation -= m_CameraRotationSpeed * seconds;

        m_Camera.SetRotation(m_CameraRotation);
    }

    m_Camera.SetPosition(m_CameraPosition);
    m_CameraTranslationSpeed = m_ZoomLevel * 2.0f;
}

void OrthographicCameraController::OnEvent(Event& event)
{
    EventDispatcher dispatcher(event);
    dispatcher.Dispatch<MouseScrolledEvent>(
        [this](MouseScrolledEvent& scrollEvent)
        {
            return OnMouseScrolled(scrollEvent);
        });
    dispatcher.Dispatch<WindowResizeEvent>(
        [this](WindowResizeEvent& resizeEvent)
        {
            return OnWindowResized(resizeEvent);
        });
}

void OrthographicCameraController::OnResize(float width, float height)
{
    if (height <= 0.0f)
        return;

    m_AspectRatio = width / height;
    RecalculateProjection();
}

void OrthographicCameraController::SetZoomLevel(float zoomLevel)
{
    m_ZoomLevel = std::max(zoomLevel, 0.25f);
    RecalculateProjection();
}

bool OrthographicCameraController::OnMouseScrolled(MouseScrolledEvent& event)
{
    SetZoomLevel(m_ZoomLevel - static_cast<float>(event.GetYOffset()) * 0.25f);
    return false;
}

bool OrthographicCameraController::OnWindowResized(WindowResizeEvent& event)
{
    OnResize(static_cast<float>(event.GetWidth()), static_cast<float>(event.GetHeight()));
    return false;
}

void OrthographicCameraController::RecalculateProjection()
{
    m_Camera.SetProjection(
        -m_AspectRatio * m_ZoomLevel,
         m_AspectRatio * m_ZoomLevel,
        -m_ZoomLevel,
         m_ZoomLevel);
}
