# Cubit Networking Stage 4 — The Shot

_Written 2026-09-08. Status: designed, not implemented._

Stage 4 of the arc laid out in
[`2026-08-27-networking-design.md`](2026-08-27-networking-design.md), following
[Stage 3](2026-09-03-networking-stage-3-design.md), which shipped 2026-09-05.
**Read the arc document first** — it holds the arc-level decisions, the reasoning for
doing networking before gameplay, and the glossary. Terms used here without definition
are defined there.

The arc document listed lag compensation as explicitly out of scope, because it "needs
hitscan, and hitboxes to rewind." This stage builds both.

**Where Stage 3 left things.** The client predicts its own movement, replays what the
server has not acknowledged, and snaps only above a 0.15-block deadzone. Remote players
are interpolated six ticks behind the newest snapshot and never extrapolated. Protocol
version 2, 416 tests, zero corrections on a clean link and 3 per 1,000 ticks at 20%
loss. Single-player is byte-for-byte unchanged.

What is missing is a game. Two people share a world, walk around it, and dig it up.
Neither can do anything to the other.

---

## Why this before the rest of gameplay

The argument that put networking before gameplay was that prediction is the only thing
genuinely expensive to retrofit. That argument is now spent for *movement*, but not for
shooting.

Lag compensation requires the server to keep a history of where everyone has been and
to resolve a shot against the world as the shooter saw it. That reaches into the tick
loop, the snapshot path, and the trust boundary — the same shape of retrofit cost
prediction had. Teams, weapons, scoring and objectives are all additive on top of a
server that can already answer "did that shot connect?" honestly. So this is the last
structurally expensive netcode piece, and everything after it is game design.

## What this stage delivers

A player can shoot another player, and it works at 150 ms in the way a shooter is
expected to work: if the target was under your crosshair on your screen, you hit them.
Three hits kill; death respawns you.

Concretely, when this is done: a scripted run at `--latency 150` against a strafing
target reports a hit rate, and the same run with the rewind disabled reports a
materially worse one.

## Decisions already made

| Decision | Choice | Why |
|---|---|---|
| Scope | Hit, health, death, instant respawn | The smallest thing that is visibly a game, and it makes a wrong rewind observable rather than a number in a log |
| Shot feedback | Local tracer immediately, hit marker only on the server's word | Nothing that could be retracted is ever shown — the same discipline that keeps Stage 3 from extrapolating remotes |
| Rewind cap | 250 ms (15 ticks), favouring the shooter under it | Covers the 150 ms link this arc targets with room for jitter, and bounds the "shot behind cover" window at a quarter second |
| Rewind target | The client declares the instant it rendered; the server clamps it | The client knows this exactly; the server can only estimate it |
| Terrain | Not rewound. Players only | Deferred deliberately, see Out of scope |

## The rewind instant is fractional, and that is load-bearing

`MatchClient::m_RemoteClock` is a `double`, and `PoseOf` lerps between the two snapshot
samples bracketing `m_RemoteClock - InterpolationDelayTicks`. The screen the shooter
aimed at therefore showed the target *between* two ticks, not on one.

So `FireMessage` carries `RenderTick` (a server tick) **and** `RenderAlpha` (a float) —
the same `(tick, alpha)` pair `FrameClock` hands the renderer everywhere else in this
codebase — and the server lerps its own history the same way.

This is not a refinement. It is what lets the acceptance oracle assert an *exact* hit
rather than an approximate one: the server reproduces the client's lerp, it does not
estimate it. Snapping to the nearest whole tick would leave the rewind up to half a step
adrift, which against a strafing 0.6-wide box at range is the difference between a hit
and a miss with no way to tell which answer was correct — and a gate that cannot
distinguish those is not a gate.

## The shooter and the targets rewind to different instants

The subtle heart of lag compensation, stated plainly because it is the thing most likely
to be implemented as one instant by mistake:

