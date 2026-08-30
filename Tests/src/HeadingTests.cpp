#include <doctest.h>

#include "Cubit/Voxel/Heading.h"
#include "Cubit/Renderer/PerspectiveCamera.h"

#include <glm/glm.hpp>

TEST_CASE("Heading matches the camera's flattened forward and right")
{
    //The oracle. PerspectiveCamera owns the yaw convention the whole engine
    //renders with; Heading is a second copy of it that cannot see the camera,
    //so it is checked against the original rather than against a formula
    //rewritten from the same memory that produced it.
    //
    //Pitch is varied deliberately: the camera builds forward from yaw AND
    //pitch and the old walk code flattened the result, so if Heading (yaw
    //only) is to replace that, it has to agree at every pitch. It does,
    //because PerspectiveCameraController clamps pitch to +/-89 degrees, so
    //cos(pitch) is always positive and drops out of the normalise.
    PerspectiveCamera camera(60.0f, 16.0f / 9.0f, 0.1f, 1000.0f);

    for (float yaw = -360.0f; yaw <= 360.0f; yaw += 7.5f)
    {
        for (float pitch : { -89.0f, -45.0f, 0.0f, 45.0f, 89.0f })
        {
            camera.SetRotation(yaw, pitch);

            glm::vec3 forward = camera.GetForwardDirection();
            forward.y = 0.0f;
            forward = glm::normalize(forward);

            glm::vec3 right = camera.GetRightDirection();
            right.y = 0.0f;
            right = glm::normalize(right);

            const glm::vec3 headingForward = HeadingForward(yaw);
            const glm::vec3 headingRight = HeadingRight(yaw);

            INFO("yaw " << yaw << " pitch " << pitch);
            CHECK(headingForward.x == doctest::Approx(forward.x).epsilon(0.0001));
            CHECK(headingForward.y == doctest::Approx(0.0f));
            CHECK(headingForward.z == doctest::Approx(forward.z).epsilon(0.0001));
            CHECK(headingRight.x == doctest::Approx(right.x).epsilon(0.0001));
            CHECK(headingRight.y == doctest::Approx(0.0f));
            CHECK(headingRight.z == doctest::Approx(right.z).epsilon(0.0001));
        }
    }
}

TEST_CASE("Heading is unit length and flat")
{
    for (float yaw = -180.0f; yaw <= 180.0f; yaw += 11.0f)
    {
        CHECK(glm::length(HeadingForward(yaw)) == doctest::Approx(1.0f));
        CHECK(glm::length(HeadingRight(yaw)) == doctest::Approx(1.0f));
        CHECK(HeadingForward(yaw).y == doctest::Approx(0.0f));
        CHECK(HeadingRight(yaw).y == doctest::Approx(0.0f));
    }
}

TEST_CASE("Yaw zero faces positive x, with right toward positive z")
{
    //Written out so the convention is legible without deriving it. Movement
    //tests elsewhere use yaw 0 and rely on exactly this.
    CHECK(HeadingForward(0.0f).x == doctest::Approx(1.0f));
    CHECK(HeadingForward(0.0f).z == doctest::Approx(0.0f));
    CHECK(HeadingRight(0.0f).x == doctest::Approx(0.0f));
    CHECK(HeadingRight(0.0f).z == doctest::Approx(1.0f));
}
