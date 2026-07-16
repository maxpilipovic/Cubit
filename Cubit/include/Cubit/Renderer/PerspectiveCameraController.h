#pragma once

#include "Cubit/Events/ApplicationEvent.h"
#include "Cubit/Events/MouseEvent.h"
#include "Cubit/Renderer/PerspectiveCamera.h"
#include "Cubit/Timestep.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

class CB_API PerspectiveCameraController
{
public:
    //Creates a first-person camera controller for the supplied aspect ratio.
    explicit PerspectiveCameraController(float aspectRatio);

    //Moves the camera from the current keyboard state.
    void OnUpdate(Timestep timestep);

    //Handles mouse-look and projection resize events.
    void OnEvent(Event& event);

    PerspectiveCamera& GetCamera() { return m_Camera; }
    const PerspectiveCamera& GetCamera() const { return m_Camera; }

private:
    bool OnMouseMoved(MouseMovedEvent& event);
    bool OnWindowResized(WindowResizeEvent& event);
    void UpdateCamera();

    float m_AspectRatio;
    float m_FieldOfView = 60.0f;
    float m_NearClip = 0.1f;
    float m_FarClip = 1000.0f;
    PerspectiveCamera m_Camera;
    glm::vec3 m_Position{ 0.0f, 0.0f, 3.0f };
    float m_Yaw = -90.0f;
    float m_Pitch = 0.0f;
    float m_MovementSpeed = 3.0f;
    float m_MouseSensitivity = 0.12f;
    float m_LastMouseX = 0.0f;
    float m_LastMouseY = 0.0f;
    bool m_HasMousePosition = false;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
