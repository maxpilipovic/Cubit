# Cubit Networking Stage 3 — The Feel

_Written 2026-09-03. Status: shipped 2026-09-05, `d24c941..fa4599c`._

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

**Measured 2026-09-05, `Tests/src/PredictionTests.cpp`.** Both figures come from a
`MatchServer`/`MatchClient` pair on a `LoopbackNetwork`, 2,120 ticks each, past a
120-tick warm-up. The spec's round number is 150 ms RTT, but 75 ms one-way is 4.5
ticks, and this suite's rule is that every test latency is a whole tick multiple — a
half-tick latency makes every arrival ambiguous by a tick, the exact defect Task 2
removed. So the tests below use **166.7 ms RTT (5 ticks one-way)**, one tick above the
spec's number; the real-app run at `--latency 150` (Task 11) asserts nothing about
which tick anything landed on.

- **Clean link, no loss, no jitter (GATE):** 0 corrections, both during the 120-tick
  warm-up and across the following 1,000 ticks of varied input, jumps included, over
  1,113 snapshots reconciled. `Corrections().Count` only counts disagreements over the
  0.15-block threshold by design, so this by itself proves "no disagreement exceeded
  0.15 blocks" rather than "no disagreement at all" — a permanent sub-threshold
  divergence would report the same clean zero. The test closes that gap directly: after
  the 1,000 ticks it drains both sides to a stop with no input and asserts the two
  positions agree to within 1 mm. That is the design's own weakest claim, confirmed
  rather than assumed: with no loss and no jitter, prediction and the authoritative step
  do not disagree at all, not merely "not enough to show".
