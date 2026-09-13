# Cubit Networking Stage 5 — Predicted Edits

_Designed 2026-09-12. Resume cold from this document: it records what was measured, what
was decided and by whom, and what is deliberately left out._

**Glossary.** *Tick* — one fixed 1/60 s simulation step. *Client tick* — the placing
client's own tick numbering, the one its inputs are stamped with and the server echoes
back as `LastInputTick`. *Prediction* — the client simulating its own player (and now its
own edits) before the server has ruled. *Replay* — after a snapshot, resetting to the
server's state and re-simulating every input the server has not yet acknowledged.
*Correction* — a replay whose result differs from what was predicted by more than
`CorrectionThreshold` (0.15 blocks), which snaps the player on screen.

## Why

A player jumping and placing blocks as fast as they could, at 150 ms round trip, got
**21 corrections of 0.2 to 2 blocks in 30 seconds** and described it as teleporting
between blocks. Measured in the Stage 4 live run with per-link logging (see that spec's
"The application"). Every other source of correction was ruled out in the same run:
starved server ticks and dropped frames produced none.

The cause is structural. Edits are not predicted. The server applies a placed block before
its next step; the placing client's world gains that block only when `EditApplied` arrives
a round trip later. For that round trip the client predicts jumps and landings against a
world without the block, and every snapshot snaps it to where the server has it standing.

Stage 3 deferred predicted edits on purpose, so that two unproven mechanisms would not be
built at once. Reconciliation is now proven — zero corrections on a clean link, and three
per thousand ticks at 20% loss — so this is the stage that faces the deferred risk.

## What this stage delivers

At 150 ms, a player can pillar up by jumping and placing blocks under themselves, and dig
down by breaking the block underfoot, **with zero corrections**. A placed block appears the
instant it is clicked. An edit the rules forbid does nothing at all on the client that
tried it.

## Decisions already made

Every row below was chosen by the user on 2026-09-12, from options with trade-offs laid
out. They are not open.

| Decision | Choice | Why |
|---|---|---|
| Bar | Zero corrections from a player's own legal edits, measured | Turns "smooth" into a number, the way Stage 3 did for movement |
| Server rules | Validate reach and overlap | An edit placed inside a player traps them, and an unchecked reach lets anyone edit across the map |
| Approach | Edits ride the input stream (approach A below) | The only approach that puts the edit and the movement on one timeline |

## The three approaches, and why A

**A — edits ride the input stream (chosen).** Each input tick may carry at most one edit,
sent inside the same redundant three-input bundle as movement. The server validates and
applies that edit when it steps that tick, before anybody moves; the client does the same
in the same order. Both machines therefore simulate the same world at every step, and an
edit is protected against loss by exactly the redundancy movement already has.

**B — separate reliable edits, stamped with a tick.** The server would hold each edit until
it steps that tick. Rejected: the reliable and unreliable channels arrive independently, so
a retransmitted edit can reach the server after it has already stepped the tick, forcing a
late apply and a correction. It is Stage 3's convergence trap with two streams to align.

**C — Minecraft-style: predict, apply on arrival, acknowledge by sequence number.**
Rejected: it fails the bar in exactly the pillar-jump case. Minecraft gets away with it by
largely trusting the client's own position; Cubit's server is authoritative for movement.

## Protocol version 4

`ProtocolVersion` becomes 4. Message ids are never reused: `EditRequest = 5` is retired, not
recycled.

### `InputMessage` entries may carry an edit

Each entry gains a one-byte flag, and when the flag is set, one `BlockEdit` (12 bytes of
position, 2 of block id, as today). A three-input bundle with no edits grows from 61 to
**64 bytes**; each entry carrying an edit adds 14 more. An edit is therefore resent in the
three consecutive packets that carry its tick.

### `EditResult` (`EditResult = 9`, server to the editing client only, reliable)

`ClientTick` (u64), `Accepted` (u8), `Edit` (`BlockEdit`: the position, and **the block the
server now has there** — the requested block when accepted, the unchanged block when
refused). 24 bytes.

Reliable, because a lost refusal would leave a block on one client that the server never
had: a permanent desync rather than a correction.

### `EditApplied` goes to everyone except the editor

