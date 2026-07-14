#pragma once

#include "Cubit/Core.h"
#include "Cubit/Events/ApplicationEvent.h"
#include "Cubit/Events/MouseEvent.h"
#include "Cubit/Renderer/OrthographicCamera.h"
#include "Cubit/Timestep.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

class CB_API OrthographicCameraController
{
public:
    //Creates a camera controller for the supplied viewport aspect ratio.
    OrthographicCameraController(float aspectRatio, bool enableRotation = false);

    //Moves and rotates the camera from the current polled input state.
    void OnUpdate(Timestep timestep);

    //Handles resize and scroll events that affect the camera projection.
    void OnEvent(Event& event);

    //Updates the camera projection for a new logical viewport size.
    void OnResize(float width, float height);

    //Returns the camera controlled by this input adapter.
    OrthographicCamera& GetCamera() { return m_Camera; }

    //Returns the camera controlled by this input adapter without allowing changes.
    const OrthographicCamera& GetCamera() const { return m_Camera; }

    //Returns the current orthographic half-height.
    float GetZoomLevel() const { return m_ZoomLevel; }

    //Changes the orthographic half-height and refreshes the projection.
    void SetZoomLevel(float zoomLevel);

private:
    //Changes zoom in response to a mouse-wheel event.
    bool OnMouseScrolled(MouseScrolledEvent& event);

    //Changes the camera aspect ratio after a logical window resize.
    bool OnWindowResized(WindowResizeEvent& event);

    //Rebuilds the camera projection from the current aspect ratio and zoom.
    void RecalculateProjection();

    float m_AspectRatio;
    float m_ZoomLevel = 1.0f;
    OrthographicCamera m_Camera;
    bool m_RotationEnabled;
    glm::vec3 m_CameraPosition{ 0.0f };
    float m_CameraRotation = 0.0f;
    float m_CameraTranslationSpeed = 2.0f;
    float m_CameraRotationSpeed = 90.0f;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
