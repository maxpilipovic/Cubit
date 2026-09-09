# Networking Stage 4 (The Shot) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A player can shoot another player at 150 ms and hit them if they were under the crosshair on the shooter's own screen; three hits kill, death respawns, and a scripted run reports a hit rate that gets materially worse when the rewind is switched off.

**Architecture:** The server keeps a short ring of past positions per player. A client's `Fire` message declares the fractional instant its screen was showing, in the server's tick numbering; the server clamps that into a 15-tick window, rebuilds every other player's hitbox at that instant by the same lerp the client rendered with, and resolves a ray against those boxes and the terrain. The shooter's own eye comes from a different instant — the input tick they fired on — because that is where the server already believes they stand.

**Tech Stack:** C++20, MSVC (Visual Studio 18 / vs2026), premake5, doctest, GLM, ENet.

**Spec:** `docs/superpowers/specs/2026-09-08-networking-stage-4-design.md` (read it; the arc-level spec it builds on is `docs/superpowers/specs/2026-08-27-networking-design.md`, and Stage 3's is `docs/superpowers/specs/2026-09-03-networking-stage-3-design.md`)

## Global Constraints

- **C++20**, `cppdialect "C++20"` in `premake5.lua`. `std::span` and `std::bit_cast` are available.
- **`Cubit/src/Voxel/`, `Cubit/include/Cubit/Voxel/`, `Cubit/src/Net/` and `Cubit/include/Cubit/Net/` must stay GL-free.** No `glad`, `GLFW`, or `gl*`. This is what lets the simulation and the server run headless.
- **Any exported class (`CB_API`) with a `std::` or `glm::` member needs the 4251 pragma guard**, matching the file it lives in:
  ```cpp
  #ifdef _MSC_VER
  #pragma warning(push)
  #pragma warning(disable: 4251)
  #endif
  // ... declarations ...
  #ifdef _MSC_VER
  #pragma warning(pop)
  #endif
  ```
- **`CB_API` is `dllexport`. An exported class must define every member it declares.** Never declare a member and leave its body for a later commit — that is LNK2019, not a TODO.
- **Every new `.cpp` under `Cubit/src/` must `#include "cub.h"` as its first line.** The project uses a precompiled header; the build fails otherwise.
- **Premake globs expand at generation time.** After adding any new file, run `/c/dev/premake/premake5 vs2026` from the repo root. Do **not** run `GenerateProjects.bat` — it deletes `bin/` and ends in `pause`, which hangs a non-interactive shell.
- **Tests include `<doctest.h>`**, not `<doctest/doctest.h>`.
- **Build command** (repo root):
  ```bash
  MSB="/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe"
  "$MSB" Cubit.slnx -p:Configuration=Debug -p:Platform=x64 -m -v:minimal -nologo
  ```
  The suite runs as a post-build step, so a failing test fails the build. One case on its own: `./bin/Debug-windows-x86_64/Tests/Tests.exe -tc="the case name"`.
- **`NetworkSim::Latency` is ONE-WAY seconds. The `--latency` command-line flag is ROUND-TRIP milliseconds** and is halved on the way in.
- **All test latencies must be whole tick multiples.** One tick is `1.0 / 60.0` s. The canonical test network is 50 ms one-way (exactly 3 ticks), 100 ms RTT, 5% loss, seed 1.
- **Wire fields are fixed-width little-endian.** No varints, no bit packing, no compression.
- **`ProtocolVersion` becomes 3 in Task 4 and stays 3.**
- **Never add Claude co-author trailers or attribution to commits.** Do end each commit with the `Claude-Session:` trailer the session provides, matching every commit already on this branch.
- **Commit messages are a subject plus a wrapped prose body.** The `git commit -m "subject"` lines below are shorthand for the subject, not permission to skip the body.
- **Single-player must not change.** The acceptance check is `Sandbox.exe` with no flags reporting `POS 240.500000 26.900099 300.500000` and `FACES 1927774`. `FACES` only reaches that number after amortized meshing settles (~1110 frames), so sample it when `PendingCount() == 0`, never at a fixed frame count.
- **Try to make every new invariant test fail before trusting it.** Stage 1's determinism test and Stage 2's `Serial` tie-break both looked solid and were not. If a mutation will not turn a test red, work out whether the test is weak or the property is genuinely unfalsifiable *before* deciding anything.

---

## File Structure

| File | Responsibility |
|---|---|
| `Cubit/include/Cubit/Voxel/HitboxHistory.h` `Cubit/src/Voxel/HitboxHistory.cpp` | **Create.** Per-player ring of past positions; rebuilds an AABB at a fractional instant. No networking, no GL. |
| `Cubit/include/Cubit/Voxel/ResolveShot.h` `Cubit/src/Voxel/ResolveShot.cpp` | **Create.** Pure ray-vs-(boxes + terrain) resolution. |
| `Tests/src/HitboxHistoryTests.cpp` | **Create.** Ring wrap, fractional lerp against a hand-computed oracle, the no-record rule. |
| `Tests/src/ResolveShotTests.cpp` | **Create.** Terrain occlusion, ties, misses, shooter exclusion. |
| `Tests/src/LagCompensationTests.cpp` | **Create.** The rewind oracle (Task 3) and the networked acceptance gate and hit-rate number (Task 9). |
| `Cubit/include/Cubit/Voxel/Heading.h` | **Modify.** `AimDirection(yaw, pitch)` — the pitched sibling of `HeadingForward`, which does not exist today. |
| `Tests/src/HeadingTests.cpp` | **Modify.** Pin `AimDirection` against `PerspectiveCamera`'s own forward vector. |
| `Cubit/include/Cubit/Net/Protocol.h` `Cubit/src/Net/Protocol.cpp` | **Modify.** Version 3: `FireMessage`, `ShotResolvedMessage`, `PlayerSnapshot::Health`. |
| `Tests/src/ProtocolTests.cpp` | **Modify.** Round-trips for both new messages, the `Health` field, the bumped guard constant. |
| `Cubit/include/Cubit/Net/MatchServer.h` `Cubit/src/Net/MatchServer.cpp` | **Modify.** History recording, `Fire` handling, the clamp, the rate limit, damage, respawn. |
| `Tests/src/MatchServerTests.cpp` | **Modify.** Recording, clamping, rate limiting, damage, death, respawn. |
| `Cubit/include/Cubit/Net/MatchClient.h` `Cubit/src/Net/MatchClient.cpp` | **Modify.** `Fire`, `ShotResolved` handling, the last shot result for the HUD. |
| `Sandbox/src/DebugFont.h` | **Modify.** The missing letters. |
| `Sandbox/src/Sandbox.cpp` | **Modify.** Middle mouse fires, the local tracer, the HUD. |
| `docs/engine-roadmap.md` `docs/superpowers/specs/2026-09-08-networking-stage-4-design.md` | **Modify.** Record what shipped and what turned out differently. |

---

## Task 1: `HitboxHistory`

**Files:**
- Create: `Cubit/include/Cubit/Voxel/HitboxHistory.h`, `Cubit/src/Voxel/HitboxHistory.cpp`
- Test: `Tests/src/HitboxHistoryTests.cpp`

**Interfaces:**
- Consumes: `PlayerId`, `InvalidPlayer` from `Cubit/Voxel/MatchState.h`.
- Produces:
  - `struct Aabb { glm::vec3 Min; glm::vec3 Max; };`
  - `constexpr std::size_t MaxHistorySamples = 16;`
  - `constexpr int MaxRewindTicks = 15;`
  - `void HitboxHistory::Record(PlayerId, std::uint64_t tick, const glm::vec3& position)`
  - `void HitboxHistory::Forget(PlayerId)`
  - `bool HitboxHistory::BoxAt(PlayerId, double instant, const glm::vec3& halfExtents, Aabb& out) const`
  - `std::size_t HitboxHistory::SampleCount(PlayerId) const`

- [ ] **Step 1: Write the failing tests**

Create `Tests/src/HitboxHistoryTests.cpp`:

```cpp
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
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `"$MSB" Cubit.slnx -p:Configuration=Debug -p:Platform=x64 -m -v:minimal -nologo`
Expected: the build fails — `Cubit/Voxel/HitboxHistory.h` does not exist. (Run `/c/dev/premake/premake5 vs2026` first so the new test file is in the project.)

- [ ] **Step 3: Write the header**

Create `Cubit/include/Cubit/Voxel/HitboxHistory.h`:

```cpp
#pragma once

#include "Cubit/Core.h"
#include "Cubit/Voxel/MatchState.h"

#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

//An axis-aligned box in world space.
struct Aabb
{
    glm::vec3 Min{ 0.0f };
    glm::vec3 Max{ 0.0f };
};

//How many past positions are kept per player.
//
//MaxRewindTicks of window plus one more, because a fractional instant needs a
//sample on either side of it to lerp between. Fifteen alone would leave the
//oldest reachable instant with nothing below it to bracket against.
constexpr std::size_t MaxHistorySamples = 16;

//How far back a shot may be resolved, in ticks. Fifteen is 250 ms at 60 Hz.
//
//This is the "shot behind cover" window: a target who reached safety within
//this many ticks can still be hit by someone whose screen had not caught up.
//Chosen to cover the 150 ms link this arc targets with room for jitter.
constexpr int MaxRewindTicks = 15;

//Where everybody has recently been, so a shot can be resolved against the world
//as the shooter saw it rather than as it is now.
//
//Holds no networking and no GL, which is the same rule that keeps MatchState in
//Voxel/ - and it is what lets this be tested with no server, no transport and
//no socket.
//
//Positions only. A hitbox is HalfExtents around a position; neither velocity nor
//grounded shapes it, and keeping a whole CharacterController here would invite
//somebody to rewind physics rather than geometry.
class CB_API HitboxHistory
{
public:
    //Appends this player's position for a tick, evicting the oldest sample once
    //the ring is full. Ticks are expected to arrive in increasing order, which
    //is what a server stepping once per tick produces.
    void Record(PlayerId player, std::uint64_t tick, const glm::vec3& position);

    //Drops everything known about a player. Called on respawn and on
    //disconnect - on respawn because a shot must never rewind across a death
    //and damage whoever now stands where the dead player did.
    void Forget(PlayerId player);

    //Rebuilds this player's box at a fractional instant, lerping between the
    //two bracketing samples exactly as MatchClient::PoseOf does when it draws
    //them.
    //
    //Returns false when there is no record of this player at that instant -
    //either they are unknown, or the instant predates their oldest sample. That
    //is deliberately not a hit of zero size: "no record" and "recorded, and the
    //ray missed" are different answers, and a caller must not confuse them.
    //
    //An instant newer than every sample holds the newest rather than
    //extrapolating, for the reason PoseOf gives: being late costs a box drawn
    //where it was; being early costs a guess that has to be taken back.
    bool BoxAt(PlayerId player, double instant, const glm::vec3& halfExtents,
        Aabb& out) const;

    //How many samples are kept for a player. For tests and diagnostics.
    std::size_t SampleCount(PlayerId player) const;

private:
    struct Sample
    {
        std::uint64_t Tick = 0;
        glm::vec3 Position{ 0.0f };
    };

    std::map<PlayerId, std::deque<Sample>> m_Samples;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
```

- [ ] **Step 4: Write the implementation**

Create `Cubit/src/Voxel/HitboxHistory.cpp`:

```cpp
#include "cub.h"

#include "Cubit/Voxel/HitboxHistory.h"

namespace
{
    Aabb BoxAround(const glm::vec3& position, const glm::vec3& halfExtents)
    {
        return Aabb{ position - halfExtents, position + halfExtents };
    }
}

void HitboxHistory::Record(PlayerId player, std::uint64_t tick, const glm::vec3& position)
{
    std::deque<Sample>& samples = m_Samples[player];
    samples.push_back(Sample{ tick, position });

    if (samples.size() > MaxHistorySamples)
        samples.pop_front();
}

void HitboxHistory::Forget(PlayerId player)
{
    m_Samples.erase(player);
}

bool HitboxHistory::BoxAt(PlayerId player, double instant, const glm::vec3& halfExtents,
    Aabb& out) const
{
    const auto found = m_Samples.find(player);
    if (found == m_Samples.end() || found->second.empty())
        return false;

    const std::deque<Sample>& samples = found->second;

    //Older than anything kept is NOT a hit at the oldest position. There is no
    //record, and saying so is what stops a shot rewinding across a respawn.
    if (instant < static_cast<double>(samples.front().Tick))
        return false;

    const Sample& newest = samples.back();
    if (instant >= static_cast<double>(newest.Tick))
    {
        out = BoxAround(newest.Position, halfExtents);
        return true;
    }

    for (std::size_t i = 1; i < samples.size(); ++i)
    {
        const Sample& previous = samples[i - 1];
        const Sample& next = samples[i];

        if (instant > static_cast<double>(next.Tick))
            continue;

        const double span = static_cast<double>(next.Tick - previous.Tick);
        const float t = span <= 0.0
            ? 0.0f
            : static_cast<float>((instant - static_cast<double>(previous.Tick)) / span);

        out = BoxAround(glm::mix(previous.Position, next.Position, t), halfExtents);
        return true;
    }

    out = BoxAround(newest.Position, halfExtents);
    return true;
}

std::size_t HitboxHistory::SampleCount(PlayerId player) const
{
    const auto found = m_Samples.find(player);
    return found == m_Samples.end() ? 0 : found->second.size();
}
```

- [ ] **Step 5: Regenerate the project and build**

Run:
```bash
/c/dev/premake/premake5 vs2026
"$MSB" Cubit.slnx -p:Configuration=Debug -p:Platform=x64 -m -v:minimal -nologo
```
Expected: builds, and all five new cases pass.

- [ ] **Step 6: Try to make the lerp test fail**

Change `glm::mix(previous.Position, next.Position, t)` to `previous.Position` and rebuild.
Expected: "A box is rebuilt at a fractional instant" turns red (0.7 vs -0.3). Revert.

If it does **not** go red, the test is not pinning the lerp and must be fixed before continuing.

- [ ] **Step 7: Commit**

```bash
git add Cubit/include/Cubit/Voxel/HitboxHistory.h Cubit/src/Voxel/HitboxHistory.cpp Tests/src/HitboxHistoryTests.cpp
git commit -m "Remember where everybody has recently been"
```

---

## Task 2: `ResolveShot`

**Files:**
- Create: `Cubit/include/Cubit/Voxel/ResolveShot.h`, `Cubit/src/Voxel/ResolveShot.cpp`
- Test: `Tests/src/ResolveShotTests.cpp`

**Interfaces:**
- Consumes: `Aabb` from Task 1; `World`, `VoxelRaycast::Cast(world, origin, direction, maxDistance, solidOnly)`.
- Produces:
  - `struct ShotCandidate { PlayerId Player = InvalidPlayer; Aabb Box; };`
  - `struct ShotResult { PlayerId Victim = InvalidPlayer; glm::vec3 Impact{0.0f}; float Distance = 0.0f; };`
  - `CB_API ShotResult ResolveShot(const World&, std::span<const ShotCandidate>, const glm::vec3& origin, const glm::vec3& direction, float maxDistance);`

- [ ] **Step 1: Write the failing tests**

Create `Tests/src/ResolveShotTests.cpp`:

```cpp
#include <doctest.h>

#include "Cubit/Voxel/ResolveShot.h"
#include "Cubit/Voxel/World.h"

#include <glm/glm.hpp>
#include <vector>

namespace
{
    //An empty world, so terrain never interferes unless a test puts a block in.
    World EmptyWorld()
    {
        return World(2, 2, 2);
    }

    Aabb BoxAt(float x, float y, float z)
    {
        const glm::vec3 centre(x, y, z);
        const glm::vec3 half(0.3f, 0.9f, 0.3f);
        return Aabb{ centre - half, centre + half };
    }
}

TEST_CASE("A ray through a box reports that player and where it entered")
{
    World world = EmptyWorld();
    const std::vector<ShotCandidate> candidates{ ShotCandidate{ 2, BoxAt(10.0f, 1.0f, 0.0f) } };

    const ShotResult result = ResolveShot(
        world, candidates, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 128.0f);

    CHECK(result.Victim == 2);
    CHECK(result.Distance == doctest::Approx(9.7f));
    CHECK(result.Impact.x == doctest::Approx(9.7f));
}

TEST_CASE("A ray that misses every box reports no victim and the end of its range")
{
    World world = EmptyWorld();
    const std::vector<ShotCandidate> candidates{ ShotCandidate{ 2, BoxAt(10.0f, 1.0f, 5.0f) } };

    const ShotResult result = ResolveShot(
        world, candidates, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 128.0f);

    CHECK(result.Victim == InvalidPlayer);
    CHECK(result.Distance == doctest::Approx(128.0f));
    CHECK(result.Impact.x == doctest::Approx(128.0f));
}

TEST_CASE("The nearest of two boxes is the one that is hit")
{
    World world = EmptyWorld();
    const std::vector<ShotCandidate> candidates{
        ShotCandidate{ 2, BoxAt(20.0f, 1.0f, 0.0f) },
        ShotCandidate{ 3, BoxAt(10.0f, 1.0f, 0.0f) }
    };

    const ShotResult result = ResolveShot(
        world, candidates, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 128.0f);

    CHECK(result.Victim == 3);
}

TEST_CASE("Terrain in front of a player stops the shot")
{
    //Nobody may be shot through a wall. Ties go to terrain for the same reason.
    World world = EmptyWorld();
    world.SetBlock(5, 1, 0, BlockId{ 1 });

    const std::vector<ShotCandidate> candidates{ ShotCandidate{ 2, BoxAt(10.0f, 1.0f, 0.0f) } };

    const ShotResult result = ResolveShot(
        world, candidates, glm::vec3(0.0f, 1.5f, 0.5f), glm::vec3(1.0f, 0.0f, 0.0f), 128.0f);

    CHECK(result.Victim == InvalidPlayer);
    CHECK(result.Distance == doctest::Approx(5.0f));
}

TEST_CASE("Terrain behind a player does not stop the shot")
{
    World world = EmptyWorld();
    world.SetBlock(15, 1, 0, BlockId{ 1 });

    const std::vector<ShotCandidate> candidates{ ShotCandidate{ 2, BoxAt(10.0f, 1.5f, 0.5f) } };

    const ShotResult result = ResolveShot(
        world, candidates, glm::vec3(0.0f, 1.5f, 0.5f), glm::vec3(1.0f, 0.0f, 0.0f), 128.0f);

    CHECK(result.Victim == 2);
}

TEST_CASE("A box beyond the range is not hit")
{
    World world = EmptyWorld();
    const std::vector<ShotCandidate> candidates{ ShotCandidate{ 2, BoxAt(10.0f, 1.0f, 0.0f) } };

    const ShotResult result = ResolveShot(
        world, candidates, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 5.0f);

    CHECK(result.Victim == InvalidPlayer);
    CHECK(result.Distance == doctest::Approx(5.0f));
}

TEST_CASE("A box behind the shooter is not hit")
{
    World world = EmptyWorld();
    const std::vector<ShotCandidate> candidates{ ShotCandidate{ 2, BoxAt(-10.0f, 1.0f, 0.0f) } };

    const ShotResult result = ResolveShot(
        world, candidates, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 128.0f);

    CHECK(result.Victim == InvalidPlayer);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `/c/dev/premake/premake5 vs2026` then the build command.
Expected: build fails — `Cubit/Voxel/ResolveShot.h` does not exist.

- [ ] **Step 3: Write the header**

Create `Cubit/include/Cubit/Voxel/ResolveShot.h`:

```cpp
#pragma once

#include "Cubit/Core.h"
#include "Cubit/Voxel/HitboxHistory.h"
#include "Cubit/Voxel/MatchState.h"

#include <glm/glm.hpp>
#include <span>

class World;

//One player a shot might hit, at the position the shooter saw them.
struct ShotCandidate
{
    PlayerId Player = InvalidPlayer;
    Aabb Box;
};

//What a shot hit.
struct ShotResult
{
    //InvalidPlayer when the shot hit terrain or nothing at all. The impact
    //point is meaningful either way, which is why a miss is not signalled by a
    //flag: something is always drawn at the end of the ray.
    PlayerId Victim = InvalidPlayer;

    glm::vec3 Impact{ 0.0f };
    float Distance = 0.0f;
};

//Resolves one hitscan shot against player boxes and terrain, nearest first.
//
//Pure: it reads a World and a list of boxes and returns an answer. No history,
//no clock, no network. Whoever calls it decides WHICH boxes - that is where
//rewinding lives, and it is also where the shooter is left out of their own
//shot's candidates.
//
//Terrain takes ties, so a player standing flush against a wall cannot be shot
//through it.
//
//Water is aimed through rather than stopped at, matching the editing ray:
//VoxelRaycast::Cast is called with solidOnly, whose own comment has anticipated
//"a future shot" since it was added.
CB_API ShotResult ResolveShot(
    const World& world,
    std::span<const ShotCandidate> candidates,
    const glm::vec3& origin,
    const glm::vec3& direction,
    float maxDistance);
```

- [ ] **Step 4: Write the implementation**

Create `Cubit/src/Voxel/ResolveShot.cpp`:

```cpp
#include "cub.h"

#include "Cubit/Voxel/ResolveShot.h"

#include "Cubit/Voxel/VoxelRaycast.h"
#include "Cubit/Voxel/World.h"

#include <limits>

namespace
{
    //Slab method. Returns false when the ray misses, or when the box is
    //entirely behind the origin.
    //
    //A ray starting INSIDE the box returns a distance of 0, which is correct
    //here: point blank is a hit, not a miss.
    bool RayHitsBox(const glm::vec3& origin, const glm::vec3& direction,
        const Aabb& box, float maxDistance, float& distance)
    {
        float near = 0.0f;
        float far = maxDistance;

        for (int axis = 0; axis < 3; ++axis)
        {
            //Parallel to this pair of slabs: a miss unless the origin already
            //lies between them. Testing the reciprocal instead would divide by
            //zero and produce infinities that compare in surprising ways.
            if (glm::abs(direction[axis]) < 1e-8f)
            {
                if (origin[axis] < box.Min[axis] || origin[axis] > box.Max[axis])
                    return false;

                continue;
            }

            const float inverse = 1.0f / direction[axis];
            float enter = (box.Min[axis] - origin[axis]) * inverse;
            float exit = (box.Max[axis] - origin[axis]) * inverse;

            if (enter > exit)
                std::swap(enter, exit);

            near = glm::max(near, enter);
            far = glm::min(far, exit);

            if (near > far)
                return false;
        }

        distance = near;
        return true;
    }
}

ShotResult ResolveShot(
    const World& world,
    std::span<const ShotCandidate> candidates,
    const glm::vec3& origin,
    const glm::vec3& direction,
    float maxDistance)
{
    const glm::vec3 aim = glm::normalize(direction);

    PlayerId victim = InvalidPlayer;
    float best = maxDistance;

    for (const ShotCandidate& candidate : candidates)
    {
        float distance = 0.0f;
        if (!RayHitsBox(origin, aim, candidate.Box, maxDistance, distance))
            continue;

        if (distance >= best)
            continue;

        best = distance;
        victim = candidate.Player;
    }

    //Solid only: water is scenery you aim through, not a target.
    const VoxelRayHit terrain = VoxelRaycast::Cast(world, origin, aim, maxDistance, true);

    //<= rather than <, so terrain takes ties: a player flush against a wall is
    //never shot through it.
    if (terrain.Hit && terrain.Distance <= best)
    {
        ShotResult result;
        result.Victim = InvalidPlayer;
        result.Distance = terrain.Distance;
        result.Impact = origin + aim * terrain.Distance;
        return result;
    }

    ShotResult result;
    result.Victim = victim;
    result.Distance = best;
    result.Impact = origin + aim * best;
    return result;
}
```

- [ ] **Step 5: Build and run**

Run: `/c/dev/premake/premake5 vs2026` then the build command.
Expected: all seven cases pass.

- [ ] **Step 6: Try to make the tie rule fail**

Change `terrain.Distance <= best` to `terrain.Distance < best` and rebuild.
Expected: think about whether any test goes red. If none does, the tie rule is **not pinned** — add a test placing a box's near face exactly on a block boundary in front of it, confirm it goes red under the mutation, then revert. Do not leave the rule unpinned on the grounds that it "obviously works".

- [ ] **Step 7: Commit**

```bash
git add Cubit/include/Cubit/Voxel/ResolveShot.h Cubit/src/Voxel/ResolveShot.cpp Tests/src/ResolveShotTests.cpp
git commit -m "Resolve a hitscan shot against boxes and terrain"
```

---

## Task 3: The rewind oracle, with no networking in it at all

**Files:**
- Create: `Tests/src/LagCompensationTests.cpp`

**Interfaces:**
- Consumes: `HitboxHistory` (Task 1), `ResolveShot` (Task 2).
- Produces: nothing. This task adds no production code.

**Why this task exists, and why it is third rather than last.** The spec names the acceptance oracle as this design's weakest claim: rewinding is supposed to change the answer, and that has never been observed. The networked version of that gate cannot be built until Task 9, but the *claim itself* needs neither a wire nor a server — a history, a moving target and two resolutions are enough. If rewinding does not change the answer here, the design is wrong and the six tasks after this one would be built on it.

**Stop and report if the second `CHECK` in the first test does not hold.** Do not continue to Task 4.

- [ ] **Step 1: Write the oracle**

Create `Tests/src/LagCompensationTests.cpp`:

```cpp
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

    //Aim at the trailing edge of where the target actually was.
    const glm::vec3 aimPoint(exact.Min.x + 0.3f, 1.0f, exact.Min.z + 0.01f);
    const glm::vec3 eye(0.0f, 1.0f, aimPoint.z);
    const glm::vec3 direction = glm::normalize(aimPoint - eye);

    World world = EmptyWorld();

    std::vector<ShotCandidate> right{ ShotCandidate{ 2, exact } };
    std::vector<ShotCandidate> wrong{ ShotCandidate{ 2, rounded } };

    CHECK(ResolveShot(world, right, eye, direction, 128.0f).Victim == 2);
    CHECK(ResolveShot(world, wrong, eye, direction, 128.0f).Victim == InvalidPlayer);
}
```

- [ ] **Step 2: Build and run**

Run: `/c/dev/premake/premake5 vs2026` then the build command.
Expected: both cases pass.

**If "Rewinding to the instant the shooter saw" fails on its second CHECK** — meaning the uncompensated shot hits too — the rewind is not changing the answer. Stop, report the numbers, and do not start Task 4. The likely causes in order: the target is moving too slowly relative to the box width, nine ticks is too small a rewind, or the aim point is being computed from the present rather than the past.

**If the second test fails**, the fractional instant is not affecting the outcome and the `(tick, alpha)` pair on the wire cannot be justified. Report rather than deleting the test.

- [ ] **Step 3: Commit**

```bash
git add Tests/src/LagCompensationTests.cpp
git commit -m "Prove rewinding changes the answer before building a wire for it"
```

---

## Task 4: Protocol version 3

**Files:**
- Modify: `Cubit/include/Cubit/Net/Protocol.h`, `Cubit/src/Net/Protocol.cpp`
- Test: `Tests/src/ProtocolTests.cpp`

**Interfaces:**
- Produces:
  - `MessageId::Fire = 7`, `MessageId::ShotResolved = 8`
  - `ProtocolVersion == 3`
  - `struct FireMessage { std::uint64_t ClientTick; std::uint64_t RenderTick; float RenderAlpha; float Yaw; float Pitch; };`
  - `struct ShotResolvedMessage { PlayerId Shooter; PlayerId Victim; glm::vec3 Impact; std::uint8_t VictimHealth; bool Killed; };`
  - `PlayerSnapshot::Health` (`std::uint8_t`)
  - `CB_API std::vector<std::uint8_t> Encode(const FireMessage&)` / `Encode(const ShotResolvedMessage&)`
  - `CB_API bool Decode(std::span<const std::uint8_t>, FireMessage&)` / `Decode(..., ShotResolvedMessage&)`

- [ ] **Step 1: Write the failing tests**

Append to `Tests/src/ProtocolTests.cpp`:

```cpp
TEST_CASE("A fire message round-trips")
{
    FireMessage sent;
    sent.ClientTick = 4321;
    sent.RenderTick = 4300;
    sent.RenderAlpha = 0.25f;
    sent.Yaw = -137.5f;
    sent.Pitch = 12.25f;

    FireMessage received;
    REQUIRE(Decode(Encode(sent), received));

    CHECK(received.ClientTick == 4321);
    CHECK(received.RenderTick == 4300);
    CHECK(received.RenderAlpha == doctest::Approx(0.25f));
    CHECK(received.Yaw == doctest::Approx(-137.5f));
    CHECK(received.Pitch == doctest::Approx(12.25f));
}

TEST_CASE("A shot resolution round-trips, hit and miss alike")
{
    ShotResolvedMessage hit;
    hit.Shooter = 1;
    hit.Victim = 2;
    hit.Impact = glm::vec3(1.5f, -2.25f, 300.0f);
    hit.VictimHealth = 66;
    hit.Killed = false;

    ShotResolvedMessage received;
    REQUIRE(Decode(Encode(hit), received));
    CHECK(received.Shooter == 1);
    CHECK(received.Victim == 2);
    CHECK(received.Impact.z == doctest::Approx(300.0f));
    CHECK(received.VictimHealth == 66);
    CHECK_FALSE(received.Killed);

    ShotResolvedMessage miss;
    miss.Shooter = 1;
    miss.Victim = InvalidPlayer;
    miss.Impact = glm::vec3(50.0f, 0.0f, 0.0f);
    miss.Killed = false;

    REQUIRE(Decode(Encode(miss), received));
    CHECK(received.Victim == InvalidPlayer);
}

TEST_CASE("A snapshot carries health")
{
    SnapshotMessage sent;
    sent.Tick = 9;
    PlayerSnapshot entry;
    entry.Player = 3;
    entry.Health = 32;
    sent.Players.push_back(entry);

    SnapshotMessage received;
    REQUIRE(Decode(Encode(sent), received));
    REQUIRE(received.Players.size() == 1);
    CHECK(received.Players[0].Health == 32);
}

TEST_CASE("A fire message is not mistaken for a shot resolution")
{
    //The two directions must never be confused, which is why they are separate
    //ids rather than one payload with a flag.
    FireMessage fire;
    ShotResolvedMessage resolved;

    CHECK_FALSE(Decode(Encode(fire), resolved));
    CHECK_FALSE(Decode(Encode(resolved), fire));
}

TEST_CASE("A truncated fire message is refused rather than half-read")
{
    const std::vector<std::uint8_t> whole = Encode(FireMessage{});

    for (std::size_t length = 0; length < whole.size(); ++length)
    {
        FireMessage out;
        const std::span<const std::uint8_t> truncated(whole.data(), length);
        CHECK_FALSE(Decode(truncated, out));
    }
}
```

- [ ] **Step 2: Run to verify they fail**

Run the build.
Expected: compile errors — `FireMessage` is not declared.

- [ ] **Step 3: Extend the header**

In `Cubit/include/Cubit/Net/Protocol.h`:

Change the `MessageId` enum and its comment:

```cpp
//Every message the wire carries. Eight, and deliberately not nine: there are no
//join or leave messages, because a snapshot carries the whole roster every tick
//and ids are never reused, so a client derives both by diffing what it held
//last.
enum class MessageId : std::uint8_t
{
    Hello = 1,
    Welcome = 2,
    Input = 3,
    Snapshot = 4,
    EditRequest = 5,
    EditApplied = 6,
    Fire = 7,
    ShotResolved = 8
};
```

Bump the version, adding to the existing comment rather than replacing it:

```cpp
//3: shooting. A Fire message, a ShotResolved answer, and a Health byte on
//PlayerSnapshot. The last is on the per-tick path.
constexpr std::uint32_t ProtocolVersion = 3;
```

Add `Health` to `PlayerSnapshot`, after `LastInputTick`:

```cpp
    //Current health, 0 to 100. Never predicted: a client displays what arrives
    //and nothing more, because health changes only when a game rule fires and
    //the client owns no game rules.
    std::uint8_t Health = 0;
```

Add the two messages after `EditMessage`:

```cpp
//A client asking to shoot.
//
//Carries TWO instants, and conflating them is the mistake this stage is most
//likely to make. ClientTick is when the shooter fired, in their own numbering,
//and locates the shooter's own eye in the server's history. RenderTick plus
//RenderAlpha is the instant the shooter's SCREEN was showing, in the SERVER's
//numbering, and locates everybody else. They differ by roughly the interpolation
//delay plus a one-way trip.
struct FireMessage
{
    std::uint64_t ClientTick = 0;

    //Whole part of the instant the shooter's screen was showing, in the
    //server's tick numbering. Clamped by the server before it is believed.
    std::uint64_t RenderTick = 0;

    //Fractional part, in [0, 1). Carried because MatchClient::PoseOf
    //interpolates between snapshots, so the screen showed the target BETWEEN
    //two ticks; rewinding to a whole tick would aim at somewhere the target
    //never appeared to be.
    float RenderAlpha = 0.0f;

    //Aim, in degrees, in the Heading.h convention.
    //
    //Sent rather than looked up from the input at ClientTick, because that
    //input may have been dropped and may never arrive - and a shot that
    //silently became a miss because its input packet was lost is
    //indistinguishable from a bug in the rewind. Trusting client aim is
    //already this design's position: yaw is an input, not simulated state.
    float Yaw = 0.0f;
    float Pitch = 0.0f;
};

//The server's ruling on one shot, sent to everybody.
//
//To everybody rather than to the two involved, so every client can draw the
//tracer and the impact. At six shots a second and 19 bytes this is nothing
//beside the 3,900 B/s snapshot stream.
struct ShotResolvedMessage
{
    PlayerId Shooter = InvalidPlayer;

    //InvalidPlayer when the shot hit terrain or nothing.
    PlayerId Victim = InvalidPlayer;

    //Where the ray stopped, whether that was a player, a block, or the end of
    //its range. Always meaningful: something is drawn at the end of every shot.
    glm::vec3 Impact{ 0.0f };

    std::uint8_t VictimHealth = 0;
    bool Killed = false;
};
```

Declare the four functions alongside the existing ones:

```cpp
CB_API std::vector<std::uint8_t> Encode(const FireMessage& message);
CB_API std::vector<std::uint8_t> Encode(const ShotResolvedMessage& message);
```
```cpp
CB_API bool Decode(std::span<const std::uint8_t> bytes, FireMessage& out);
CB_API bool Decode(std::span<const std::uint8_t> bytes, ShotResolvedMessage& out);
```

- [ ] **Step 4: Extend the implementation**

In `Cubit/src/Net/Protocol.cpp`:

**Bump the guard constant** — this is easy to miss and the failure is silent:

```cpp
    //Bytes each entry costs on the wire. Used to reject an absurd count before
    //reserving for it, which is what stops a tiny hostile packet claiming a
    //huge collection from becoming a denial of service.
    //
    //The trailing 1 is Health, added in version 3. This constant MUST track
    //the encoder: too small and the guard rejects packets that are perfectly
    //valid, which looks like random snapshot loss rather than a decode bug.
    constexpr std::size_t PlayerSnapshotBytes = 2 + 12 + 4 + 4 + 4 + 1 + 8 + 1;
```

Write `player.Health` in `Encode(const SnapshotMessage&)`, after `writer.U64(player.LastInputTick);`:

```cpp
        writer.U8(player.Health);
```

Read it in `Decode(..., SnapshotMessage&)`, in the same position within the per-player loop:

```cpp
        entry.Health = reader.U8();
```

Add the two encoders:

```cpp
std::vector<std::uint8_t> Encode(const FireMessage& message)
{
    ByteWriter writer;
    writer.U8(static_cast<std::uint8_t>(MessageId::Fire));
    writer.U64(message.ClientTick);
    writer.U64(message.RenderTick);
    writer.F32(message.RenderAlpha);
    writer.F32(message.Yaw);
    writer.F32(message.Pitch);
    return writer.Bytes();
}

std::vector<std::uint8_t> Encode(const ShotResolvedMessage& message)
{
    ByteWriter writer;
    writer.U8(static_cast<std::uint8_t>(MessageId::ShotResolved));
    writer.U16(message.Shooter);
    writer.U16(message.Victim);
    writer.Vec3(message.Impact);
    writer.U8(message.VictimHealth);
    writer.Bool(message.Killed);
    return writer.Bytes();
}
```

And the two decoders:

```cpp
bool Decode(std::span<const std::uint8_t> bytes, FireMessage& out)
{
    ByteReader reader(bytes);
    if (!OpenAs(reader, MessageId::Fire))
        return false;

    FireMessage message;
    message.ClientTick = reader.U64();
    message.RenderTick = reader.U64();
    message.RenderAlpha = reader.F32();
    message.Yaw = reader.F32();
    message.Pitch = reader.F32();

    //No count field and no variable-length field, so there is nothing here to
    //reserve on a hostile packet's word and no allocation guard to write. Worth
    //saying so, because the guards in Decode(WelcomeMessage&) and
    //Decode(SnapshotMessage&) are not decoration and must not be deleted by
    //analogy with this one.
    if (!reader.Ok())
        return false;

    out = message;
    return true;
}

bool Decode(std::span<const std::uint8_t> bytes, ShotResolvedMessage& out)
{
    ByteReader reader(bytes);
    if (!OpenAs(reader, MessageId::ShotResolved))
        return false;

    ShotResolvedMessage message;
    message.Shooter = reader.U16();
    message.Victim = reader.U16();
    message.Impact = reader.Vec3();
    message.VictimHealth = reader.U8();
    message.Killed = reader.Bool();

    if (!reader.Ok())
        return false;

    out = message;
    return true;
}
```

- [ ] **Step 5: Build and run**

Expected: all five new protocol cases pass, and every pre-existing protocol test still passes.

- [ ] **Step 6: Prove the guard constant matters**

Set `PlayerSnapshotBytes` back to `2 + 12 + 4 + 4 + 4 + 1 + 8` and rebuild.
Expected: "A snapshot carries health" — and probably several older snapshot tests — turn red, because a valid packet is now rejected. Revert.

This is the mutation that catches the silent failure mode, so run it rather than reasoning about it.

- [ ] **Step 7: Commit**

```bash
git add Cubit/include/Cubit/Net/Protocol.h Cubit/src/Net/Protocol.cpp Tests/src/ProtocolTests.cpp
git commit -m "Carry a shot and its ruling on the wire"
```

---

## Task 5: The server remembers where everybody was

**Files:**
- Modify: `Cubit/include/Cubit/Net/MatchServer.h`, `Cubit/src/Net/MatchServer.cpp`
- Test: `Tests/src/MatchServerTests.cpp`

**Interfaces:**
- Consumes: `HitboxHistory` (Task 1).
- Produces: `const HitboxHistory& MatchServer::History() const`.

- [ ] **Step 1: Write the failing test**

Append to `Tests/src/MatchServerTests.cpp`:

This file's harness is `LoopbackNetwork`, not a bare transport: `network.Server()` for the server end and `network.AddClient(peer)` for each client, with a `Join(server, client)` helper that completes the handshake and returns the `PlayerId`. Use them.

```cpp
TEST_CASE("Nothing is recorded for a player who does not exist")
{
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    //Enough ticks to fill and overflow the ring, had anybody been in it.
    for (int tick = 0; tick < 40; ++tick)
        server.Step(FrameClock::FixedStepSeconds);

    CHECK(server.History().SampleCount(1) == 0);
}

TEST_CASE("A joined player's recorded history matches where the match stepped them")
{
    //The history must hold the positions the match actually produced, not an
    //approximation of them: a rewind is only honest if it replays the server's
    //own past.
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& client = network.AddClient(peer);
    const PlayerId player = Join(server, client);
    REQUIRE(player != InvalidPlayer);

    //Tick numbers paired with the position that tick produced, so the check
    //below is against a middle tick rather than only the newest - the newest
    //is the one value an off-by-one in the recorded tick can still get right.
    std::vector<std::pair<std::uint64_t, glm::vec3>> stepped;
    for (int tick = 0; tick < 10; ++tick)
    {
        server.Step(FrameClock::FixedStepSeconds);
        stepped.emplace_back(server.Match().Tick() - 1,
            server.Match().Player(player).Position());
    }

    const glm::vec3 halfExtents(0.3f, 0.9f, 0.3f);

    for (const auto& [tick, position] : stepped)
    {
        CAPTURE(tick);

        Aabb box;
        REQUIRE(server.History().BoxAt(player, static_cast<double>(tick), halfExtents, box));

        const glm::vec3 centre = (box.Min + box.Max) * 0.5f;
        CHECK(centre.x == doctest::Approx(position.x));
        CHECK(centre.y == doctest::Approx(position.y));
        CHECK(centre.z == doctest::Approx(position.z));
    }
}
```

- [ ] **Step 2: Run to verify it fails**

Expected: compile error — `MatchServer::History` does not exist.

- [ ] **Step 3: Add the history to the server**

In `Cubit/include/Cubit/Net/MatchServer.h`, add the include:

```cpp
#include "Cubit/Voxel/HitboxHistory.h"
```

Add the accessor to the public section, after `EditLog()`:

```cpp
    //Where everybody has recently been, which is what a shot is resolved
    //against. Exposed for tests and for nothing else: the rewind happens in
    //here.
    const HitboxHistory& History() const { return m_History; }
```

Add the member, after `m_EditLog`:

```cpp
    HitboxHistory m_History;
```

- [ ] **Step 4: Record after each step**

In `Cubit/src/Net/MatchServer.cpp`, in `Step`, between `m_Match.Step(...)` and `SendSnapshots()`:

```cpp
    m_Match.Step(commands, static_cast<float>(seconds));

    //AFTER the step and BEFORE the snapshot, so the recorded position is the
    //one this tick's snapshot reports. Recording before the step would store
    //last tick's position under this tick's number, putting every rewind one
    //step in the past - which would look exactly like a rewind that is
    //slightly too aggressive rather than like an off-by-one.
    //
    //Tick() has already been incremented by Step, so the position just computed
    //belongs to tick Tick() - 1.
    const std::uint64_t recordedTick = m_Match.Tick() - 1;
    for (const auto& [player, character] : m_Match.Players())
        m_History.Record(player, recordedTick, character.Position());

    SendSnapshots();
```

In `HandleDisconnected`, alongside `m_Match.RemovePlayer(found->Player)`:

```cpp
    if (found->Player != InvalidPlayer)
    {
        m_Match.RemovePlayer(found->Player);
        m_History.Forget(found->Player);
    }
```

- [ ] **Step 5: Build and run**

Expected: both new cases pass, and the whole existing `MatchServerTests` suite still passes.

- [ ] **Step 6: Prove the recorded tick number is pinned**

Change `m_Match.Tick() - 1` to `m_Match.Tick()` and rebuild.
Expected: "A joined player's recorded history matches where the match stepped them" turns red. Revert.

If it does not go red, the test is reading a tick whose value does not distinguish the two — pick a tick in the middle of the run rather than the last one, confirm red, then revert.

- [ ] **Step 7: Commit**

```bash
git add Cubit/include/Cubit/Net/MatchServer.h Cubit/src/Net/MatchServer.cpp Tests/src/MatchServerTests.cpp
git commit -m "Record each tick's positions for the rewind to read"
```

---

## Task 6: The server resolves a shot

**Files:**
- Modify: `Cubit/include/Cubit/Net/MatchServer.h`, `Cubit/src/Net/MatchServer.cpp`
- Test: `Tests/src/MatchServerTests.cpp`

**Interfaces:**
- Consumes: `FireMessage`, `ShotResolvedMessage` (Task 4); `ResolveShot`, `ShotCandidate`, `ShotResult` (Task 2); `MatchServer::History()` (Task 5).
- Produces: `constexpr int TicksBetweenShots = 10;`, `constexpr float ShotRange = 128.0f;`, `MatchServer::HandleFire(Client&, const FireMessage&)` as a private member, and **`AimDirection(float yawDegrees, float pitchDegrees)` in `Cubit/include/Cubit/Voxel/Heading.h`** (Step 0 below).

**Read this before starting: the aim function does not exist.** The spec assumed a yaw-and-pitch direction was already available to the server. It is not. `Heading.h` has `HeadingForward(yaw)` and `HeadingRight(yaw)` only, and its comment says pitch is *deliberately* absent — because walking speed must not change when you look up. That reasoning is about **walking**, and a shot is the case it does not cover. `PerspectiveCamera` has the pitched direction, but it lives in `Renderer/` and the server must stay GL-free, so it cannot be reached from here.

- [ ] **Step 0: Add `AimDirection` and pin it against the camera**

Append to `Cubit/include/Cubit/Voxel/Heading.h`:

```cpp
//Full 3D aim direction from a yaw and a pitch, both in degrees.
//
//The pitched sibling of HeadingForward, and the note above about pitch being
//deliberately absent still stands for everything it was written about: WALKING
//must not change when you look up. Aiming is the case that reasoning does not
//cover - a shot goes where you are looking, including up and down.
//
//Identical to PerspectiveCamera::RecalculateViewMatrix's forward vector, and
//HeadingTests pins the two together rather than reading the formula twice. If
//they ever drift, every shot lands somewhere other than the crosshair while
//every unit test still passes.
inline glm::vec3 AimDirection(float yawDegrees, float pitchDegrees)
{
    const float yaw = glm::radians(yawDegrees);
    const float pitch = glm::radians(pitchDegrees);

    return glm::normalize(glm::vec3(
        std::cos(yaw) * std::cos(pitch),
        std::sin(pitch),
        std::sin(yaw) * std::cos(pitch)));
}
```

Append to `Tests/src/HeadingTests.cpp`, in the idiom that file already uses to pin `HeadingForward` against the camera:

```cpp
TEST_CASE("AimDirection matches what the camera renders with")
{
    //The shot must leave along the direction the crosshair points. Pinned
    //against the camera itself, not against a second reading of the formula -
    //the same standard HeadingForward is held to in this file.
    for (const float yaw : { -180.0f, -90.0f, 0.0f, 37.5f, 90.0f, 179.0f })
    {
        for (const float pitch : { -89.0f, -45.0f, 0.0f, 22.5f, 89.0f })
        {
            PerspectiveCamera camera(45.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
            camera.SetRotation(yaw, pitch);

            const glm::vec3 expected = camera.GetForwardDirection();
            const glm::vec3 actual = AimDirection(yaw, pitch);

            CAPTURE(yaw);
            CAPTURE(pitch);
            CHECK(actual.x == doctest::Approx(expected.x));
            CHECK(actual.y == doctest::Approx(expected.y));
            CHECK(actual.z == doctest::Approx(expected.z));
        }
    }
}
```

Check `PerspectiveCamera`'s real constructor arity and its rotation setter before writing this — a wrong arity here is the same defect the Stage 1 pre-flight scan caught. Build, confirm the case passes, then mutate `std::sin(pitch)` to `-std::sin(pitch)` and confirm it turns red before moving on.

- [ ] **Step 1: Write the failing tests**

Append to `Tests/src/MatchServerTests.cpp`:

First add three helpers to the file's existing anonymous namespace, beside `LastSnapshot` and `Join`:

```cpp
    //Sends one input for a client's own tick, the way MatchClient does.
    //Unreliable, matching the real client - inputs are the one thing on this
    //wire that is cheaper to lose than to delay.
    void SendInput(Transport& client, std::uint64_t tick, const CharacterInput& input)
    {
        InputMessage message;
        message.FirstTick = tick;
        message.Inputs.push_back(input);
        client.Send(LoopbackNetwork::ServerPeer, Encode(message), Channel::Unreliable);
    }

    //Fires one shot, claiming an instant and an aim.
    void SendFire(Transport& client, std::uint64_t clientTick, std::uint64_t renderTick,
        float renderAlpha, float yaw, float pitch)
    {
        FireMessage fire;
        fire.ClientTick = clientTick;
        fire.RenderTick = renderTick;
        fire.RenderAlpha = renderAlpha;
        fire.Yaw = yaw;
        fire.Pitch = pitch;
        client.Send(LoopbackNetwork::ServerPeer, Encode(fire), Channel::Reliable);
    }

    //The newest shot ruling waiting on this endpoint, and how many arrived.
    //The count matters on its own for the fire rate, where the question is
    //whether a second ruling exists at all.
    std::optional<ShotResolvedMessage> LastShotResolved(Transport& transport, int& count)
    {
        std::optional<ShotResolvedMessage> latest;
        count = 0;

        NetEvent event;
        while (transport.Poll(event))
        {
            if (event.Type != NetEventType::Message)
                continue;

            ShotResolvedMessage resolved;
            if (Decode(event.Data, resolved))
            {
                latest = resolved;
                ++count;
            }
        }

        return latest;
    }
```

Then the three cases:

```cpp
TEST_CASE("A shot claiming an ancient instant is clamped into the window")
{
    //A lying client gets aimed at a quarter-second-old world, which is exactly
    //what an honest 250 ms player gets. The lie buys nothing, and that is the
    //entire trust story for RenderTick.
    //
    //Falsifiable because of how BoxAt answers an instant it has no record of:
    //WITHOUT the clamp, tick 0 falls before the target's oldest sample, so the
    //target is not a candidate and the shot comes back a miss.
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId shooterPeer = InvalidPeer;
    Transport& shooter = network.AddClient(shooterPeer);
    const PlayerId shooterId = Join(server, shooter);

    PeerId targetPeer = InvalidPeer;
    Transport& target = network.AddClient(targetPeer);
    const PlayerId targetId = Join(server, target);

    REQUIRE(shooterId != InvalidPlayer);
    REQUIRE(targetId != InvalidPlayer);

    //Walk the target away along +x, then let them stand. Yaw 0 faces +x and
    //Move.y walks forward, per Heading.h and CharacterInput.
    CharacterInput walk;
    walk.Move = glm::vec2(0.0f, 1.0f);
    walk.Yaw = 0.0f;

    for (std::uint64_t tick = 0; tick < 30; ++tick)
    {
        SendInput(target, tick, walk);
        server.Step(FrameClock::FixedStepSeconds);
    }

    //Standing still for longer than the ring is deep, so every instant in the
    //window reports the same position and the two shots below are comparable.
    for (int tick = 0; tick < 20; ++tick)
        server.Step(FrameClock::FixedStepSeconds);

    int ignored = 0;
    LastShotResolved(shooter, ignored);

    //An honest claim: the oldest instant the window allows.
    SendFire(shooter, 1, server.Match().Tick() - MaxRewindTicks, 0.0f, 0.0f, 0.0f);
    server.Step(FrameClock::FixedStepSeconds);

    int honestCount = 0;
    const std::optional<ShotResolvedMessage> honest = LastShotResolved(shooter, honestCount);
    REQUIRE(honest.has_value());

    for (int tick = 0; tick < TicksBetweenShots; ++tick)
        server.Step(FrameClock::FixedStepSeconds);

    //A claim from before the match had any history at all.
    SendFire(shooter, 2, 0, 0.0f, 0.0f, 0.0f);
    server.Step(FrameClock::FixedStepSeconds);

    int liarCount = 0;
    const std::optional<ShotResolvedMessage> liar = LastShotResolved(shooter, liarCount);
    REQUIRE(liar.has_value());

    CHECK(honest->Victim == targetId);
    CHECK(liar->Victim == targetId);
}

TEST_CASE("A second shot within the fire rate is dropped")
{
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& shooter = network.AddClient(peer);
    REQUIRE(Join(server, shooter) != InvalidPlayer);

    int ignored = 0;
    LastShotResolved(shooter, ignored);

    SendFire(shooter, 1, server.Match().Tick(), 0.0f, 0.0f, 0.0f);
    server.Step(FrameClock::FixedStepSeconds);

    SendFire(shooter, 2, server.Match().Tick(), 0.0f, 0.0f, 0.0f);
    server.Step(FrameClock::FixedStepSeconds);

    int count = 0;
    LastShotResolved(shooter, count);

    //One ruling, not two: the second shot came a tick after the first, and the
    //weapon fires once every ten.
    CHECK(count == 1);
}

TEST_CASE("A fire before the handshake is ignored")
{
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& stranger = network.AddClient(peer);

    //No Hello. A peer exists; a player does not.
    SendFire(stranger, 1, 0, 0.0f, 0.0f, 0.0f);
    server.Step(FrameClock::FixedStepSeconds);

    int count = 0;
    LastShotResolved(stranger, count);

    CHECK(count == 0);
    CHECK(server.Match().Players().empty());
}
```

- [ ] **Step 2: Run to verify they fail**

Expected: no `ShotResolved` is ever sent, because nothing handles `Fire`.

- [ ] **Step 3: Declare the constants and the handler**

In `Cubit/include/Cubit/Net/MatchServer.h`, above the class:

```cpp
//Minimum ticks between one client's shots. Ten is six shots a second.
//
//It is the weapon's rate of fire and, at the same time, the flood answer for a
//new reliable client-to-server message: a client that spams Fire has its extras
//dropped rather than queued.
constexpr int TicksBetweenShots = 10;

//How far a shot carries, in blocks.
constexpr float ShotRange = 128.0f;
```

Add to `Client`:

```cpp
        //Server tick of this client's last accepted shot, for the fire rate.
        //Zero means they have not fired; the first shot of a match is
        //therefore always allowed.
        std::uint64_t LastShotTick = 0;

        //Set once a shot has been dropped for the fire rate and cleared once
        //one is accepted, so a client holding the button down logs one warning
        //per episode rather than one per dropped shot. Same shape as
        //QueueOverflowWarned.
        bool FireRateWarned = false;
```

Add the private handler declaration, near `ApplyPendingEdits`:

```cpp
    //Resolves one shot against the world as the shooter saw it and tells
    //everybody the answer.
    void HandleFire(Client& shooter, const FireMessage& fire);
```

- [ ] **Step 4: Implement the handler**

In `Cubit/src/Net/MatchServer.cpp`, add the case to `HandleMessage`'s switch, beside `EditRequest`:

```cpp
    case MessageId::Fire:
    {
        FireMessage fire;
        if (!Decode(data, fire) || client->Player == InvalidPlayer)
            return;

        HandleFire(*client, fire);
        return;
    }
```

and add `ShotResolved` to the list of server-to-client ids that are ignored on arrival:

```cpp
    case MessageId::Welcome:
    case MessageId::Snapshot:
    case MessageId::EditApplied:
    case MessageId::ShotResolved:
        return;
```

Then the handler itself:

```cpp
void MatchServer::HandleFire(Client& shooter, const FireMessage& fire)
{
    const std::uint64_t now = m_Match.Tick();

    //The fire rate, which is also the flood guard.
    if (shooter.LastShotTick != 0 && now - shooter.LastShotTick < static_cast<std::uint64_t>(TicksBetweenShots))
    {
        if (!shooter.FireRateWarned)
        {
            //CB_WARN takes ONE argument and does no formatting - it is
            //`Logger::Warn(message)`. Build the string, matching how the
            //input-queue warning a few lines up already does it.
            CB_WARN("Dropping a shot from player " + std::to_string(shooter.Player)
                + " fired faster than the weapon allows");
            shooter.FireRateWarned = true;
        }

        return;
    }

    shooter.FireRateWarned = false;
    shooter.LastShotTick = now;

    //THE CLAMP. Applied to the combined fractional instant, never to the whole
    //part alone: clamping the two separately would let a claim of tick 0 with
    //alpha 0.9 survive as a fractional offset on a completely different tick.
    const double claimed = static_cast<double>(fire.RenderTick) + static_cast<double>(fire.RenderAlpha);
    const double newest = static_cast<double>(now);
    const double oldest = newest - static_cast<double>(MaxRewindTicks);
    const double instant = glm::clamp(claimed, oldest, newest);

    //THE TARGETS rewind to the instant the shooter's screen was showing.
    std::vector<ShotCandidate> candidates;
    for (const auto& [player, character] : m_Match.Players())
    {
        //Never a candidate against their own shot.
        if (player == shooter.Player)
            continue;

        Aabb box;
        //False means there is no record of them at that instant - they joined
        //after it, or they have respawned since. Not a hit of zero size.
        if (!m_History.BoxAt(player, instant, character.Config().HalfExtents, box))
            continue;

        candidates.push_back(ShotCandidate{ player, box });
    }

    //THE SHOOTER'S OWN EYE comes from a different instant: the tick the server
    //last stepped them, which is where it already believes they stand. Using
    //the render instant here would put their eye a round trip behind where they
    //believe they are, and every shot fired while moving would leave from the
    //wrong place.
    const CharacterController& character = m_Match.Player(shooter.Player);
    const glm::vec3 eye = character.Position() + glm::vec3(0.0f, character.Config().EyeOffset, 0.0f);
    const glm::vec3 direction = AimDirection(fire.Yaw, fire.Pitch);

    const ShotResult shot = ResolveShot(
        m_Match.GetWorld(), candidates, eye, direction, ShotRange);

    ShotResolvedMessage resolved;
    resolved.Shooter = shooter.Player;
    resolved.Victim = shot.Victim;
    resolved.Impact = shot.Impact;
    resolved.VictimHealth = 0;
    resolved.Killed = false;

    SendToJoined(Encode(resolved), Channel::Reliable);
}
```

Add the includes at the top of the file:

```cpp
#include "Cubit/Voxel/Heading.h"
#include "Cubit/Voxel/ResolveShot.h"
```

**Before writing `HeadingToDirection`, open `Cubit/include/Cubit/Voxel/Heading.h` and use the name and argument order it actually declares.** The Sandbox and `PerspectiveCamera` already agree on one convention; a second one invented here is how a shot ends up 90° off with every unit test passing.

- [ ] **Step 5: Build and run**

Expected: the three new cases pass.

- [ ] **Step 6: Prove the shooter exclusion and the clamp**

Two mutations, run one at a time:

1. Delete `if (player == shooter.Player) continue;`. Expected: a test goes red — if none does, add one where a player fires straight down and must not hit themselves, confirm red, revert.
2. Change the clamp to `glm::clamp(static_cast<double>(fire.RenderTick), oldest, newest) + fire.RenderAlpha`. Expected: "A shot claiming an ancient instant is clamped" turns red, because a `RenderTick` of 0 now contributes its alpha to a clamped tick. If it does not, the test is not exercising a nonzero alpha — fix it.

Revert both.

- [ ] **Step 7: Commit**

```bash
git add Cubit/include/Cubit/Net/MatchServer.h Cubit/src/Net/MatchServer.cpp Tests/src/MatchServerTests.cpp
git commit -m "Resolve a shot against the world the shooter saw"
```

---

## Task 7: Health, damage, death and respawn

**Files:**
- Modify: `Cubit/include/Cubit/Net/MatchServer.h`, `Cubit/src/Net/MatchServer.cpp`
- Test: `Tests/src/MatchServerTests.cpp`

**Interfaces:**
- Consumes: `HandleFire` (Task 6); `PlayerSnapshot::Health` (Task 4); `MatchState::PlayerForWrite`, `MatchState::TeleportPlayer`.
- Produces: `constexpr std::uint8_t StartingHealth = 100;`, `constexpr std::uint8_t ShotDamage = 34;`, `std::uint8_t MatchServer::HealthOf(PlayerId) const`.

- [ ] **Step 1: Write the failing tests**

Append to `Tests/src/MatchServerTests.cpp`, again using the file's existing join helper:

```cpp
TEST_CASE("Three hits kill, and the third respawns the victim")
{
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId shooterPeer = InvalidPeer;
    Transport& shooter = network.AddClient(shooterPeer);
    const PlayerId shooterId = Join(server, shooter);

    PeerId targetPeer = InvalidPeer;
    Transport& target = network.AddClient(targetPeer);
    const PlayerId targetId = Join(server, target);

    REQUIRE(shooterId != InvalidPlayer);
    REQUIRE(targetId != InvalidPlayer);

    //Both stand on the spawn point, because players do not collide with each
    //other. The shot is therefore point blank and its aim does not matter: a
    //ray beginning inside a box hits it at distance zero. What this case tests
    //is the arithmetic of damage, not the geometry of aiming - the geometry is
    //Task 2's and Task 9's.
    CHECK(server.HealthOf(targetId) == StartingHealth);

    const auto fireOnce = [&]() -> ShotResolvedMessage
    {
        int ignored = 0;
        LastShotResolved(shooter, ignored);

        SendFire(shooter, 1, server.Match().Tick(), 0.0f, 0.0f, 0.0f);
        server.Step(FrameClock::FixedStepSeconds);

        int count = 0;
        const std::optional<ShotResolvedMessage> resolved = LastShotResolved(shooter, count);
        REQUIRE(resolved.has_value());

        //Wait out the fire rate so the next call is not silently dropped.
        for (int tick = 0; tick < TicksBetweenShots; ++tick)
            server.Step(FrameClock::FixedStepSeconds);

        return *resolved;
    };

    const ShotResolvedMessage first = fireOnce();
    CHECK(first.Shooter == shooterId);
    CHECK(first.Victim == targetId);
    CHECK(first.VictimHealth == 66);
    CHECK_FALSE(first.Killed);

    const ShotResolvedMessage second = fireOnce();
    CHECK(second.VictimHealth == 32);
    CHECK_FALSE(second.Killed);

    const ShotResolvedMessage third = fireOnce();

    //Zero, not the respawned 100. Reporting health AFTER the respawn would make
    //a kill indistinguishable from a graze on the wire.
    CHECK(third.VictimHealth == 0);
    CHECK(third.Killed);

    //Alive again, standing where they started. Only x and z are checked: the
    //steps that waited out the fire rate have applied gravity since.
    CHECK(server.HealthOf(targetId) == StartingHealth);
    CHECK(server.Match().Player(targetId).Position().x == doctest::Approx(Spawn.x));
    CHECK(server.Match().Player(targetId).Position().z == doctest::Approx(Spawn.z));
}

TEST_CASE("A kill forgets the victim's history")
{
    //THE RULE THAT STOPS A DEAD PLAYER BEING KILLED TWICE, tested where it can
    //actually be falsified.
    //
    //Deliberately split from the shooting: proving it end-to-end would need the
    //victim to die somewhere the respawn point is NOT, because both players
    //stand on the same spawn and a ray starting inside a box always hits it -
    //so an end-to-end version passes whether or not Forget is called. Task 1
    //already proves that a forgotten player cannot be hit at a past instant.
    //What is left to prove here is that the server forgets, and that is this.
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId shooterPeer = InvalidPeer;
    Transport& shooter = network.AddClient(shooterPeer);
    REQUIRE(Join(server, shooter) != InvalidPlayer);

    PeerId targetPeer = InvalidPeer;
    Transport& target = network.AddClient(targetPeer);
    const PlayerId targetId = Join(server, target);
    REQUIRE(targetId != InvalidPlayer);

    //Fill the ring well past MaxHistorySamples.
    for (int tick = 0; tick < 40; ++tick)
        server.Step(FrameClock::FixedStepSeconds);

    REQUIRE(server.History().SampleCount(targetId) == MaxHistorySamples);

    for (int shot = 0; shot < 3; ++shot)
    {
        SendFire(shooter, 1, server.Match().Tick(), 0.0f, 0.0f, 0.0f);
        server.Step(FrameClock::FixedStepSeconds);

        for (int tick = 0; tick < TicksBetweenShots; ++tick)
            server.Step(FrameClock::FixedStepSeconds);
    }

    //The kill cleared the ring, and only the ticks since have refilled it. If
    //Forget were not called this would still be at MaxHistorySamples.
    CHECK(server.History().SampleCount(targetId) < MaxHistorySamples);
}

TEST_CASE("Health arrives in the snapshot")
{
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId shooterPeer = InvalidPeer;
    Transport& shooter = network.AddClient(shooterPeer);
    REQUIRE(Join(server, shooter) != InvalidPlayer);

    PeerId targetPeer = InvalidPeer;
    Transport& target = network.AddClient(targetPeer);
    const PlayerId targetId = Join(server, target);
    REQUIRE(targetId != InvalidPlayer);

    server.Step(FrameClock::FixedStepSeconds);

    const auto healthInSnapshot = [&](Transport& endpoint) -> std::uint8_t
    {
        const std::optional<SnapshotMessage> snapshot = LastSnapshot(endpoint);
        REQUIRE(snapshot.has_value());

        for (const PlayerSnapshot& entry : snapshot->Players)
        {
            if (entry.Player == targetId)
                return entry.Health;
        }

        FAIL("the snapshot did not mention the target");
        return 0;
    };

    CHECK(healthInSnapshot(target) == StartingHealth);

    SendFire(shooter, 1, server.Match().Tick(), 0.0f, 0.0f, 0.0f);
    server.Step(FrameClock::FixedStepSeconds);
    server.Step(FrameClock::FixedStepSeconds);

    CHECK(healthInSnapshot(target) == 66);
}
```

- [ ] **Step 2: Run to verify they fail**

Expected: health is always zero; nobody ever dies.

- [ ] **Step 3: Add health to the server**

In `Cubit/include/Cubit/Net/MatchServer.h`, above the class:

```cpp
//Health a player starts and respawns with.
constexpr std::uint8_t StartingHealth = 100;

//Damage one shot does. Three shots kill, with the third overshooting by two -
//health is clamped at zero rather than wrapping, which an unsigned type makes
//worth stating.
constexpr std::uint8_t ShotDamage = 34;
```

Add to `Client`:

```cpp
        //Health lives here rather than on MatchState because MatchState is the
        //whole SIMULATED state and health is not simulated by Step - it changes
        //only when a game rule fires. MatchState::PlayerForWrite exists for
        //exactly this kind of caller, and says so in its own comment.
        std::uint8_t Health = StartingHealth;
```

Add the accessor:

```cpp
    //A player's current health, or zero if nobody holds that id.
    std::uint8_t HealthOf(PlayerId player) const;
```

- [ ] **Step 4: Apply damage and respawn**

In `MatchServer::HandleFire`, replace the tail that built `resolved`:

```cpp
    ShotResolvedMessage resolved;
    resolved.Shooter = shooter.Player;
    resolved.Victim = shot.Victim;
    resolved.Impact = shot.Impact;
    resolved.VictimHealth = 0;
    resolved.Killed = false;

    if (shot.Victim != InvalidPlayer)
    {
        Client* victim = nullptr;
        for (Client& candidate : m_Clients)
        {
            if (candidate.Player == shot.Victim)
                victim = &candidate;
        }

        if (victim != nullptr)
        {
            //Clamped rather than allowed to wrap. Health is unsigned, so
            //34 subtracted from 32 is not -2, it is 224 - a dead player at
            //more than full health.
            victim->Health = victim->Health <= ShotDamage
                ? std::uint8_t{ 0 }
                : static_cast<std::uint8_t>(victim->Health - ShotDamage);

            resolved.VictimHealth = victim->Health;
            resolved.Killed = victim->Health == 0;

            if (resolved.Killed)
            {
                m_Match.TeleportPlayer(victim->Player, m_Spawn);
                m_Match.PlayerForWrite(victim->Player).SetVerticalVelocity(0.0f);
                victim->Health = StartingHealth;

                //THE HISTORY GOES TOO. Without this, a shot already in flight
                //could rewind to before the death, find the victim standing
                //where they fell, and damage the player who has since
                //respawned there.
                m_History.Forget(victim->Player);
            }
        }
    }

    SendToJoined(Encode(resolved), Channel::Reliable);
```

Add the accessor's definition — an exported class must define every member it declares:

```cpp
std::uint8_t MatchServer::HealthOf(PlayerId player) const
{
    for (const Client& client : m_Clients)
    {
        if (client.Player == player)
            return client.Health;
    }

    return 0;
}
```

In `SendSnapshots`, fill the new field where each `PlayerSnapshot` is built. Health is per player, so it is looked up from the client that holds that id:

```cpp
        entry.Health = HealthOf(player);
```

- [ ] **Step 5: Build and run**

Expected: the three new cases pass, and every existing server and prediction test still passes. Health defaulting to `StartingHealth` must not perturb any Stage 3 assertion.

- [ ] **Step 6: Prove the clamp and the forget**

1. Change the health subtraction to `static_cast<std::uint8_t>(victim->Health - ShotDamage)` unconditionally. Expected: "Three hits kill" turns red — the third shot leaves 224, not 0. Revert.
2. Delete `m_History.Forget(victim->Player)`. Expected: "A kill forgets the victim's history" turns red. Revert.

- [ ] **Step 7: Commit**

```bash
git add Cubit/include/Cubit/Net/MatchServer.h Cubit/src/Net/MatchServer.cpp Tests/src/MatchServerTests.cpp
git commit -m "Make a hit cost something"
```

---

## Task 8: The client fires

**Files:**
- Modify: `Cubit/include/Cubit/Net/MatchClient.h`, `Cubit/src/Net/MatchClient.cpp`
- Test: `Tests/src/PredictionTests.cpp`

**Interfaces:**
- Consumes: `FireMessage`, `ShotResolvedMessage` (Task 4).
- Produces:
  - `void MatchClient::Fire(float alpha)`
  - `struct MatchClient::ShotReport { PlayerId Shooter; PlayerId Victim; glm::vec3 Impact; std::uint8_t VictimHealth; bool Killed; std::uint64_t ReceivedAtTick; };`
  - `const std::optional<MatchClient::ShotReport>& MatchClient::LastShot() const`
  - `std::uint8_t MatchClient::LocalHealth() const`

- [ ] **Step 1: Write the failing test**

Append to `Tests/src/PredictionTests.cpp`:

```cpp
TEST_CASE("A fired shot declares the instant the client is rendering")
{
    //The client must send the SAME instant PoseOf is drawing at, because that
    //is the whole contract: the server reproduces the shooter's screen. Any
    //other number and the server aims at a target the shooter never saw.
    LoopbackTransport transport;
    MatchClient client(transport, GoodLoader());

    //(Bring the client up against a server the way this file's other cases do,
    //step it far enough that m_RemoteClock is running, then:)
    //
    //Fire with a known alpha, capture the FireMessage the transport carried,
    //and check RenderTick + RenderAlpha equals the instant PoseOf uses for the
    //same alpha - m_RemoteClock + alpha - InterpolationDelayTicks.
}

TEST_CASE("A shot resolution is reported to the caller")
{
    //(Have the server rule on a shot, and check LastShot() reports the victim,
    //the impact and the kill flag the server sent.)
}
```

**Write both out fully in the file's idiom.** `PredictionTests.cpp` already has the harness for a client talking to a server.

- [ ] **Step 2: Run to verify it fails**

Expected: compile error — `MatchClient::Fire` does not exist.

- [ ] **Step 3: Add the API**

In `Cubit/include/Cubit/Net/MatchClient.h`, public section:

```cpp
    //Asks the server to resolve a shot, declaring the instant this client is
    //currently rendering remote players at.
    //
    //`alpha` is the renderer's position within the current step - the same
    //number handed to PoseOf, and it must be the same value in the same frame.
    //Passing a different one asks the server to rewind to an instant this
    //client never drew.
    //
    //Nothing happens locally. Whether the shot HIT is the server's to say, and
    //showing a hit marker that could be retracted is worse than showing one a
    //round trip late.
    void Fire(float alpha);

    //The server's ruling on the most recent shot anybody fired, or nothing if
    //no shot has been resolved yet. Held rather than delivered by callback so
    //the Sandbox can draw it for as many frames as it likes.
    struct ShotReport
    {
        PlayerId Shooter = InvalidPlayer;
        PlayerId Victim = InvalidPlayer;
        glm::vec3 Impact{ 0.0f };
        std::uint8_t VictimHealth = 0;
        bool Killed = false;

        //This client's own tick when the ruling arrived, so a caller can fade
        //the marker out without keeping its own clock.
        std::uint64_t ReceivedAtTick = 0;
    };

    const std::optional<ShotReport>& LastShot() const { return m_LastShot; }

    //This client's own health, as last reported by a snapshot. Zero before the
    //first snapshot arrives.
    std::uint8_t LocalHealth() const { return m_LocalHealth; }
```

Private:

```cpp
    void HandleShotResolved(std::span<const std::uint8_t> data);

    std::optional<ShotReport> m_LastShot;
    std::uint8_t m_LocalHealth = 0;
```

- [ ] **Step 4: Implement**

In `Cubit/src/Net/MatchClient.cpp`:

```cpp
void MatchClient::Fire(float alpha)
{
    if (!m_Connected)
        return;

    //EXACTLY the instant PoseOf draws at for this alpha. Duplicated
    //deliberately rather than factored out: the two are one contract, and a
    //shared helper would hide that changing one changes the other.
    const double instant = m_RemoteClock + static_cast<double>(alpha) - InterpolationDelayTicks;

    //A negative instant cannot be split into a whole tick and a fraction in
    //[0, 1), and it happens for real: the first six ticks after Welcome are
    //before the interpolation delay has anything behind it.
    const double clamped = instant < 0.0 ? 0.0 : instant;
    const double whole = std::floor(clamped);

    FireMessage fire;
    fire.ClientTick = m_Match.Tick();
    fire.RenderTick = static_cast<std::uint64_t>(whole);
    fire.RenderAlpha = static_cast<float>(clamped - whole);
    fire.Yaw = m_Input.Yaw;
    fire.Pitch = m_Input.Pitch;

    m_Transport.Send(m_ServerPeer, Encode(fire), Channel::Reliable);
}

void MatchClient::HandleShotResolved(std::span<const std::uint8_t> data)
{
    ShotResolvedMessage message;
    if (!Decode(data, message))
        return;

    ShotReport report;
    report.Shooter = message.Shooter;
    report.Victim = message.Victim;
    report.Impact = message.Impact;
    report.VictimHealth = message.VictimHealth;
    report.Killed = message.Killed;
    report.ReceivedAtTick = m_Match.Tick();

    m_LastShot = report;
}
```

Add the dispatch case where `EditApplied` is handled, and set `m_LocalHealth` in `HandleSnapshot` where the local player's entry is read:

```cpp
    case MessageId::ShotResolved: HandleShotResolved(event.Data); break;
```

Add `#include <cmath>` for `std::floor` if the file does not already have it.

- [ ] **Step 5: Build and run**

Expected: both new cases pass; every Stage 3 prediction test still passes.

- [ ] **Step 6: Prove the declared instant is pinned**

Change `- InterpolationDelayTicks` to `+ InterpolationDelayTicks` in `Fire` and rebuild.
Expected: "A fired shot declares the instant the client is rendering" turns red. Revert.

- [ ] **Step 7: Commit**

```bash
git add Cubit/include/Cubit/Net/MatchClient.h Cubit/src/Net/MatchClient.cpp Tests/src/PredictionTests.cpp
git commit -m "Let a client ask for a shot at the instant it is drawing"
```

---

## Task 9: The networked acceptance gate and the measured number

**Files:**
- Modify: `Tests/src/LagCompensationTests.cpp`

**Interfaces:**
- Consumes: everything from Tasks 1–8.
- Produces: nothing. Tests only.

- [ ] **Step 1: Write the end-to-end gate**

Append to `Tests/src/LagCompensationTests.cpp`. This mirrors Task 3's oracle but drives it through a real server, a real client and `SimulatedTransport`:

```cpp
TEST_CASE("A shot aimed where the client renders a target hits it, through the wire")
{
    //THE STAGE'S GATE. The shooter aims at exactly what PoseOf reports - the
    //same function that draws the target - and the server must agree.
    //
    //Two clients on a 3-tick one-way link (100 ms RTT). One strafes; the other
    //aims at the pose it is DRAWING and fires. Every shot must connect.
    //
    //Use the canonical test network: 50 ms one-way, seed 1. Latency must be a
    //whole tick multiple.
    //
    //(Stand up a MatchServer and two MatchClients over SimulatedTransport the
    //way PredictionTests.cpp does. Walk the target with a constant strafe
    //input. Each time the shooter is allowed to fire:
    //   const MatchClient::RemotePose pose = shooter.PoseOf(targetId, alpha);
    //   aim from the shooter's own eye at pose.Position;
    //   set that yaw/pitch as the shooter's input, step once, then Fire(alpha).
    // Count how many of the resulting ShotResolved rulings name the target.)

    CHECK(hits == shotsFired);
}

TEST_CASE("The same shots miss when the server does not rewind")
{
    //THE CONTRAST. Without this number the gate above proves only that the
    //test is easy to pass.
    //
    //Same run, with the server resolving against present positions. Rather
    //than adding a production flag for a test's benefit, resolve the same
    //captured shots directly through ResolveShot against the CURRENT boxes,
    //the way Task 3's oracle does - the server's own rewind is what task 3
    //already pinned.

    CHECK(uncompensatedHits < hits);
}

TEST_CASE("Hit rate across latencies")
{
    //THE MEASURED NUMBER, reported rather than asserted tightly. 200 shots at
    //each of 0, 100, 150 and 300 ms RTT. The 300 ms row is above the 250 ms
    //cap and is EXPECTED to be worse - how much worse is what says whether the
    //cap was set sensibly.
    //
    //150 ms is not a whole tick multiple (4.5 ticks), so use 166.7 ms as
    //Stage 3 did for the same reason, and say so in the printed output.
    //
    //MESSAGE the results with doctest's MESSAGE() so they appear in the build
    //log, and assert only the floor that the design actually promises: every
    //latency at or under the cap hits at least 95% of the time.
}
```

**Fill all three in fully.** The bracketed prose is the shape, not the deliverable.

- [ ] **Step 2: Build and run**

Expected: the gate passes and the hit-rate table prints.

**If the gate fails, do not adjust the test until the cause is known.** In order of likelihood: the alpha handed to `PoseOf` differs from the one handed to `Fire`; the shooter's eye offset is applied on one side and not the other; `HeadingToDirection`'s convention is inverted relative to the aim computed in the test; or the server is rewinding to the right instant but the shooter's own eye is being taken from the render instant too. Each of those produces a near-miss rather than a wild one, so print the impact point and the target's box before changing anything.

- [ ] **Step 3: Record the numbers in the spec**

Add a "Shipped" section to `docs/superpowers/specs/2026-09-08-networking-stage-4-design.md` holding the measured hit-rate table, in the shape Stage 3's spec uses. Numbers, not adjectives.

- [ ] **Step 4: Commit**

```bash
git add Tests/src/LagCompensationTests.cpp docs/superpowers/specs/2026-09-08-networking-stage-4-design.md
git commit -m "Measure how often a shot lands where the shooter aimed"
```

---

## Task 10: The debug font learns the rest of the alphabet

**Files:**
- Modify: `Sandbox/src/DebugFont.h`

**Interfaces:**
- Produces: `DebugFont::Order` covering `0123456789-.: ABCDEFGHIKLMNOPRSTUVWY`.

**Why this is its own task.** An unsupported character renders as a **blank**, not an error, so a wrong HUD label reads as a rendering bug. This is the third stage running in which the font has silently eaten a label, and doing it separately means the glyphs can be reviewed as glyphs rather than buried in a gameplay diff.

- [ ] **Step 1: Add the missing glyphs**

In `Sandbox/src/DebugFont.h`, extend `Order` and add a 5x7 glyph for each new letter, **in the same order**, following the style of the existing ones:

```cpp
    constexpr std::string_view Order = "0123456789-.: ABCDEFGHIKLMNOPRSTUVWY";
```

The existing table already holds `A C D E F G N O P S T U`. Add `B`, `H`, `I`, `K`, `L`, `M`, `R`, `V`, `W`, `Y` and re-sort the entries so the table order matches `Order` exactly — a mismatch here draws the wrong letter, silently. For example:

```cpp
        { "####.", "#...#", "#...#", "####.", "#...#", "#...#", "####." }, // B
        { "#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#" }, // H
        { ".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###." }, // I
        { "#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#" }, // K
        { "#....", "#....", "#....", "#....", "#....", "#....", "#####" }, // L
        { "#...#", "##.##", "#.#.#", "#...#", "#...#", "#...#", "#...#" }, // M
        { "####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#" }, // R
        { "#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.." }, // V
        { "#...#", "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#" }, // W
        { "#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.." }  // Y
```

- [ ] **Step 2: Verify the table and the order line up**

There is no test for this today and adding a real one is worth it. Append to any existing Sandbox-adjacent test, or add `Tests/src/DebugFontTests.cpp`:

```cpp
TEST_CASE("Every character in the order string has a glyph")
{
    //A mismatch between Order and Glyphs draws the WRONG letter with no error
    //at all, which is the failure mode this whole task exists to end.
    CHECK(DebugFont::Order.size() == DebugFont::GlyphCount);
}

TEST_CASE("The letters the HUD actually uses are all drawable")
{
    for (const char character : std::string_view("HEALTH HIT KILLED PING RTT PLAYERS"))
    {
        if (character == ' ')
            continue;

        CAPTURE(character);
        CHECK(DebugFont::IndexOf(character) != DebugFont::IndexOf(' '));
    }
}
```

`DebugFont.h` is under `Sandbox/src/`, so check whether the Tests project can include it before writing this — if it cannot, add the include path in `premake5.lua` rather than skipping the test.

- [ ] **Step 3: Build and run**

Expected: both cases pass. If the first fails, the table and `Order` are out of step — fix the table, not the test.

- [ ] **Step 4: Commit**

```bash
git add Sandbox/src/DebugFont.h Tests/src/DebugFontTests.cpp premake5.lua
git commit -m "Teach the debug font the letters the HUD needs"
```

---

## Task 11: The Sandbox shoots

**Files:**
- Modify: `Sandbox/src/Sandbox.cpp`

**Interfaces:**
- Consumes: `MatchClient::Fire`, `MatchClient::LastShot`, `MatchClient::LocalHealth` (Task 8); `DebugDraw::Line`; `MouseCode::Middle`.

- [ ] **Step 1: Fire on middle mouse**

In `OnMouseButtonPressed`, before the existing left/right handling, add a branch for `MouseCode::Middle`. Left and right keep their exact current behaviour — the Stage 2 and 3 acceptance probes depend on it.

```cpp
        //Middle rather than left, and the reason is verification rather than
        //ergonomics: a script can drive the mouse but NOT the keyboard, so a
        //fire bound to a key would make this the one feature nobody can
        //screenshot. It also leaves both edit paths byte-for-byte as they were.
        if (button == MouseCode::Middle)
        {
            if (m_Client)
                m_Client->Fire(m_LastAlpha);

            //Single-player has nobody to shoot, so the local trace is all
            //there is. Drawn regardless so the binding is visibly alive.
            m_PendingTracer = TracerFrom(camera);
            return true;
        }
```

Store the render alpha where `OnRender` already receives it, so `Fire` and `PoseOf` are handed the same number in the same frame — that identity is the contract, not a coincidence.

- [ ] **Step 2: Draw the local tracer immediately**

Where the aimed-voxel outline is already drawn each frame, add the tracer. Keep it short-lived — a few frames — and drawn from the eye to the local raycast's impact:

```cpp
    //The LOCAL trace, drawn the instant the button goes down. Whether it HIT
    //anybody is not shown here: that is the server's to say, and a hit marker
    //that has to be taken back is worse than one a round trip late.
    if (m_TracerFramesLeft > 0)
    {
        DebugDraw::Line(m_Tracer.From, m_Tracer.To, glm::vec4(1.0f, 0.9f, 0.4f, 1.0f));
        --m_TracerFramesLeft;
    }
```

- [ ] **Step 3: Draw the server's ruling**

When `m_Client->LastShot()` holds a report this client has not drawn yet, draw a small `DebugDraw::Box` at its impact point, and if `Victim == m_Client->LocalPlayer()`'s target, show the hit line on the HUD. Every client receives every ruling, so other players' shots draw too.

- [ ] **Step 4: Put health on the HUD**

Add a line to the existing HUD readout. It can now say `HEALTH` — Task 10 is what makes that legible rather than a row of blanks.

- [ ] **Step 5: Verify single-player is untouched**

Run `Sandbox.exe` with no arguments and confirm `POS 240.500000 26.900099 300.500000` and `FACES 1927774`, sampling `FACES` when `PendingCount() == 0`.

Screen capture of this window is unreliable — three consecutive blank frames with a healthy process is a known DWM/OpenGL `CopyFromScreen` failure, not a code fault. The reliable substitute is a temporary `CB_INFO` probe logging the values, then `git checkout --` on the file. Grab the GLFW30 window, not `MainWindowHandle`.

- [ ] **Step 6: Commit**

```bash
git add Sandbox/src/Sandbox.cpp
git commit -m "Bind a shot to the middle mouse button"
```

---

## Task 12: The three-process run, and writing down what happened

**Files:**
- Modify: `docs/engine-roadmap.md`, `docs/superpowers/specs/2026-09-08-networking-stage-4-design.md`

- [ ] **Step 1: Run a real match**

`Server.exe`, plus two `Sandbox.exe --connect --latency 150` clients. One player strafes; the other shoots. Confirm on screen: tracers appear instantly, hit markers appear about a round trip later, health falls, and the third hit respawns the victim at the spawn point.

Remote motion cannot be driven from a script — keyboard input cannot be delivered to this window at all — so this is a by-hand check, the same way Stage 3's jump and F9 checks were.

- [ ] **Step 2: Record the measured numbers**

Fill in the spec's "Shipped" section with the real hit-rate table from Task 9 and anything that turned out differently from the design. Stage 3's spec has a "What turned out differently from the design" section; write the equivalent honestly, including anything this plan predicted that did not happen.

**In particular, record whether the acceptance oracle's mutation actually turned it red.** The spec deliberately declined to assert that. Whichever way it went, that is the single most valuable sentence this stage produces.

- [ ] **Step 3: Update the roadmap**

In `docs/engine-roadmap.md`, under the "An entity or actor concept" bullet where Stages 1–3 are recorded, add Stage 4 in the same voice: what shipped, the measured hit rate, and what is still open — predicted terrain edits, the unbounded edit log, and the undiagnosed session death under `--loss 80/90`.

- [ ] **Step 4: Commit**

```bash
git add docs/engine-roadmap.md docs/superpowers/specs/2026-09-08-networking-stage-4-design.md
git commit -m "Record networking stage 4 as shipped"
```

---

## Self-Review Notes

**Spec coverage.** Every section of the spec maps to a task: the fractional instant (Tasks 1, 3, 8), the two different rewind instants (Task 6), the client-declares-with-a-clamp mechanism (Tasks 6, 8), protocol v3 (Task 4), `HitboxHistory` and `ResolveShot` (Tasks 1, 2), the no-record-across-respawn rule (Tasks 1, 7), game rules and the rate limit (Tasks 6, 7), failure handling (Tasks 4, 6), the client and Sandbox (Tasks 8, 11), the font (Task 10), and the acceptance gate plus measured number (Tasks 3, 9).

**Four things this plan adds that the spec did not have.** All four came from scanning the real headers before writing tasks, which is the practice that caught three would-not-link defects in Stage 1.

1. **`AimDirection` does not exist and must be written (Task 6, Step 0).** The spec assumed the server could turn a yaw and a pitch into a direction. `Heading.h` provides `HeadingForward(yaw)` and `HeadingRight(yaw)` only, and states that pitch is *deliberately* absent — correct reasoning about walking, which does not cover aiming. The pitched formula exists only on `PerspectiveCamera`, in `Renderer/`, which the headless server may not touch. Left undetected, this surfaces as either a compile error or — far worse, if somebody writes their own formula — every shot landing off the crosshair with all unit tests green. It is pinned against the camera, the way `HeadingForward` already is.
2. **`PlayerSnapshotBytes` must be bumped from 35 to 36** when `Health` joins `PlayerSnapshot`. The spec never mentions it because it is an implementation detail of the decode allocation guard, and getting it wrong is silent: valid packets are refused and it reads as random snapshot loss. Task 4 Step 6 makes it a mutation rather than a hope.
3. **`CB_WARN` takes one argument and does no formatting.** It is `Logger::Warn(message)`. The first draft of Task 6 called it with a format string and two arguments, which would not have compiled.
4. **Task 3 exists at all.** The spec puts the acceptance oracle at the end, where it belongs as a gate; this plan additionally proves the *claim underneath it* — that rewinding changes the answer — with no networking, third, before six tasks are built on it. Task 3 has an explicit stop condition.

**One test this plan deliberately weakened, and why.** The spec's "a shot may not rewind across a respawn" reads naturally as an end-to-end test: kill someone, then shoot at where they used to be. That test **cannot fail**, because both players stand on the same spawn point and a ray beginning inside a box hits it at distance zero — so the shot connects whether or not the history was cleared. Rather than build elaborate scenery to separate the two players, Task 7 asserts that the server forgets (`SampleCount` drops), and Task 1 separately proves that a forgotten player cannot be hit at a past instant. This is the same failure shape as the transparency test that could not detect a wrong unflood because `Flood` refilled the column for free. **Check a test can actually fail.**

**Known soft spots, flagged rather than hidden.** Tasks 8 and 9 still describe several tests in prose rather than giving complete code — the client-side harness in `PredictionTests.cpp` and the multi-client rig Task 9 needs are both larger than a plan should transcribe blind. **Scan that file before starting each of those tasks**, and treat any test whose assertions this plan states in prose as unwritten until it exists and has been made to fail. Task 9's gate in particular is the stage's weakest claim; it is the one place where writing the test to pass rather than to be right would waste the whole stage.