Unchanged in shape. The editor learns the outcome of its own edit from `EditResult`
instead, tagged with the tick it made the edit on.

### `EditRequest` is removed

An edit reaches the server only inside an input.

## The rules

One function in `Cubit/src/Voxel/`, called by the server when it applies an edit and by the
client before it predicts one — identical code, so identical answers for identical state.
`ReachDistance` (12) moves out of `Sandbox.cpp` to sit with it.

An edit is legal when all of these hold, evaluated against the state **at the start of the
step that carries it**:

1. **In bounds, and it changes something.** `ApplyBlockEdit`'s existing conditions.
2. **Reach.** The editor's eye — `Position() + EyeOffset` — is within `ReachDistance` of the
   nearest point of the target cell.
3. **No overlap, for a placement.** A non-air block's unit cell does not *strictly* overlap
   any player's box. Touching is allowed, which is what lets a player place the cell their
   feet are resting on top of. Breaking has no overlap rule.

## The server

When the server takes a client's input for tick T off its queue, it checks that tick's edit
against the rules, in player-id order across clients, and applies the legal ones. Only then
does the match step. A refused edit changes nothing, and the input's movement still applies.

Accepted edits are appended to the edit log that `Welcome` carries, as today. Every edit —
accepted or refused — produces one `EditResult` to its editor; accepted ones also produce
`EditApplied` to every other joined client.

## The client

### Predicting an edit

`MatchClient::RequestEdit` no longer sends anything. It queues the edit for the next input
tick: at most one edit per tick, further clicks waiting their turn, and a small cap (4)
dropping any beyond it.

On the step that takes an edit off that queue, the client runs the rules against the state
it is about to step from — its own box exactly, and other players at their most recent
snapshot positions.

- **Illegal:** the edit is dropped. Nothing is sent and nothing changes, the way Minecraft
  ignores a click it will not allow.
- **Legal:** the edit is applied for real — relit and remeshed, the cost a client pays today
  when `EditApplied` arrives — and recorded against that input with the value it replaced.
  Then the client steps.

### A confirmed layer underneath the predictions

The visible world is the server-confirmed world with this client's pending predicted edits
on top. When anything from the server changes a cell that carries a pending prediction —
another player's `EditApplied`, or an `EditResult` — it updates the recorded value *beneath*
the prediction, and the visible block stays the predicted one. A cell with no pending
prediction takes the change directly.

This is what makes conflicts converge: whatever order the server applied two players' edits
to one cell in, each client ends up with the server's final value once its own predictions
on that cell are resolved. It is also what stops a quick place-then-break from showing the
placed block again when the placement's result arrives after the break was predicted.

### Results

- **Accepted:** the pending edit is dropped; its block is confirmed.
- **Refused:** the pending edit is dropped and the cell's confirmed value becomes the
  server's block — which is what shows, unless a newer prediction on the same cell is still
  pending on top of it. The next snapshot corrects the player's movement if the missing
  block mattered. That correction is the stated cost of an illegal edit, which honest play
  does not produce.

### Replay

On a snapshot acknowledging client tick A:

1. Pending edits from ticks after A are undone, newest first, **as block writes only** — no
   relighting, nothing marked dirty.
2. The player is reset to the server's state, exactly as today.
3. Each unacknowledged tick is replayed in order: its edit is re-checked and re-applied in
   the same write-only way, then the player steps. The re-check covers **only the editor's
   own conditions** — bounds, reach, and overlap with the editor's own replayed box. Overlap
   with other players is checked once, at prediction, and not again: re-checking it against
   a newer snapshot could flip an edit the server will accept, undo it for a moment, and put
   it back when its result arrives — a flicker the server never caused.
4. The world ends exactly where it began, so nothing is relit and nothing remeshes. If a
   re-check changes an edit's outcome, that single edit becomes a real apply or undo.

Edits from ticks at or before A are left in place until their `EditResult` arrives, because a
reliable result can lag an unreliable snapshot.

The write-only path is safe because collision and the fluid checks read block ids, never
light. It must not be the ordinary `World::SetBlock`: that marks the chunk and its
neighbours dirty, and `docs/performance.md` measures an edit's remesh at about four chunks
× 6.4 ms. Replay runs on every snapshot — sixty times a second while edits are pending.

