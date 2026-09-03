# Cubit Networking Stage 3 — The Feel

_Written 2026-09-03. Status: designed, not yet built._

Stage 3 of the arc laid out in
[`2026-08-27-networking-design.md`](2026-08-27-networking-design.md), following
[Stage 2](2026-08-31-networking-stage-2-design.md), which shipped 2026-09-03.
**Read the arc document first** — it holds the arc-level decisions, the reasoning for
doing networking before gameplay, and the glossary. Terms used here without definition
are defined there.

**Where Stage 2 left things.** A headless `Server.exe` owns the only `MatchState`
anyone believes. `MatchClient` never steps — input goes up, snapshots come down and are
written straight in. Two clients share a world and see each other. `Transport` is an
interface with a loopback, a deterministic bad-network decorator, and a real ENet
socket behind it. 386 tests. Measured cost: 3,900 B/s down per client.

And the latency is plainly visible, on purpose. Pressing `W` does not move the view for
a full round trip. **This stage is the one that fixes that.**

---

## What this stage delivers

The client simulates its own character immediately, and is corrected when it is wrong
without the correction being visible. Remote players move smoothly rather than stepping
per packet. Terrain edits are untouched — they still take a round trip, and that is
deliberate.

Concretely, when this is done: at `--latency 150`, pressing `W` moves the view on the
same frame, and a scripted run reports how often the server disagreed and by how much.

## Decisions already made

| Decision | Choice | Why |
|---|---|---|
| Scope | Movement prediction only | Predicted edits are a second hard problem; the unsolved rollback risk is faced later, on a working reconciliation loop |
| Correction | Snap above a threshold | The only option with no new failure mode, and the only cleanly testable one |
| Acceptance | Oracles plus a measured correction rate | Turns "feels smooth" into a figure, and catches regressions a playtest cannot |
| Convergence | Ack-and-replay, uniform stepping, redundant input bundles | Below |
| Client clock | Free-running from `Welcome.Tick` | The ack aligns client and server; no clock-sync subsystem |

**Why movement only.** The arc spec sketches predicted terrain edits with rollback in
this stage. They are deferred. The recorded risk — rolling back a rejected edit can
invalidate predicted *movement*, because the world the character collided against
changed underneath it — has no answer yet, and facing it at the same time as building
reconciliation means two unproven things holding each other up. Deferring also keeps
the stage legible: `W` becomes instant, digging stays a round trip.

---

## The convergence trap, and the three ways out

This is the part of Stage 3 that is not obvious, and it decides the architecture.

**Reconciliation converges only if the server consumes exactly as many input steps as
the client produced.** The client predicts by stepping once per input. If the server
steps a different number of times, its state is not a prefix of the client's prediction
and the difference never goes away.

But the server steps at a fixed 60 Hz whether or not an input arrived. If the input for
tick 4 is late by one tick, the server applies `1, 2, 3, ∅, 4` while the client
predicted `1, 2, 3, 4`. Those disagree, permanently, by one step. Repeating the last
input instead of nothing does not help — it applies `1, 2, 3, 3, 4` and disagrees just
as much. **Under jitter, every late input is a correction.**

Three ways out were considered.

**A — Uniform stepping, redundant input bundling. Chosen.** Each `InputMessage` carries
the last three inputs rather than only the newest, so a single lost or late packet is
covered by the next one and starvation becomes rare rather than routine. When it does
happen the cost is one step of walking — `WalkSpeed × dt` = `5.0 / 60` = **0.083
blocks** — which is inside the correction threshold and produces no visible movement.
`MatchState::Step` keeps its current contract. The resulting correction rate is the
number this stage measures.

Stage 2 already anticipated this. The comment on `MatchServer::Client::HasInput` reads:
*"a lost input should cost one step of movement and be visible, because that is what
motivates Stage 3 bundling inputs redundantly."*

**B — Per-player input-driven stepping.** The server advances a player once per input it
consumes, draining a queue. Step counts then match by construction and starvation costs
nothing. Rejected for now because it breaks `MatchState::Step`'s contract that every
player advances one step per tick — the contract Stage 1's determinism oracle is written
against — and it makes gravity per-player-clocked. **Kept as the named escape hatch** if
the measured correction rate comes back too high.

**C — Client runs ahead on an adaptive clock offset.** What commercial engines do:
estimate RTT, run far enough ahead that inputs always arrive before the server needs
them, nudge the offset as RTT drifts. Eliminates starvation at the source and is the
prerequisite for hitscan lag compensation. Deferred: it is a clock-synchronisation
subsystem, which is a classic home of non-deterministic bugs, and lag compensation is
out of scope for the whole slice until gameplay exists.

---

## Protocol version 2

Both changed layouts are on the per-tick path, so `ProtocolVersion` goes to **2**. The
version check exists for exactly this: two builds disagreeing about field widths produce
garbage positions, which read as a physics bug and cost a day.

### `InputMessage` carries a bundle and a tick

