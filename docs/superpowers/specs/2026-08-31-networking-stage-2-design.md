# Cubit Networking Stage 2 — The Wire

_Written 2026-08-31. Status: **SHIPPED 2026-09-03**._

Stage 2 of the arc laid out in
[`2026-08-27-networking-design.md`](2026-08-27-networking-design.md). **Read that
document first** — it holds the arc-level decisions, the reasoning for doing
networking before gameplay, and the glossary. Terms used here without definition are
defined there.

**Where Stage 1 left things.** `MatchState` owns the world, the roster and the tick.
`CharacterInput` is local axes plus yaw and pitch. `CharacterController::Step` is pure.
`SandboxLayer` is a caller of the simulation rather than the simulation itself. 315
tests. Nothing yet knows what a socket is.

---

## What this stage delivers

An ENet server and two connected clients, moving, in one world, editing terrain that
everyone sees — **with the latency deliberately visible.**

That last clause is the stage, not a caveat on it. There is no prediction here. The
point is to prove the wire honestly before prediction hides it, so that any
rubber-banding seen in Stage 3 is *new* rather than inherited from a transport nobody
ever validated.

Concretely, when this is done:

```
Server.exe                        headless, no window, no Application
Sandbox.exe --connect 127.0.0.1   window 1
Sandbox.exe --connect 127.0.0.1   window 2
Sandbox.exe                       unchanged single-player, no socket
```

**Sandbox with no flag must remain byte-for-byte the app it is today.** This is not
politeness toward the existing code — the project's entire rendering verification story
is scripted screenshots and `POS` / `FACES` probes run against Sandbox. None of that may
start depending on a socket being up.

## If Stage 2 feels good to play, it was built wrong

Pressing W will not move your view until the input has gone to the server and a
snapshot has come back. At `--latency 150` that is genuinely unpleasant.

That is the deliverable. Feeling the unplayable version is what makes Stage 3's payoff
legible, and a Stage 2 that already feels fine is one that quietly grew prediction.

---

## Decisions made in this pass

| Decision | Choice | Why it went this way |
|---|---|---|
| Launch shape | New `Server` app; `Sandbox --connect <host>` | Keeps the single-player verification path socket-free |
| Client architecture | Client never calls `Step` | Makes Stage 3's diff one sentence |
| Terrain edits | In scope, server-authoritative, no rollback | Cheap without prediction, and the alternative silently desyncs |
| Snapshot rate | Every tick, 60 Hz | Rate reduction and interpolation are one problem; splitting them teaches the wrong lesson |
| Look vs move | Look is instant, movement waits | Yaw is an input, not simulated state — nothing to reconcile |
| Wire format | Hand-rolled little-endian codec | Precedent: the `.vox` codec. No dependency exists and none is needed |
| `MatchState` location | Stays in `Voxel/` | It contains no networking; a client has one whether or not a socket exists |

### The client never steps

Three options were considered:

- **The client never steps.** It owns a `MatchState`, but `Step` is never called on it.
  Input goes up; snapshots come down and are written straight in. The client is a
  renderer of the server's state.
- **The client steps its local player and snapshots correct it.** That is prediction
  wearing a disguise. It would feel better immediately and would hide the latency this
  stage exists to expose, and Stage 3 would begin from a half-built reconciliation loop
  rather than a clean one.
- **The client buffers two snapshots and interpolates between them.** That is entity
  interpolation — real Stage 3 work, built before the thing that motivates it exists.

The first was chosen. Its decisive property is what it does to the *next* stage: Stage 3
becomes "start calling `Step` on the client, and replay unacknowledged inputs after each
snapshot." One sentence, and it is one sentence only because Stage 2 left the socket
clean. The other two smear that boundary, and debugging prediction and transport
simultaneously with no known-good wire underneath is the worst position this project
could put itself in.

### Terrain edits are in scope, and the alternative was not "do nothing"

