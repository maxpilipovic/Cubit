#include <doctest.h>

#include "Cubit/Renderer/PerspectiveCameraController.h"
#include "Cubit/Events/MouseEvent.h"

TEST_CASE("Setting rotation aims the camera")
{
    PerspectiveCameraController controller(16.0f / 9.0f);

    //Yaw 0 faces +x under the camera's convention.
    controller.SetRotation(0.0f, 0.0f);

    const glm::vec3 forward = controller.GetCamera().GetForwardDirection();
    CHECK(forward.x == doctest::Approx(1.0f));
    CHECK(forward.y == doctest::Approx(0.0f));
    CHECK(forward.z == doctest::Approx(0.0f).epsilon(0.001));
    CHECK(controller.GetYaw() == doctest::Approx(0.0f));
    CHECK(controller.GetPitch() == doctest::Approx(0.0f));
}

TEST_CASE("Setting rotation clamps pitch to the mouse-look limit")
{
    PerspectiveCameraController controller(16.0f / 9.0f);

    controller.SetRotation(0.0f, 175.0f);

    CHECK(controller.GetPitch() == doctest::Approx(89.0f));
}

TEST_CASE("A mouse move continues from a rotation that was set")
{
    //The bug this guards: yaw and pitch are stored on both the controller and
    //the camera. Aiming the camera directly leaves the controller's copy stale,
    //and the next mouse move recomputes from it and snaps the view back.
    PerspectiveCameraController controller(16.0f / 9.0f);
    controller.SetRotation(0.0f, 0.0f);

    //The first move only records a reference position, so it takes two to
    //produce an offset — the same as the real input path.
    MouseMovedEvent first(100.0, 100.0);
    controller.OnEvent(first);
    MouseMovedEvent second(110.0, 100.0);
    controller.OnEvent(second);

    //10 pixels at the 0.12 sensitivity is 1.2 degrees on top of the yaw set
    //above, not 1.2 degrees on top of the -90 default.
    CHECK(controller.GetYaw() == doctest::Approx(1.2f));
}
