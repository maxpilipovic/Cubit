#pragma once

#include "Cubit/Core.h"

#include <glm/glm.hpp>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

class CB_API OrthographicCamera
{
public:
    //Creates an orthographic camera with the supplied visible bounds.
    OrthographicCamera(float left, float right, float bottom, float top);

    //Replaces the camera projection with new visible bounds.
    void SetProjection(float left, float right, float bottom, float top);

    //Moves the camera and rebuilds its view matrix.
    void SetPosition(const glm::vec3& position);

    //Returns the camera position in world space.
    const glm::vec3& GetPosition() const { return m_Position; }

    //Rotates the camera around the forward axis in degrees.
    void SetRotation(float rotation);

    //Returns the camera rotation in degrees.
    float GetRotation() const { return m_Rotation; }

    //Returns the matrix that projects camera space into clip space.
    const glm::mat4& GetProjectionMatrix() const { return m_ProjectionMatrix; }

    //Returns the matrix that transforms world space into camera space.
    const glm::mat4& GetViewMatrix() const { return m_ViewMatrix; }

    //Returns the combined world-to-clip-space matrix.
    const glm::mat4& GetViewProjectionMatrix() const { return m_ViewProjectionMatrix; }

private:
    //Rebuilds the view and combined matrices after the camera moves.
    void RecalculateViewMatrix();

    glm::mat4 m_ProjectionMatrix{ 1.0f };
    glm::mat4 m_ViewMatrix{ 1.0f };
    glm::mat4 m_ViewProjectionMatrix{ 1.0f };
    glm::vec3 m_Position{ 0.0f };
    float m_Rotation = 0.0f;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