Without prediction, edits are almost free: a client sends a request on the reliable
channel, the server applies it and broadcasts the result. No rollback, because nothing
was predicted to roll back.

The trap is that "defer edits to Stage 3" does **not** mean leaving the edit keys alone.
A local-only edit desyncs the client's world from the server's, and the character then
collides against terrain the server does not have. The symptom is rubber-banding that
looks exactly like a prediction bug and is not. So the real choice was between
replicating edits and *disabling* them while connected — and replicating them is barely
more work, exercises the reliable channel alongside the unreliable one, and makes the
round trip vividly legible: you click, and the block disappears 150 ms later.

`BlockEdit` was written for this. Its own header already says an edit is "an intent, not
a record", that "a client sending an edit cannot honestly report the previous block",
and that a bad coordinate is "malformed input rather than a bug in the caller" once
edits "arrive from a file or a socket."

### 60 Hz snapshots, overriding the parent spec's "20 Hz is conventional"

Two players is 65 bytes per snapshot, about 3.9 KB/s down; inputs are 22 bytes, about
1.3 KB/s up.
Bandwidth is not a risk this stage retires, and optimising it here would be optimising
the thing that is not hurting.

The substantive reason is that **snapshot rate and entity interpolation are one
problem.** Drop to 20 Hz without interpolation and remote players visibly step three
times slower than they move. That is a snapshot-rate artifact which looks exactly like a
latency artifact — and confounding those two while learning netcode is precisely the
outcome to avoid. 60 Hz also keeps client and server ticks corresponding 1:1, removing a
whole category of confusion from a stage that has enough.

Deferred to Stage 3, where it belongs, alongside the interpolation that makes it viable.
The measured bandwidth is recorded on completion so Stage 3 optimises against a number
rather than an intuition.

### Looking is instant; moving is not

The one deliberate exception to "no prediction."

Yaw and pitch are *inputs*, not simulated state. The server never contradicts them, so
there is nothing to reconcile and nothing to hide. Position is simulated, so it waits for
the round trip. Concretely: the camera turns the instant the mouse moves, but strafing
takes a round trip to appear.

Making camera rotation lag as well would be nauseating and would teach nothing that the
position lag does not already teach. Movement direction is still resolved server-side
from the yaw carried in the input, so this concedes no authority — the client is not
telling the server where it moved, only where it is looking.

---

## Architecture

```
Cubit/  (SharedLib, GL-free)
  Voxel/MatchState              unchanged home; gains four API members
  Net/Transport.h               PeerId, Channel, NetEvent, the interface
  Net/EnetTransport             real sockets
  Net/LoopbackTransport         in-process, no socket
  Net/SimulatedTransport        latency / jitter / loss decorator over any Transport
  Net/ByteWriter, ByteReader    little-endian codec
  Net/Protocol                  message ids, encode, decode
  Net/MatchServer               owns a MatchState, is authoritative
  Net/MatchClient               owns a MatchState, is a receptacle

Server/src/Server.cpp           ~80 lines: FrameClock + MatchServer
Sandbox/src/Sandbox.cpp         gains --connect; holds an optional MatchClient
vendor/ENet/                    StaticLib project in the Dependencies group
```

**Why all of it inside the Cubit DLL.** `Tests` links Cubit and nothing else, and the
entire justification for the `Transport` seam is that a doctest can drive a server and
two clients. Net code that `Tests` cannot reach defeats its own purpose. It stays
GL-free, like `Voxel/`.

### `Transport`

Packet-level, mirroring ENet rather than inventing a vocabulary over it:

```cpp
using PeerId = std::uint32_t;
constexpr PeerId InvalidPeer = 0;

enum class Channel : std::uint8_t { Unreliable = 0, Reliable = 1 };
enum class NetEventType { None, Connected, Disconnected, Message };

struct NetEvent
{
    NetEventType Type = NetEventType::None;
    PeerId Peer = InvalidPeer;
    std::vector<std::uint8_t> Data;
};

//No CB_API: a pure interface exports nothing, and an exported class must define
//every member it declares.
class Transport
{
public:
    virtual ~Transport() = default;
    virtual void Send(PeerId peer, std::span<const std::uint8_t> data, Channel channel) = 0;
    virtual void Broadcast(std::span<const std::uint8_t> data, Channel channel) = 0;
    virtual bool Poll(NetEvent& out) = 0;      //drains one event; false when empty
    virtual void Advance(double seconds) = 0;  //service the transport for one step
    virtual double RoundTripTime(PeerId peer) const = 0;   //seconds, for the HUD
};
```

Two channels, not more. **Unreliable for snapshots** — a stale snapshot is worthless, so
resending one wastes bandwidth to deliver something already superseded. **Reliable and
ordered for joins and edits** — an edit that arrives late is still correct; one that
vanishes is a permanent world desync.

`Advance(seconds)` rather than a wall clock is what makes the whole test strategy work:
the caller decides how much time passed, so a test runs a hundred simulated seconds
instantly and identically every run.

### `SimulatedTransport` — one mechanism, two payoffs

A decorator wrapping *any* `Transport`, adding deterministic latency, jitter and loss
from a seeded RNG:

```cpp
struct NetworkSim
{
    double Latency = 0.0;    //ONE-WAY seconds
    double Jitter  = 0.0;    //uniform +/- seconds
    float  Loss    = 0.0f;   //fraction of unreliable packets dropped
    std::uint64_t Seed = 1;
};
```

**`Latency` is one-way; the `--latency` command-line flag is round-trip.** Two units for
the same idea invites exactly one bug, so it is pinned here: the flag takes the RTT a
player would recognise, and halves it on the way in. The spec says RTT wherever it talks
about how the game feels, and one-way wherever it talks about `NetworkSim`.

- **In tests:** wrapping `LoopbackTransport` — a server and two clients at 100 ms RTT
  (50 ms one-way, exactly 3 ticks) and 5% loss, in one process, no socket, no flake,
  byte-identical every run. **Test latencies are whole tick multiples on purpose**, so
  the oracle below can assert exact equality against an offset history rather than
  equality within a tolerance.
- **In the demo:** wrapping `EnetTransport` — `Sandbox.exe --connect 127.0.0.1
  --latency 150 --loss 5`. Loopback has no real latency, so without this knob the demo
  would prove nothing about the very thing the stage exists to show. The demo is free to
  use a latency that is not a tick multiple; nothing asserts against it.

Loss applies only to the unreliable channel. A reliable packet selected for loss is
re-queued with an extra delay rather than discarded, which is what ENet's retransmission
does — modelling reliability as "never delayed" would make the tests lie about the one
property the reliable channel actually costs.

`Transport` also exposes `double RoundTripTime(PeerId) const`, so the HUD has a number to
show. `EnetTransport` reports ENet's own estimate; `SimulatedTransport` reports twice its
configured one-way latency. This is why Stage 2 needs no input acknowledgement in the
snapshot — Stage 3 adds one because reconciliation needs it, not because the HUD does.

---

## `MatchState` additions

The four gaps recorded at the end of Stage 1. They land in the opening commits, before
any socket exists — items 1 and 2 bite on the server's first day, items 3 and 4 on
reconciliation's, and retrofitting them around an existing transport is strictly worse.

```cpp
//On MatchState:
PlayerId AddPlayer(PlayerId id, const glm::vec3& spawn);         //mint the server's id 7
const std::map<PlayerId, CharacterController>& Players() const;  //enumerate the roster
void SetTick(std::uint64_t tick);                                //align to the server

//On CharacterController:
void SetState(const glm::vec3& position, const glm::vec3& previousPosition,
              float verticalVelocity, bool grounded);
```

`Players()` returns the ordered map itself rather than a copied vector of ids.
`MatchState.h` already documents that ordered iteration is load-bearing for
reproducibility; handing back the ordered container makes that promise visible to callers
instead of hiding it behind a copy.