`Sequence` was always documented as a placeholder — *"a counter, not a tick … Stage 3 is
where an input acquires a real tick, because that is when replay needs to know where to
reinsert it."* Now it does.

```
Input: u8 id | u8 count | u64 firstTick
     | count × { f32 moveX, f32 moveY, f32 yaw, f32 pitch, u8 jump }
```

The client produces exactly one input per tick, so a bundle's ticks are always
consecutive and only the oldest needs sending. At `count = 3` that is **61 bytes**, so
upstream goes 1,320 → **3,660 B/s** per client. The redundancy is the entire defence
against starvation.

`count` is bounded on decode against the remaining buffer, the same guard shape as
`WelcomeMessage`'s edit count — see the note in `Protocol.cpp` about what happens
without it.

### `PlayerSnapshot` gains `LastInputTick`

The ack that makes replay possible: the newest input from that player which the server
has consumed.

Per-*player* rather than per-recipient, deliberately. A per-recipient ack would force
the server to encode a separate snapshot for every client; `SendToJoined` currently
encodes once and sends identical bytes to everyone. The cost is 8 bytes per player:
`PlayerSnapshot` goes 27 → **35 bytes**, a two-player snapshot 65 → **81 bytes**, and
downstream 3,900 → **4,860 B/s** per client. Still no bandwidth problem.

---

## The server

`Client::LastSequence` becomes `LastInputTick`, and `Client` gains a small input queue.

Arriving bundles are filtered: anything at or below `LastInputTick`, or already queued,
is a duplicate and is dropped. That filter is what makes redundant bundling free rather
than harmful.

Each `Step` pops the **oldest** queued input, uses it as that client's command, and sets
`LastInputTick` to its tick. An empty queue means no input that tick, exactly as today.

Popping oldest rather than newest is load-bearing. Taking the newest would discard
intent the client has already predicted on, guaranteeing a correction every time a
bundle arrived after a gap — which is precisely the case bundling exists to survive.

The queue is capped at 8. Overflow means the client is running further ahead than this
design assumes, so it drops and logs rather than silently absorbing; a silent drop here
would present as unexplained corrections much later.

---

## The client

### `MatchState::StepPlayer`

New, and it exists because of a constraint the arc spec does not mention: **the client
must advance its own character without advancing anyone else's.** Remote players are
never predicted. `MatchState::Step` advances every present player, so a client calling
it would simulate remotes under gravity between snapshots and then stamp over them —
the stepping artifact that choosing 60 Hz snapshots was meant to avoid.

```cpp
//Advances exactly one player. Leaves the tick and every other player alone.
//
//The server uses Step; a client uses this. A client predicts only itself,
//because it has no idea what anybody else is about to do.
void StepPlayer(PlayerId player, const CharacterInput& input, float seconds);
```

The server's `Step` is unchanged, and so is Stage 1's determinism oracle.

### The tick stops being the server's

Today `HandleSnapshot` calls `SetTick(snapshot.Tick)`, so the client's clock trails the
server's. Prediction inverts that. The client's tick free-runs from `Welcome.Tick`,
advancing once per predicted step, and an input's tick is that number. It is the
client's own numbering, echoed back untouched in `LastInputTick`; two clients need not
agree about it, because each reads only its own entry.

The server's tick is still tracked, separately, because remote-player interpolation is
expressed in it.

### Each tick

Stamp the current input with the current tick, push it onto the unacked ring,
`StepPlayer` with it, and send a bundle of the last three.

### On a snapshot

Four steps, in this order:

1. Record the current predicted position as `before`.
2. `SetState` from the authoritative entry — position, previous position, vertical
   velocity, grounded. All four. This is why the Stage 2 review's third `MatchState` gap
   mattered: `Teleport` flattens the previous position and would destroy exactly the
   interpolation a correction is meant to hide.
3. Replay every buffered input with `tick > LastInputTick`, then drop the acked ones.
4. Compare the replayed result against `before`. Under the threshold, restore `before`
   wholesale and discard the correction. Over it, keep the replayed state — that is the
   snap.

Step 4 is all-or-nothing on purpose. Accepting the authoritative velocity while keeping
the predicted position would leave the character in a state neither machine ever
simulated, and the next step would compound it.

**Threshold: 0.15 blocks**, on full 3D distance. One starved tick while walking costs
0.083 blocks, so 0.15 absorbs a single dropped input and little more. It is a tunable,
and the measured correction rate is what validates it.

Replay cost is bounded by the unacked depth — about `RTT / 16.7 ms` steps, so roughly 9
at 150 ms, sixty times a second. Character stepping is box collision against a handful
of voxels. This is not a performance question.

---

## Remote players

Never predicted, never extrapolated.

Each remote player gets a ring of `(serverTick, position, yaw, pitch)` samples taken
from snapshots. Rendering targets `latestServerTick − InterpolationDelayTicks` — **6
ticks, 100 ms** — and interpolates between the two samples bracketing that time. When
the target is newer than the newest sample, hold the newest rather than guessing
forward: a wrong extrapolation has to be taken back, and taking it back looks exactly
like the stutter it was trying to avoid.