## The Sandbox

`OnMouseButtonPressed` keeps its raycast and calls `RequestEdit` as today. Single-player is
untouched: it still applies edits directly and keeps its undo stack.

## Testing strategy

Every gate runs in-process over `SimulatedTransport` at **166.7 ms RTT** (5 ticks each way):
150 ms is not a whole number of ticks, and Stage 3 substituted the same figure for the same
reason. **Each gate must be seen to fail against its named mutation before it is trusted** —
and, per Stage 4's lesson, its oracle must be what the consumer sees, not the mechanism's own
bookkeeping.

1. **Pillar gate.** A client jumps repeatedly and places a block under its feet at each apex,
   about 30 blocks up. **Zero corrections**, and every placed cell's block matches the
   server's. *Mutation:* the server applies edits on arrival, which is today's behaviour.
2. **Dig gate.** A client breaks the block underfoot repeatedly while moving. **Zero
   corrections.** *Mutation:* replay without undoing edits made on later ticks.
3. **Under loss.** The pillar run at 5% loss with jitter. The correction count is reported and
   expected to be zero: an edit is lost only when its whole input is, and then the server
   never steps that tick either.
4. **Replay does not remesh.** Across a pillar run, chunks are marked dirty only by real
   edits. *Mutation:* replay writes through `World::SetBlock`.
5. **Refusal.** A forced out-of-reach edit, and a placement into another player's box, each
   come back refused, and the client's world returns to the server's block.
6. **Rules.** Reach at exactly `ReachDistance`; overlap with the editor's own box and with
   another player's; touching allowed; no overlap rule for breaking.
7. **Conflicts.** "Two clients editing the same block on the same tick converge" is extended
   so both clients predict, and both still match the server. A quick place-then-break must
   never show the placed block again.
8. **Protocol version 4.** Round trips and truncation for `EditResult` and for input entries
   carrying an edit. "A three-input bundle is 61 bytes" is re-pinned to 64. `EditRequest` is
   removed from "Every message id the wire carries is recognised".
9. **Single-player unchanged.** `POS 240.500000 26.900099 300.500000` and `FACES 1927774`,
   sampled at `PendingCount() == 0`.
10. **Live.** `Server.exe` plus two `Sandbox.exe --connect --latency 150`, played by hand:
    pillar-jumping and digging for a minute gives zero corrections apart from respawns,
    confirmed with temporary per-correction logging, and the clients closed through their
    game windows so `NETSTATS` is written.

### Tests this stage replaces on purpose

- `WireOracleTests`: "An edit takes a round trip and is not applied locally first" pins the
  old contract. It is replaced by its opposite, not quietly deleted.
- `MatchServerTests`: "An applied edit reaches every joined client and is remembered for the
  next one" changes — the editor now receives `EditResult`, every other client `EditApplied`.
- `ProtocolTests`: "Edit messages round-trip and keep their own identity" loses
  `EditRequest`.

## Known limits, accepted and recorded

- **A placement right next to another player can mispredict.** The client checks overlap
  against where it last saw them, a snapshot old; the server checks where they are.
- **Other players' edits still arrive a round trip late**, and can still correct a player
  standing on or beside a block someone else just changed.
- **At most one edit per tick** — 60 a second.
- **The edit log still grows without bound.**

## Explicitly out of scope

- Predicting other players' edits.
- Rate limits on edits beyond one per tick, and any rule about which blocks may be placed or
  broken. Water is still unbreakable only because the Sandbox's ray passes through it.
- Rolling back terrain for shots. Lag compensation still rewinds players only.

## Risks

- **Replay's write-only path is the first place block ids and light can disagree on
  purpose.** Safe only while nothing replay runs reads light, and only because the world is
  restored before replay returns. A later change that reads light during simulation, or that
  returns from replay early, would break that silently.
- **The confirmed layer is new bookkeeping on every cell a prediction touches.** A result that
  arrives for a cell with no matching pending prediction — a duplicate, or one for an edit the
  client already dropped — must change nothing but the confirmed value.
- **The gate may pass while the live run does not**, as Stage 4's hit-rate gate passed with
  every rewind a tick short. The live check is part of acceptance, not a courtesy.