`SetState` exists because `Teleport` is wrong for this job in two ways: it deliberately
flattens `PreviousPosition`, destroying exactly the interpolation a correction should
hide, and it cannot restore `m_Grounded`, which `Step` reads from the previous step when
deciding whether a jump fires.

`AddPlayer(PlayerId, ...)` must reject an id already present, and must advance
`m_NextPlayer` past any id it accepts, so a server that later calls the auto-minting
overload cannot collide with an id it was handed.

---

## The protocol

Six messages.

| Message | Direction | Channel | Payload |
|---|---|---|---|
| `Hello` | C to S | reliable | protocol version |
| `Welcome` | S to C | reliable | your id, map name, map hash, tick, edit log |
| `Input` | C to S | unreliable | sequence, `CharacterInput` |
| `Snapshot` | S to C | unreliable | tick, then per player: id, position, yaw, pitch, vertical velocity, grounded |
| `EditRequest` | C to S | reliable | `BlockEdit` |
| `EditApplied` | S to C | reliable | `BlockEdit` |

**There are no join or leave messages.** The snapshot carries the full roster every tick
and ids are never reused, so a client derives both by diffing against what it held last.
Two message types deleted before they were written.

**`Input` carries a sequence number, not a tick, and is unreliable.** The client sends one
per fixed step from its own `FrameClock`; the counter increases monotonically and means
nothing but "which of my inputs is this". Unreliable means unordered, so the server drops
any input whose sequence is not greater than the last it applied for that player.

Deliberately *not* a tick number. A tick would imply the client knows which simulation
step its input belongs to, and in Stage 2 it does not — it never steps. Stage 3 is where
an input acquires a real tick, because that is when replay needs to know where to
reinsert it. Shipping a field called `Tick` that is not yet a tick would be a lie the next
stage has to unpick.

A lost input is one missed step of movement — a small stutter under 5% loss. That is
honest, and it is exactly what motivates Stage 3 bundling the last several inputs
redundantly into each packet.

**Wire sizing.** Snapshot, per player: id `u16`, position 3 x `f32`, yaw `f32`, pitch
`f32`, vertical velocity `f32`, grounded `u8` — 27 bytes. Header: message id `u8`, tick
`u64`, count `u16` — 11 bytes. Two players is 65 bytes, 3.9 KB/s at 60 Hz. Input: message
id `u8`, sequence `u32`, move `2 x f32`, yaw `f32`, pitch `f32`, jump `u8` — 22 bytes,
1.3 KB/s.

All fields are fixed-width little-endian. No varints, no bit packing, no compression —
there is no bandwidth problem to solve, and a hand-rolled codec's bugs should be as
boring as possible to find.

Yaw and pitch travel in the snapshot because `CharacterController` does not store them —
they live in `CharacterInput`. `MatchClient` keeps a small `map<PlayerId, ViewAngles>`
beside its `MatchState` rather than widening `MatchState` for a rendering concern.

### The server

`Server.exe` follows the `MapGen` precedent exactly: a `ConsoleApp` that links Cubit,
runs a `main()`, and never touches `Application`, `Window` or `Renderer`. It loads the
same `battlefield.vox` a client does — path from `argv`, defaulting to the same asset
path Sandbox uses — and picks spawns with the existing `SpawnFinder`, so a joining player
lands somewhere legal rather than inside terrain.

Its loop is a `FrameClock` and nothing else: advance the transport, drain events into
this tick's inputs and edits, apply edits in player-id order, `MatchState::Step`,
broadcast a snapshot. No rendering, no window, no GL context, no interpolation — `Alpha()`
is meaningless to a machine that never draws.

`Hello` carries a protocol version, and **a version mismatch is a disconnect with a
logged reason, not a best-effort attempt to continue.** Two builds of a hand-rolled wire
format disagreeing about field widths produce garbage positions, which look like a
physics bug and cost a day.

