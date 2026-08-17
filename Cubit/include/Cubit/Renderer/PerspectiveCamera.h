#pragma once

#include "Cubit/Renderer/Camera.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

class CB_API PerspectiveCamera final : public Camera
{
public:
    //Creates a perspective camera using vertical field of view in degrees.
    PerspectiveCamera(float fieldOfView, float aspectRatio, float nearClip, float farClip);

    //Updates the perspective projection parameters.
    void SetProjection(float fieldOfView, float aspectRatio, float nearClip, float farClip);

    //Moves the camera to a world-space position.
    void SetPosition(const glm::vec3& position);

    //Sets horizontal yaw and vertical pitch in degrees.
    void SetRotation(float yaw, float pitch);

    //Returns the yaw and pitch, in degrees, that face from one point toward
    //another.
    //
    //Lives here because this is where the convention lives: it inverts the
    //forward vector RecalculateViewMatrix builds, which is the reason -90
    //degrees means -z. A caller doing its own atan2 would be duplicating a
    //constant it does not own.
    static glm::vec2 YawPitchToward(const glm::vec3& from, const glm::vec3& to);

    const glm::vec3& GetPosition() const { return m_Position; }
    const glm::vec3& GetForwardDirection() const { return m_ForwardDirection; }
    glm::vec3 GetRightDirection() const;
    const glm::mat4& GetViewProjectionMatrix() const override { return m_ViewProjectionMatrix; }

private:
    //Rebuilds direction and view matrices after the camera moves or rotates.
    void RecalculateViewMatrix();

    glm::mat4 m_ProjectionMatrix{ 1.0f };
    glm::mat4 m_ViewMatrix{ 1.0f };
    glm::mat4 m_ViewProjectionMatrix{ 1.0f };
    glm::vec3 m_Position{ 0.0f, 0.0f, 3.0f };
    glm::vec3 m_ForwardDirection{ 0.0f, 0.0f, -1.0f };
    float m_Yaw = -90.0f;
    float m_Pitch = 0.0f;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
