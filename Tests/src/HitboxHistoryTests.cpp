#include <doctest.h>

#include "Cubit/Voxel/HitboxHistory.h"

#include <glm/glm.hpp>
#include <cstdint>

namespace
{
    const glm::vec3 HalfExtents{ 0.3f, 0.9f, 0.3f };
}

TEST_CASE("A box is rebuilt at a fractional instant by lerping the bracketing samples")
{
    //THE PROPERTY THE WHOLE STAGE RESTS ON. The client renders a remote by
    //lerping between two snapshot samples; if the server rebuilt the box any
    //other way it would be aiming at a different target than the one the
    //shooter saw, and no amount of correct networking would fix it.
    HitboxHistory history;
    history.Record(1, 10, glm::vec3(0.0f, 0.0f, 0.0f));
    history.Record(1, 11, glm::vec3(4.0f, 0.0f, 0.0f));

    Aabb box;
    REQUIRE(history.BoxAt(1, 10.25, HalfExtents, box));

    //A quarter of the way from x=0 to x=4 is x=1, so the box spans 0.7 to 1.3.
    CHECK(box.Min.x == doctest::Approx(0.7f));
    CHECK(box.Max.x == doctest::Approx(1.3f));
    CHECK(box.Min.y == doctest::Approx(-0.9f));
    CHECK(box.Max.y == doctest::Approx(0.9f));
}

TEST_CASE("A whole-tick instant lands exactly on its sample")
{
    HitboxHistory history;
    history.Record(1, 10, glm::vec3(0.0f, 0.0f, 0.0f));
    history.Record(1, 11, glm::vec3(4.0f, 0.0f, 0.0f));

    Aabb box;
    REQUIRE(history.BoxAt(1, 11.0, HalfExtents, box));
    CHECK(box.Min.x == doctest::Approx(3.7f));
    CHECK(box.Max.x == doctest::Approx(4.3f));
}

TEST_CASE("A player with no record at that instant is not a candidate")
{
    //This one rule covers three cases at once: a respawn (which Forgets), a
    //player who joined two ticks ago, and the first ticks of a match. Without
    //it, an in-flight shot could rewind past a death and damage the player who
    //has since respawned there.
    HitboxHistory history;
    history.Record(1, 100, glm::vec3(0.0f));
    history.Record(1, 101, glm::vec3(0.0f));

    Aabb box;
    CHECK_FALSE(history.BoxAt(1, 99.0, HalfExtents, box));
    CHECK_FALSE(history.BoxAt(2, 100.5, HalfExtents, box));

    history.Forget(1);
    CHECK_FALSE(history.BoxAt(1, 100.5, HalfExtents, box));
    CHECK(history.SampleCount(1) == 0);
}

TEST_CASE("An instant newer than every sample holds the newest rather than guessing")
{
    //Same discipline as PoseOf: extrapolation is right most of the time and
    //wrong exactly at a stop, a turn or a jump.
    HitboxHistory history;
    history.Record(1, 10, glm::vec3(0.0f));
    history.Record(1, 11, glm::vec3(4.0f, 0.0f, 0.0f));

    Aabb box;
    REQUIRE(history.BoxAt(1, 50.0, HalfExtents, box));
    CHECK(box.Min.x == doctest::Approx(3.7f));
}

TEST_CASE("The ring keeps the newest MaxHistorySamples and evicts the rest")
{
    HitboxHistory history;
    for (std::uint64_t tick = 0; tick < MaxHistorySamples + 10; ++tick)
        history.Record(1, tick, glm::vec3(static_cast<float>(tick), 0.0f, 0.0f));

    CHECK(history.SampleCount(1) == MaxHistorySamples);

    //Tick 9 was evicted; the oldest kept is tick 10.
    Aabb box;
    CHECK_FALSE(history.BoxAt(1, 9.0, HalfExtents, box));
    CHECK(history.BoxAt(1, 10.0, HalfExtents, box));
}