### Joining, and why `Welcome` carries an edit log

The map is 23.8 MB. It is never sent. `Welcome` carries:

- **the map name**, which the client loads from its own assets;
- **a 64-bit FNV-1a hash of the map file's bytes**, which the client compares against its
  own copy and disconnects loudly on mismatch. A client with a different
  `battlefield.vox` must fail at the handshake, not desync silently an hour later;
- **the tick**, so the client's match aligns with the server's;
- **the edit log** — every `BlockEdit` applied since the map loaded.

The edit log is what makes late joining correct. Client B connecting after client A dug a
hole would otherwise get a pristine world. Replaying a few hundred `ApplyBlockEdit` calls
is milliseconds.

### Edit flow

A client sends `EditRequest`. The server applies it with `ApplyBlockEdit`, which returns
the inverse — or nothing, if the world did not change or the position was out of range.
On nothing, the server broadcasts nothing. On success it appends to the edit log and
broadcasts `EditApplied` to everyone, the requester included. The requester's own world
changes only when that broadcast arrives.

**Edit ordering is the server's job, and it does not come for free.** The server drains
its transport into a per-tick buffer and applies that tick's edits **in player-id order**
before stepping. Applying them in arrival order instead would make the outcome depend on
socket scheduling, which is not reproducible and would poison every test that touches
edits. This is a real requirement on `MatchServer`, not an accident of `MatchState`
iterating an ordered map — `MatchState` never sees an edit.

Two clients editing the same block on the same tick therefore converge on the same
result, and the same delivery schedule always produces the same world.

---

## Sandbox integration

`Sandbox.cpp` is 620 lines and now gains a second mode. The branch stays in one place
because the file already has the right pattern — it reaches everything through the
`World_()` and `LocalPlayer()` accessors added in Stage 1.

`MatchClient` owns its own `MatchState`; single-player owns a bare one; a single
`MatchState& Match()` picks between them. **Three branch points in total, and no more:**

- **the accessor** — `Match()` returns the client's match or the local one.
- **fixed update** — connected: send `Input`, poll the transport, apply snapshots.
  Not connected: `Step` locally, as today.
- **edit** — connected: send `EditRequest`. Not connected: `ApplyBlockEdit` locally, as
  today.

A fourth branch point appearing during implementation is the signal to extract rather
than to continue.

Remote players draw as `DebugDraw::Box` wireframes at their real half extents. Not a
character model — that is gameplay, and it would be a guess.

The HUD gains RTT, server tick against client tick, and packets per second, so the
latency is legible on screen rather than merely felt.

---

## Testing strategy

The project's standard is a reference oracle, not a vibe.

**The oracle.** Stage 1's was "`MatchState` must agree with bare `CharacterController`s
fed the same inputs." Stage 2's is: **a client's `MatchState`, driven only by snapshots,
equals the server's `MatchState` delayed by exactly the transport's downstream latency.**
At zero latency that is tick-for-tick equality; at 100 ms RTT it is equality against a
recorded server history offset by three ticks — the *one-way* 50 ms, because a snapshot
only travels one way. That is a falsifiable statement about the whole wire.

The distinction matters and is easy to get backwards: **a snapshot is 3 ticks old when it
arrives, but your own keypress takes 6 ticks to show up**, because the input has to go up
before the snapshot reflecting it can come down. The oracle asserts the first number. The
second is what the stage makes you feel.

| Property | How it is proved |
|---|---|
| The wire is faithful | Client state equals server history offset by the latency |
| Messages survive a round trip | Every message encodes and decodes to itself, empty roster included |
| Malformed input is safe | Every message truncated at every length: no crash, no state mutation |
| The suite is trustworthy | Same seed, same delivery schedule, byte-identical state across runs |
| Late joining is correct | Client B joins after 100 edits; its world matches A's block for block |
| Concurrent edits converge | Two clients edit one block on one tick; both agree afterwards |
| ENet is actually wired up | `MatchServer` and two `MatchClient`s over a real socket on `127.0.0.1` |
| Single-player is untouched | Sandbox with no flag spawns at the same `POS`, reports the same `FACES` |