- **The targets** go back to the instant the shooter's screen was showing —
  `RenderTick + RenderAlpha`, roughly one round trip plus the interpolation delay in the
  past.
- **The shooter's own eye** comes from their position at the input tick they fired on,
  which the server has already stepped and already echoes back as
  `PlayerSnapshot::LastInputTick`.

Both readings come from the same history buffer. They are simply read at different
times. Using the render instant for the shooter too would place their eye a round trip
behind where they believe they are standing, and every shot fired while moving would
leave from the wrong place.

## Why the client declares the instant, rather than the server deriving it

Two mechanisms were considered.

**The client declares it (chosen).** The client already interpolates remotes against
`m_RemoteClock`, which is expressed in the *server's* tick numbering — so it knows
exactly what instant its screen showed and can simply say so. The server clamps the
claim into `[serverTick - 15, serverTick]` before anything reads it. The clamp applies
to the combined fractional instant `RenderTick + RenderAlpha`, not to the whole part
alone — clamping the two separately would let a claim of tick 0 with alpha 0.9 survive
as a fractional offset applied to a completely different tick.

**The server derives it (rejected).** Take the input tick the shot rides on and subtract
a measured one-way delay plus the fixed six-tick interpolation constant. This trusts
nothing, which is its whole appeal — but it re-derives, from a jittery estimate, a
number the other end knows exactly, and `m_RemoteClock` snaps to whichever snapshot last
*arrived*, which the server cannot observe. Under loss the two disagree and shots miss
for reasons nobody can debug. This is the same failure the `LastInputTick` ack was
invented to avoid in Stage 3: when one end knows a number exactly, send it rather than
estimating it.

**The clamp is the entire trust story, and it is enough.** A client that lies about
`RenderTick` gets aimed at a quarter-second-old world — which is precisely what an
honest 250 ms player gets. The lie buys nothing. Nothing else in the resolution comes
from the client except aim, and aim has been untrusted-by-design since Stage 3 made
looking instant: yaw is an input, not simulated state, so there has never been anything
to reconcile it against.

## Protocol version 3

Two new messages and one new field. Both messages are reliable and both mirror the
`EditRequest`/`EditApplied` shape — a request that cannot be confused with its answer,
because a client must never mistake its own shot coming back for the server's ruling.

### `FireMessage` (`Fire = 7`, client to server)

| Field | Type | Meaning |
|---|---|---|
| `ClientTick` | `uint64` | The tick the shooter fired on, in the client's own numbering |
| `RenderTick` | `uint64` | The whole part of the instant the shooter's screen was showing, in the **server's** numbering |
| `RenderAlpha` | `float` | The fractional part, in `[0, 1)` |
| `Yaw` | `float` | Aim, degrees, `Heading.h` convention |
| `Pitch` | `float` | Aim, degrees |

Aim rides in the message rather than being looked up from the input at `ClientTick`,
because that input may have been dropped and may never arrive — a shot that silently
became a miss because its input packet was lost would be indistinguishable from a bug in
the rewind.

**`FireMessage` has no variable-length field and therefore needs no count guard.** Worth
saying out loud so nobody adds one by analogy, and more importantly so nobody removes
`WelcomeMessage`'s: that count is `uint32_t` and is the one thing standing between a
malformed 11-byte packet and 30 GB of the host's RAM.

### `ShotResolvedMessage` (`ShotResolved = 8`, server to all clients)

| Field | Type | Meaning |
|---|---|---|
| `Shooter` | `PlayerId` | Who fired |
| `Victim` | `PlayerId` | Who was hit, or `InvalidPlayer` for a miss |
| `Impact` | `vec3` | Where the ray stopped — terrain, a player, or the end of its range |
| `VictimHealth` | `uint8` | The victim's health after the damage |
| `Killed` | `bool` | Whether this shot took them to zero |

Sent to every joined client rather than just the two involved, so everyone sees everyone
else's tracers. At six shots a second and 19 bytes, this is negligible next to the
measured 3,900 B/s snapshot stream.

