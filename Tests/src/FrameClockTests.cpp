#include <doctest.h>

#include "Cubit/FrameClock.h"

namespace
{
    constexpr double Step = FrameClock::FixedStepSeconds;
}

TEST_CASE("A frame shorter than a step runs nothing and banks the remainder")
{
    FrameClock clock;

    CHECK(clock.Advance(Step * 0.5) == 0);
    CHECK(clock.Alpha() == doctest::Approx(0.5f));
}

TEST_CASE("Exactly one step runs one step and leaves nothing over")
{
    FrameClock clock;

    CHECK(clock.Advance(Step) == 1);
    CHECK(clock.Alpha() == doctest::Approx(0.0f));
}

TEST_CASE("One and a half steps runs one step and keeps the half")
{
    FrameClock clock;

    CHECK(clock.Advance(Step * 1.5) == 1);
    CHECK(clock.Alpha() == doctest::Approx(0.5f));
}

TEST_CASE("A zero delta owes nothing")
{
    FrameClock clock;

    CHECK(clock.Advance(0.0) == 0);
    CHECK(clock.Alpha() == doctest::Approx(0.0f));
}

TEST_CASE("Short frames accumulate into a step on the frame that crosses")
{
    //The fencepost this unit exists for: two frames of 0.4 owe nothing, and the
    //third crosses the boundary rather than the second.
    FrameClock clock;

    CHECK(clock.Advance(Step * 0.4) == 0);
    CHECK(clock.Advance(Step * 0.4) == 0);
    CHECK(clock.Advance(Step * 0.4) == 1);
    CHECK(clock.Alpha() == doctest::Approx(0.2f));
}

TEST_CASE("A long stall runs at most the cap and discards the surplus")
{
    FrameClock clock;

    CHECK(clock.Advance(3.0) == FrameClock::MaxTicksPerFrame);

    //Whole steps still owed are dropped, so nothing is left to interpolate by.
    CHECK(clock.Alpha() == doctest::Approx(0.0f));
}

TEST_CASE("The frame after a stall carries no debt")
{
    FrameClock clock;
    clock.Advance(3.0);

    CHECK(clock.Advance(Step) == 1);
    CHECK(clock.Alpha() == doctest::Approx(0.0f));
}

TEST_CASE("A negative delta is ignored rather than subtracted")
{
    //A clock that ran backwards would hand back time the simulation already ran.
    FrameClock clock;
    clock.Advance(Step * 0.75);

    CHECK(clock.Advance(-1.0) == 0);
    CHECK(clock.Alpha() == doctest::Approx(0.75f));
}

TEST_CASE("A thousand awkward frames do not drift")
{
    FrameClock clock;

    int total = 0;
    for (int frame = 0; frame < 1000; ++frame)
        total += clock.Advance(0.0073);

    //7.3 seconds at 60 Hz is 438 steps. Allow one either side for the accumulated
    //floating-point error this test exists to bound.
    CHECK(total >= 437);
    CHECK(total <= 439);
}