Roughly 45 new tests, bringing the suite to about 360. Tests run as a build step, so a
regression fails the build.

**Malformed-packet testing is not optional.** This is the first data in the project's
history that arrives from a socket. `ByteReader` fails safe on truncation and on absurd
counts, or it is a vulnerability rather than a bug.

**Every new invariant test must be shown to fail before it is trusted.** Stage 1's first
determinism test was near-tautological and was only caught because someone tried to
falsify it and could not. Break the implementation deliberately, watch the test go red,
then fix it.

### Verifying the real app

Screen capture of the Cubit window is unreliable — three consecutive blank frames with a
healthy process, a DWM/OpenGL `CopyFromScreen` failure rather than a code fault. **The
stage's proof does not hang on it.**

The socket is proved by the suite. The app integration is verified once, by hand, with
the temporary-probe trick: a `CB_INFO` logging each client's view of the other's
position, diffed across the two logs, then `git checkout --` the probe file.

---

## Explicitly out of scope

So that the stage can end:

- **Prediction, reconciliation, entity interpolation, edit rollback** — Stage 3. All of
  it.
- **Snapshot delta compression and interest management** — no bandwidth problem exists.
- **Character models** for remote players — wireframe boxes; modelling is gameplay.
- **Authentication, encryption, anti-cheat** beyond the server simply being
  authoritative.
- **Anything past LAN and localhost** — no NAT traversal, no matchmaking.
- **Reconnection.** A dropped client is gone.

---

## Known limits, accepted and recorded

- **The edit log grows without bound** across a long session. Folding it into a
  periodically re-serialised world is later work.
- **No reconnect.** A client that drops must restart.
- **A snapshot is a full roster** every tick. Fine for two players, not for fifty.
- **The Stage 3 blocker still stands unanswered:** rolling back a rejected edit can
  invalidate predicted *movement*, because the world the character collided against
  changed. Recorded in the parent spec, still unsolved, still needed before shipping.

## Risks

- **Vendoring ENet is the first new dependency in a while.** A C static-lib project
  alongside GLAD, plus `ws2_32` and `winmm` on Windows. Low risk, but it is the one step
  that cannot be unit-tested into submission — it either links or it does not.
- **The real-socket test touches the OS network stack.** It may trip a Windows firewall
  prompt the first time it runs. It is one short localhost test with a timeout, and it
  earns its place: without it, nothing automated proves `EnetTransport` works at all.
- **`CB_API` is `dllexport`.** An exported class must define every member it declares, so
  no member may be declared and left for a later commit. `Transport` sidesteps this by
  being a plain pure-virtual interface with no `CB_API` at all.
- **New `.cpp` files under `Cubit/src/` must include `cub.h` first** — the project uses a
  precompiled header and the build fails otherwise.
- **`Sandbox.cpp` growth.** Already 620 lines. The accessor pattern confines the new mode
  to three branch points; if the implementation finds itself adding a fourth, that is the
  signal to extract rather than to continue.

---

## Shipped 2026-09-03

Thirteen tasks, `adef49a`..`1efe189`. The suite went 315 -> 386.

**A real match runs.** `Server.exe` plus two `Sandbox --connect` clients on
localhost: they take player ids 1 and 2, each sees the other's roster entry, both
mesh the same 1,927,774 faces, and one client's movement appears in the other's
world. Verified with temporary `CB_INFO` probes rather than screen capture, which
is unreliable against this window - the observing client logged the walker moving
from z=300.500000 to z=322.699921 across eight distinct positions before it walked
into terrain.

**Single-player is byte-for-byte unchanged**, which was the acceptance condition
for the whole plan. `POS 240.500000 26.900099 300.500000`, and `FACES 1927774`
once amortized meshing settles at `PENDING 0`. Both identical to the values from
before the wire existed.

