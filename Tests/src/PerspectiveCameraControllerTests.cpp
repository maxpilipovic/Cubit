#include <doctest.h>

#include "Cubit/Renderer/PerspectiveCameraController.h"
#include "Cubit/Events/MouseEvent.h"

#include <cmath>

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

TEST_CASE("After mouse tracking is reset, the next move is a new reference rather than a jump")
{
    //A3. While the cursor is released it moves freely, so the last position the
    //controller saw means nothing by the time the game takes the mouse back.
    //Measuring the first move after that against it would swing the view by
    //however far the cursor wandered.
    PerspectiveCameraController controller(16.0f / 9.0f);
    controller.SetRotation(0.0f, 0.0f);

    MouseMovedEvent first(100.0, 100.0);
    controller.OnEvent(first);
    MouseMovedEvent second(110.0, 100.0);
    controller.OnEvent(second);
    REQUIRE(controller.GetYaw() == doctest::Approx(1.2f));

    controller.ResetMouseTracking();

    MouseMovedEvent faraway(900.0, 400.0);
    controller.OnEvent(faraway);
    CHECK(controller.GetYaw() == doctest::Approx(1.2f));
    CHECK(controller.GetPitch() == doctest::Approx(0.0f));

    MouseMovedEvent onward(910.0, 400.0);
    controller.OnEvent(onward);
    CHECK(controller.GetYaw() == doctest::Approx(2.4f));
}

TEST_CASE("Setting the field of view rebuilds the projection at once")
{
    PerspectiveCameraController controller(16.0f / 9.0f);
    controller.SetRotation(0.0f, 0.0f);

    //With the camera level, the view leaves the vertical axis alone, so this
    //entry of view-projection is the projection's own 1 / tan(fov / 2): about
    //1.732 at the default 60 degrees, and exactly 1 at 90.
    CHECK(controller.GetCamera().GetViewProjectionMatrix()[1][1] ==
        doctest::Approx(1.0f / std::tan(glm::radians(30.0f))));

    controller.SetFieldOfView(90.0f);

    CHECK(controller.GetFieldOfView() == doctest::Approx(90.0f));
    CHECK(controller.GetCamera().GetViewProjectionMatrix()[1][1] == doctest::Approx(1.0f));
}

TEST_CASE("Mouse sensitivity sets how far a mouse move turns the view")
{
    PerspectiveCameraController controller(16.0f / 9.0f);
    controller.SetRotation(0.0f, 0.0f);
    controller.SetMouseSensitivity(0.5f);

    MouseMovedEvent first(100.0, 100.0);
    controller.OnEvent(first);
    MouseMovedEvent second(110.0, 100.0);
    controller.OnEvent(second);

    //10 pixels at 0.5 degrees a pixel, where the default 0.12 would give 1.2.
    CHECK(controller.GetMouseSensitivity() == doctest::Approx(0.5f));
    CHECK(controller.GetYaw() == doctest::Approx(5.0f));
}