### `PlayerSnapshot` gains `Health`

One `uint8`, adding 60 B/s per player. Health is never predicted — it arrives and is
displayed.

## The server

### `HitboxHistory`

Lives in `Cubit/include/Cubit/Voxel/`, alongside `MatchState`, under the same rule:
**it contains no networking.** That placement is not filing, it is what makes the type
unit-testable with no transport, no socket and no server.

Per player, a ring of **16** `(tick, position)` samples, written once per server tick
after `MatchState::Step`. Sixteen because the 250 ms cap is 15 ticks and one more is
needed to bracket a fractional instant.

Position only. The hitbox is `CharacterConfig::HalfExtents` around the position; neither
velocity nor grounded shapes it, and storing the whole `CharacterController` would
invite someone to rewind physics rather than geometry.

`BoxAt(player, tick, alpha)` lerps between the two bracketing samples and returns the
AABB, mirroring what `PoseOf` does on the client.

### A shot may not rewind across a respawn

An in-flight shot that rewound past a death would find the victim standing where they
were before they died and would damage the player who has since respawned there.

One rule covers it: **if the requested instant is older than a player's oldest sample,
that player is not a candidate at all.** You cannot shoot someone at a time the server
has no record of them standing anywhere. Respawn clears that player's history, so the
rule applies itself. A player who joined two ticks ago is covered by the same rule for
free, and so is the first fifteen ticks of the match.

### `ResolveShot`

A pure free function, also in `Voxel/`: world, candidate boxes, origin, direction, range
in, nearest hit out.

- Ray-vs-AABB against each rewound candidate box.
- `VoxelRaycast::Cast(..., solidOnly = true)` for terrain — water is scenery you aim
  through, which `Cast`'s own comment has anticipated since the day `solidOnly` was
  added.
- **Nearest wins, and terrain takes ties**, so no shot can ever pass through a wall.
- The shooter is never a candidate against their own shot.

Range is 128 blocks.

### Game rules

100 health, 34 damage, so three shots kill. Death teleports to spawn, clears vertical
velocity, restores health, and clears that player's hitbox history.

**Health lives on `MatchServer`, not `MatchState`.** `MatchState` is "the whole
*simulated* state of a match" and health is not simulated by `Step` — it changes only
when a game rule says so. `MatchState::PlayerForWrite` already exists carrying the
comment "for callers that own game rules the match itself does not — respawning." This
stage is that caller, arriving as predicted.

**One shot per 10 ticks**, server-enforced. This is the weapon's rate of fire and
simultaneously the flood answer for a new reliable client-to-server message: a client
that spams `Fire` has its extras dropped with a rate-limited warning, following the
episode-not-per-event precedent set for the input queue overflow in `3686456`.

### Failure handling

Every case follows a precedent that already exists, and none of them throw.

| Case | Behaviour | Precedent |
|---|---|---|
| Malformed `Fire` | `Decode` returns false, nothing changes | Every decoder on this wire — malformed input is a routine wire condition, not a caller bug |
| `Fire` before the handshake | Ignored | `SendToJoined` skips un-handshaken peers for the same class of reason |
| `Fire` naming an absent player | Ignored | `MatchState::Step` ignores commands naming players who have left |
| `Fire` above the rate limit | Dropped, warned once per episode | `3686456` |
| `RenderTick` outside the window | Clamped to the window | This stage |
| Target has no sample at that instant | Not a candidate | This stage |

## The client and the Sandbox

`MatchClient` gains `Fire()`, which stamps the message with its current tick and the
instant `PoseOf` is currently rendering remotes at, and a handler for `ShotResolved`.

**Middle mouse fires.** Left and right are already bound to break and place, and the
binding matters more than ergonomics here: scripted verification can drive the mouse but
**not** the keyboard, so a fire bound to a key would make this the one stage nobody can
screenshot. Middle mouse also leaves both existing edit paths byte-for-byte untouched,
which keeps the Stage 2 and 3 acceptance probes valid. A real binding is a configuration
question, and configuration is already on the roadmap's "let the game pull these" list.