### Measured bandwidth

**3,900 B/s down per client, with two players connected.** A snapshot is 65 bytes
- 1 id + 8 tick + 2 count + 2 x 27 per player - at 60 Hz. That is almost exactly
the 3.9 KB/s the design predicted, so the estimate needs no correction and Stage 3
has a real number to optimise against rather than an intuition. Upstream is one
22-byte `InputMessage` per tick per client, 1,320 B/s.

The per-player cost is 27 bytes, so the snapshot grows by 1,620 B/s per additional
player. Delta compression stays correctly out of scope: there is still no
bandwidth problem at this scale.

### What the design got wrong

Eight defects, none of them fatal, all found by scanning the plan against the real
headers or by running it - never by reading it.

- **The `Broadcast` ruling had already been made and the plan predated it.** Task
  6 established that `MatchServer` must send per peer; the task 8 brief still said
  `Broadcast`. The ledger outranks the brief when they disagree.
- **Two guards that turn a malformed packet into a crash.**
  `MatchState::AddPlayer` throws on `InvalidPlayer`, and both `PlayerSnapshot::Player`
  and `WelcomeMessage::You` are raw u16s off the wire that `Decode` has no reason
  to reject. Same shape as the count-guard finding from task 4.
- **A packet leak on the normal path.** `enet_peer_send` returns -1 without taking
  ownership when a peer is not yet connected, and `EnetTransport::Connect`
  deliberately registers its peer before the handshake, so that window is routine.
- **The oracle's skew is two values, not one.** See below - this is the finding
  most worth carrying forward.
- **The guard-rail test for "the client never steps" did not test that.** It
  compared ticks, but `HandleSnapshot` calls `SetTick` on every snapshot, so a
  stepping client is dragged back on the next packet and never overtakes a server
  it runs behind. Rewritten to stop the server, drain what is in flight, and
  assert a client driven with walking input does not move.
- **Two tests in the task 8 brief could not pass**, and one of them was
  interesting: asserting a joining player stands exactly on the spawn point would
  assert that a joiner skips a tick, which would leave it permanently a tick
  behind everyone else.
- **The plan said `OnAttach`; the Sandbox has none.** Setup is in the
  `SandboxLayer` constructor, which also builds the shader - so an early return
  for the connected path would have left `OnRender` with a null shader.

### What Stage 3 inherits

The three claims the plan asked to re-confirm at the end all still stand:

- **The client's tick equals the last snapshot's tick.** `HandleSnapshot` sets it
  and nothing else does.
- **`SetState` restores everything a replay needs** - position, previous position,
  vertical velocity and grounded. `MatchClient` already calls it per snapshot.
- **The edit log is the only unbounded structure.**

Two things Stage 3 should know that the design did not anticipate:

- **`SimulatedTransport`'s delay is not an exact tick multiple even when you ask
  for one.** It accumulates `m_Now += seconds` but computes `Due = m_Now + Latency`
  once, and for about 17% of ticks the accumulated clock lands one ULP (~1e-17)
  below the due time, so `Due <= m_Now` fails and the packet waits an extra tick.
  Measured 4-tick skew x280 and 5-tick x59 where the design predicted a constant 3.
  Verified independently by replaying the same double additions outside the test:
  67 of 400 slip. **Any Stage 3 test that wants exact tick alignment must fix this
  first** - an epsilon on the comparison, or an integer tick clock. Not done here
  because it would re-pin task 6's golden-schedule test.
- **The +1 that is not a bug.** Separately from the ULP slip, the client always
  trails by one more tick than the latency, because its `Step` runs before the
  server's next one. Measurement phase, not delay. Worth knowing before anyone
  "fixes" it.

### Not reviewed

Tasks 8 through 13 were executed directly rather than subagent-driven, and none of
them went through the review round that tasks 1-7 each had - every one of which
found something. `67960db..1efe189` is unreviewed.