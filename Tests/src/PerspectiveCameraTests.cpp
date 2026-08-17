#include <doctest.h>

#include "Cubit/Renderer/PerspectiveCamera.h"

#include <glm/glm.hpp>

TEST_CASE("Yaw and pitch toward a point match the camera's convention")
{
    //-90 degrees faces -z, which is the camera's default facing.
    const glm::vec2 backward =
        PerspectiveCamera::YawPitchToward(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    CHECK(backward.x == doctest::Approx(-90.0f));
    CHECK(backward.y == doctest::Approx(0.0f));

    //0 degrees faces +x.
    const glm::vec2 right =
        PerspectiveCamera::YawPitchToward(glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    CHECK(right.x == doctest::Approx(0.0f));
    CHECK(right.y == doctest::Approx(0.0f));
}

TEST_CASE("Yaw and pitch toward a point carry elevation")
{
    //Level with the target horizontally, one unit up: 45 degrees of pitch.
    const glm::vec2 up =
        PerspectiveCamera::YawPitchToward(glm::vec3(0.0f), glm::vec3(1.0f, 1.0f, 0.0f));

    CHECK(up.x == doctest::Approx(0.0f));
    CHECK(up.y == doctest::Approx(45.0f));
}

TEST_CASE("A camera aimed toward a point looks at it")
{
    //The strongest check: round-trip the result back through the camera and
    //confirm the forward vector really points at the target. A sign or axis
    //error survives the angle checks above but fails here.
    const glm::vec3 eye(3.0f, 10.0f, -4.0f);
    const glm::vec3 target(70.0f, 2.0f, 55.0f);

    const glm::vec2 rotation = PerspectiveCamera::YawPitchToward(eye, target);

    PerspectiveCamera camera(60.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
    camera.SetPosition(eye);
    camera.SetRotation(rotation.x, rotation.y);

    const glm::vec3 expected = glm::normalize(target - eye);
    const glm::vec3 actual = camera.GetForwardDirection();

    CHECK(glm::dot(expected, actual) == doctest::Approx(1.0f).epsilon(0.0001));
}

TEST_CASE("Yaw and pitch toward the camera's own position fall back to the default")
{
    //No direction to derive. Returning the default beats returning a NaN that
    //would silently poison the view matrix.
    const glm::vec2 rotation =
        PerspectiveCamera::YawPitchToward(glm::vec3(5.0f), glm::vec3(5.0f));

    CHECK(rotation.x == doctest::Approx(-90.0f));
    CHECK(rotation.y == doctest::Approx(0.0f));
}