**What is drawn, and when.** On click, a `DebugDraw` tracer from the eye to whatever the
local raycast hit, immediately — the local trace. The hit marker, the impact and the
health change appear only on `ShotResolved`. The split is the point: the shot *feels*
instant, but nothing that could be retracted is ever shown.

**`DebugFont` needs its missing letters.** `Order` is currently
`"0123456789-.: ACDEFGNOPSTU"`, so `HEALTH`, `HP` and `HIT` all contain glyphs it cannot
draw — and an unsupported character renders as a **blank**, not an error, so a wrong HUD
label reads as a rendering bug. This is the third stage running in which the font has
silently eaten a label, so the missing letters go in rather than another workaround
around them.

## Prerequisites, all of which already exist

- `CharacterInput` carries `Pitch`, added in Stage 1 and documented at the time as
  "carried but not consumed by movement, deliberately... hitscan will need it."
- `VoxelRaycast::Cast` takes `solidOnly`, documented as "what an edit — or a future shot
  — wants."
- `MatchState::PlayerForWrite` exists for respawning.
- `DebugDraw` is callable from anywhere with no renderer reference, which is what lets a
  tracer be drawn from wherever the shot is resolved.
- `SimulatedTransport` delivers a whole-tick latency on a whole tick as of Stage 3, so a
  test can reason about which tick a shot landed on.

Nothing here needs to be built first. That is four stages of groundwork arriving at
once, and it is worth noticing that every one of those pieces was added because someone
wrote down what it would later be for.

## Testing strategy

### The acceptance oracle

At 100 ms RTT with the target strafing, the shooter aims at exactly where **`PoseOf`
says the target is on its own screen** — the same function that renders it — fires, and
the server must report a hit.

The mutation that must turn it red is disabling the rewind and resolving against present
state.

**This is this design's weakest claim, and it is named here the way Stage 3 named its
clean-link zero-corrections gate.** It follows from the design and has never been
observed. Per the lesson that cost six defects in one plan, *this document does not
assert that the mutation turns the test red* — that is a prediction to be verified by
running it, and if the test will not go red, the gate is wrong before the code is.

### The measured number

Hit rate over 200 shots at 0 / 100 / 150 / 300 ms RTT, each run twice — rewind on and
rewind off. The rewind-off column is not decoration: it is the contrast that proves the
mechanism did something rather than the test being easy. The 300 ms row is above the
250 ms cap and is expected to be worse; how much worse is the number that tells us
whether the cap was set sensibly.

### The rest

- **Clamp:** a `Fire` claiming a 100-tick-old instant resolves identically to one
  claiming 15.
- **No record:** a shot rewinding past a respawn, or past a join, cannot hit that player.
- **`HitboxHistory`:** ring wrap, fractional lerp against a hand-computed oracle,
  eviction past 16.
- **`ResolveShot`:** terrain occludes a player behind it, a tie goes to terrain, the
  shooter is excluded, a miss reports the range endpoint.
- **Rate limit:** a second shot fired within ten ticks of the first is dropped, and a
  client holding the button down warns once per episode rather than once per dropped
  shot.
- **Single-player unchanged:** `POS 240.500000 26.900099 300.500000` and
  `FACES 1927774`, the latter sampled at `PendingCount() == 0` and never at a fixed frame
  count.
- **Three processes:** `Server.exe` plus two `Sandbox.exe --connect --latency 150`, one
  strafing, one shooting, reporting a hit rate.

Every new invariant test gets an attempt to make it fail before it is trusted. Stage 1's
determinism test and Stage 2's `Serial` tie-break both looked solid and were not, and
both were caught by an implementer trying to falsify them and stopping when they would
not go red.

## Explicitly out of scope

- **Rewinding terrain.** Someone digging away the wall you were shot through is a real
  case, and `ApplyBlockEdit` already returns inverses, so a per-tick inverse log would
  make it possible. It buys a corner case for the cost of a second history structure.
  Deferred, not solved.
