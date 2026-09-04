# Networking Stage 3 (The Feel) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Pressing `W` moves the view on the same frame at `--latency 150`, remote players move smoothly rather than stepping per packet, and a scripted run reports how often the server disagreed and by how much.

**Architecture:** The client starts stepping — but only its own player, through a new `MatchState::StepPlayer`. Its tick free-runs from `Welcome.Tick` and stamps every input it produces. Inputs are kept in an unacked ring and sent three at a time; the server queues them, consumes the oldest one per tick, and acknowledges it in every snapshot. On each snapshot the client writes the authoritative state in, replays everything above the ack, and either keeps the replayed result (a snap) or throws the whole correction away as invisible. Remote players are never predicted: they are drawn from a ring of snapshot samples, interpolated six ticks behind the newest.

**Tech Stack:** C++20, MSVC (Visual Studio 18 / vs2026), premake5, doctest, GLM, ENet.

**Spec:** `docs/superpowers/specs/2026-09-03-networking-stage-3-design.md` (read it; the arc-level spec it builds on is `docs/superpowers/specs/2026-08-27-networking-design.md`, and Stage 2's is `docs/superpowers/specs/2026-08-31-networking-stage-2-design.md`)

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
- **`ProtocolVersion` becomes 2 in Task 4 and stays 2.** Both changed layouts are on the per-tick path.
- **Never add Claude co-author trailers or attribution to commits.**
- **Commit messages are a subject plus a wrapped prose body.** The `git commit -m "subject"` lines below are shorthand for the subject, not permission to skip the body.
- **Single-player must not change.** The acceptance check is `Sandbox.exe` with no flags reporting `POS 240.500000 26.900099 300.500000` and `FACES 1927774`. `FACES` only reaches that number after amortized meshing settles (~1110 frames), so sample it when `PendingCount() == 0`, never at a fixed frame count.

---

## File Structure

| File | Responsibility |
|---|---|
| `Cubit/src/Net/SimulatedTransport.cpp` | **Modify.** Stop losing the due-time comparison to float accumulation. |
| `Tests/src/SimulatedTransportTests.cpp` | **Modify.** Pin exact tick alignment; re-pin the golden schedule. |
| `Cubit/include/Cubit/Voxel/MatchState.h` `Cubit/src/Voxel/MatchState.cpp` | **Modify.** `StepPlayer` — advance exactly one player, leave the tick alone. |
| `Tests/src/MatchStateTests.cpp` | **Modify.** `StepPlayer` oracle and isolation. |
| `Cubit/include/Cubit/Net/Protocol.h` `Cubit/src/Net/Protocol.cpp` | **Modify.** Version 2: input bundles carrying a real tick, `PlayerSnapshot::LastInputTick`. |
| `Tests/src/ProtocolTests.cpp` | **Modify.** Bundle round-trip, the new count guard, the ack field. |
| `Cubit/include/Cubit/Net/MatchServer.h` `Cubit/src/Net/MatchServer.cpp` | **Modify.** Per-client input queue, oldest-first consumption, ack in every snapshot. |
| `Tests/src/MatchServerTests.cpp` | **Modify.** Queue order, duplicate filtering, the cap, the ack. |
| `Cubit/include/Cubit/Net/MatchClient.h` `Cubit/src/Net/MatchClient.cpp` | **Modify.** Prediction, the unacked ring, reconciliation, correction stats, remote sample rings. |
| `Tests/src/WireOracleTests.cpp` | **Modify.** The Stage 2 oracle becomes a remote-player oracle; the "never steps" test is replaced by its inverse; the bad-network drain gets the deadzone's tolerance. |
| `Tests/src/PredictionTests.cpp` | **Create.** Stage 3's oracles: convergence with no network at all, the deadzone in both directions, remote interpolation, and the acceptance gates. |
| `Sandbox/src/Sandbox.cpp` | **Modify.** Draw remote players from interpolated poses; log the correction figures on shutdown. |
| `docs/superpowers/specs/2026-09-03-networking-stage-3-design.md`, `docs/engine-roadmap.md` | **Modify.** Record the measured numbers and mark the stage shipped. |

---

## Task Order and Why

**Task 1 is a review, not code.** Tasks 8–13 of Stage 2 (`67960db..1efe189`) were executed without review, and every one of tasks 1–7 had a review that found something. Stage 3's reconciliation sits directly on `MatchServer` and `MatchClient`, two of the five unreviewed commits. Finding a defect there after prediction is layered on top means debugging two things at once.

**Tasks 2 and 3 are the spec's two prerequisites.** No Stage 3 test can assert exact tick alignment until Task 2 lands, and Task 3 is the single method the whole client side is built on.

**Tasks 4 and 5 change the wire before anything uses the new fields.** Each keeps the suite green by adapting `MatchServer` and `MatchClient` minimally; the real behaviour arrives in Tasks 6–8.

**The server (6) precedes the client (7, 8)** because reconciliation is defined against what the server acknowledges. A client replaying against an ack the server does not yet send would be testing nothing.

---

## A note on falsification

Stage 2's plan asserted that deleting X would turn test Y red, and **five of those assertions did not reproduce when actually run**. Every one was caught by an implementer trying to make a test fail and stopping when it would not.

So: where this plan names a mutation, it is because the effect was **observed in Stage 2** and the observation is cited. Where a mutation is only expected, it says "expected". If a mutation you try does not go red, that is a finding about the test, not a formality to wave through — work out whether the property is provably unfalsifiable (like `Decode`'s snapshot count guard) or merely looks it (like `SimulatedTransport`'s `Serial` tie-break), and record which, before deciding anything.

The other half of the same lesson: **read the ledger before the brief.** `.superpowers/sdd/2026-08-31-networking-stage-2/progress.md` holds every ruling from Stage 2 with its cost-if-wrong. When it and a task below disagree, the task is stale, not wrong — the ledger wins and the disagreement is worth reporting.

---

### Task 1: Review Stage 2's unreviewed commits

Not a code task in itself. `67960db..1efe189` is five commits — `MatchServer`, `MatchClient` and its oracle suite, `EnetTransport`, `Server.exe`, and the Sandbox client — none of which was reviewed. The spec names this as its largest carried risk and as task 1.

**Files:**
- Review: `Cubit/src/Net/MatchServer.cpp`, `Cubit/src/Net/MatchClient.cpp`, `Cubit/src/Net/EnetTransport.cpp`, `Server/src/Server.cpp`, `Sandbox/src/Sandbox.cpp`, `Sandbox/src/HudLayer.h`, and the tests added alongside them.
- Modify: whatever the review finds.

**Interfaces:**
- Consumes: nothing.
- Produces: nothing new. Any fix must keep every existing signature, because Tasks 4–9 are written against the headers as they stand today.

- [ ] **Step 1: Read the diff**

```bash
git log --oneline 67960db..1efe189
git diff 67960db..1efe189 -- Cubit/ Server/ Sandbox/
```

- [ ] **Step 2: Review it**

Use `superpowers:requesting-code-review` against that range. The brief: correctness bugs, and anything a comment claims that the code does not demonstrate.

Three things to look at specifically, because they are the failure modes this arc has actually produced:

1. **Comments asserting more than was demonstrated.** Seen three times in Stage 2, twice inside commits whose whole purpose was to stop overclaiming.
2. **`MatchClient::HandleSnapshot`'s roster diffing** — Task 8 rewrites the middle of it, and a latent bug there will present as a reconciliation bug.
3. **`EnetTransport`'s peer lifetime** — the one file in the range with no deterministic test behind it (`EnetTransportTests.cpp` has a single localhost case).

- [ ] **Step 3: Fix what the review finds, one commit per finding**

Each fix needs a test that fails without it, unless the finding is provably unfalsifiable by return value — in which case document it in place, the way `Decode`'s snapshot count guard is documented.

- [ ] **Step 4: Run the suite**

```bash
MSB="/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe"
"$MSB" Cubit.slnx -p:Configuration=Debug -p:Platform=x64 -m -v:minimal -nologo
```
Expected: PASS, 386 tests plus any this task added.

- [ ] **Step 5: Commit**

If the review found nothing worth changing, commit nothing and say so in the task report. An empty review is a result; inventing a change to have something to commit is worse than none.

```bash
git commit -m "Fix <what the review found>"
```

---

### Task 2: Deliver a whole-tick latency on a whole tick

The spec's first prerequisite. `SimulatedTransport` accumulates `m_Now += seconds` but computes `Due = m_Now + Latency` once, so for a latency that is an exact tick multiple the two sums differ by about one ULP and the packet waits an extra tick. Stage 2 measured it: a delivery skew of 4 ticks ×280 and 5 ×59 where exact arithmetic predicts a constant 4, and 67 of 400 sends slipping when the additions were replayed outside the test.

**Files:**
- Modify: `Cubit/src/Net/SimulatedTransport.cpp:93-104` (the `stable_partition` predicate in `Advance`)
- Modify: `Tests/src/SimulatedTransportTests.cpp`
- Modify: `Tests/src/WireOracleTests.cpp:24-28` (the skew bounds)

**Interfaces:**
- Consumes: nothing.
- Produces: no signature change. The behavioural contract Tasks 8 and 10 rely on: **a latency that is a whole multiple of the step is delivered after exactly that many steps, every time.**

- [ ] **Step 1: Write the failing test**

Append to `Tests/src/SimulatedTransportTests.cpp` (it already has `Drain`, `OneWayLatency` and the doctest/FrameClock includes; add `<vector>` if it is not there):

```cpp
TEST_CASE("A latency of a whole number of ticks is delivered after exactly that many ticks")
{
    //The prerequisite for every Stage 3 test that reasons about which tick a
    //packet lands on. Without it, "50 ms at 60 Hz is exactly 3 ticks" is true
    //in arithmetic and false in doubles: the clock accumulates by repeated
    //addition while a due time is computed once, so about 17% of packets lose
    //a one-ULP comparison and wait an extra tick. Measured in Stage 2 as a
    //delivery skew of 4 ticks x280 and 5 x59 where a constant was predicted.
    LoopbackNetwork network;
    PeerId peer = InvalidPeer;
    Transport& rawClient = network.AddClient(peer);

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    SimulatedTransport client(rawClient, sim);
    Drain(network.Server());
    Drain(client);

    //Four bytes carrying the tick the packet was sent on. A one-byte payload
    //cannot count past 255, and the slip needs a few hundred ticks to show up
    //as anything other than luck.
    const auto tickBytes = [](int tick)
    {
        std::vector<std::uint8_t> bytes(4);
        for (int i = 0; i < 4; ++i)
            bytes[i] = static_cast<std::uint8_t>((tick >> (i * 8)) & 0xFF);
        return bytes;
    };

    const auto readTick = [](const std::vector<std::uint8_t>& bytes)
    {
        int tick = 0;
        for (int i = 0; i < 4; ++i)
            tick |= static_cast<int>(bytes[i]) << (i * 8);
        return tick;
    };

    std::vector<int> delays;

    for (int tick = 0; tick < 400; ++tick)
    {
        client.Send(LoopbackNetwork::ServerPeer, tickBytes(tick), Channel::Unreliable);
        client.Advance(FrameClock::FixedStepSeconds);

        NetEvent event;
        while (network.Server().Poll(event))
        {
            if (event.Type == NetEventType::Message)
                delays.push_back(tick - readTick(event.Data));
        }
    }

    REQUIRE(delays.size() == 400);

    //Every one, not most. A single slipped delivery is the whole defect.
    for (const int delay : delays)
        CHECK(delay == delays.front());
}
```

- [ ] **Step 2: Run it to confirm it fails**

```bash
./bin/Debug-windows-x86_64/Tests/Tests.exe -tc="A latency of a whole number of ticks is delivered after exactly that many ticks"
```
Expected: FAIL. Stage 2 measured roughly 17% of sends slipping, so expect on the order of 60 failed `CHECK`s. Record the actual number in the task report — it is the evidence that this test measures the known defect rather than a fresh one.

- [ ] **Step 3: Fix the comparison**

In `Cubit/src/Net/SimulatedTransport.cpp`, in `Advance`:

```cpp
void SimulatedTransport::Advance(double seconds)
{
    m_Now += seconds;

    //A packet is due when its due time has arrived, and "arrived" needs a
    //tolerance. The clock accumulates by repeated `m_Now += seconds` while a
    //due time was computed once as `m_Now + Latency`, so for a latency that is
    //an exact multiple of the step those two sums are not the same double: the
    //accumulated one lands about one ULP (~1e-17) below, `Due <= m_Now` fails,
    //and the packet waits a whole extra tick. Stage 2 measured that as ~17% of
    //packets arriving one tick late, and as a delivery skew that varied between
    //two values where a constant was predicted.
    //
    //A nanosecond is six orders of magnitude below the smallest latency this
    //models and eight above the error it absorbs, so it can neither hide a real
    //delay nor fail to cover an accumulated one. It does not make the model
    //approximate - the model is exact, and this is what stops the arithmetic
    //disagreeing with it.
    constexpr double DueEpsilon = 1e-9;

    //Everything now due, in a total order: by time, then by send order. The
    //partition keeps the not-yet-due entries without rebuilding the vector.
    std::vector<Pending> due;
    const auto split = std::stable_partition(m_Outbound.begin(), m_Outbound.end(),
        [this](const Pending& pending) { return pending.Due <= m_Now + DueEpsilon; });
```

The rest of `Advance` is unchanged.

- [ ] **Step 4: Run the new test**

Expected: PASS, every delay equal.

- [ ] **Step 5: Re-pin the golden schedule**

The fix changes when packets land, so `"The canonical network's delivery schedule is pinned"` is expected to fail. That test says so itself: *"if the RNG, its draw order, or the ordering/loss rules change on purpose, these numbers are EXPECTED to change too — re-observe and re-pin rather than assume the test rotted."*

```bash
./bin/Debug-windows-x86_64/Tests/Tests.exe -tc="The canonical network's delivery schedule is pinned"
```

Pin the **observed** vectors from the failure output. Do not adjust `DueEpsilon` to keep the old numbers, and do not re-derive the expected values by reasoning — the value of the pin is that it was observed. Add a line to that test's comment recording that the schedule was re-pinned when the due-time epsilon landed, and what moved.

- [ ] **Step 6: Tighten the oracle's skew bounds**

`Tests/src/WireOracleTests.cpp` currently allows two values:

```cpp
    constexpr std::uint64_t MinSkew = LatencyTicks + 1;
    constexpr std::uint64_t MaxSkew = LatencyTicks + 2;
```

The `+2` was the float slip and should now be gone. The `+1` is real and stays — it is where the measurement is taken, not latency: a snapshot describing tick T is queued at the end of the server's step and picked up by the client's next `Step`, which in that loop runs before the server steps again.

Run `"A client's state is the server's state, delayed by exactly the one-way latency"` and see what the skew actually is. If it is the single value `LatencyTicks + 1`, set both bounds to it, keep the `+1` paragraph of the comment, and replace the floating-point paragraph with one sentence saying the second value was the accumulation defect and naming this commit. **If two values still show up, stop and investigate** — do not widen the bounds back. Something other than the epsilon is delaying packets, and Tasks 8 and 10 are built on this being exact.

- [ ] **Step 7: Run the full suite**

```bash
"$MSB" Cubit.slnx -p:Configuration=Debug -p:Platform=x64 -m -v:minimal -nologo
```
Expected: PASS.

- [ ] **Step 8: Commit**

```bash
git commit -m "Deliver a whole-tick latency on a whole tick"
```

---

### Task 3: `MatchState::StepPlayer`

The spec's second prerequisite, and the method the entire client side stands on. `Step` advances everybody; a client that called it would simulate remote players under gravity between snapshots and then stamp over them — the stepping artifact that choosing 60 Hz snapshots was meant to avoid.

**Files:**
- Modify: `Cubit/include/Cubit/Voxel/MatchState.h`, `Cubit/src/Voxel/MatchState.cpp`
- Modify: `Tests/src/MatchStateTests.cpp`

**Interfaces:**
- Consumes: `void MatchState::Step(std::span<const PlayerCommand>, float)`, `void MatchState::SetTick(std::uint64_t)`, `const std::map<PlayerId, CharacterController>& MatchState::Players() const`.
- Produces: `void MatchState::StepPlayer(PlayerId player, const CharacterInput& input, float seconds)` — advances that one player by one fixed step. Does not touch the tick. Does nothing when the player is absent.

- [ ] **Step 1: Write the failing tests**

Append to `Tests/src/MatchStateTests.cpp`:

```cpp
TEST_CASE("Stepping one player leaves everyone else exactly where they were")
{
    //The constraint that makes client-side prediction possible at all: this
    //machine may simulate itself, and must not simulate anybody else, because
    //it has no idea what they are about to do.
    MatchState match(FlatWorld());

    const PlayerId mine = match.AddPlayer(glm::vec3(4.0f, 10.0f, 4.0f));
    const PlayerId theirs = match.AddPlayer(glm::vec3(8.0f, 10.0f, 8.0f));

    const glm::vec3 theirsBefore = match.Player(theirs).Position();
    const glm::vec3 theirsPreviousBefore = match.Player(theirs).PreviousPosition();

    CharacterInput walking;
    walking.Move = glm::vec2(0.0f, 1.0f);
    walking.Yaw = 90.0f;

    for (int i = 0; i < 30; ++i)
        match.StepPlayer(mine, walking, FrameClock::FixedStepSeconds);

    CHECK(match.Player(mine).Position() != glm::vec3(4.0f, 10.0f, 4.0f));

    //Not "approximately still there". A remote player left alone for thirty
    //steps must not have fallen a millimetre, or prediction is quietly
    //simulating everybody.
    CHECK(match.Player(theirs).Position() == theirsBefore);
    CHECK(match.Player(theirs).PreviousPosition() == theirsPreviousBefore);
}

TEST_CASE("Stepping one player does not advance the tick")
{
    //The tick belongs to the match, and on a client it is the client's own
    //clock, advanced deliberately once per predicted step. If StepPlayer moved
    //it, an input's tick would depend on how many players happened to be
    //stepped, and the server's ack would name the wrong input.
    MatchState match(FlatWorld());
    const PlayerId player = match.AddPlayer(glm::vec3(4.0f, 10.0f, 4.0f));

    for (int i = 0; i < 10; ++i)
        match.StepPlayer(player, CharacterInput{}, FrameClock::FixedStepSeconds);

    CHECK(match.Tick() == 0);
}

TEST_CASE("Stepping a player who is not there does nothing")
{
    //A client predicts from the moment it is connected, which is before its
    //first snapshot has told it where it stands - so StepPlayer is called with
    //an id the match does not hold yet. Same treatment as a command naming an
    //absent player: routine, not an error.
    MatchState match(FlatWorld());

    match.StepPlayer(PlayerId{ 42 }, CharacterInput{}, FrameClock::FixedStepSeconds);

    CHECK(match.Players().empty());
    CHECK(match.Tick() == 0);
}

TEST_CASE("Stepping every player one at a time is stepping the whole match")
{
    //THE ORACLE FOR THIS TASK, and the property everything downstream rests on:
    //the client and the server must be running the same simulation. A client
    //that predicted through a subtly different code path would diverge from the
    //server every tick, and reconciliation would spend its life fighting the
    //difference.
    //
    //Same shape as "Stepping a match matches stepping the character directly"
    //above: a second, independent way of computing the same thing.
    MatchState viaStep(FlatWorld());
    MatchState viaStepPlayer(FlatWorld());

    const PlayerId first = viaStep.AddPlayer(glm::vec3(4.0f, 10.0f, 4.0f));
    const PlayerId second = viaStep.AddPlayer(glm::vec3(8.0f, 12.0f, 8.0f));
    viaStepPlayer.AddPlayer(first, glm::vec3(4.0f, 10.0f, 4.0f));
    viaStepPlayer.AddPlayer(second, glm::vec3(8.0f, 12.0f, 8.0f));

    for (int i = 0; i < 120; ++i)
    {
        //Varied on purpose: a constant input would let a controller that
        //ignored the input entirely still agree.
        CharacterInput firstInput;
        firstInput.Move = glm::vec2(std::sin(i * 0.1f), std::cos(i * 0.1f));
        firstInput.Yaw = static_cast<float>(i);
        firstInput.Jump = (i % 17) == 0;

        CharacterInput secondInput;
        secondInput.Move = glm::vec2(0.0f, 1.0f);
        secondInput.Yaw = -static_cast<float>(i) * 2.0f;

        const PlayerCommand commands[] = { { first, firstInput }, { second, secondInput } };
        viaStep.Step(commands, FrameClock::FixedStepSeconds);

        //In id order, matching the order Step fans commands out in - iteration
        //order is part of what makes a step reproducible.
        viaStepPlayer.StepPlayer(first, firstInput, FrameClock::FixedStepSeconds);
        viaStepPlayer.StepPlayer(second, secondInput, FrameClock::FixedStepSeconds);
        viaStepPlayer.SetTick(viaStepPlayer.Tick() + 1);

        //Bit-exact, not Approx. These are the same arithmetic twice, not two
        //estimates of one number.
        CHECK(viaStepPlayer.Player(first).Position() == viaStep.Player(first).Position());
        CHECK(viaStepPlayer.Player(first).PreviousPosition() == viaStep.Player(first).PreviousPosition());
        CHECK(viaStepPlayer.Player(first).VerticalVelocity() == viaStep.Player(first).VerticalVelocity());
        CHECK(viaStepPlayer.Player(first).Grounded() == viaStep.Player(first).Grounded());

        CHECK(viaStepPlayer.Player(second).Position() == viaStep.Player(second).Position());
        CHECK(viaStepPlayer.Tick() == viaStep.Tick());
    }
}
```

`<cmath>` is needed for `std::sin`/`std::cos`; add it to the includes if it is not already there.

- [ ] **Step 2: Run them to verify they fail**

```bash
"$MSB" Cubit.slnx -p:Configuration=Debug -p:Platform=x64 -m -v:minimal -nologo
```
Expected: compile error, `MatchState` has no member `StepPlayer`.

- [ ] **Step 3: Declare it**

In `Cubit/include/Cubit/Voxel/MatchState.h`, directly below `Step`:

```cpp
    //Advances exactly one player by one fixed step. Leaves the tick and every
    //other player alone.
    //
    //The server uses Step; a client uses this. A client predicts only itself,
    //because it has no idea what anybody else is about to do - and a client
    //that stepped a remote under gravity between snapshots, then stamped over
    //it when one arrived, would produce exactly the stutter that choosing 60 Hz
    //snapshots was meant to avoid.
    //
    //Does nothing when the player is absent, for the same reason Step ignores a
    //command naming somebody who has left: a client predicts from the moment it
    //is connected, which is before the first snapshot has told it where it
    //stands.
    //
    //The tick is deliberately not touched. On a client it is that client's own
    //clock and is advanced once per predicted step by the caller, so an input's
    //tick cannot depend on how many players happened to be stepped.
    void StepPlayer(PlayerId player, const CharacterInput& input, float seconds);
```

- [ ] **Step 4: Define it**

In `Cubit/src/Voxel/MatchState.cpp`, beside `Step`:

```cpp
void MatchState::StepPlayer(PlayerId player, const CharacterInput& input, float seconds)
{
    const auto found = m_Players.find(player);
    if (found == m_Players.end())
        return;

    found->second.Step(m_World, input, seconds);
}
```

- [ ] **Step 5: Run the tests**

Expected: PASS, all four.

- [ ] **Step 6: Try to falsify the oracle**

Make `StepPlayer` advance the tick as well (`++m_Tick`). Expected: `"Stepping one player does not advance the tick"` and the oracle's tick check both go red. Revert.

If a mutation you try comes back green, say so in the task report rather than moving on quietly — that is how the Stage 1 determinism test was found to be near-tautological.

- [ ] **Step 7: Commit**

```bash
git commit -m "Let a match step one player at a time"
```

---

### Task 4: Protocol version 2, part one — an input bundle with a real tick

`InputMessage::Sequence` was always documented as a placeholder: *"a counter, not a tick … Stage 3 is where an input acquires a real tick, because that is when replay needs to know where to reinsert it."* Now it does, and the message carries the last three inputs rather than only the newest, so a single lost or late packet is covered by the next one.

This task changes the wire and adapts `MatchServer` and `MatchClient` just enough to keep the suite green. The behaviour that uses the bundle arrives in Tasks 6 and 7.

**Files:**
- Modify: `Cubit/include/Cubit/Net/Protocol.h`, `Cubit/src/Net/Protocol.cpp`
- Modify: `Cubit/include/Cubit/Net/MatchServer.h:59`, `Cubit/src/Net/MatchServer.cpp:139-155`
- Modify: `Cubit/include/Cubit/Net/MatchClient.h:106-109`, `Cubit/src/Net/MatchClient.cpp:71-79`
- Modify: `Tests/src/ProtocolTests.cpp`, `Tests/src/MatchServerTests.cpp`

**Interfaces:**
- Consumes: `ByteWriter::U8/U64/F32/Bool`, `ByteReader::U8/U64/F32/Bool/Ok/Remaining`.
- Produces:
  - `constexpr std::uint32_t ProtocolVersion = 2;`
  - `constexpr std::uint8_t InputBundleSize = 3;`
  - `struct InputMessage { std::uint64_t FirstTick; std::vector<CharacterInput> Inputs; };` — `Inputs` is oldest first, and their ticks are `FirstTick`, `FirstTick + 1`, … consecutively.
  - `std::uint64_t MatchServer::Client::LastInputTick` replacing `std::uint32_t LastSequence`.

- [ ] **Step 1: Write the failing tests**

Replace `"Input round-trips a sequence and the character's intent"` in `Tests/src/ProtocolTests.cpp` with:

```cpp
TEST_CASE("Input round-trips a bundle and the tick it starts at")
{
    InputMessage sent;
    sent.FirstTick = 4294967300ull;   //Past a u32, so a narrowed field shows up.

    for (int i = 0; i < InputBundleSize; ++i)
    {
        CharacterInput input;
        input.Move = glm::vec2(0.25f * i, -1.0f);
        input.Yaw = 90.0f + i;
        input.Pitch = -12.5f - i;
        input.Jump = (i % 2) == 0;
        sent.Inputs.push_back(input);
    }

    InputMessage received;
    REQUIRE(Decode(Encode(sent), received));

    CHECK(received.FirstTick == 4294967300ull);
    REQUIRE(received.Inputs.size() == static_cast<std::size_t>(InputBundleSize));

    for (int i = 0; i < InputBundleSize; ++i)
    {
        CHECK(received.Inputs[i].Move == sent.Inputs[i].Move);
        CHECK(received.Inputs[i].Yaw == sent.Inputs[i].Yaw);
        CHECK(received.Inputs[i].Pitch == sent.Inputs[i].Pitch);
        CHECK(received.Inputs[i].Jump == sent.Inputs[i].Jump);
    }
}

TEST_CASE("A three-input bundle is 61 bytes")
{
    //Pinned because it is the number the stage's upstream cost is quoted from:
    //1 id + 1 count + 8 tick + 3 x 17 = 61 bytes, 3,660 B/s per client at
    //60 Hz. A field silently widening is a bandwidth regression nobody would
    //otherwise notice until a real network was involved.
    InputMessage message;
    message.FirstTick = 1;
    message.Inputs.assign(InputBundleSize, CharacterInput{});

    CHECK(Encode(message).size() == 61);
}

TEST_CASE("An input declaring more entries than it carries is refused")
{
    //The same shape of guard as the snapshot's, and the same class: a u8 count
    //cannot demand more than 255 entries, so the trailing Ok() check would
    //refuse this packet anyway - this makes the refusal instant. Read the note
    //in Decode(WelcomeMessage&) before touching any of the three; that one is
    //the guard that is not optional.
    InputMessage message;
    message.FirstTick = 7;
    message.Inputs.assign(3, CharacterInput{});

    std::vector<std::uint8_t> bytes = Encode(message);

    //Claim 200 inputs in a packet carrying three.
    bytes[1] = 200;

    InputMessage received;
    CHECK_FALSE(Decode(bytes, received));
}

TEST_CASE("An empty bundle is legal to decode and carries nothing")
{
    //Not something the client sends, but a decoder that threw or half-filled
    //on it would be a crash reachable from one hostile byte.
    InputMessage message;
    message.FirstTick = 99;

    InputMessage received;
    received.Inputs.assign(2, CharacterInput{});

    REQUIRE(Decode(Encode(message), received));
    CHECK(received.FirstTick == 99);
    CHECK(received.Inputs.empty());
}
```

In the truncation sweep `"Every message truncated at every length is refused without crashing"`, replace the `InputMessage` it builds:

```cpp
        InputMessage input;
        input.FirstTick = 9;
        input.Inputs.assign(InputBundleSize, CharacterInput{});
        messages.push_back(Encode(input));
```

- [ ] **Step 2: Run them to verify they fail**

```bash
"$MSB" Cubit.slnx -p:Configuration=Debug -p:Platform=x64 -m -v:minimal -nologo
```
Expected: compile error — `InputMessage` has no member `FirstTick`, and `InputBundleSize` is undeclared.

- [ ] **Step 3: Change the message**

In `Cubit/include/Cubit/Net/Protocol.h`:

```cpp
//Bumped whenever any message's layout changes. A mismatch is a disconnect with
//a logged reason: two builds of a hand-rolled wire format disagreeing about
//field widths produce garbage positions, which read as a physics bug and cost
//a day.
//
//2: inputs became a bundle carrying a real client tick, and PlayerSnapshot
//gained the ack that makes replay possible. Both are on the per-tick path,
//which is exactly the case this counter exists for.
constexpr std::uint32_t ProtocolVersion = 2;

//How many inputs one InputMessage carries at most.
//
//The redundancy is the entire defence against a starved server step: the
//server advances at a fixed rate whether or not an input arrived, so a late
//input means it steps once without one and its state stops being a prefix of
//what the client predicted. Sending the last three means a single lost or late
//packet is covered by the next one, at a cost of 34 bytes per message.
constexpr std::uint8_t InputBundleSize = 3;

struct InputMessage
{
    //The tick of the OLDEST input in the bundle, in the CLIENT's own
    //numbering. The client produces exactly one input per tick, so a bundle's
    //ticks are consecutive and only the first needs sending.
    //
    //A real tick now, not Stage 2's counter: replay has to know where to
    //reinsert an input, and this is the number the server echoes back in
    //PlayerSnapshot::LastInputTick.
    std::uint64_t FirstTick = 0;

    //Oldest first. Never longer than InputBundleSize when this client sent it,
    //but a decoder must not assume that of a packet off a socket.
    std::vector<CharacterInput> Inputs;
};
```

- [ ] **Step 4: Change the codec**

In `Cubit/src/Net/Protocol.cpp`, add to the anonymous namespace beside `PlayerSnapshotBytes`:

```cpp
    constexpr std::size_t CharacterInputBytes = 4 + 4 + 4 + 4 + 1;
```

Then:

```cpp
std::vector<std::uint8_t> Encode(const InputMessage& message)
{
    ByteWriter writer;
    writer.U8(static_cast<std::uint8_t>(MessageId::Input));
    writer.U8(static_cast<std::uint8_t>(message.Inputs.size()));
    writer.U64(message.FirstTick);

    for (const CharacterInput& input : message.Inputs)
    {
        writer.F32(input.Move.x);
        writer.F32(input.Move.y);
        writer.F32(input.Yaw);
        writer.F32(input.Pitch);
        writer.Bool(input.Jump);
    }

    return writer.Bytes();
}
```

```cpp
bool Decode(std::span<const std::uint8_t> bytes, InputMessage& out)
{
    ByteReader reader(bytes);
    if (!OpenAs(reader, MessageId::Input))
        return false;

    InputMessage message;
    const std::uint8_t count = reader.U8();
    message.FirstTick = reader.U64();

    //A resource guard, not a correctness one, and the same class as the
    //snapshot's: a u8 count tops out at 255 entries, so the trailing Ok() check
    //below refuses an over-claiming packet on its own once the loop runs out of
    //real bytes. What this line changes is refusing instantly rather than
    //reserving for 255 first. Decode(WelcomeMessage&) above is the one where
    //the same-shaped guard is NOT optional - its count is a u32, and deleting
    //it does not make Decode wrong, it makes Decode not return. Read that note
    //before touching any of the three.
    if (!reader.Ok() || count > reader.Remaining() / CharacterInputBytes)
        return false;

    message.Inputs.reserve(count);
    for (std::uint8_t i = 0; i < count; ++i)
    {
        CharacterInput input;
        input.Move.x = reader.F32();
        input.Move.y = reader.F32();
        input.Yaw = reader.F32();
        input.Pitch = reader.F32();
        input.Jump = reader.Bool();
        message.Inputs.push_back(input);
    }

    if (!reader.Ok())
        return false;

    out = std::move(message);
    return true;
}
```

- [ ] **Step 5: Adapt the server and the client so everything still compiles**

Both are interim: Task 6 replaces the server's handling with a queue, Task 7 replaces the client's counter with its real tick. Keep them minimal and say so in the code.

`Cubit/include/Cubit/Net/MatchServer.h`, in `Client`, replacing `LastSequence`:

```cpp
        //Newest input tick this client has had applied. The unreliable channel
        //is unordered and bundles are redundant, so anything not strictly
        //greater is stale or a duplicate and is dropped.
        std::uint64_t LastInputTick = 0;
```

`Cubit/src/Net/MatchServer.cpp`, in `HandleMessage`'s `Input` case:

```cpp
    case MessageId::Input:
    {
        InputMessage input;
        if (!Decode(data, input) || client->Player == InvalidPlayer)
            return;

        if (input.Inputs.empty())
            return;

        //INTERIM, replaced in the task that adds the input queue: only the
        //newest input in the bundle is taken, which is exactly Stage 2's
        //behaviour with a wider counter. Taking the newest is the wrong answer
        //once replay exists - it discards intent the client has already
        //predicted on - and the queue is what fixes it.
        const std::uint64_t newest = input.FirstTick + input.Inputs.size() - 1;
        if (newest <= client->LastInputTick)
            return;

        client->LastInputTick = newest;
        client->HasInput = true;
        client->Input = input.Inputs.back();
        client->Yaw = client->Input.Yaw;
        client->Pitch = client->Input.Pitch;
        return;
    }
```

`Cubit/include/Cubit/Net/MatchClient.h`, replacing `m_Sequence`:

```cpp
    //INTERIM: a counter widened to a tick's type, not yet a tick. The client
    //still does not step in this commit, so it has no simulation tick to name.
    std::uint64_t m_InputTick = 0;
```

`Cubit/src/Net/MatchClient.cpp`, in `Step`:

```cpp
    InputMessage message;
    message.FirstTick = ++m_InputTick;
    message.Inputs = { m_Input };
```

- [ ] **Step 6: Update the server's tests**

In `Tests/src/MatchServerTests.cpp`, the three places that set `Sequence`:

```cpp
    for (std::uint64_t i = 1; i <= 30; ++i)
    {
        InputMessage input;
        input.FirstTick = i;
        CharacterInput held;
        held.Move = glm::vec2(0.0f, 1.0f);
        held.Yaw = 0.0f;
        input.Inputs = { held };
        client.Send(LoopbackNetwork::ServerPeer, Encode(input), Channel::Unreliable);
        server.Step(FrameClock::FixedStepSeconds);
    }
```

and in `"A stale or duplicated input is ignored"`, `newer.FirstTick = 10` / `stale.FirstTick = 9`, each with a one-entry `Inputs`.

- [ ] **Step 7: Run the suite**

Expected: PASS. If `"A stale or duplicated input is ignored"` fails, the interim newest-of-bundle rule is wrong, not the test.

- [ ] **Step 8: Commit**

```bash
git commit -m "Give an input a tick and send it three times"
```

---

### Task 5: Protocol version 2, part two — the ack

`PlayerSnapshot` gains `LastInputTick`: the newest input from that player the server has consumed. It is the ack that makes replay possible.

Per-*player* rather than per-recipient, deliberately. A per-recipient ack would force the server to encode a separate snapshot for every client; `SendToJoined` currently encodes once and sends identical bytes to everyone. The cost is 8 bytes per player: `PlayerSnapshot` 27 → 35 bytes, a two-player snapshot 65 → 81, downstream 3,900 → 4,860 B/s per client.

**Files:**
- Modify: `Cubit/include/Cubit/Net/Protocol.h:74-87`, `Cubit/src/Net/Protocol.cpp` (`PlayerSnapshotBytes`, both snapshot codecs)
- Modify: `Cubit/src/Net/MatchServer.cpp:211-244` (`SendSnapshots`)
- Modify: `Tests/src/ProtocolTests.cpp`, `Tests/src/MatchServerTests.cpp`

**Interfaces:**
- Consumes: `MatchServer::Client::LastInputTick` from Task 4.
- Produces: `std::uint64_t PlayerSnapshot::LastInputTick` — in the *client's* tick numbering, echoed back untouched. Zero for a player whose input has never been applied.

- [ ] **Step 1: Write the failing tests**

In `Tests/src/ProtocolTests.cpp`, add `first.LastInputTick = 4294967301ull;` and `second.LastInputTick = 0;` to `TwoPlayerSnapshot()`, and add to `"Snapshot round-trips every player"`:

```cpp
    CHECK(received.Players[0].LastInputTick == 4294967301ull);
    CHECK(received.Players[1].LastInputTick == 0);
```

And a new case:

```cpp
TEST_CASE("A two-player snapshot is 81 bytes")
{
    //Pinned for the same reason the input bundle's size is: this is where the
    //stage's 4,860 B/s per client comes from. 1 id + 8 tick + 2 count +
    //2 x 35 = 81.
    CHECK(Encode(TwoPlayerSnapshot()).size() == 81);
}
```

In `Tests/src/MatchServerTests.cpp`:

```cpp
TEST_CASE("A snapshot acknowledges the input the server applied")
{
    //The ack is what makes replay possible: a client keeps every input the
    //server has not confirmed and replays them on top of each correction. An
    //ack that named the wrong input would have the client replay something
    //already applied, which is a permanent divergence rather than a glitch.
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& client = network.AddClient(peer);
    const PlayerId player = Join(server, client);
    REQUIRE(player != InvalidPlayer);

    CharacterInput walking;
    walking.Move = glm::vec2(0.0f, 1.0f);

    //One input per step, ticks 1..5. Driven a step at a time rather than sent
    //in one burst so this stays true both now and once inputs are queued and
    //consumed one per tick.
    for (std::uint64_t tick = 1; tick <= 5; ++tick)
    {
        InputMessage input;
        input.FirstTick = tick;
        input.Inputs = { walking };
        client.Send(LoopbackNetwork::ServerPeer, Encode(input), Channel::Unreliable);
        server.Step(FrameClock::FixedStepSeconds);
    }

    //Stepped until the ack catches up rather than checked immediately: how many
    //ticks the server takes to work through what it has been sent is its own
    //property, not something this test should pin.
    std::uint64_t acked = 0;
    for (int step = 0; step < 16 && acked < 5; ++step)
    {
        server.Step(FrameClock::FixedStepSeconds);

        const std::optional<SnapshotMessage> snapshot = LastSnapshot(client);
        if (!snapshot.has_value())
            continue;

        REQUIRE(snapshot->Players.size() == 1);
        CHECK(snapshot->Players[0].Player == player);
        acked = snapshot->Players[0].LastInputTick;
    }

    CHECK(acked == 5);
}
```

- [ ] **Step 2: Run to verify they fail**

Expected: compile error — `PlayerSnapshot` has no member `LastInputTick`.

- [ ] **Step 3: Add the field**

In `Cubit/include/Cubit/Net/Protocol.h`, at the end of `PlayerSnapshot`:

```cpp
    //The newest input from this player that the server has applied, in the
    //CLIENT's own tick numbering, echoed back untouched. The client replays
    //everything above it on top of the state in this snapshot.
    //
    //Per-player rather than per-recipient so the server still encodes one
    //snapshot and sends identical bytes to everybody. Two clients need not
    //agree about each other's numbering: each reads only its own entry, and
    //nobody else's is meaningful to it.
    std::uint64_t LastInputTick = 0;
```

- [ ] **Step 4: Carry it on the wire**

In `Cubit/src/Net/Protocol.cpp`:

```cpp
    //Bytes each entry costs on the wire. Used to reject an absurd count before
    //reserving for it, which is what stops a tiny hostile packet claiming a
    //huge collection from becoming a denial of service.
    constexpr std::size_t PlayerSnapshotBytes = 2 + 12 + 4 + 4 + 4 + 1 + 8;
```

Append `writer.U64(player.LastInputTick);` to the encode loop and `player.LastInputTick = reader.U64();` to the decode loop, both after `Grounded`.

- [ ] **Step 5: Fill it in on the server**

In `SendSnapshots`, inside the `owner != m_Clients.end()` branch:

```cpp
        if (owner != m_Clients.end())
        {
            entry.Yaw = owner->Yaw;
            entry.Pitch = owner->Pitch;
            entry.LastInputTick = owner->LastInputTick;
        }
```

- [ ] **Step 6: Run the suite**

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git commit -m "Acknowledge the input a snapshot was stepped from"
```

---

### Task 6: The server consumes inputs oldest first

Bundles are redundant, so most of what arrives is already known. The server filters duplicates, queues the rest, and consumes exactly one per tick — **the oldest**. Taking the newest would discard intent the client has already predicted on, guaranteeing a correction every time a bundle arrived after a gap, which is precisely the case bundling exists to survive.

**Files:**
- Modify: `Cubit/include/Cubit/Net/MatchServer.h:52-73`, `Cubit/src/Net/MatchServer.cpp:40-60, 139-155`
- Modify: `Tests/src/MatchServerTests.cpp`

**Interfaces:**
- Consumes: `InputMessage::FirstTick`, `InputMessage::Inputs` (Task 4); `PlayerSnapshot::LastInputTick` (Task 5).
- Produces: no public signature change. The contract Task 8 replays against: **`LastInputTick` in a snapshot is the tick of the input that was applied on the step that snapshot describes, and every input the client sent above it is still unapplied.**

- [ ] **Step 1: Write the failing tests**

Append to `Tests/src/MatchServerTests.cpp`. `WalkedSteps` builds the reference oracle — a bare `CharacterController` stepped n times — so these assert against a second, independent computation rather than against the server's own arithmetic:

```cpp
namespace
{
    //Where a character starting at the spawn ends up after `steps` walking
    //steps, computed with no server involved. The reference every queue test
    //below is checked against: "moved a bit" would pass under a queue that
    //dropped half its inputs.
    glm::vec3 WalkedFromSpawn(int steps)
    {
        World world = FlatWorld();
        CharacterController character;
        character.Teleport(Spawn);

        CharacterInput walking;
        walking.Move = glm::vec2(0.0f, 1.0f);

        for (int i = 0; i < steps; ++i)
            character.Step(world, walking, FrameClock::FixedStepSeconds);

        return character.Position();
    }

    InputMessage Bundle(std::uint64_t firstTick, int count)
    {
        CharacterInput walking;
        walking.Move = glm::vec2(0.0f, 1.0f);

        InputMessage message;
        message.FirstTick = firstTick;
        message.Inputs.assign(count, walking);
        return message;
    }
}

TEST_CASE("A bundle's inputs are applied one per tick, oldest first")
{
    //One tick, one input - the contract that makes reconciliation converge at
    //all. If the server applied a whole bundle on one tick, or dropped all but
    //the newest, its state would stop being a prefix of what the client
    //predicted and the difference would never go away.
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& client = network.AddClient(peer);
    REQUIRE(Join(server, client) != InvalidPlayer);
    const PlayerId player = server.Match().Players().begin()->first;

    client.Send(LoopbackNetwork::ServerPeer, Encode(Bundle(1, 3)), Channel::Unreliable);

    //One step: exactly one input applied, however many arrived.
    server.Step(FrameClock::FixedStepSeconds);
    CHECK(server.Match().Player(player).Position() == WalkedFromSpawn(1));

    server.Step(FrameClock::FixedStepSeconds);
    CHECK(server.Match().Player(player).Position() == WalkedFromSpawn(2));

    server.Step(FrameClock::FixedStepSeconds);
    CHECK(server.Match().Player(player).Position() == WalkedFromSpawn(3));

    //Queue empty: this tick has no input at all, exactly as when a packet is
    //lost. On flat ground a walking character with no input simply stands
    //still, which is why the oracle is unchanged.
    server.Step(FrameClock::FixedStepSeconds);
    CHECK(server.Match().Player(player).Position() == WalkedFromSpawn(3));
}

TEST_CASE("An input repeated by the next bundle is applied once")
{
    //The redundancy is only free if duplicates are dropped. Applying tick 2
    //twice would walk the player a step further than it ever asked to go, and
    //the client would be corrected for the server's mistake.
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& client = network.AddClient(peer);
    REQUIRE(Join(server, client) != InvalidPlayer);
    const PlayerId player = server.Match().Players().begin()->first;

    //Ticks 1,2,3 then 2,3,4 - four distinct inputs across two bundles.
    client.Send(LoopbackNetwork::ServerPeer, Encode(Bundle(1, 3)), Channel::Unreliable);
    client.Send(LoopbackNetwork::ServerPeer, Encode(Bundle(2, 3)), Channel::Unreliable);

    for (int i = 0; i < 10; ++i)
        server.Step(FrameClock::FixedStepSeconds);

    CHECK(server.Match().Player(player).Position() == WalkedFromSpawn(4));
}

TEST_CASE("An input already applied is never applied again")
{
    //Stage 2's staleness rule, now expressed against the queue. A bundle that
    //arrives late and repeats what has already been stepped must change
    //nothing at all.
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& client = network.AddClient(peer);
    REQUIRE(Join(server, client) != InvalidPlayer);
    const PlayerId player = server.Match().Players().begin()->first;

    client.Send(LoopbackNetwork::ServerPeer, Encode(Bundle(1, 3)), Channel::Unreliable);
    for (int i = 0; i < 5; ++i)
        server.Step(FrameClock::FixedStepSeconds);

    const glm::vec3 settled = server.Match().Player(player).Position();
    REQUIRE(settled == WalkedFromSpawn(3));

    //The same three inputs arrive again, late.
    client.Send(LoopbackNetwork::ServerPeer, Encode(Bundle(1, 3)), Channel::Unreliable);
    for (int i = 0; i < 5; ++i)
        server.Step(FrameClock::FixedStepSeconds);

    CHECK(server.Match().Player(player).Position() == settled);
}

TEST_CASE("The input queue is capped, and overflow is dropped rather than absorbed")
{
    //A client running further ahead than this design assumes is a fault worth
    //seeing. Silently absorbing its backlog would present as unexplained
    //corrections much later, on a machine nobody is debugging.
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& client = network.AddClient(peer);
    REQUIRE(Join(server, client) != InvalidPlayer);
    const PlayerId player = server.Match().Players().begin()->first;

    //Twenty inputs with no step in between: the queue can hold eight.
    for (std::uint64_t tick = 1; tick <= 20; ++tick)
        client.Send(LoopbackNetwork::ServerPeer, Encode(Bundle(tick, 1)), Channel::Unreliable);

    for (int i = 0; i < 40; ++i)
        server.Step(FrameClock::FixedStepSeconds);

    //Eight applied, twelve dropped. Not "fewer than twenty": the exact number
    //is what distinguishes a cap from a leak.
    CHECK(server.Match().Player(player).Position() == WalkedFromSpawn(8));
}

TEST_CASE("A snapshot acknowledges the oldest input of the bundle first")
{
    //Which end of the queue is consumed, asserted directly. Popping the newest
    //would ack 3 on the first step; popping the oldest acks 1, then 2, then 3.
    //This is the one assertion that tells those two implementations apart, and
    //the difference between them is a correction on every gap.
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& client = network.AddClient(peer);
    REQUIRE(Join(server, client) != InvalidPlayer);

    client.Send(LoopbackNetwork::ServerPeer, Encode(Bundle(1, 3)), Channel::Unreliable);

    for (std::uint64_t expected = 1; expected <= 3; ++expected)
    {
        server.Step(FrameClock::FixedStepSeconds);

        const std::optional<SnapshotMessage> snapshot = LastSnapshot(client);
        REQUIRE(snapshot.has_value());
        REQUIRE(snapshot->Players.size() == 1);
        CHECK(snapshot->Players[0].LastInputTick == expected);
    }
}
```

`WalkedFromSpawn` needs `#include "Cubit/Voxel/CharacterController.h"` and `"Cubit/Voxel/World.h"` if they are not already pulled in.

- [ ] **Step 2: Run to verify they fail**

Expected: `"A bundle's inputs are applied one per tick, oldest first"` fails at the first `CHECK` — the interim server applies only the newest of the bundle, so after one step the player has taken one step but from input 3, and after three steps it has moved one step, not three. `"The input queue is capped"` fails because there is no queue.

- [ ] **Step 3: Give the client record a queue**

In `Cubit/include/Cubit/Net/MatchServer.h`, add `#include <deque>` and replace `HasInput`/`Input` in `Client`:

```cpp
        //One input waiting for a tick to consume it, in the client's own tick
        //numbering.
        struct QueuedInput
        {
            std::uint64_t Tick = 0;
            CharacterInput Input;
        };

        //Newest input tick APPLIED, not the newest received. This is what a
        //snapshot acknowledges and what the client replays on top of: naming
        //something merely received would have the client discard an input the
        //server has not stepped yet.
        std::uint64_t LastInputTick = 0;

        //Oldest first. Inputs arrive bundled and out of order on an unordered
        //channel; a step takes the front.
        //
        //A queue rather than Stage 2's single slot because the client now
        //predicts: it has already simulated each of these and is waiting to be
        //told they were right. Dropping all but the newest, which is what the
        //single slot did, would throw away intent that has already been shown
        //on somebody's screen.
        std::deque<QueuedInput> Queue;
```

- [ ] **Step 4: Fill and drain it**

In `Cubit/src/Net/MatchServer.cpp`, add to the anonymous namespace:

```cpp
namespace
{
    //How many unapplied inputs one client may have waiting.
    //
    //Eight is comfortably more than the two or three a healthy client keeps
    //there - it sends three at a time and the server takes one per tick - and
    //small enough that a client running far ahead is refused rather than
    //buffered. Overflow is dropped and logged: absorbing it silently would
    //show up as unexplained corrections much later.
    constexpr std::size_t MaxQueuedInputs = 8;
}
```

The `Input` case in `HandleMessage`:

```cpp
    case MessageId::Input:
    {
        InputMessage input;
        if (!Decode(data, input) || client->Player == InvalidPlayer)
            return;

        for (std::size_t i = 0; i < input.Inputs.size(); ++i)
        {
            const std::uint64_t tick = input.FirstTick + i;

            //Already applied. Bundles repeat, so this is the common case
            //rather than an anomaly, and dropping it here is what makes the
            //redundancy free instead of a rewind.
            if (tick <= client->LastInputTick)
                continue;

            const bool waiting = std::any_of(client->Queue.begin(), client->Queue.end(),
                [tick](const Client::QueuedInput& queued) { return queued.Tick == tick; });

            if (waiting)
                continue;

            if (client->Queue.size() >= MaxQueuedInputs)
            {
                CB_WARN("Dropping an input: this client's queue is full");
                break;
            }

            client->Queue.push_back(Client::QueuedInput{ tick, input.Inputs[i] });
        }

        //The unreliable channel reorders, so a bundle can arrive carrying ticks
        //older than ones already queued. A step takes the front, so the front
        //has to be the oldest.
        std::sort(client->Queue.begin(), client->Queue.end(),
            [](const Client::QueuedInput& a, const Client::QueuedInput& b)
            {
                return a.Tick < b.Tick;
            });

        return;
    }
```

And the command-gathering loop in `Step`:

```cpp
    for (Client& client : m_Clients)
    {
        //An empty queue means no input this tick, exactly as in Stage 2 when a
        //packet was lost. The player simply does not move; the client sees a
        //correction of one step of walking, 0.083 blocks, which is inside the
        //threshold and invisible. That is the whole reason bundling exists: it
        //makes this rare rather than routine.
        if (client.Player == InvalidPlayer || client.Queue.empty())
            continue;

        //THE OLDEST, not the newest. Taking the newest would discard intent the
        //client has already predicted on and shown on screen, guaranteeing a
        //correction every time a bundle arrived after a gap - precisely the
        //case bundling exists to survive.
        const Client::QueuedInput queued = client.Queue.front();
        client.Queue.pop_front();

        client.LastInputTick = queued.Tick;
        client.Yaw = queued.Input.Yaw;
        client.Pitch = queued.Input.Pitch;

        commands.push_back(PlayerCommand{ client.Player, queued.Input });
    }
```

- [ ] **Step 5: Run the tests**

Expected: PASS, all five, plus Task 5's ack test.

- [ ] **Step 6: Try to falsify the oldest-first rule**

Change `Queue.front()` / `pop_front()` to `Queue.back()` / `pop_back()`. Expected: `"A snapshot acknowledges the oldest input of the bundle first"` goes red immediately, and `"A bundle's inputs are applied one per tick, oldest first"` with it. Revert.

Then raise `MaxQueuedInputs` to 64 and check `"The input queue is capped"` goes red. Revert. If either mutation stays green, report it — a cap nothing pins is a cap somebody will delete.

- [ ] **Step 7: Run the full suite and commit**

```bash
"$MSB" Cubit.slnx -p:Configuration=Debug -p:Platform=x64 -m -v:minimal -nologo
git commit -m "Queue a client's inputs and spend them one a tick"
```

---

### Task 7: The client predicts itself

The stage's turn. The client's tick stops being the server's and free-runs from `Welcome.Tick`; every input is stamped with the tick it produced, stepped immediately through `StepPlayer`, kept in an unacked ring, and sent as a bundle of the last three.

Snapshots are still written straight in, so the local player still gets yanked back once a snapshot arrives. That is fixed in Task 8, and splitting it here is deliberate: this task proves the client steps *only itself*, which is a different property from reconciliation converging.

**Files:**
- Modify: `Cubit/include/Cubit/Net/MatchClient.h`, `Cubit/src/Net/MatchClient.cpp`
- Modify: `Tests/src/WireOracleTests.cpp`

**Interfaces:**
- Consumes: `MatchState::StepPlayer` (Task 3), `InputBundleSize`, `InputMessage::FirstTick`/`Inputs` (Task 4).
- Produces:
  - `std::uint64_t MatchClient::ServerTick() const` — the newest server tick this client has seen, tracked separately from its own.
  - `MatchClient::PendingInput { std::uint64_t Tick; CharacterInput Input; }`, private.
  - `m_Match.Tick()` on a client now means **the client's own tick**, not the server's. Task 8 replays against it, Task 9 interpolates against `ServerTick()`.

- [ ] **Step 1: Rewrite the two tests this task changes, and run them red**

Two Stage 2 tests are about to become false. Neither is deleted: one is replaced by its inverse, and one narrows to the half of the property that survives.

In `Tests/src/WireOracleTests.cpp`, replace `"The client never steps the simulation itself"` with:

```cpp
TEST_CASE("The client steps its own player and nobody else's")
{
    //The replacement for Stage 2's "the client never steps the simulation
    //itself", which this stage deliberately makes false. It is replaced rather
    //than deleted so the record survives that the old constraint was a choice:
    //the client now steps, and what must still be true is that it steps only
    //itself. A client that predicted a remote would simulate them under gravity
    //between snapshots and then stamp over the result, which is exactly the
    //stepping artifact 60 Hz snapshots were chosen to avoid.
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    SimulatedTransport serverNet(network.Server(), sim);

    PeerId firstPeer = InvalidPeer;
    PeerId secondPeer = InvalidPeer;
    SimulatedTransport firstNet(network.AddClient(firstPeer), sim);
    SimulatedTransport secondNet(network.AddClient(secondPeer), sim);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, serverNet);
    MatchClient first(firstNet, GoodLoader());
    MatchClient second(secondNet, GoodLoader());

    for (int i = 0; i < 200; ++i)
    {
        first.SetInput(Walking(90.0f));
        second.SetInput(CharacterInput{});
        first.Step(FrameClock::FixedStepSeconds);
        second.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }

    REQUIRE(first.Connected());
    REQUIRE(second.Connected());
    REQUIRE(first.Match().HasPlayer(second.LocalPlayer()));

    //Stop the server and let everything in flight drain, so nothing arrives
    //from now on and the only motion left is what this client produces itself.
    for (int i = 0; i < 30; ++i)
    {
        first.SetInput(Walking(90.0f));
        first.Step(FrameClock::FixedStepSeconds);
    }

    const glm::vec3 mineBefore = first.Match().Player(first.LocalPlayer()).Position();
    const glm::vec3 theirsBefore = first.Match().Player(second.LocalPlayer()).Position();

    for (int i = 0; i < 120; ++i)
    {
        first.SetInput(Walking(90.0f));
        first.Step(FrameClock::FixedStepSeconds);

        //Not one millimetre. A remote is never predicted and never
        //extrapolated: with nothing arriving, there is nothing to say about
        //where they are, and guessing is a wrong answer that has to be taken
        //back.
        CHECK(first.Match().Player(second.LocalPlayer()).Position() == theirsBefore);
    }

    //And the local player kept walking with no server at all. This is the half
    //of the assertion that fails on Stage 2's code.
    CHECK(first.Match().Player(first.LocalPlayer()).Position() != mineBefore);
}
```

Replace `"A client's state is the server's state, delayed by exactly the one-way latency"` with the remote-player form of the same oracle. The local half of it is now false on purpose — the local player is deliberately *ahead* of the server, which is the entire deliverable — but the property still holds for everybody else, and it is still what catches a wire that corrupts, reorders, or mistimes state:

```cpp
TEST_CASE("A client's view of a remote player is the server's, delayed by the one-way latency")
{
    //STAGE 2'S ORACLE, NARROWED TO WHAT IS STILL TRUE. It used to cover every
    //player, including this client's own. Prediction makes the local player
    //deliberately ahead of the server, so asserting it here would be asserting
    //the stage had not happened. Remote players are still written straight from
    //snapshots and must still match the server exactly, offset by flight time.
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    SimulatedTransport serverNet(network.Server(), sim);

    PeerId firstPeer = InvalidPeer;
    PeerId secondPeer = InvalidPeer;
    SimulatedTransport firstNet(network.AddClient(firstPeer), sim);
    SimulatedTransport secondNet(network.AddClient(secondPeer), sim);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, serverNet);
    MatchClient walker(firstNet, GoodLoader());
    MatchClient watcher(secondNet, GoodLoader());

    //Server tick -> where the walker stood at the end of it.
    std::map<std::uint64_t, glm::vec3> history;
    std::vector<std::uint64_t> observedSkew;

    for (int i = 0; i < 400; ++i)
    {
        //ORDER MATTERS AND IS PART OF THE ASSERTION. The clients send and apply
        //first, then the server receives and steps. If the skew below leaves
        //its bound, do NOT widen it - confirm this loop order first, because an
        //unexpected offset means a packet is being serviced in the wrong phase.
        walker.SetInput(Walking(90.0f));
        watcher.SetInput(CharacterInput{});
        walker.Step(FrameClock::FixedStepSeconds);
        watcher.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);

        if (server.Match().HasPlayer(walker.LocalPlayer()))
            history[server.Match().Tick()] = server.Match().Player(walker.LocalPlayer()).Position();

        if (!watcher.Connected() || !watcher.Match().HasPlayer(walker.LocalPlayer()))
            continue;

        //ASSERTION ONE: at whatever server tick the watcher last heard about,
        //its picture of the walker must be the server's picture at that exact
        //tick. Exact equality, not Approx: these are the same floats
        //round-tripped through the codec, not two computations of one number.
        const auto recorded = history.find(watcher.ServerTick());
        if (recorded != history.end())
            CHECK(watcher.Match().Player(walker.LocalPlayer()).Position() == recorded->second);

        if (i > 60)
            observedSkew.push_back(server.Match().Tick() - watcher.ServerTick());
    }

    REQUIRE_FALSE(observedSkew.empty());

    //ASSERTION TWO: the delay is BOUNDED. See the note where MinSkew and
    //MaxSkew are defined for where the constant comes from and why it is not
    //simply the latency.
    for (const std::uint64_t skew : observedSkew)
    {
        CHECK(skew >= MinSkew);
        CHECK(skew <= MaxSkew);
    }
}
```

Run both. Expected: the first fails on its final `CHECK` (a Stage 2 client does not move with the server stopped); the second fails to compile, because `ServerTick()` does not exist yet.

- [ ] **Step 2: Declare the client's own clock and its unacked ring**

In `Cubit/include/Cubit/Net/MatchClient.h`, add `#include <deque>`, and to the public section:

```cpp
    //The newest tick any snapshot has reported. NOT this client's own tick:
    //since prediction, Match().Tick() is the client's, free-running from the
    //one Welcome carried and advanced once per predicted step. The server's is
    //tracked separately because remote-player interpolation is expressed in it.
    std::uint64_t ServerTick() const { return m_ServerTick; }
```

and to the private section:

```cpp
    //One input the server has not yet acknowledged, kept so it can be replayed
    //on top of a correction.
    struct PendingInput
    {
        std::uint64_t Tick = 0;
        CharacterInput Input;
    };

    //Oldest first, and consecutive: one input is produced per tick and they are
    //dropped from the front as they are acknowledged.
    std::deque<PendingInput> m_Unacked;

    std::uint64_t m_ServerTick = 0;
```

Replace the interim `m_InputTick` with nothing — the tick now comes from `m_Match.Tick()`.

Add the ring's bound above the class:

```cpp
//How many unacknowledged inputs a client keeps for replay.
//
//Two seconds at 60 Hz, and far more than the ten or so a healthy connection
//holds (about RTT / 16.7 ms). A client that reaches this has heard nothing from
//the server for two seconds and has a bigger problem than replay accuracy; the
//bound exists so a silent server cannot grow this without limit.
constexpr std::size_t MaxUnackedInputs = 120;
```

- [ ] **Step 3: Predict**

In `Cubit/src/Net/MatchClient.cpp`, replace the tail of `Step`:

```cpp
    if (!m_Connected || !m_HasInput)
        return;

    //The tick this step is about to produce. Stamped before the step so an
    //input's tick names the step it caused, which is the number the server
    //echoes back and the number replay reinserts against.
    const std::uint64_t tick = m_Match.Tick() + 1;

    m_Unacked.push_back(PendingInput{ tick, m_Input });

    //A silent server cannot grow this without limit. Dropping the oldest loses
    //replay history for an input that is never going to be acknowledged
    //anyway.
    if (m_Unacked.size() > MaxUnackedInputs)
        m_Unacked.pop_front();

    //PREDICTION. This player only: this machine has no idea what anybody else
    //is about to do, and StepPlayer does nothing at all before the first
    //snapshot has said where this player stands.
    m_Match.StepPlayer(m_LocalPlayer, m_Input, static_cast<float>(seconds));
    m_Match.SetTick(tick);

    //The last three, oldest first. The redundancy is the whole defence against
    //the server stepping a tick with nothing to apply: one lost or late packet
    //is covered by the next one.
    const std::size_t count = std::min<std::size_t>(m_Unacked.size(), InputBundleSize);
    const std::size_t begin = m_Unacked.size() - count;

    InputMessage message;
    message.FirstTick = m_Unacked[begin].Tick;
    for (std::size_t i = begin; i < m_Unacked.size(); ++i)
        message.Inputs.push_back(m_Unacked[i].Input);

    //Unreliable: a resend would deliver an intent the player has already
    //replaced, and the bundle already covers the loss.
    m_Transport.Send(m_ServerPeer, Encode(message), Channel::Unreliable);
    m_HasInput = false;
```

In `HandleSnapshot`, replace `m_Match.SetTick(snapshot.Tick);` with:

```cpp
    //The client's own tick is not the server's any more - it free-runs and is
    //what an input is stamped with. Adopting the server's number here would
    //rewind it every snapshot and stamp two different inputs with one tick.
    m_ServerTick = snapshot.Tick;
```

In `HandleWelcome`, add `m_ServerTick = welcome.Tick;` beside the existing `m_Match.SetTick(welcome.Tick)` — that call now means "start this client's clock here" and deserves a comment saying so.

Update the class comment on `MatchClient` in the header. It currently opens *"The client half of a match. It NEVER STEPS."* — that is the sentence this task deletes. Replace it with what is now true, keeping the record of why it used to say that:

```cpp
//The client half of a match. It predicts its own player and nothing else.
//
//Through Stage 2 it never stepped at all, deliberately, so the latency was
//plainly visible rather than hidden behind a guess. This is the stage that
//hides it: input is stepped immediately, kept until the server acknowledges
//it, and replayed on top of every correction.
```

- [ ] **Step 4: Run the two rewritten tests**

Expected: PASS.

- [ ] **Step 5: Run the full suite**

Expected: PASS. `"The wire survives 5% loss and 150 ms RTT with jitter"` is expected to still pass here — snapshots are still written straight in, so the client is still dragged to the server's position when one arrives. If it fails, stop: that is prediction diverging with no reconciliation to catch it, and it means the tick or the ack is wrong, not the test.

- [ ] **Step 6: Commit**

```bash
git commit -m "Let the client move before the server says so"
```

---

### Task 8: Reconciliation, and a correction you cannot see

Four steps on each snapshot, in this order: record what prediction believes, write the authoritative state in, replay every input the server has not acknowledged, then compare. Under the threshold the whole correction is thrown away; over it, the replayed state stands and the player snaps.

**Files:**
- Modify: `Cubit/include/Cubit/Net/MatchClient.h`, `Cubit/src/Net/MatchClient.cpp`
- Modify: `Tests/src/WireOracleTests.cpp:479-517` (the bad-network drain)
- Create: `Tests/src/PredictionTests.cpp`

**Interfaces:**
- Consumes: `PlayerSnapshot::LastInputTick` (Task 5), the server's oldest-first consumption (Task 6), `m_Unacked` and the client's own tick (Task 7), `CharacterController::SetState`.
- Produces:
  - `constexpr float CorrectionThreshold = 0.15f;`
  - `struct MatchClient::CorrectionStats { std::uint64_t Snapshots; std::uint64_t Count; float Mean; float Max; };`
  - `CorrectionStats MatchClient::Corrections() const;`

- [ ] **Step 1: Write the failing tests**

Create `Tests/src/PredictionTests.cpp`:

```cpp
#include <doctest.h>

#include "Cubit/FrameClock.h"
#include "Cubit/Net/LoopbackTransport.h"
#include "Cubit/Net/MatchClient.h"
#include "Cubit/Net/MatchServer.h"
#include "Cubit/Net/SimulatedTransport.h"
#include "Cubit/Voxel/CharacterController.h"
#include "Cubit/Voxel/MatchState.h"

#include <glm/glm.hpp>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace
{
    constexpr int LatencyTicks = 3;
    constexpr double OneWayLatency = LatencyTicks * FrameClock::FixedStepSeconds;
    constexpr std::uint64_t MapHash = 0xFEEDFACEull;
    const glm::vec3 Spawn{ 8.0f, 2.0f, 8.0f };

    World FlatWorld()
    {
        World world(2, 2, 2);

        for (int z = 0; z < world.GetDepth(); ++z)
            for (int x = 0; x < world.GetWidth(); ++x)
                world.SetBlock(x, 0, z, BlockId{ 1 });

        return world;
    }

    MatchClient::MapLoader GoodLoader()
    {
        return [](const std::string&) -> std::optional<LoadedMap>
        {
            return LoadedMap{ FlatWorld(), MapHash };
        };
    }

    //A varied input sequence. Constant input would let a replay that ignored
    //its arguments still agree with a straight simulation.
    CharacterInput InputForTick(int tick)
    {
        CharacterInput input;
        input.Move = glm::vec2(std::sin(tick * 0.3f), std::cos(tick * 0.17f));
        input.Yaw = static_cast<float>(tick) * 3.0f;
        input.Pitch = -10.0f;
        input.Jump = (tick % 23) == 0;
        return input;
    }
}

TEST_CASE("Replaying inputs onto a stale state is simulating them in the first place")
{
    //THE ORACLE FOR THIS STAGE, and there is no network in it at all.
    //
    //Reconciliation is exactly this: take the last state the server vouched
    //for, apply everything it has not seen yet, and you are where you should
    //be. That it can be tested with no transport, no server and no client is
    //the whole reason CharacterController::Step was made a pure function of
    //(state, input, world) back in August.
    //
    //If this fails, nothing downstream is worth debugging: the client is not
    //running the same simulation as the server, and no amount of correcting
    //will settle it.
    MatchState straight(FlatWorld());
    const PlayerId player = straight.AddPlayer(Spawn);

    MatchState replayed(FlatWorld());
    replayed.AddPlayer(player, Spawn);

    constexpr int Total = 60;
    constexpr int Authoritative = 24;   //Where the "server" got to.

    glm::vec3 authoritativePosition{ 0.0f };
    glm::vec3 authoritativePrevious{ 0.0f };
    float authoritativeVelocity = 0.0f;
    bool authoritativeGrounded = false;

    for (int tick = 1; tick <= Total; ++tick)
    {
        straight.StepPlayer(player, InputForTick(tick), FrameClock::FixedStepSeconds);

        if (tick == Authoritative)
        {
            const CharacterController& character = straight.Player(player);
            authoritativePosition = character.Position();
            authoritativePrevious = character.PreviousPosition();
            authoritativeVelocity = character.VerticalVelocity();
            authoritativeGrounded = character.Grounded();
        }
    }

    //The client's picture: the authoritative state, plus everything above it.
    replayed.PlayerForWrite(player).SetState(authoritativePosition, authoritativePrevious,
        authoritativeVelocity, authoritativeGrounded);

    for (int tick = Authoritative + 1; tick <= Total; ++tick)
        replayed.StepPlayer(player, InputForTick(tick), FrameClock::FixedStepSeconds);

    //Bit-exact. One simulation, run twice.
    CHECK(replayed.Player(player).Position() == straight.Player(player).Position());
    CHECK(replayed.Player(player).PreviousPosition() == straight.Player(player).PreviousPosition());
    CHECK(replayed.Player(player).VerticalVelocity() == straight.Player(player).VerticalVelocity());
    CHECK(replayed.Player(player).Grounded() == straight.Player(player).Grounded());
}

TEST_CASE("Grounded is part of the state a replay needs, not decoration")
{
    //Why SetState takes all four. Step consults the previous step's grounded
    //flag when deciding whether a jump fires, so a replay that restored only
    //the position would diverge on the first replayed jump - and diverge
    //silently, since the position it started from was right.
    MatchState withFlag(FlatWorld());
    MatchState withoutFlag(FlatWorld());

    const PlayerId player = withFlag.AddPlayer(Spawn);
    withoutFlag.AddPlayer(player, Spawn);

    //Settle onto the ground so Grounded is genuinely true.
    for (int i = 0; i < 30; ++i)
    {
        withFlag.StepPlayer(player, CharacterInput{}, FrameClock::FixedStepSeconds);
        withoutFlag.StepPlayer(player, CharacterInput{}, FrameClock::FixedStepSeconds);
    }

    const CharacterController& settled = withFlag.Player(player);
    REQUIRE(settled.Grounded());

    withFlag.PlayerForWrite(player).SetState(settled.Position(), settled.PreviousPosition(),
        settled.VerticalVelocity(), true);
    withoutFlag.PlayerForWrite(player).SetState(settled.Position(), settled.PreviousPosition(),
        settled.VerticalVelocity(), false);

    CharacterInput jump;
    jump.Jump = true;

    withFlag.StepPlayer(player, jump, FrameClock::FixedStepSeconds);
    withoutFlag.StepPlayer(player, jump, FrameClock::FixedStepSeconds);

    //One of these jumped and one did not.
    CHECK(withFlag.Player(player).Position().y != withoutFlag.Player(player).Position().y);
}

TEST_CASE("A correction smaller than the threshold is not shown")
{
    //The deadzone, downward. A disagreement of a few centimetres is what one
    //starved server tick costs (WalkSpeed / 60 = 0.083 blocks); showing it
    //would be a visible twitch for something the player cannot have noticed.
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    SimulatedTransport serverNet(network.Server(), sim);

    PeerId peer = InvalidPeer;
    SimulatedTransport clientNet(network.AddClient(peer), sim);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, serverNet);
    MatchClient client(clientNet, GoodLoader());

    for (int i = 0; i < 120; ++i)
    {
        client.SetInput(CharacterInput{});
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }
    REQUIRE(client.Connected());

    //Shove the prediction sideways by less than the threshold, then let one
    //snapshot land on it.
    const glm::vec3 predicted = client.Match().Player(client.LocalPlayer()).Position();
    const glm::vec3 nudged = predicted + glm::vec3(0.05f, 0.0f, 0.0f);
    client.MatchForWrite().TeleportPlayer(client.LocalPlayer(), nudged);

    const std::uint64_t before = client.Corrections().Snapshots;

    for (int i = 0; i < 30 && client.Corrections().Snapshots == before; ++i)
    {
        client.SetInput(CharacterInput{});
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }

    REQUIRE(client.Corrections().Snapshots > before);

    //Kept where prediction had it, not dragged back.
    CHECK(client.Match().Player(client.LocalPlayer()).Position().x == doctest::Approx(nudged.x));
    CHECK(client.Corrections().Count == 0);
}

TEST_CASE("A correction bigger than the threshold snaps")
{
    //The deadzone, upward, and the same setup so the two differ in exactly one
    //number. A deadzone with no ceiling is not a deadzone, it is a divergence.
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    SimulatedTransport serverNet(network.Server(), sim);

    PeerId peer = InvalidPeer;
    SimulatedTransport clientNet(network.AddClient(peer), sim);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, serverNet);
    MatchClient client(clientNet, GoodLoader());

    for (int i = 0; i < 120; ++i)
    {
        client.SetInput(CharacterInput{});
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }
    REQUIRE(client.Connected());

    const glm::vec3 predicted = client.Match().Player(client.LocalPlayer()).Position();
    client.MatchForWrite().TeleportPlayer(client.LocalPlayer(), predicted + glm::vec3(1.0f, 0.0f, 0.0f));

    const std::uint64_t before = client.Corrections().Snapshots;

    for (int i = 0; i < 30 && client.Corrections().Snapshots == before; ++i)
    {
        client.SetInput(CharacterInput{});
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }

    REQUIRE(client.Corrections().Count == 1);
    CHECK(client.Corrections().Max > CorrectionThreshold);

    //Back to where the server says, not left a metre out.
    CHECK(client.Match().Player(client.LocalPlayer()).Position().x
        == doctest::Approx(predicted.x).epsilon(0.01));
}
```

After creating the file:

```bash
/c/dev/premake/premake5 vs2026
```

- [ ] **Step 2: Run to verify they fail**

Expected: compile error — `MatchClient` has no member `Corrections`, and `CorrectionThreshold` is undeclared. The oracle's first two cases should compile and **pass immediately**: they are about `MatchState` and `CharacterController`, which already work. That is the point of them — they are the floor everything else stands on, and if either fails, stop and fix that instead.

- [ ] **Step 3: Declare the threshold and the stats**

In `Cubit/include/Cubit/Net/MatchClient.h`, above the class:

```cpp
//How far prediction may disagree with the server before the correction is
//shown, in blocks, as a full 3D distance.
//
//One starved server tick while walking costs WalkSpeed / 60 = 0.083 blocks, so
//0.15 absorbs a single dropped input and little more.
//
//It is a deadzone, which has a known cost: a small persistent error is never
//corrected, so the client is not exactly the server between snaps. That is
//bounded by construction - past this it snaps - and deliberate. This is the one
//number in the stage chosen by reasoning rather than measurement; if the
//measured correction rate is bad, suspect this first.
constexpr float CorrectionThreshold = 0.15f;
```

and in the public section:

```cpp
    //What reconciliation has actually been doing. The stage's acceptance
    //number: "corrections per 1000 ticks" replaces "it feels smooth" the way
    //3,900 B/s replaced "bandwidth is fine", and unlike a playtest it can be
    //re-run to catch a regression.
    struct CorrectionStats
    {
        //Snapshots reconciled - the denominator, and not the same as the number
        //of ticks: snapshots are lost.
        std::uint64_t Snapshots = 0;

        //Reconciliations whose disagreement exceeded the threshold and were
        //therefore shown.
        std::uint64_t Count = 0;

        //Mean and largest magnitude of those, in blocks. Zero when there have
        //been none.
        float Mean = 0.0f;
        float Max = 0.0f;
    };

    CorrectionStats Corrections() const;
```

private:

```cpp
    //Writes the authoritative state in, replays what the server has not
    //acknowledged, and decides whether the difference is worth showing.
    void Reconcile(const PlayerSnapshot& entry);

    //The step length prediction used, so replay uses the same one. A replay at
    //a different step length is a different simulation.
    float m_StepSeconds = static_cast<float>(FrameClock::FixedStepSeconds);

    std::uint64_t m_SnapshotsReconciled = 0;
    std::uint64_t m_CorrectionCount = 0;
    float m_CorrectionTotal = 0.0f;
    float m_CorrectionMax = 0.0f;
```

`FrameClock.h` needs including in the header for that default.

- [ ] **Step 4: Reconcile**

In `Cubit/src/Net/MatchClient.cpp`, at the very top of `Step`, before the transport is serviced:

```cpp
    //Recorded before anything is drained, because a snapshot handled below
    //replays inputs and must replay them at the length prediction used.
    m_StepSeconds = static_cast<float>(seconds);
```

In `HandleSnapshot`, inside the loop over `snapshot.Players`, the local player takes a different path from everybody else:

```cpp
        if (entry.Player == m_LocalPlayer && m_Match.HasPlayer(entry.Player))
        {
            Reconcile(entry);
            continue;
        }
```

placed after the `present.push_back(entry.Player)` line and after the `AddPlayer` call for a player not yet known, so `HasPlayer` is true by then and the guard is belt and braces. On the very first snapshot the local player has just been added at the authoritative position, so that reconcile finds nothing to disagree with and costs one snapshot in the denominator — which is honest: a snapshot was reconciled, and it produced no correction.

The `SetState` and `m_ViewAngles` lines below it still run for everybody else. Reconciling and then falling through to them would write the authoritative position straight back over the replayed one, undoing the whole task.

Then:

```cpp
void MatchClient::Reconcile(const PlayerSnapshot& entry)
{
    CharacterController& character = m_Match.PlayerForWrite(m_LocalPlayer);

    //What prediction believes, kept whole. If the correction turns out to be
    //too small to be worth showing, this is restored in one piece.
    const glm::vec3 before = character.Position();
    const glm::vec3 beforePrevious = character.PreviousPosition();
    const float beforeVelocity = character.VerticalVelocity();
    const bool beforeGrounded = character.Grounded();

    //All four, through SetState rather than Teleport. Teleport writes both
    //positions together, which would flatten the previous position and destroy
    //exactly the interpolation a correction exists to hide.
    //
    //The previous position comes from prediction rather than the wire because
    //the snapshot does not carry one - it is a render-smoothing value, not
    //simulation state anybody else needs. Whenever there is anything at all to
    //replay it is overwritten on the first replayed step; it only survives when
    //the server has caught up completely, and then continuing to interpolate
    //from where this client was drawing is the right answer anyway.
    character.SetState(entry.Position, beforePrevious, entry.VerticalVelocity, entry.Grounded);

    //Everything the server has confirmed is history now.
    while (!m_Unacked.empty() && m_Unacked.front().Tick <= entry.LastInputTick)
        m_Unacked.pop_front();

    //And everything it has not seen is applied on top. This is reconciliation
    //entire: the authoritative state plus the inputs it does not know about is
    //what this machine should be showing.
    for (const PendingInput& pending : m_Unacked)
        m_Match.StepPlayer(m_LocalPlayer, pending.Input, m_StepSeconds);

    ++m_SnapshotsReconciled;

    const float error = glm::distance(character.Position(), before);

    if (error <= CorrectionThreshold)
    {
        //Thrown away WHOLE - position, previous position, velocity and grounded
        //together. Keeping the predicted position while accepting the
        //authoritative velocity would leave the character in a state neither
        //machine ever simulated, and the next step would compound it.
        character.SetState(before, beforePrevious, beforeVelocity, beforeGrounded);
        return;
    }

    //Over the threshold: the replayed state stands, and the player snaps. There
    //is no smoothing here on purpose - a snap is the one option with no new
    //failure mode and the only one that is cleanly testable.
    ++m_CorrectionCount;
    m_CorrectionTotal += error;
    m_CorrectionMax = std::max(m_CorrectionMax, error);
}

MatchClient::CorrectionStats MatchClient::Corrections() const
{
    CorrectionStats stats;
    stats.Snapshots = m_SnapshotsReconciled;
    stats.Count = m_CorrectionCount;
    stats.Max = m_CorrectionMax;
    stats.Mean = m_CorrectionCount == 0
        ? 0.0f
        : m_CorrectionTotal / static_cast<float>(m_CorrectionCount);
    return stats;
}
```

`<algorithm>` is already included for `std::max`.

- [ ] **Step 5: Run the new tests**

Expected: PASS, all four.

- [ ] **Step 6: Give the bad-network drain the deadzone's tolerance**

`"The wire survives 5% loss and 150 ms RTT with jitter"` ends by asserting the client and server agree **exactly** once movement stops. That is now false by design, and the reason is worth writing down rather than loosening quietly. Change the tail to:

```cpp
    REQUIRE(client.Connected());

    //Exact equality was Stage 2's assertion and cannot hold here: the deadzone
    //deliberately leaves a sub-threshold disagreement uncorrected, because
    //showing a two-centimetre correction is worse than carrying it. What must
    //hold is that the disagreement is BOUNDED by that threshold - past it, the
    //client snaps - which is the property the deadzone is only acceptable
    //because of.
    CHECK(glm::distance(client.Match().Player(client.LocalPlayer()).Position(),
        server.Match().Player(client.LocalPlayer()).Position()) <= CorrectionThreshold);
```

- [ ] **Step 7: Try to falsify the deadzone**

Invert the comparison (`error > CorrectionThreshold` for the restore branch). Expected: both deadzone tests go red — one keeps the nudge it should have snapped away, the other snaps something it should have kept.

Then delete the `beforePrevious` argument's use, passing `entry.Position` for both positions instead. This one is **expected but not observed**: it should show up as a remote-smoothing regression only when the unacked list is empty. If nothing goes red, do not add a test to force it — record that the argument is currently unpinned and why, in the comment on that line.

- [ ] **Step 8: Run the full suite and commit**

```bash
"$MSB" Cubit.slnx -p:Configuration=Debug -p:Platform=x64 -m -v:minimal -nologo
git commit -m "Replay what the server has not seen yet"
```

---

### Task 9: Remote players are interpolated, never guessed at

A remote is drawn from a ring of snapshot samples at a point six ticks behind the newest server tick this client has seen. When the target is newer than the newest sample, the newest is held: a wrong extrapolation has to be taken back, and taking it back looks exactly like the stutter it was trying to avoid.

**Files:**
- Modify: `Cubit/include/Cubit/Net/MatchClient.h`, `Cubit/src/Net/MatchClient.cpp`
- Modify: `Tests/src/PredictionTests.cpp`
- Modify: `Sandbox/src/Sandbox.cpp` (`DrawRemotePlayers`)

**Interfaces:**
- Consumes: `MatchClient::ServerTick()` (Task 7).
- Produces:
  - `struct MatchClient::RemotePose { glm::vec3 Position; float Yaw; float Pitch; };`
  - `RemotePose MatchClient::PoseOf(PlayerId player, float alpha) const` — the pose to draw this frame. `alpha` is the renderer's position within the current step, the same number it passes to `InterpolatedPosition`.

- [ ] **Step 1: Write the failing tests**

Append to `Tests/src/PredictionTests.cpp`. These hand-deliver snapshots from a raw loopback endpoint rather than running a server, so the samples are exact known numbers and the interpolation can be checked against arithmetic instead of against itself:

```cpp
namespace
{
    //Hand-built snapshots, so a test can say exactly where a remote was at
    //exactly which tick. A real server would work too, but then the expected
    //values would have to be read back out of it, and a test that asks the
    //subject what the answer is proves very little.
    SnapshotMessage SnapshotAt(std::uint64_t tick, PlayerId local, PlayerId remote,
        const glm::vec3& remotePosition)
    {
        SnapshotMessage snapshot;
        snapshot.Tick = tick;

        PlayerSnapshot mine;
        mine.Player = local;
        mine.Position = Spawn;
        mine.Grounded = true;

        PlayerSnapshot theirs;
        theirs.Player = remote;
        theirs.Position = remotePosition;
        theirs.Yaw = static_cast<float>(tick);
        theirs.Grounded = true;

        snapshot.Players = { mine, theirs };
        return snapshot;
    }
}

TEST_CASE("A remote player is drawn between the two samples that bracket the interpolation point")
{
    LoopbackNetwork network;
    PeerId peer = InvalidPeer;
    Transport& raw = network.AddClient(peer);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, network.Server());
    MatchClient client(raw, GoodLoader());

    for (int i = 0; i < 5; ++i)
    {
        client.SetInput(CharacterInput{});
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }
    REQUIRE(client.Connected());

    const PlayerId local = client.LocalPlayer();
    const PlayerId remote = PlayerId{ static_cast<std::uint16_t>(local + 1) };

    //Twenty snapshots, the remote walking one block per tick along x, so the
    //expected interpolated x IS the interpolated tick. Any arithmetic error
    //shows up as a number rather than as a wobble somebody has to see.
    for (std::uint64_t tick = 100; tick <= 120; ++tick)
    {
        network.Server().Send(peer,
            Encode(SnapshotAt(tick, local, remote, glm::vec3(static_cast<float>(tick), 2.0f, 8.0f))),
            Channel::Unreliable);

        client.SetInput(CharacterInput{});
        client.Step(FrameClock::FixedStepSeconds);
    }

    REQUIRE(client.ServerTick() == 120);

    //Six ticks behind the newest, on the nose.
    const MatchClient::RemotePose onTick = client.PoseOf(remote, 0.0f);
    CHECK(onTick.Position.x == doctest::Approx(114.0f));

    //And half a tick further on, which must be halfway between two samples
    //rather than either of them.
    const MatchClient::RemotePose halfway = client.PoseOf(remote, 0.5f);
    CHECK(halfway.Position.x == doctest::Approx(114.5f));
}

TEST_CASE("A remote player is held, never extrapolated, when nothing new arrives")
{
    //The rule that keeps a remote honest. Guessing forward is right most of the
    //time and wrong exactly when it matters - at a stop, a turn, or a jump -
    //and being wrong means taking the guess back, which looks precisely like
    //the stutter extrapolation was meant to prevent.
    LoopbackNetwork network;
    PeerId peer = InvalidPeer;
    Transport& raw = network.AddClient(peer);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, network.Server());
    MatchClient client(raw, GoodLoader());

    for (int i = 0; i < 5; ++i)
    {
        client.SetInput(CharacterInput{});
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }
    REQUIRE(client.Connected());

    const PlayerId local = client.LocalPlayer();
    const PlayerId remote = PlayerId{ static_cast<std::uint16_t>(local + 1) };

    for (std::uint64_t tick = 100; tick <= 110; ++tick)
    {
        network.Server().Send(peer,
            Encode(SnapshotAt(tick, local, remote, glm::vec3(static_cast<float>(tick), 2.0f, 8.0f))),
            Channel::Unreliable);

        client.SetInput(CharacterInput{});
        client.Step(FrameClock::FixedStepSeconds);
    }

    //Nothing more arrives for a second.
    for (int i = 0; i < 60; ++i)
    {
        client.SetInput(CharacterInput{});
        client.Step(FrameClock::FixedStepSeconds);

        //Never past the newest thing anybody actually said.
        CHECK(client.PoseOf(remote, 0.0f).Position.x <= doctest::Approx(110.0f));
    }

    //And it settles on the newest sample rather than drifting back to the
    //oldest or to the origin.
    CHECK(client.PoseOf(remote, 0.0f).Position.x == doctest::Approx(110.0f));
}
```

- [ ] **Step 2: Run to verify they fail**

Expected: compile error — no `PoseOf`, no `RemotePose`.

- [ ] **Step 3: Declare the ring and the query**

In `Cubit/include/Cubit/Net/MatchClient.h`, above the class:

```cpp
//How far behind the newest snapshot a remote player is drawn, in ticks. Six is
//100 ms at 60 Hz - six snapshots of cushion, generous on purpose. Being late
//costs a remote being drawn where they were; being early costs a guess that has
//to be taken back, which is worse.
constexpr int InterpolationDelayTicks = 6;

//Samples kept per remote player. Enough for the interpolation delay plus a
//burst of jitter; older ones can never be drawn, so keeping them is only
//memory.
constexpr std::size_t MaxRemoteSamples = 32;
```

public:

```cpp
    //Where a remote player should be DRAWN this frame: interpolated between the
    //two snapshots bracketing a point InterpolationDelayTicks behind the newest
    //server tick this client has seen.
    //
    //A render-time query, not simulation state: it is deliberately never
    //written back into the MatchState character, so simulation and rendering
    //stay separate and nothing else can start depending on an interpolated
    //position. Nothing needs one - players do not collide with each other.
    //
    //Not for the local player. That one is predicted, and drawing it six ticks
    //in the past is exactly the lag this stage removes; asking for it returns
    //an empty pose, because no samples are kept for it.
    struct RemotePose
    {
        glm::vec3 Position{ 0.0f };
        float Yaw = 0.0f;
        float Pitch = 0.0f;
    };

    //`alpha` is the renderer's position within the current step - the same
    //number it hands InterpolatedPosition.
    RemotePose PoseOf(PlayerId player, float alpha) const;
```

private:

```cpp
    //One remote player's pose as of one server tick.
    struct RemoteSample
    {
        std::uint64_t ServerTick = 0;
        glm::vec3 Position{ 0.0f };
        float Yaw = 0.0f;
        float Pitch = 0.0f;
    };

    std::map<PlayerId, std::deque<RemoteSample>> m_RemoteSamples;

    //An estimate of the server's clock in ticks, for rendering only. Snapped to
    //a snapshot's tick when one arrives, and advanced by one per step in
    //between so a frame that falls between snapshots still has somewhere to
    //interpolate to. Not a clock-synchronisation subsystem and not used by the
    //simulation: nothing that affects state reads it.
    double m_RemoteClock = 0.0;
```

- [ ] **Step 4: Sample and interpolate**

In `HandleSnapshot`, for a player that is not the local one, after the existing `SetState` call:

```cpp
        std::deque<RemoteSample>& samples = m_RemoteSamples[entry.Player];
        samples.push_back(RemoteSample{ snapshot.Tick, entry.Position, entry.Yaw, entry.Pitch });

        if (samples.size() > MaxRemoteSamples)
            samples.pop_front();
```

Also `m_RemoteClock = static_cast<double>(snapshot.Tick);` beside `m_ServerTick = snapshot.Tick;`, and `m_RemoteSamples.erase(player);` beside the existing `m_ViewAngles.erase(player)` for departed players. In `Step`, where the client advances its own tick, add `m_RemoteClock += 1.0;`.

```cpp
MatchClient::RemotePose MatchClient::PoseOf(PlayerId player, float alpha) const
{
    const auto found = m_RemoteSamples.find(player);
    if (found == m_RemoteSamples.end() || found->second.empty())
        return RemotePose{};

    const std::deque<RemoteSample>& samples = found->second;

    //Deliberately in the past. Drawing at the newest sample would mean every
    //packet that arrives late is a remote standing still and then jumping.
    const double target = m_RemoteClock + alpha - InterpolationDelayTicks;

    //Newer than anything anybody has said: HOLD, do not guess. Extrapolation
    //is right most of the time and wrong exactly at a stop, a turn or a jump,
    //and being wrong means taking it back.
    const RemoteSample& newest = samples.back();
    if (target >= static_cast<double>(newest.ServerTick))
        return RemotePose{ newest.Position, newest.Yaw, newest.Pitch };

    //Older than anything kept: the connection has been quiet for longer than
    //the ring is deep. Hold the oldest for the same reason.
    const RemoteSample& oldest = samples.front();
    if (target <= static_cast<double>(oldest.ServerTick))
        return RemotePose{ oldest.Position, oldest.Yaw, oldest.Pitch };

    for (std::size_t i = 1; i < samples.size(); ++i)
    {
        const RemoteSample& previous = samples[i - 1];
        const RemoteSample& next = samples[i];

        if (target > static_cast<double>(next.ServerTick))
            continue;

        const double span = static_cast<double>(next.ServerTick - previous.ServerTick);
        const float t = span <= 0.0
            ? 0.0f
            : static_cast<float>((target - static_cast<double>(previous.ServerTick)) / span);

        RemotePose pose;
        pose.Position = glm::mix(previous.Position, next.Position, t);
        pose.Yaw = glm::mix(previous.Yaw, next.Yaw, t);
        pose.Pitch = glm::mix(previous.Pitch, next.Pitch, t);
        return pose;
    }

    return RemotePose{ newest.Position, newest.Yaw, newest.Pitch };
}
```

Yaw is interpolated linearly and will take the long way round across the ±180° seam. Leave it: nothing draws a remote's facing yet — `DrawRemotePlayers` draws an axis-aligned box — and a wrap-aware lerp with no way to see it working is a guess. Say so in a comment beside the `glm::mix` on `Yaw`, so whoever first draws a facing model knows to fix it rather than discovering it.

- [ ] **Step 5: Draw remotes from it**

In `Sandbox/src/Sandbox.cpp`, `DrawRemotePlayers`:

```cpp
        for (const auto& [player, character] : Match_().Players())
        {
            if (player == m_LocalPlayer)
                continue;

            //From the interpolation ring rather than from the character, which
            //holds whatever the last snapshot said and steps between packets.
            //The character is still what supplies the box: how big a player is
            //is simulation, where they are drawn is not.
            const MatchClient::RemotePose pose = m_Client->PoseOf(player, alpha);
            const glm::vec3 half = character.Config().HalfExtents;
            DebugDraw::Box(pose.Position - half, pose.Position + half, RemotePlayerColor);
        }
```

- [ ] **Step 6: Run the tests, then the suite**

Expected: PASS.

- [ ] **Step 7: Try to falsify the hold**

Replace the `target >= newest.ServerTick` early return with an extrapolation from the last two samples. Expected: `"A remote player is held, never extrapolated"` goes red within a few ticks of the snapshots stopping. Revert.

- [ ] **Step 8: Commit**

```bash
git commit -m "Draw a remote player where they were, not where they might be"
```

---

### Task 10: The acceptance number

"It feels smooth" is not a result. This task turns the stage's claim into a figure that can be re-run: corrections per 1000 ticks, with their mean and maximum magnitude, measured on a clean link and on a bad one.

Two of these are gates and one is a record. The gates: **on a clean link, corrections must be zero** — with no loss and no jitter the server never starves, so any correction at all means prediction and the authoritative step genuinely disagree, which is a defect and not a network condition. And **under loss the maximum must be bounded and must not grow across the run** — a rising maximum means error is accumulating rather than being corrected, which is the failure reconciliation exists to prevent.

**A note on the latency used.** The spec quotes 150 ms RTT. 75 ms one-way is 4.5 ticks, and this suite's rule is that every test latency is a whole tick multiple, so the tests below use **5 ticks one-way, 166.7 ms RTT** — one tick more than the spec's round number, and the reason the rule wins is that a half-tick latency makes every arrival ambiguous by a tick. The Sandbox run in Task 11 uses `--latency 150` as the spec says; it asserts nothing about which tick anything landed on.

**Files:**
- Modify: `Tests/src/PredictionTests.cpp`
- Modify: `docs/superpowers/specs/2026-09-03-networking-stage-3-design.md` (write the measured figures in)

**Interfaces:**
- Consumes: `MatchClient::Corrections()` (Task 8).
- Produces: no code. The measured figures, recorded in the spec.

- [ ] **Step 1: Write the gates**

Append to `Tests/src/PredictionTests.cpp`:

```cpp
TEST_CASE("On a clean link, prediction is never corrected")
{
    //THE GATE THAT CATCHES A REAL DEFECT. With no loss and no jitter the server
    //never steps a tick with an empty queue, so its state stays a prefix of
    //what this client predicted and replay reproduces it exactly. A single
    //correction here means prediction and the authoritative step disagree about
    //the simulation itself - which is not a network condition and must not be
    //absorbed by widening the threshold.
    //
    //If this goes red, the three suspects, in order: the server stepped without
    //an input (look at the queue depth), replay ran at a different step length
    //from prediction, or the ack is off by one and replay is reapplying an
    //input the server already consumed.
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = OneWayLatency;

    SimulatedTransport serverNet(network.Server(), sim);
    PeerId peer = InvalidPeer;
    SimulatedTransport clientNet(network.AddClient(peer), sim);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, serverNet);
    MatchClient client(clientNet, GoodLoader());

    //Warm-up. Joining is a transient: for the first few ticks this client has
    //no player yet, then it has one whose inputs the server has not
    //acknowledged, and the corrections that fall out of that are about the
    //handshake rather than about prediction.
    for (int tick = 0; tick < 120; ++tick)
    {
        client.SetInput(InputForTick(tick));
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }
    REQUIRE(client.Connected());

    const std::uint64_t settled = client.Corrections().Count;
    const std::uint64_t settledSnapshots = client.Corrections().Snapshots;

    for (int tick = 120; tick < 1120; ++tick)
    {
        client.SetInput(InputForTick(tick));
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }

    //A thousand ticks of varied input, including jumps, and not one
    //disagreement worth showing.
    CHECK(client.Corrections().Count == settled);

    //And the denominator is real: a client that stopped receiving snapshots
    //entirely would also report no corrections.
    CHECK(client.Corrections().Snapshots > settledSnapshots + 900);

    MESSAGE("clean link: corrections during warm-up = " << settled);
}

TEST_CASE("Under loss and jitter, corrections are bounded and do not grow")
{
    //THE RECORDED NUMBER. Nothing here is compared against a target invented in
    //advance - nobody knows the right value yet, and a threshold guessed here
    //would be a number to argue with rather than evidence. What is asserted is
    //the shape: bounded, and not growing.
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = 5 * FrameClock::FixedStepSeconds;   //166.7 ms RTT, a whole tick multiple.
    sim.Jitter = FrameClock::FixedStepSeconds;
    sim.Loss = 0.05f;
    sim.Seed = 1;

    SimulatedTransport serverNet(network.Server(), sim);
    PeerId peer = InvalidPeer;
    SimulatedTransport clientNet(network.AddClient(peer), sim);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, serverNet);
    MatchClient client(clientNet, GoodLoader());

    for (int tick = 0; tick < 120; ++tick)
    {
        client.SetInput(InputForTick(tick));
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }
    REQUIRE(client.Connected());

    const MatchClient::CorrectionStats start = client.Corrections();

    for (int tick = 120; tick < 1120; ++tick)
    {
        client.SetInput(InputForTick(tick));
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }

    const MatchClient::CorrectionStats half = client.Corrections();

    for (int tick = 1120; tick < 2120; ++tick)
    {
        client.SetInput(InputForTick(tick));
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }

    const MatchClient::CorrectionStats end = client.Corrections();

    MESSAGE("166.7 ms RTT, 5% loss, jitter: corrections per 1000 ticks = "
        << (end.Count - start.Count) / 2 << ", mean = " << end.Mean
        << ", max = " << end.Max);

    //NOT GROWING: the second thousand ticks must not set a new record by more
    //than one threshold's worth. A maximum that climbs run-on means error is
    //accumulating between corrections instead of being cleared by them.
    CHECK(end.Max <= half.Max + CorrectionThreshold);

    //BOUNDED: pin this at a round number above what the run actually reports,
    //once it has been observed. See the step below - do not leave the number
    //below as it stands without checking it.
    CHECK(end.Max < 1.0f);
}
```

- [ ] **Step 2: Run them and read the numbers**

```bash
./bin/Debug-windows-x86_64/Tests/Tests.exe -tc="On a clean link, prediction is never corrected" -s
./bin/Debug-windows-x86_64/Tests/Tests.exe -tc="Under loss and jitter, corrections are bounded and do not grow" -s
```

`-s` makes doctest print the `MESSAGE` lines. Write both figures into the task report.

If the clean-link gate is not zero **after** warm-up, stop and debug it — the three suspects are named in the test's comment. Do not raise `CorrectionThreshold` to make it pass; the threshold is calibrated against one starved walking tick and changing it to hide a disagreement destroys the only number this stage produces.

If warm-up itself produces corrections, that is expected and is worth understanding rather than assuming: report how many and where they come from.

- [ ] **Step 3: Pin the bound at an observed number**

Replace `1.0f` in the last `CHECK` with the next round number above the observed maximum (0.5, 0.75, 1.0 — whichever it is), and add a one-line comment giving the observed value and the date. A bound pinned above a measurement is evidence; one guessed in advance is decoration.

- [ ] **Step 4: Write the figures into the spec**

In `docs/superpowers/specs/2026-09-03-networking-stage-3-design.md`, under "The acceptance number", add the measured values the way Stage 2's bandwidth figure was recorded: the clean-link count, and the corrections per 1000 ticks with mean and maximum at 166.7 ms and 5% loss. Say which are gates and which is the baseline a later change is compared against, and note that 166.7 ms was used rather than 150 because a test latency must be a whole tick multiple.

- [ ] **Step 5: Run the full suite and commit**

```bash
"$MSB" Cubit.slnx -p:Configuration=Debug -p:Platform=x64 -m -v:minimal -nologo
git commit -m "Measure how often the server disagrees"
```

---

### Task 11: The Sandbox, and the run that proves it

Everything so far is deterministic and headless. This is the part that shows a person pressing `W` and the view moving on the same frame at `--latency 150`.

**Files:**
- Modify: `Sandbox/src/Sandbox.cpp`
- Temporarily modify, then revert: `Sandbox/src/Sandbox.cpp` (`ReadWalkInput`)

**Interfaces:**
- Consumes: `MatchClient::Corrections()` (Task 8), `MatchClient::PoseOf` (Task 9, already wired).
- Produces: nothing new. Evidence.

- [ ] **Step 1: Log the correction figures when the Sandbox shuts down**

In `SandboxLayer`, add (or extend) `OnDetach`:

```cpp
    //The stage's acceptance number, from a real run rather than a test.
    //
    //Logged rather than drawn on the HUD, and that is not laziness: the debug
    //font carries only "0123456789-.: ACDEFGNOPSTU", so most of the words this
    //needs cannot be rendered at all - and an unsupported character draws as a
    //BLANK rather than failing, so a wrong label reads as a rendering bug. See
    //the note on the same trap in the HUD's own header.
    void OnDetach() override
    {
        if (!m_Client)
            return;

        const MatchClient::CorrectionStats stats = m_Client->Corrections();
        const double perThousand = stats.Snapshots == 0
            ? 0.0
            : 1000.0 * static_cast<double>(stats.Count) / static_cast<double>(stats.Snapshots);

        CB_INFO("NETSTATS snapshots=" + std::to_string(stats.Snapshots)
            + " corrections=" + std::to_string(stats.Count)
            + " per1000=" + std::to_string(perThousand)
            + " mean=" + std::to_string(stats.Mean)
            + " max=" + std::to_string(stats.Max));
    }
```

- [ ] **Step 2: Build, and check single-player has not moved**

```bash
"$MSB" Cubit.slnx -p:Configuration=Debug -p:Platform=x64 -m -v:minimal -nologo
```

Run `Sandbox.exe` with no arguments and confirm `POS 240.500000 26.900099 300.500000` and `FACES 1927774`. `FACES` only reaches that number once amortized meshing settles (~1110 frames): sample it when `PendingCount() == 0`, never at a fixed frame count, or a partial number reads exactly like a regression.

Screen capture of this window is unreliable — three consecutive blank frames with a healthy process is a DWM/OpenGL `CopyFromScreen` failure, not a code fault. The reliable substitute is a temporary `CB_INFO` probe logging the two values, then `git checkout --` the file.

- [ ] **Step 3: Add the temporary walk override**

Keyboard input cannot be scripted into this window at all, so a run that has to walk needs a way in. Same probe Stage 2's verification used. In `ReadWalkInput`:

```cpp
        //TEMPORARY - REVERT BEFORE COMMITTING. Keyboard input cannot be
        //scripted into a GLFW window, and this run has to walk.
        if (std::getenv("CUBIT_WALK") != nullptr)
            return glm::vec2(0.0f, 1.0f);
```

- [ ] **Step 4: Run a real match at `--latency 150`**

Three processes: the server, and two clients so remote interpolation is exercised too.

```powershell
$bin = "bin/Debug-windows-x86_64"
Start-Process "$bin/Server/Server.exe" -RedirectStandardOutput server.log
$env:CUBIT_WALK = "1"
Start-Process "$bin/Sandbox/Sandbox.exe" -ArgumentList "--connect","127.0.0.1","--latency","150" -RedirectStandardOutput client-a.log
Start-Process "$bin/Sandbox/Sandbox.exe" -ArgumentList "--connect","127.0.0.1","--latency","150" -RedirectStandardOutput client-b.log
```

Let it run at least 30 seconds, then close each window with `WM_CLOSE` — killing the process loses the logs, and the `NETSTATS` line is written on shutdown:

```powershell
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win {
  [DllImport("user32.dll", CharSet=CharSet.Auto)]
  public static extern IntPtr FindWindowEx(IntPtr parent, IntPtr child, string cls, string title);
  [DllImport("user32.dll")]
  public static extern IntPtr SendMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
}
"@
$h = [IntPtr]::Zero
while (($h = [Win]::FindWindowEx([IntPtr]::Zero, $h, "GLFW30", $null)) -ne [IntPtr]::Zero) {
    [Win]::SendMessage($h, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
}
```

The window class is `GLFW30`, not whatever `MainWindowHandle` reports — that has cost this project time before.

What to confirm, and to write into the task report:

1. **The `NETSTATS` line from each client.** This is the stage's headline number from a real run.
2. **Both clients see each other** — each log should show a second player in the match.
3. **Prediction is doing its job.** This one cannot be read from a log, and it is the deliverable: with `CUBIT_WALK` unset, hold `W` and confirm the view moves on the same frame at `--latency 150`. Stage 2's own note is the check to apply — *"if Stage 2 feels good to play, it was built wrong"* — so if this still feels like Stage 2, prediction is not running.

- [ ] **Step 5: Revert the probe**

```bash
git diff -- Sandbox/src/Sandbox.cpp   # confirm ONLY the CUBIT_WALK block is left to remove
git checkout -- Sandbox/src/Sandbox.cpp
```

Careful: `git checkout --` on that file reverts Step 1's `OnDetach` too if it has not been committed yet. Commit Step 1 first, then add the probe, then revert. If the order slipped, re-apply `OnDetach` rather than pretending it was there.

- [ ] **Step 6: Commit**

```bash
git commit -m "Report how often the server disagreed"
```

---

### Task 12: Record the stage as shipped

**Files:**
- Modify: `docs/superpowers/specs/2026-09-03-networking-stage-3-design.md`
- Modify: `docs/engine-roadmap.md`

**Interfaces:**
- Consumes: the figures from Tasks 10 and 11.
- Produces: nothing.

- [ ] **Step 1: Update the Stage 3 spec**

Change the status line from `designed, not yet built` to shipped with the date and the commit range. Then, in the body:

- The measured acceptance figures, if Task 10 did not already write them in.
- **What turned out differently from the design.** Stage 2's spec did this and it is the most useful part of both documents. Candidates, whichever actually happened: the golden schedule's new pinned values, the skew that was two numbers and is now one, the clean-link correction count during warm-up, whether the queue cap of 8 was ever reached, whether the `beforePrevious` argument turned out to be pinned by anything.
- **Whether the deferred escape hatch is still deferred.** The spec names approach B — per-player input-driven stepping — as the answer if the correction rate came back too high. Say what the rate was and therefore whether B is still parked.

- [ ] **Step 2: Update the roadmap**

`docs/engine-roadmap.md` records Stage 1 as shipped around line 307. Add Stage 3 in the same shape, alongside Stage 2's entry. Keep it to what changed for a reader of the roadmap: the client predicts, remotes are interpolated, and the arc's remaining unsolved risk is predicted terrain edits and the rollback problem underneath them.

- [ ] **Step 3: Note what is still open**

Four things stay explicitly unfinished, and they belong in the spec's own "Known limits" rather than in anybody's head:

- **Predicted terrain edits and edit rollback**, deferred with the risk intact: rolling back a rejected edit can invalidate predicted *movement*, because the world the character collided against changed underneath it.
- **The edit log still grows without bound.**
- **Approach B** as the named escape hatch if the correction rate ever becomes a problem.
- **Yaw interpolation takes the long way round across the ±180° seam**, harmless until something draws a remote's facing.

- [ ] **Step 4: Commit**

```bash
git commit -m "Record networking stage 3 as shipped"
```

---

## Self-Review

Run against the spec after the plan was written.

**Spec coverage.** Every section maps to a task: the convergence trap and approach A → Tasks 4, 6, 7; protocol version 2 → Tasks 4 and 5; the server → Task 6; the client, `StepPlayer`, the free-running tick, the four steps on a snapshot → Tasks 3, 7, 8; remote players → Task 9; both prerequisites → Tasks 2 and 3; the testing strategy's seven properties → Tasks 3, 7, 8, 9, 10 and the single-player check in Task 11; the acceptance number → Tasks 10 and 11; the unreviewed Stage 2 commits → Task 1.

**Three things the spec left for the plan to decide**, called out because they are choices a reviewer should be able to disagree with:

1. **Where the previous position comes from in a correction.** The spec says to restore all four values from the authoritative entry, but the snapshot carries no previous position — it is a render-smoothing value, not simulation state. Task 8 uses the predicted one and explains why it is overwritten by the first replayed step anyway.
2. **`m_RemoteClock`.** Interpolating six ticks behind the newest server tick needs a local estimate of where the server's clock is *now*, between snapshots. Task 9 advances it one per step and snaps it to each snapshot's tick. This is not approach C's adaptive offset — nothing that affects simulation state reads it, and it exists only so a frame falling between two snapshots has somewhere to interpolate to.
3. **166.7 ms rather than 150 ms in the tests.** The suite's whole-tick-multiple rule wins; the Sandbox run still uses `--latency 150`.

**One test this plan makes false, deliberately, and one it narrows.** `"The client never steps the simulation itself"` is replaced by its inverse in Task 7 — deleting it silently would lose the record that Stage 2's constraint was a choice. `"A client's state is the server's state, delayed by exactly the one-way latency"` narrows to remote players, because the local player being ahead of the server is the deliverable. The bad-network drain in Task 8 loosens from exact equality to within the threshold, which is the deadzone's known and accepted cost.

**Where this plan is most likely to be wrong.** The clean-link zero-corrections gate in Task 10 is the assertion with the most riding on it and the least evidence behind it: it follows from the design but has not been observed. If it comes back non-zero, the design is what is wrong, not the gate — and the escape hatch the spec already names is approach B.
