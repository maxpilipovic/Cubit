#include <doctest.h>

#include "Cubit/Voxel/HitboxHistory.h"
#include "Cubit/Voxel/ResolveShot.h"
#include "Cubit/Voxel/World.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace
{
    const glm::vec3 PlayerHalfExtents{ 0.3f, 0.9f, 0.3f };

    World EmptyWorld()
    {
        return World(4, 2, 4);
    }

    //A target strafing along z at 5 blocks a second, sampled once per tick.
    glm::vec3 TargetAt(std::uint64_t tick)
    {
        const float seconds = static_cast<float>(tick) / 60.0f;
        return glm::vec3(20.0f, 1.0f, seconds * 5.0f);
    }
}

TEST_CASE("Rewinding to the instant the shooter saw is what turns a miss into a hit")
{
    //THE ORACLE FOR THIS STAGE. Everything else is plumbing that arranges for
    //these two resolutions to happen with the right numbers.
    //
    //A target strafes past. The shooter's screen is six ticks behind the
    //server plus three ticks of latency, so they aim at where the target was
    //nine ticks ago - which is where the target genuinely was, on their screen.
    //The server, stepping in the present, sees the target 0.75 blocks further
    //along. That is more than the 0.6-block width of the box, so the two
    //answers MUST differ. If they do not, this stage has nothing to build.
    HitboxHistory history;
    for (std::uint64_t tick = 0; tick <= 100; ++tick)
        history.Record(2, tick, TargetAt(tick));

    const std::uint64_t serverTick = 100;
    const double renderedInstant = 91.0;

    //Aim at exactly where the shooter's screen showed the target: the centre
    //of the box the history rebuilds at that instant. This is the same
    //computation MatchClient::PoseOf performs to draw them, which is the point
    //- the test aims at the rendered position, not at a guess about it.
    Aabb seen;
    REQUIRE(history.BoxAt(2, renderedInstant, PlayerHalfExtents, seen));
    const glm::vec3 aimPoint = (seen.Min + seen.Max) * 0.5f;

    const glm::vec3 eye(0.0f, 1.0f, aimPoint.z);
    const glm::vec3 direction = glm::normalize(aimPoint - eye);

    World world = EmptyWorld();

    //WITH the rewind: resolve against the box as it was at the rendered instant.
    std::vector<ShotCandidate> rewound{ ShotCandidate{ 2, seen } };
    const ShotResult compensated =
        ResolveShot(world, rewound, eye, direction, 128.0f);

    //WITHOUT the rewind: resolve against the box as it is now. This is the
    //mutation the gate exists to catch, run as a branch rather than left to a
    //reviewer to perform by hand.
    Aabb present;
    REQUIRE(history.BoxAt(2, static_cast<double>(serverTick), PlayerHalfExtents, present));
    std::vector<ShotCandidate> live{ ShotCandidate{ 2, present } };
    const ShotResult uncompensated =
        ResolveShot(world, live, eye, direction, 128.0f);

    CHECK(compensated.Victim == 2);

    //THE CLAIM THE WHOLE DESIGN RESTS ON. If this fails, the rewind is not
    //doing anything and the stage is pointless - either the target is too slow,
    //the latency too small, or the design is wrong. Report before continuing.
    CHECK(uncompensated.Victim == InvalidPlayer);
}

TEST_CASE("Half a tick of rewind error is enough to miss")
{
    //Why the wire carries a fractional instant rather than a whole tick. At
    //5 blocks a second a half tick is 0.042 blocks, which alone would not miss
    //- so this test uses the edge of the box, where it does. The point is that
    //a whole-tick rewind is not free, and that the error is invisible when you
    //aim at the middle.
    HitboxHistory history;
    for (std::uint64_t tick = 0; tick <= 100; ++tick)
        history.Record(2, tick, TargetAt(tick));

    Aabb exact;
    REQUIRE(history.BoxAt(2, 91.5, PlayerHalfExtents, exact));

    Aabb rounded;
    REQUIRE(history.BoxAt(2, 91.0, PlayerHalfExtents, rounded));

    //Aim just inside the LEADING edge of where the target actually was.
    //
    //The edge matters and picking the wrong one makes this test unpassable.
    //The rounded box is half a tick EARLIER, so it sits 0.042 blocks back
    //along +z: exact spans [7.325, 7.925] and rounded spans [7.283, 7.883].
    //The sliver that is in exact but not in rounded is the leading edge,
    //(7.883, 7.925] - aiming near exact.Min.z lands inside BOTH boxes and the
    //miss never happens.
    const glm::vec3 aimPoint(exact.Min.x + 0.3f, 1.0f, exact.Max.z - 0.01f);
    const glm::vec3 eye(0.0f, 1.0f, aimPoint.z);
    const glm::vec3 direction = glm::normalize(aimPoint - eye);

    World world = EmptyWorld();

    std::vector<ShotCandidate> right{ ShotCandidate{ 2, exact } };
    std::vector<ShotCandidate> wrong{ ShotCandidate{ 2, rounded } };

    CHECK(ResolveShot(world, right, eye, direction, 128.0f).Victim == 2);
    CHECK(ResolveShot(world, wrong, eye, direction, 128.0f).Victim == InvalidPlayer);
}