- **Predicted terrain edits and edit rollback.** Still the arc's other open risk, still
  untouched by this stage.
- **Teams, spawn sides, friendly fire, scoring, weapon variety, damage falloff,
  reloading, recoil, spread.** All additive on a server that can answer honestly, which
  is what this stage builds.
- **Respawn timers and a death state on the wire.** Neither proves anything about a
  rewind.
- **Anti-cheat beyond the clamp and the rate limit.** The server being authoritative is
  still the whole model.
- **The unbounded edit log**, which this stage does not make worse.

## Risks

- **The oracle gate may not go red under its mutation.** Named above. If prediction and
  rewind happen to agree closely enough at 100 ms that a present-state resolution still
  hits, the test proves nothing and needs a faster target or a longer range before the
  stage can be trusted. Find this out before building on it.
- **A fractional rewind that is subtly off is invisible.** A half-tick error still hits
  most of the time. This is why the oracle compares against `PoseOf` itself rather than
  against a hand-written expectation of where the target "should" be — the two lerps are
  either the same computation or they are not.
- **The 250 ms cap is a guess dressed as a number.** It is derived from the link this
  arc targets, not from play. The measured hit-rate table is what turns it into a
  decision.
- **`ShotResolved` is reliable and unordered against snapshots.** A hit marker can
  therefore arrive a tick before or after the snapshot carrying the health change. Both
  orders must look correct on screen.

## Shipped 2026-09-12

_All twelve tasks, from Task 1's `0a6121c` onward; the suite went 416 -> 458. The gate's numbers below
were first recorded 2026-09-10 by `Tests/src/LagCompensationTests.cpp` and re-measured
2026-09-12, after the history off-by-one recorded at the end of "What the numbers say" was
fixed. The application run, the oracle's mutation, and what turned out differently follow
the numbers._

**The harness.** A `MatchServer` and two `MatchClient`s over `SimulatedTransport`, seed 1,
5% loss, no jitter, on a 64 x 64 floor. The shooter walks fifteen blocks off the spawn and
then stands still; the target strafes five blocks each way across the line of sight, at
walk speed, for the whole run. Each shot takes the pose `PoseOf` reports for the alpha the
frame is rendering, turns it into a yaw and a pitch from the shooter's own predicted eye,
and hands the same alpha to `Fire`. Shots are 30 ticks apart, not the weapon's minimum of
10 — see "What ran differently" below. Every hit is a `ShotResolved` ruling that came back
over the wire.

**The gate.** 100 ms RTT, 5% loss: **60 of 60 shots hit**, across 20 deaths and ten strafe
reversals. Largest disagreement between the eye the shot was aimed from and the eye the
server fired it from over the whole run: **0.000 blocks**. And the box the server rewound to
sat **0.000 blocks** from the pose the shooter drew, over the 59 of those shots whose rewind
fit inside the window — checked, not inferred from the hits, for the reason the off-by-one
below gives.

**The table.** 200 shots a row, 5% loss, seed 1. The right-hand column is the same rays
resolved against the boxes the server holds when it handles the shot — the rewind switched
off and nothing else changed. The last column is the largest distance between the pose the
shooter drew and the box the server rebuilt, over the shots whose declared instant sat
inside the rewind window.

| Link                       | Rewound            | Rewind off       | Rewind error          |
| -------------------------- | ------------------ | ---------------- | --------------------- |
| 0 ms RTT                   | 200/200 (100.0%)   | 14/200 (7.0%)    | 0.000 over 200 shots  |
| 100 ms RTT                 | 200/200 (100.0%)   | 12/200 (6.0%)    | 0.000 over 188 shots  |
| 166.7 ms RTT               | **189/200 (94.5%)**| 1/200 (0.5%)     | 0.000 over 189 shots  |
| 300 ms RTT                 | 1/200 (0.5%)       | 1/200 (0.5%)     | none inside the window|
| 166.7 ms RTT, no loss      | 200/200 (100.0%)   | 1/200 (0.5%)     | 0.000 over 200 shots  |

