# Cubit Networking — Design

_Written 2026-08-27. Status: **Stage 1 shipped 2026-08-30.** Stages 2 and 3 shaped only._

The arc that turns Cubit from a single-player voxel sandbox into a server-authoritative
multiplayer game. This document is the one to re-read when picking the work back up:
it holds the decisions already made and why, what each stage delivers, and what each
later stage still has to decide.

**Read the [glossary](#glossary) first if the terms are unfamiliar.** The author is new
to netcode; this document is written to be resumed cold.

---

## Why this before gameplay

Everything else on the engine roadmap can wait for a game to ask for it. Networking
cannot, for one specific reason: **prediction and reconciliation are the only thing
that is genuinely expensive to retrofit.** Gameplay written against local state
assumes it can read and mutate the world directly and get an answer immediately.
Converting that later means rewriting every system that touched state.

Nothing else here has that property. An entity system, a `Mesh` type, an asset layer,
a render target — each of those is additive, and each is better designed once
something concretely needs it.

Two preconditions are already met, and both by accident of doing other work well:

- **`Cubit/src/Voxel/` includes no GL headers.** Verified 2026-08-27: no `glad`,
  `GLFW`, or `gl*` anywhere in it. The simulation core already runs without a
  context, which is the hardest part of a headless server to retrofit.
- **`CharacterController::Step` is a pure function of (state, input, world).** Built
  2026-08-27 when the character controller came out of `SandboxLayer`. Replaying
  inputs from an authoritative state — which *is* reconciliation — is only possible
  because the step does not poll a keyboard.

`BlockEdit` returning its own inverse (built for the undo stack) turns out to be
exactly what client-side edit rollback needs. That was not planned.

## Two "blockers" that were not

Recorded because both were asserted in `engine-roadmap.md` and repeated in planning
before anyone checked, and both dissolved on inspection. The lesson is the same one
`performance.md` keeps learning: **verify the claim before building a plan on it.**

- **"`Application` hard-creates a window, so a headless server needs it split."**
  The server does not need to be an `Application`. `MapGen` is already a `ConsoleApp`
  that links Cubit, runs headless, and never touches `Application`, `Window` or
  `Renderer`. The server follows that precedent. Zero engine surgery.
- **"`FrameClock` has no monotonic tick, so reconciliation needs one added."**
  The tick belongs to the *simulation*, not the clock. `MatchState` owns and
  increments it, which is more correct: the server's authoritative tick is the
  simulation's tick. `FrameClock` is unchanged.

---

## Decisions already made

| Decision | Choice | Reversibility |
|---|---|---|
| Slice scope | Two clients, seeing each other, plus replicated terrain edits | Expensive — shapes the snapshot format |
| Transport | Vendor **ENet** as a StaticLib in the existing Dependencies group | **Cheap** — hidden behind an interface |
| Simulation seam | Shared `MatchState`; server is a bare `main()` like `MapGen` | Expensive — everything depends on it |
| Input shape | Raw local axes + yaw/pitch; server resolves direction | Expensive — it is the wire format |

**Why two clients rather than one.** One client proves reconciliation, which is the
hard part. But nothing would force the snapshot to carry more than one entity, and a
snapshot format designed around exactly one character is the same "abstraction over a
single instance" mistake this project has now declined twice (greedy meshing's
premise, and the entity system). Two forces the snapshot to be a collection.

**Why ENet.** It provides precisely the two channel types needed — unreliable for
snapshots, reliable for edits — plus handshake, timeout, MTU discovery, sequencing,
fragmentation and acks. Hand-rolling those is a project *before* the game part starts,
and unlike this project's other hand-rolled pieces (profiler, mesher, sky light, `.vox`
codec) its bugs are non-deterministic and awkward to unit test. The effort belongs in
prediction and reconciliation, which are the parts specific to this game.

**Why raw axes plus yaw rather than a client-computed world vector.** The server must
be able to reproduce the movement step, not merely trust its result, and it needs to
know where each player is looking — for rendering remote players now, and for hitscan
lag compensation later. Sending a world-space direction makes movement direction
client-authoritative and leaves yaw as decorative state the simulation never consumes.

---

## Architecture