- **166.7 ms RTT, 5% loss, 1-tick jitter, seed 1 (BASELINE, realistic rate):** 0
  corrections per 1,000 ticks, mean 0, max 0, across both halves of a 2,000-tick run.
  This is the expected result at this loss rate, not an untested corner of it: a single
  lost input tick costs about 0.083 blocks (`CorrectionThreshold`'s own design point),
  under the 0.15 threshold on its own, and a lost tick is never converged back once
  `Reconcile` discards it below threshold — so what would clear the threshold is either
  a jump tick going missing outright, or two lost ticks anywhere in the run (not
  necessarily adjacent) landing within about 51° of each other in direction. Losing all
  three redundant copies of one tick's input happens with probability 0.05³ ≈ 1.25e-4
  per tick (not the four-consecutive-losses, 6e-6-per-tick figure this document
  previously stated, which was wrong by about three orders of magnitude and, separately,
  wrong to require adjacency at all) — enough that a percent-level chance of at least one
  correction across 2,000 ticks is expected, and this particular run's seed came back
  clean. **Caveat:** because this run reports zero, it cannot by itself distinguish
  "correction accounting works under network loss" from "the counter is stuck at zero" —
  that machinery is pinned by two other tests in this file that inject a divergence
  directly (`"A correction smaller than the threshold is not shown"` and `"A correction
  bigger than the threshold snaps"`), and by the 20%-loss run below, which does exercise
  it under real network conditions.
- **166.7 ms RTT, 20% loss, 1-tick jitter (BASELINE, loss-side):** seed 1 (the recorded
  run) reports 3 corrections per 1,000 ticks, mean 0.193, max 0.291; seeds 2 and 3, run
  before pinning the bound so the figure would not be one seed's luck, reported max 0.288
  and 0.227 respectively — all comfortably under 0.5. At 20% loss all three redundant
  copies of a tick's input are lost together with probability 0.2³ = 8e-3, roughly 16
  fully-dropped input ticks across the run; those ~0.083-block offsets never converge
  back on their own, but they do not accumulate as a random walk across all sixteen
  either. `Reconcile`'s `error` is the distance between the current predicted position —
  which still carries every not-yet-corrected drop — and that call's freshly replayed
  authoritative state, which carries none, so every reconciliation re-measures the
  running total rather than an increment, and the total is zeroed the moment it first
  crosses `CorrectionThreshold`. At ~0.083 blocks a drop, that is roughly three or four
  drops, not sixteen — which is why the run reports 6 corrections (3 per 1,000 ticks)
  with a mean of 0.193, close to the threshold, rather than the single ~0.33-block
  correction a √16 random-walk model would predict. Unlike the 5%-loss run above, this
  one reliably produces corrections from the network condition itself rather than from
  an injected teleport, and is what actually exercises the counting and snapping
  machinery end to end. Any of these figures rising in a future run — corrections per
  1,000 ticks, mean, or max — is the regression these baselines exist to catch.

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
  prevented. Approach B prevents it; approach B is not built. It is also not needed by
  the measurement: at 20% loss, well past this design's target, the rate is 3
  corrections per 1,000 ticks with a maximum of 0.291 blocks — see "Whether the escape
  hatch is still needed" below.
- **The threshold is a deadzone, so a small persistent error is never corrected.**
  Bounded by construction — once it exceeds 0.15 blocks it snaps — but it means the
  client is not exactly the server between snaps, by design.
- **The edit log still grows without bound.**
- **The rolled-back-edit risk is untouched** and still blocks predicted edits. Rolling
  back a rejected edit can invalidate predicted *movement*, because the world the
  character collided against changed underneath it — the risk the design deferred
  movement-only prediction to avoid facing at the same time as reconciliation, and it
  is still facing it.
- **Yaw interpolation takes the long way round across the ±180° seam.** Harmless today
  because nothing draws a remote's facing yet; commented at the site
  (`MatchClient::PoseOf`, `Cubit/src/Net/MatchClient.cpp`) so a future caller meets a
  known limit instead of rediscovering it as a bug.
- **An undiagnosed ~6-second session death under heavy loss (`--loss 80/90`) is still
  open, but narrowed, not solved, by the final review.** `SimulatedTransport` sits
  above `EnetTransport`, so modelled loss never touches ENet's own reliable machinery.
  With `--loss` set and no `--latency`, `sim.Latency` is 0, so the reliable-retransmission
  branch adds `2.0 * 0.0` — exactly nothing — meaning reliable traffic passes through
  untouched at any loss rate this flag can produce. `ENET_PEER_TIMEOUT_MINIMUM` fires
  only on an unacknowledged reliable command, and the only steady reliable traffic is
  ENet's own ping, which `SimulatedTransport` never sees at all. So the ~5 s coincidence
  with that timeout constant is either not an ENet timeout, or is driven by something
  other than the dropped datagrams — most likely host-side stalling of the fixed-step
  loop. That points the next investigation at wall-clock instrumentation of the loop
  rather than at packet accounting, which is a much smaller search space than where this
  was left.
  _(Closed as not reproduced 2026-09-14: 48 sessions under 80 and 90% loss, on this
  stage's own build and the current one, never died — see "Still open" in
  `2026-09-12-predicted-edits-design.md`.)_

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

---

## Shipped 2026-09-05

Twelve tasks, `d24c941..fa4599c` (Task 1's review-fix commits onward — `d24c941` is
where Stage 2's unreviewed tail closed). The suite went 386 -> 415.

**The application, not just the tests.** `Server.exe` plus two `Sandbox.exe --connect
127.0.0.1 --latency 150` clients ran for about 40 seconds: 3,851 and 3,625 snapshots
reconciled on the two clients, zero corrections on either side, both clients reaching
`PLAYERS 2`. A real injected keydown (`keybd_event`, not a posted window message — this
project already knows keyboard input cannot be scripted into a GLFW window any other
way) plus a per-tick timestamped log showed the character already moving about 10 ms
after the keydown — inside one 60 Hz step, and far short of the 150 ms round trip a
server-driven move would need. Pressing `W` moves the view on the same frame. The
nonzero snapshot counts prove `Reconcile` actually ran on both clients; they do not by
themselves prove `m_CorrectionCount` is wired, since `Snapshots` and `Count` increment
in different places under different conditions — the same distinction the caveat on the
5%-loss run above draws correctly. What backs the zero corrections here is the same
machinery that caveat leans on: the deadzone tests that pin counting and snapping
directly, and the 20%-loss run that exercises them under real network conditions.

**Single-player is byte-for-byte unchanged.** `POS 240.500000 26.900099 300.500000`,
`FACES 1927774` — identical to the values from before this stage.

### The measured figures, in shape

Recorded in full above in "The acceptance number." What they show, together:

- **Clean link (GATE): zero corrections**, through warm-up and across 1,000 further
  ticks over 1,113 reconciled snapshots, confirmed further by draining both sides to a
  stop and finding the two positions agree to under a millimetre. This was the plan's
  own nominated weakest claim — that reconciliation genuinely produces no disagreement
  on a clean link followed from the design but had never been observed. It is now
  observed.
- **166.7 ms RTT, 5% loss, jitter (BASELINE): zero corrections.** Expected: three-deep
  bundling absorbs a single lost tick inside the threshold, and clearing it needs either
  an unlucky jump tick or two lost ticks anywhere in the run landing within about 51° of
  each other — percent-level per run, not the four-consecutive-drops figure this spec
  briefly carried before a fix round corrected the arithmetic. **A zero here is weaker
  evidence than it looks.** This run alone cannot tell "correction accounting works
  under loss" from "the counter is stuck at zero" — that is exactly what the 20% case
  below, and the two tests that inject a divergence directly, exist to rule out.
- **166.7 ms RTT, 20% loss, jitter (BASELINE): 3 corrections per 1,000 ticks, mean
  0.193, max 0.291 blocks**, seed 1; seeds 2 and 3, run before pinning the bound, gave
  maxima 0.288 and 0.227. This is the one measurement in the stage where the counting-
  and-snapping machinery is exercised by an actual network condition rather than an
  injected teleport.

### What turned out differently from the design

- **The `SimulatedTransport` due-time fix was a genuine prerequisite, and it moved two
  pinned tests.** Comparing against `m_Now` plus a one-nanosecond epsilon, instead of
  `m_Now` alone, fixed the float-accumulation slip where an exact-tick latency landed
  one ULP below its own due time. The golden delivery schedule re-pinned from
  `{2,3,5,6,7,7,8,9,11,12}` to `{2,3,4,5,6,7,8,9,10,11}` — the irregularities were the
  slip, not real jitter. The client/server clock skew the state-lag oracle allows
  collapsed from two values (`LatencyTicks + 1` or `+2`) to the single value 4
  (`LatencyTicks + 1`); `MinSkew == MaxSkew` now. Stage 2 had measured the old skew as 4
  ×280, 5 ×59 and left it alone deliberately — Stage 3 could not, because prediction and
  replay both reason about which tick a packet landed on.
- **Approach B stays deferred, and now for a measured reason rather than a hopeful
  one.** See "Whether the escape hatch is still needed" below.
- **`MatchClient`'s tick advances only on a tick where input was set**, because `Step`
  returns early without one. Not designed for, but it turns out to be exactly what
  keeps a bundle's ticks consecutive by construction — the property both the server's
  duplicate filter and the client's own replay depend on.
- **The render clock runs at least one tick ahead of the newest applied snapshot**
  whenever both happen inside the same `Step` call, because `Step` polls — and snaps
  `m_RemoteClock` to the snapshot — before it predicts, which advances the clock once
  more. Deliberate once found: a snapshot describes a tick the server has already left,
  so one local step is a floor on elapsed time, not an overestimate. Its absence is what
  made this plan's own interpolation test constants wrong by exactly one tick until a
  fix round corrected them.
- **A pre-existing test fragility was unmasked, not introduced.** The bad-network
  prediction test walked a character 25 blocks across a 32-block world from a spawn at
  x=8; under Stage 2 it only passed because 5% loss shaved the walk to about 31.75
  blocks, a quarter-block short of falling off the edge. This stage's redundant
  bundling recovers exactly the inputs that used to be lost, pushing the same test over
  the edge. Shortened to 200 ticks (16.7 blocks of travel) rather than reworked, since
  nothing the test exists to prove needed the extra distance — worth recording as a
  trap for any future test that walks a character a long way.
- **The queue cap of 8 was reached only in the test built to reach it.** A dedicated
  test sends twenty inputs to force an overflow (raising the cap to 64 is the mutation
  that catches it); nothing in the realistic-network acceptance runs — 5% loss, 20%
  loss, or the three-process application run at 150 ms — came close to it. The
  overflow-drop path is pinned deliberately, not observed under load.
- **The correction's `before` (previous-position) argument stayed unpinned.** The
  design left open where a correction's previous-position value comes from, since the
  wire carries no such field; the implementation restores the predicted value rather
  than the authoritative one, reasoning that the first replayed step overwrites it
  immediately. Collapsing the two arguments to one did not turn any test red. The
  reasoning stands, but the argument is recorded as currently unpinned by any test
  rather than claimed as verified.
- **Clean-link warm-up produced zero corrections**, exactly as the design implied: 120
  ticks with no loss and no jitter is a link on which the server never starves, so
  replay reproduces the server's own state exactly.

### Whether the escape hatch is still needed

**Approach B — per-player input-driven stepping — stays deferred, and now for a
measured reason rather than a hopeful one.** It was named as the answer if the measured
correction rate came back too high. At a realistic 5% loss the rate is zero; at 20%
loss — well past what this design targets — it is 3 corrections per 1,000 ticks with a
maximum (0.291 blocks) comfortably under half a block. The escape hatch was not needed,
and that is now a number rather than an assumption.

### Not reviewed, then reviewed

Unlike Stage 2's tail, every task here went through the review round: Task 1 was
itself a review of Stage 2's unreviewed `67960db..1efe189`, and each of Tasks 2–11 had
either a full review or a scoped re-review before being marked complete. Nothing in
this stage carries Stage 2's "not reviewed" caveat forward.