With the off-by-one below put back, the first two columns come back identical shot for shot
and the last reads 0.083 on every row that has a shot in it.

166.7 ms rather than 150: 150 ms RTT is 4.5 ticks one way and every latency here has to be
a whole tick multiple. Stage 3 substituted the same number for the same reason.

### What the numbers say

**The rewind depth is `2L + InterpolationDelayTicks - 1 - alpha` ticks**, for a one-way
latency of `L` ticks: `L` for the snapshot to arrive, `L` for the `Fire` to come back, six
because `PoseOf` draws that far behind the newest snapshot, and one back because the server
handles a shot before it steps. That is 7, 11, 15 and 23 ticks for the four rows, against a
`MaxRewindTicks` of 15 — 7 and not 5 on the first, because the harness cannot deliver a
packet sooner than the next tick, so a zero-latency link still costs one each way. The
server's own logs agree: shots that were not retransmitted asked for 6.25–7, 10.25–11,
14.25–15 and 22.25–23 ticks, the spread being alpha. It was also checked against the run: at the
166.7 ms row, the distance between the pose the client drew and the server's live position
on the tick a shot fired then is handled measured 1.25 blocks at alpha 0 - 15 ticks at walk
speed.

**The 166.7 ms row measures 94.5%, and the 95% promise is narrowed to the shots the window
can cover.** The same latency with no loss lands every one of 200 shots, so the rewind is
exact at that depth and what runs out is the cap. `SimulatedTransport` models the loss of a
reliable packet as a retransmission costing one extra round trip, and `Fire` is reliable, so
the ~5% of shots whose `Fire` is lost arrive ten ticks late and need a depth of 24.25–25.
They are clamped to 15, resolve against a box 0.70–0.83 blocks further along, and miss — 11
of 200 against an expected 10, and all 11 are retransmissions. The same thing happens at
100 ms and does not cost a hit: a retransmitted shot there needs 17 and is clamped by 2
ticks, which is 0.167 blocks, still inside the 0.3-block half width of the box.

This row was held open until the off-by-one below was fixed, in case it was costing hits
too. It was not. **The promise, as decided 2026-09-12:** at every latency up to the cap,
every shot whose rewind fits inside the window lands exactly where the shooter drew the
target, and 95% of all shots hit; a shot retransmitted past the window is an expected miss,
which on this row puts the floor at 185 of 200. Raising `MaxRewindTicks` to 25 would buy
those shots back and widen the "shot behind cover" window to about 417 ms for every shot to
do it, and was decided against.

**The 250 ms cap is worth less than it sounds.** Six of its fifteen ticks are spent on the
interpolation delay before any latency at all, so on a clean link the cap covers RTT up to
exactly 10 ticks — 166.7 ms, with nothing left over. That is the link this arc targets, and
it fits with zero margin. Under 5% loss a retransmitted shot stays inside the cap only up
to about 66.7 ms RTT; between there and 166.7 ms it survives on the width of the hitbox
rather than on the window.

**Above the cap it is a cliff, not a slope.** The 300 ms row is 8 ticks over, which is 0.67
blocks at walk speed against a box 0.3 blocks wide — so the answer is not "degraded" but
"gone": 0.5%, identical to the rewind-off column. Whether the cap is set right is therefore
a question about 150-200 ms links, because at 300 ms the mechanism does not partially work.

**The rewind landed one tick later than the pose the shooter aimed at, and that is fixed.**
Measured, not inferred: with latency and loss held so the declared instant could be computed
from `MatchClient::ServerTick()` alone, the centre of the box `MatchServer::History()`
rebuilt at the instant the client declared sat **1.000 tick further along the target's
travel** than the pose `PoseOf` drew at that same instant — 0.083 blocks at walk speed, 28%
of the box's half width. `MatchServer::Step` recorded history under `m_Match.Tick() - 1`
while `SendSnapshots` labels the same position `m_Match.Tick()`, and the client's
interpolation timeline is the snapshot's.