```
Cubit/  (SharedLib, GL-free simulation core)
  Voxel/World, Chunk, SkyLight, VoxelCollision, BlockEdit
  Voxel/CharacterController      Step(world, input, seconds)   pure
  Voxel/MatchState               Step(commands, seconds)       pure, owns tick
  Net/Transport                  interface                     (Stage 2)
  Net/EnetTransport              ENet behind the interface     (Stage 2)
  Net/LoopbackTransport          deterministic latency/loss    (Stage 2)

Server/ (ConsoleApp, no window, no Renderer, no Application)
  main() { FrameClock + Transport + MatchState::Step }         authority

Sandbox/ (ConsoleApp + Application + Window + Renderer)
  SandboxLayer -> client: gather input, MatchState::Step       prediction
```

**One simulation, two callers.** This is the load-bearing property of the whole
design. If server and client ever step differently, reconciliation fights the player
forever and the symptom is rubber-banding that looks like a network problem and is
not.

---

## Stage 1 — shared simulation. No networking at all.

**Delivers:** the Sandbox, still single-player, still behaving identically, with the
simulation moved behind the seam every later stage needs. Zero network risk, and it
removes most of the *structural* risk.

### `MatchState`

New, in `Cubit/include/Cubit/Voxel/` beside `World` and `CharacterController` — it is
part of the GL-free simulation core, and a `Net/` folder for something containing no
network code would be a lie. It may move in Stage 2.

```cpp
using PlayerId = std::uint16_t;
constexpr PlayerId InvalidPlayer = 0;   // ids start at 1

struct PlayerCommand
{
    PlayerId Player = InvalidPlayer;
    CharacterInput Input;
};

class CB_API MatchState
{
public:
    explicit MatchState(World world);

    PlayerId AddPlayer(const glm::vec3& spawn);
    void RemovePlayer(PlayerId player);
    bool HasPlayer(PlayerId player) const;

    // Advances every present player by one fixed step, then increments the tick.
    // A command naming an absent player is ignored rather than throwing: once
    // commands arrive off a socket, a stale id is malformed input, not a caller
    // bug. Same reasoning as ApplyBlockEdit rejecting an out-of-range position.
    void Step(std::span<const PlayerCommand> commands, float seconds);

    std::uint64_t Tick() const;
    const CharacterController& Player(PlayerId player) const;
    void TeleportPlayer(PlayerId player, const glm::vec3& position);

    // F9 reload: new terrain, same players, same tick.
    void ReplaceWorld(World world);

    World& GetWorld();
    const World& GetWorld() const;
};
```

A `World`, a map of `PlayerId → CharacterController`, a tick, and a `Step` that fans
commands out. **Deliberately not** an entity system, an event bus, or a component
store — there is one kind of actor, and an abstraction over one kind is a guess.

### `CharacterInput` changes shape

```cpp
struct CharacterInput
{
    glm::vec2 Move{ 0.0f };   // LOCAL: x = strafe, y = forward. Was world-space.
    float Yaw = 0.0f;         // new
    float Pitch = 0.0f;       // new — carried, not yet consumed by movement
    bool Jump = false;
};
```

`CharacterController::Step` resolves yaw into a direction itself, so movement stays
owned by one type and the server reproduces it rather than trusting it.

`Pitch` is carried but unused by movement, on purpose: remote-player rendering wants
it in Stage 3 and hitscan wants it later, and adding a field to the wire format after
the fact is worse than carrying an unused one now.

### `SandboxLayer` becomes a client

Stops owning `m_World` and stops simulating. Per fixed step: read WASD into local
axes, take yaw and pitch from the camera controller, build one `PlayerCommand`, call
`m_Match.Step(...)`, then read results back for the HUD and camera. `LoadWorld`/F9
calls `ReplaceWorld`. Externally nothing changes — same spawn, same feel.

### Tests

- **Determinism.** Two `MatchState`s built identically and fed identical command
  sequences end **bit-exact** identical. This is the property every later stage rests
  on; cheap to assert now, expensive to discover missing in Stage 3.
- **Yaw convention.** The new GL-free yaw→direction must agree with
  `PerspectiveCamera::GetForwardDirection()` across a range of yaws. The camera work
  of 2026-08-16 already lost time to `-90°` meaning `-z`; two independent copies of
  that convention is precisely how it recurs. Verified feasible: `PerspectiveCamera`
  contains no GL calls and is already exercised by `PerspectiveCameraTests.cpp`, so
  the oracle needs no new machinery.
- Tick starts at 0 and increments once per `Step`, regardless of command count.
- Players are independent: stepping one walking and one idle moves only the first.
- A command naming an absent player is ignored and does not throw.
- `ReplaceWorld` keeps players and tick.
- The 16 existing `CharacterController` tests are migrated to the new input shape.
  This is real work, not a rename — each needs a yaw chosen deliberately.

### Known cost