The interpolated pose is a render-time query on `MatchClient`, not written back into the
`MatchState` character. Simulation state and render state stay separate, and nothing
else reads a remote's position — players do not collide with each other in this slice.

100 ms is the arc spec's number and is generous at 60 Hz, six snapshots of cushion. It
is a constant to tune once there is something to watch.

---

## Prerequisites

Two, both before any prediction work.

**`SimulatedTransport`'s due-time comparison must stop losing to float accumulation.**
It accumulates `m_Now += seconds` but computes `Due = m_Now + Latency` once, so for
about 17% of ticks the accumulated clock lands one ULP (~1e-17) below the due time,
`Due <= m_Now` fails, and the packet waits an extra tick. Measured in Stage 2: skew of 4
ticks ×280 and 5 ×59 where an exact-arithmetic reading predicts a constant. **No Stage 3
test can assert exact tick alignment until this is fixed.** An epsilon on the comparison
or an integer tick clock; either re-pins task 6's golden-schedule test, which is why it
is a task of its own.

**`MatchState::StepPlayer`**, as above.

---

## Testing strategy

The project's standard is a reference oracle, not a vibe.

| Property | How it is proved |
|---|---|
| Reconciliation converges | Stale authoritative state + input history, replayed == straight simulation. No network at all — possible only because `Step` is pure |
| The client steps only itself | A remote player's position does not change between snapshots |
| Prediction is invisible when right | On a clean link, no correction ever exceeds the threshold |
| The deadzone works in both directions | A forced sub-threshold divergence produces no movement; a forced super-threshold one snaps |
| It survives a bad network | 150 ms RTT, 5% loss, jitter — client and server agree exactly once movement stops |
| Remote poses are interpolated, never extrapolated | A rendered remote pose always lies between two received samples |
| Behaviour is unchanged | Sandbox with no arguments still reports `POS 240.500000 26.900099 300.500000` and `FACES 1927774` |

**One Stage 2 test becomes false by design.** *"The client never steps the simulation
itself"* is the exact invariant this stage deletes. It is replaced by its inverse rather
than quietly removed: the client now steps, and the new test asserts it steps only its
own player. Deleting it silently would lose the record that the constraint was
deliberate.

### The acceptance number

A scripted run at `--latency 150` reporting **corrections per 1000 ticks**, and their
mean and maximum magnitude. That figure replaces "feels smooth", the way 3,900 B/s
replaced "bandwidth is fine", and unlike a playtest it can be re-run to catch a
regression.

The number is recorded, not compared against a target invented in advance — nobody
knows the right value yet, and a threshold guessed here would be a number to argue with
rather than evidence. What is a gate:

- **On a clean localhost link, corrections per 1000 ticks must be zero.** With no loss
  and no jitter the server never starves, so any correction at all means prediction and
  the authoritative step disagree — a real defect, not a network condition.
- **At 150 ms and 5% loss, the maximum correction magnitude must be bounded and must not
  grow across the run.** A rising maximum means error is accumulating rather than being
  corrected, which is the failure mode reconciliation exists to prevent.

The measured figure at 150 ms is then written into this spec the way Stage 2's bandwidth
was, and becomes the baseline a later change is compared against.

Keyboard input cannot be scripted into the Cubit window, so the run uses the same
temporary walk-override probe Stage 2's verification used. Screen capture of this window
is unreliable; the probe is the evidence.

---

## Explicitly out of scope

- **Predicted terrain edits and edit rollback.** Deferred with the risk intact.
- **Snapshot delta compression.** No bandwidth problem exists at 4,860 B/s.
- **Lag compensation, and the adaptive clock offset it needs.** Needs hitscan, which
  needs gameplay.
- **Reconnection.** A dropped client is still gone.
- **Player-versus-player collision.** Still one kind of actor.

## Known limits, accepted and recorded

- **A starved tick still costs a correction**, absorbed by the threshold rather than
  prevented. Approach B prevents it; approach B is not built.
- **The threshold is a deadzone, so a small persistent error is never corrected.**
  Bounded by construction — once it exceeds 0.15 blocks it snaps — but it means the
  client is not exactly the server between snaps, by design.
- **The edit log still grows without bound.**
- **The rolled-back-edit risk is untouched** and still blocks predicted edits.

## Risks

- **Float determinism across machines is still not relied on.** Correction is
  state-based, not lockstep, so the server continuously fixes drift rather than assuming
  it cannot happen. The oracle covers one binary, which is what it claims.
- **Tasks 8–13 of Stage 2 are unreviewed** (`67960db..1efe189`), and Stage 3's
  reconciliation sits directly on `MatchServer` and `MatchClient`. Every one of tasks
  1–7's reviews found something. **This is task 1 of the implementation plan.**
- **The threshold is the one number here chosen by reasoning rather than measurement.**
  0.15 blocks is derived from one starved walking tick; if the measured correction rate
  is bad, the threshold is the first thing to suspect and approach B is the second.