Fixed 2026-09-12 by recording under `m_Match.Tick()`: the wire's numbering is the authority,
because it is the only one the client ever sees. **It cost no hits anywhere in the table**,
which is why the table could not find it. Fixed and unfixed, every row is identical shot for
shot: 0.083 blocks stays inside the box, and the rewind depths do not move either, because
the clamp compares two numbers off the wire and the history's labels never enter it. What
the fix changes is where a shot inside the window lands, and that is what the rewind-error
column now checks on every run. Task 5's test compared the history against `MatchState`'s
own tick counter — the same convention on both sides — and passed with the bug present. Its
replacements read the snapshot the client actually receives (`MatchServerTests`) and the box
the server rebuilds as it receives each `Fire` (`LagCompensationTests`), and both were seen
to go red with the bug put back.

### What ran differently from the plan

- **Shots are 30 ticks apart, not the weapon's 10-tick minimum.** A retransmitted `Fire`
  arrives one round trip late, but the reliable channel is sequenced rather than spaced, so
  the next shot is not delayed with it and lands 2L ticks closer behind. At 20 ticks apart
  that pushed 15 of 200 shots on the 300 ms row inside the fire rate, where the server
  dropped them: a rate limiter's number, not a rewind's. Thirty is the deepest
  retransmission here (18 ticks) plus the weapon's limit plus slack.
- **The shooter holds fire for 40 ticks after each respawn.** A death forgets the target's
  history and teleports it, and both reach the shooter L+6 ticks late, so a shot fired
  sooner declares an instant the server has deliberately thrown away. Those shots measure
  the respawn rule, not the rewind.
- **The gate's contrast is 60 shots rather than 200**, at 100 ms and 5% loss: rewound 60/60
  (100.0%), rewind off 5/60 (8.3%).

### The application

`Server.exe` plus two `Sandbox.exe --connect 127.0.0.1 --latency 150` clients, played by
hand for about six minutes on 2026-09-12: one player walked off the shared spawn and
strafed, the other aimed and middle-clicked. Confirmed by eye: the tracer draws the moment
the button goes down; the impact marker and `HIT` arrive about a round trip later; fired
slowly, the target's `HEALTH` reads 100, 66, 32, and the third hit shows `KILLED` on the
shooter and puts the target back on the spawn at 100. Fired quickly, a kill *looks* like two
hits, because `HIT` stays up for 45 ticks while the weapon allows a shot every 10 — the
readout, not the damage.

The two clients reconciled 21,590 and 21,515 snapshots and logged **12 and 41 corrections**
(0.56 and 1.91 per 1,000), mean 1.80 and 1.12 blocks, maximum 10.76 and 9.81. Stage 3's
run had zero on both sides. This one is not the same experiment: every kill teleports the
victim to the spawn, and its client, which predicted it standing where it died, can only be
corrected there. Maxima of ten blocks fit that exactly.

**Attributed the same day, in a second run instrumented for it.** Every kill, every ruling
each client received, every snapshot where a client's own health rose, every correction,
every tick the server found a player's input queue empty, and every frame that dropped
ticks was logged. Of 11 kills, 8 hit a victim standing exactly on the spawn point, freshly
respawned, and respawning there again moved nobody; the other 3 produced exactly 3
corrections, of 9.1, 27.4 and 52.6 blocks. The remaining **21 corrections were all one
player's, in one 30-second stretch, 0.2 to 2 blocks and almost all vertical, while that
player jumped and placed blocks as fast as they could** — the player described it as
teleporting between blocks. Edits are not predicted: the server applies a placed block
before its next step, and the placing client's world gains it only when EditApplied
arrives a round trip later, so for that round trip the client predicts jumps and landings
against a world without the block. 1,018 starved server ticks and every dropped frame,
including a 204 ms stall, produced no correction at all. A stale swim state was ruled out
by reading the step: `CharacterController::Step` recomputes both fluid flags from the world
before it uses them, so a correction cannot leave them wrong.