Yesterday's `CharacterInput` was half-right. Migrating those tests is the price of
finding that out before the shape reached the wire rather than after.

### Shipped 2026-08-30

`MatchState` owns the world, an ordered player registry and the tick;
`CharacterInput` carries local axes plus yaw and pitch; `SandboxLayer` is a
caller rather than a simulator. Behaviour is unchanged — same spawn, same face
count.

Two things this stage added that the design did not call for. Movement is now
**capped after resolution rather than normalised before it**, which fixes
diagonal walking being `sqrt(2)` times faster and leaves room for analog input.
And `Heading.h` exists as a separate GL-free unit pinned against
`PerspectiveCamera`, rather than the resolution living inline in the
controller, because the yaw convention is the thing most likely to be
duplicated wrongly.

**Note on commit attribution.** The commit message for 41efdaa states that
`ReadWalkInput` stops doing camera maths and reports held keys in the character's
own frame, but this change actually landed in commit 73e9562. The Task 5 diff for
41efdaa does not touch `ReadWalkInput`. The history is not being rewritten; this
note records the misattribution plainly so that a corrected record exists.

---

## Stage 2 — the wire. Server-authoritative, deliberately no prediction.

**Delivers:** an ENet server and two connected clients, moving. Movement goes to the
server and comes back, **so the latency is visible.** That is the point: it proves the
wire honestly before prediction hides it, and it means any rubber-banding seen in
Stage 3 is new rather than inherited.

**Shape:** `Transport` interface; `EnetTransport`; a `Server` ConsoleApp with a
`FrameClock` loop and no window; connect/disconnect; clients send `CharacterInput`
each tick; server steps `MatchState` and broadcasts full snapshots.

**The decision that matters most in this stage: `Transport` is an interface, not
inline ENet calls — for testing, not portability.** With the seam, an in-process
implementation can inject deterministic latency, jitter and packet loss, so a doctest
runs a server and two clients under 150 ms RTT and 5% loss with no socket and no
flakiness. Without it, netcode is verified by two people on a call saying "feels
fine." This codebase's entire quality story is a reference oracle proving something
cell for cell; a deterministic bad network is netcode's version of that oracle.

**What Stage 2 must add to `MatchState` first.** The final review of Stage 1 found four
gaps in the `MatchState` API. None of these are defects in Stage 1 - they are API that
Stage 1 correctly did not build, because nothing needed it yet. A single process with
one `MatchState` never had to mint a specific id, enumerate a roster, replay a step, or
let anything but itself advance the tick. A server and a client talking over a wire do,
so Stage 2 needs to add:

1. **`AddPlayer` cannot mint a chosen id.** It only hands out `m_NextPlayer++`. A client
   must be able to create *the server's* player 7 locally; today it cannot without
   creating six throwaway players first. Needs an `AddPlayer(PlayerId, const glm::vec3&)`
   overload. Cheapest while there is one call site.
2. **No roster enumeration.** Only `HasPlayer` / `Player(id)` exist. A server
   broadcasting snapshots and a client rendering remote players both need to ask "who is
   in this match". The server can hoard the ids `AddPlayer` returned; a client cannot.
3. **`m_Grounded` is cross-step state with no setter.** `CharacterController::Step` reads
   the *previous* step's `m_Grounded` when deciding whether a jump fires. Stage 3 resets
   a player to an authoritative state and replays inputs, but `PlayerForWrite` exposes
   only `Teleport` and `SetVerticalVelocity` - so a replayed jump on the first tick after
   a correction can diverge. Worse, `Teleport` deliberately flattens `PreviousPosition`,
   destroying exactly the interpolation a correction is supposed to hide. The missing
   piece is a single `SetState(position, previousPosition, velocity, grounded)`.
4. **`Tick()` is read-only.** A client cannot align its match to the server's tick
   number.

Items 1-2 bite on day one of the server; items 3-4 bite on day one of reconciliation.
Both pairs belong in Stage 2's first commits, not as retrofits after the transport
exists.

**Still to decide in Stage 2's own design pass:**

- Snapshot rate (60 Hz is wasteful; 20 Hz is conventional) and whether it is a tick
  multiple.
- Wire serialisation: hand-rolled byte writer vs something structured. No dependency
  exists today, and the `.vox` codec is precedent for hand-rolling this one.
- How the client's tick relates to the server's before prediction exists.
- Connection lifecycle: what a joining client is sent (full world? the map name?) —
  the shipped map is 23.8 MB, so almost certainly a name, not the data.
- Whether `MatchState` moves to `Cubit/Net/`.

---

## Stage 3 — the feel. Prediction, reconciliation, interpolation, edits.

**Delivers:** the thing that is actually playable.

**Shape:**

- **Prediction.** The client steps its own character immediately on local input, and
  keeps a ring buffer of `(tick, CharacterInput)` for inputs the server has not yet
  acknowledged.
- **Reconciliation.** Each snapshot carries the server tick and, per client, the last
  input tick it consumed. The client resets its character to that authoritative state
  and replays every buffered input after that ack. If the replayed result matches what
  it already predicted, nothing visibly moves.
- **Remote players.** Never predicted. Buffered and rendered ~100 ms in the past,
  interpolated between snapshots. No extrapolation in this slice.
- **Terrain edits.** Client applies its own edit immediately and keeps the inverse
  `ApplyBlockEdit` already returns. Server validates against `ReachDistance` from the
  player's authoritative position, applies, and broadcasts on the reliable channel. A
  rejected edit is rolled back with the stored inverse.

**Reconciliation is unit-testable with no network at all:** hand a client a stale
authoritative state plus an input history, replay, and assert the result equals a
straight simulation. That works only because `Step` is pure.

**Still to decide in Stage 3's own design pass:**

- Error threshold before a correction is applied at all, and whether a correction
  snaps or smooths. Smoothing can push a character into geometry; snapping is visible.
- How far ahead of the server the client runs, and how that offset adapts to changing
  RTT.
- What happens to predicted *movement* when a predicted *edit* is rolled back — see
  Risks.
- Whether snapshots become delta-compressed against the last acknowledged one.

---

## Explicitly out of scope for the whole slice

Each of these needs gameplay to exist before it can be designed against anything real:

- **Lag compensation for hitscan** — needs hitscan, and hitboxes to rewind.
- **Interest management / relevancy** — a no-op with two players.
- **An entity system** — still one kind of actor. Still a guess.
- Matchmaking, encryption, NAT traversal, reconnection, anti-cheat beyond the
  server simply being authoritative.

---

## Testing strategy

The project's standard is a reference oracle, not a vibe. The equivalents here:

| Property | How it is proved |
|---|---|
| Simulation is deterministic | Two `MatchState`s, identical inputs, bit-exact equality (Stage 1) |
| Yaw convention is single | New resolver checked against `PerspectiveCamera` (Stage 1) |
| Reconciliation converges | Stale state + input history replayed == straight simulation (Stage 3) |
| It survives a bad network | `LoopbackTransport` with deterministic latency, jitter, loss (Stage 2) |
| Behaviour is unchanged | Sandbox spawns at the same `POS` and reports the same `FACES` |

Tests run as a build step, so a regression fails the build.

---

## Risks

- **A rolled-back terrain edit can invalidate predicted movement.** The world the
  character collided against changed. Accepted for the slice and recorded rather than
  solved; it needs a real answer before shipping.
- **Float determinism across machines is not guaranteed** with different compilers or
  optimisation settings. This design does not depend on it: reconciliation is
  state-based, not lockstep, so the server continuously corrects drift rather than
  assuming it cannot happen. The Stage 1 determinism test covers one binary, which is
  what it claims.
- **Scope.** This is the largest single piece of work in the project's history. The
  three-stage split exists so that each stage ships something runnable; resist merging
  them.
- **Author is new to netcode.** Stages 2 and 3 are shaped, not designed. Each gets its
  own brainstorm before implementation, on the evidence of the stage before it.

---

## Glossary

- **Authoritative server** — one machine's simulation is the truth. Clients display
  it and guess ahead of it; they never decide it.
- **Tick** — one fixed simulation step, 1/60 s here. Numbered monotonically so client
  and server can agree what moment they are talking about.
- **Snapshot** — the server's periodic "here is the state of the world at tick N."
- **Prediction** — the client simulating its own input immediately rather than waiting
  a round trip for the server to confirm it.
- **Reconciliation** — on receiving a snapshot, rewinding to the authoritative state
  and replaying inputs made since, so a correct guess costs nothing visible.
- **Interpolation** — drawing remote players slightly in the past, between two
  received snapshots, so their motion is smooth rather than stepping per packet.
- **Reliable / unreliable channel** — reliable resends until acknowledged (terrain
  edits); unreliable does not (snapshots, where the next one supersedes the last).
- **RTT** — round-trip time; how long a packet takes to get there and back.
- **Rubber-banding** — the visible symptom of reconciliation disagreeing with
  prediction: the character is repeatedly yanked back.