Single-player is unchanged: `POS 240.500000 26.900099 300.500000` and `FACES 1927774` once
meshing settles, and a scripted middle click draws a tracer without moving the player or
changing the mesh. The first attempt at that check read different numbers, because the
freshly launched window had keyboard focus; the same binary run again read the values
exactly.

### Whether the acceptance oracle's mutation turned it red

**Yes — and it was not enough.** The design declined to assert that disabling the rewind
would fail the gate. It does, decisively: the same 60 rays resolved against present-state
boxes land 5 (8.3%) against the rewind's 60, and 200-shot rows go 14/12/1 against
200/200/189. Task 3's oracle, with no network in it, fails the same way.

But the gate passed, 60 of 60, while every rewind in the game was a whole tick short. Hits
cannot see an error smaller than the target: one tick at walk speed is 0.083 blocks inside
a 0.3-block half width, and the table came back identical shot for shot with the bug fixed
and with it put back. The bug was found by building the gate, not by the gate — and Task 5's
own test, which had a genuine mutation proof, pinned the wrong tick numbering because it
compared the history against the server's own counter instead of the snapshot the client
reads. The oracle that now catches it measures *where* each shot is resolved, against the
pose the shooter drew. **A test's oracle has to be what the consumer sees, and a pass rate
is not a measurement of accuracy.**

### What turned out differently from the design

- **The history filed every position one tick early.** `MatchState::Step` increments its
  tick after stepping, so the server recorded under `Tick() - 1` a position its snapshot
  labels `Tick()`. Fixed by recording under the wire's number, the only one a client has.
- **The 95% floor is narrowed, not met, at 166.7 ms under loss.** See "What the numbers
  say". Every shot whose rewind fits the window lands exactly; a retransmitted `Fire` needs
  25 ticks against a 15-tick cap. Raising the cap would widen the shot-behind-cover window
  for every shot and was decided against.
- **The 250 ms cap has no margin at the link it was chosen for.** Six of its fifteen ticks
  are the interpolation delay, so a clean 166.7 ms link needs exactly fifteen. With the fix,
  the history's oldest sample is exactly the clamp's floor — valid, with nothing spare.
- **`FireMessage::ClientTick` is written and never read** anywhere in the stage.
- **The Sandbox refreshes the input just before `Fire`.** `Fire` sends the input's yaw and
  pitch, which the last fixed step set up to a whole step earlier, and the mouse moves
  between steps.
- **The tracer starts beside the eye, not at it.** Drawn from the eye along the view ray,
  it projects onto one point under the crosshair and the shooter never sees it. The shot
  itself still leaves from the eye.
- **Tracer and marker lifetimes are ticks, not frames.** Debug renders at 144 fps; "a few
  frames" is about 20 ms.
- **The font test the plan specified checked the wrong words.** Its list was PING, RTT and
  PLAYERS, which the HUD has never drawn; the test checks the labels the readout actually
  uses. A count check alone cannot catch a duplicated glyph, so the table is also checked
  for distinct glyphs.
- **`LocalHealth` had no test** until the HUD came to depend on it.

### Still open

- Predicted terrain edits, and the rollback risk: undoing a rejected edit can invalidate
  predicted movement, because the world the character collided against changed.
- The edit log grows without bound.
- The session death after about six seconds under `--loss 80/90`, still undiagnosed.
- A player's own edits are not predicted, and it shows: placing blocks while jumping at
  150 ms produced 21 visible corrections in 30 seconds. Predicting them is the open
  terrain-edit risk above, now with a measured cost.
- The gate's rewind-off column resolves a retransmitted shot at its scheduled tick rather
  than the tick the server really handled it — about 5% of shots. The test now knows the
  real moment and could use it.
