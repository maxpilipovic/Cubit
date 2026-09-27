# Match rules: capture the intel — design (roadmap D1, with B3a and D5)

**Date:** 2026-09-27. **Status:** approved in conversation, section by section; this
records it. An implementation plan follows in `docs/superpowers/plans/`.

## Problem

Cubit has players, shooting, digging and building, but no match. There are no teams:
everybody spawns at one authored column (D5). There is no match state, no objective and
no scoreboard. A kill teleports the victim straight back to that column on the same tick.
The scope doc asks for:

| Item | What it asks for |
|---|---|
| GAM-01 | Teams: players join a valid team and spawn accordingly, with minimal team UI and spawn ownership. |
| GAM-02 | Spawn areas that still work after terrain edits. |
| GAM-03 | A scoreboard of kills, deaths, teams and score; a simple overlay is enough. |
| GAM-04 | Warmup, active and end-of-match states, progressed by the server. |
| GAM-05 | One real win condition. |
| PLY-06, PLY-08 | Respawn hooks, a respawn timer, and spawn protection. |
| NET-07 | Join-in-progress state sync. |

## Decisions

Each of these was asked and answered in conversation.

- **The objective is capture the intel.** It is Ace of Spades' mode. It uses the forts
  the map already has, and it makes digging and building matter to winning. Territory
  control and tickets were the alternatives.
- **Match rules live in the game, behind an engine hook.** The engine's `MatchServer`
  takes a game-supplied `GameMode` and calls it at fixed moments. The mode's state
  crosses the wire as bytes the engine carries without reading. This keeps B8's rule
  that the engine knows no game rules. The rejected alternatives were building teams
  into the engine, and a game-side wrapper with its own message stream, which would have
  two streams to keep ordered against each other.
- **Teams:** a joiner is auto-balanced onto the smaller team, and F2 asks to switch.
  No menu, so nothing is needed from B4a.
- **Match flow:** warmup until each team has a player, then a countdown. The first team
  to 3 captures wins, with a 20-minute limit. The result shows, then the same map resets.
- **The intel on its carrier's death:** it drops where they fell. A touch by the owning
  team, or 30 s untouched, sends it home.
- **Out of scope here:** D4 (forts that scale with the map). On battlefield512 the forts
  are about 495 blocks apart, so a capture run takes about 100 s: long, but playable.

## Stage 1 — Engine

### `GameMode` and `GameModeHost` — `Cubit/include/Cubit/Net/GameMode.h`

```cpp
//What a game mode may ask of the server. MatchServer implements it; a test
//implements it with no network at all.
class GameModeHost
{
public:
    virtual std::uint64_t Tick() const = 0;
    virtual const MatchState& Match() const = 0;   //positions, the world, the roster
    virtual bool IsAlive(PlayerId player) const = 0;
    virtual void Respawn(PlayerId player, const glm::vec3& position) = 0;
    virtual void Kill(PlayerId player) = 0;        //dead, with no kill ruled: a team switch
    virtual void ResetWorld() = 0;                 //every edited cell back to the map's block
};

class GameMode
{
public:
    virtual ~GameMode() = default;
    virtual void OnJoin(GameModeHost& host, PlayerId player) = 0;
    virtual void OnLeave(GameModeHost& host, PlayerId player) = 0;
    virtual bool CanDamage(const GameModeHost& host, PlayerId shooter, PlayerId victim) const = 0;
    virtual void OnKilled(GameModeHost& host, PlayerId victim, PlayerId killer) = 0;
    virtual void OnCommand(GameModeHost& host, PlayerId player, std::span<const std::uint8_t> bytes) = 0;
    virtual void Step(GameModeHost& host) = 0;     //once per tick, after the players step
    virtual bool TakeStateChanged() = 0;           //true once after the state changed
    virtual std::vector<std::uint8_t> EncodeState() const = 0;
};
```

`MatchServer`'s constructor gains a `GameMode*` parameter, defaulting to null. **Null keeps
today's behaviour exactly:** a joiner appears at the constructor's spawn, and a kill
respawns the victim there on the same tick. The about 105 existing construction sites and
every existing test are unchanged.

### Dead is a state

- A player the rules kill becomes **dead**. `GameMode::OnKilled` is called, and nothing
  else happens until the mode calls `Respawn`.
- A dead player is not stepped, cannot be shot, is `Forget`-ed from the hitbox history
  (as today), and has their edits and shots refused.
- Their inputs are still consumed and acknowledged, so `LastInputTick` keeps moving and
  the client's queue does not back up.
- They stay in the roster; the snapshot carries an **alive** bit.
- With a mode, a joiner is added dead, and `OnJoin` decides when and where they first
  appear. The CTF mode respawns them at once.
- `Respawn` teleports, clears vertical velocity, restores `StartingHealth`, and marks the
  player alive.

### Game state and commands on the wire — protocol 8

| Change | Detail |
|---|---|
| `GameState` = 10 | Server to client, reliable: the mode's bytes, sent to everyone on the tick `TakeStateChanged` says so. At most 4 KB; the encoder refuses anything larger, loudly. |
| `GameCommand` = 11 | Client to server, reliable: bytes for `OnCommand`. At most 64; the server drops anything larger. |
| `Welcome` | Gains the current state bytes, so a late joiner starts from the match as it stands (NET-07). |
| Snapshot flag byte | Bit 2 is alive. The entry stays 37 bytes. |

### `MatchServer::ResetWorld()`

Every cell the edit log touched goes back to its map block, taken from the `m_MapBlock`
it already keeps. This is applied through the existing batch path, so it relights and
broadcasts as one operation. Afterwards the edit log is empty, so a joiner's `Welcome`
carries no edits.

### `MatchClient`

| Addition | What it does |
|---|---|
| `IsAlive(player)` | From the snapshot bit. |
| `GameState()`, `GameStateSerial()` | The latest bytes from `Welcome` or `GameState`, and a counter that moves when they change. |
| `SendCommand(bytes)` | Sends a `GameCommand`. |

**While the local player is dead:**
- The client does not predict. It keeps sending input, so the server's acknowledgements
  keep moving.
- The respawn snapshot is applied as a teleport, with no correction smoothing and nothing
  counted as a correction.
- A dead remote player is not drawn.

### `TerrainGen::FortCentres(size)`

This exposes the two fort centres the generator already computes (x at `FortEdgeOffset`
and at its mirror, z at the middle), so the game places bases where the forts actually
are rather than on a second copy of the arithmetic.

### Tests (engine suite)

A test mode records every call. The cases:

- with no mode, everything is as before (the existing suite);
- a kill makes the victim dead: not stepped, not in the history, shots and edits refused;
- `Respawn` brings the player back where the mode asked;
- `CanDamage = false` means no damage and no kill;
- a command reaches `OnCommand` with its bytes, and an oversized one does not;
- a state change reaches every client once, and a late joiner's `Welcome` carries it;
- `ResetWorld` restores every edited cell, and a later joiner sees the map pristine;
- the alive bit round-trips, and the protocol version is 8;
- a dead client's inputs are still acknowledged, so there are no corrections at respawn.

## Stage 2 — The rules: `CtfMode` (game side)

Files: `game/Game/src/CtfRules.h`, `CtfState.h/.cpp` (the state and its encoding, which
the client decodes too), and `CtfMode.h/.cpp`.

### `CtfRules` (tuning, not logic)

| Rule | Default |
|---|---|
| Captures to win | 3 |
| Time limit | 20 min |
| Warmup countdown | 10 s |
| End screen | 15 s |
| Respawn delay | 5 s |
| Spawn protection | 2 s |
| Intel pickup radius | 1.5 m |
| Base (capture) radius | 4 blocks horizontal, 4 vertical, around own intel home |
| Dropped intel returns after | 30 s |
| Event history kept | 8 |

All in ticks internally, at the 60 Hz step.

### Teams

- **Blue** owns the fort at the low-x end, **Green** the high-x end.
- A joiner goes on the smaller team; ties are broken by a seeded generator, so tests are
  deterministic.
- **F2** sends a switch command. It is granted only if the counts afterwards differ by at
  most one. A granted switch calls `Kill` (no death counted), drops a carried intel, and
  the player respawns at the new base after the usual delay.

### Spawning

`FindSpawn` near the team's fort centre is run against the current world at every
respawn, so it survives digging and building (GAM-02, and D5 with it). If it finds
nothing, the search widens to the map centre, and it logs.

Spawn protection is implemented as `CanDamage` refusing damage to a player for 2 s after
they spawn.

Friendly fire is off: `CanDamage` is false between teammates.

### Phases

**Warmup**
- Runs until both teams have a player, then counts down 10 s.
- The countdown restarts if a team empties.
- Kills are not counted, and the intel cannot be picked up.

**Active**
- The first team to 3 captures wins.
- At 20 minutes the team with more captures wins; equal captures is a draw.
- If a team empties, the match goes on, and that team simply cannot score.

**End**
- The result shows for 15 s.
- Then, in order: `ResetWorld()`, scores and stats cleared, both intels home, everyone
  killed and respawned at their base at once, and back to warmup.

### The intel

- **Home spot:** the air cell above the highest solid block at each fort centre, found
  when the phase goes active, and again after every reset.
- **Pickup:** an alive enemy within 1.5 m of the intel, during the active phase only.
- **Carried:** it moves with the carrier.
- **Capture:** the carrier comes within the base radius of their own intel's home spot.
  This scores a capture for the team and one for the carrier, and sends the enemy intel
  home. Your own intel does not need to be at home.
- **Drop:** the carrier dies, leaves or switches, and the intel falls at their feet. If
  that is below the world, it goes home instead.
- **Dropped:** an owning-team touch sends it home at once, an enemy touch picks it up,
  and 30 s untouched sends it home.

### Scoring

- Team captures, and per player: kills, deaths and captures.
- Counted during the active phase only.
- A kill by the victim's own team cannot happen, because friendly fire is off.

### The state bytes (`CtfState`)

Written with the engine's `ByteWriter` and read with `ByteReader`, versioned by its own
first byte:

- the phase, and the tick it ends;
- each team's captures;
- per player: id, team, kills, deaths, captures, alive, and the tick they respawn at;
- per intel: Home, Carried by whom, or Dropped where, plus its home spot;
- the event history: serial, kind (Captured, PickedUp, Returned, MatchWon, Draw), team,
  and player.

The client reads the event history with the same serial reader the audio work built
(`NewEvents`).

### Tests (GameTests), on a fake `GameModeHost`

- **Balancing:** balancing on join; switch granted and refused; a switch drops the intel.
- **Spawning:** respawn waits 5 s and lands at your own base; the spawn search still
  works after the base is dug out; spawn protection; no friendly fire.
- **Phases:** warmup counts nothing; the countdown starts and restarts; first to 3 wins;
  the time limit, including a draw; end resets the world, the scores and the intels.
- **Intel:** pickup only by the enemy and only when active; carry, capture, and the
  scores it gives; drop on death; owner touch returns it; timeout returns it; a drop
  below the world returns it.
- **Encoding:** state bytes round-trip; unknown versions and short input are refused.

## Stage 3 — What the player sees and hears (client)

- **Team colours (B3a).**
  - *Changed from the in-chat design, which said a tint uniform plus a palette mask.*
    The player model is meshed twice at load, once per team, with palette entry 2 (the
    torso) set to the team colour. `ModelMesher` already bakes palette colours, so this
    needs no shader or engine change and costs nothing per draw.
  - A remote player is drawn with their team's mesh. A dead player is not drawn.
- **The intels.** A 0.6 m cube in the team colour, meshed once from a code-made model. It
  is drawn at its home spot, where it was dropped, or on the carrier's back.
- **HUD** (`GameHudLayer`, real font):
  - Top centre: `BLUE 1 - 0 GREEN` and the phase line: `WARMUP`, `STARTS IN 7`, the
    `12:43` countdown, `BLUE WINS` or `DRAW`.
  - Your team under the debug readout.
  - `YOU HAVE THE INTEL - RETURN TO BASE` while carrying.
  - `RESPAWN IN 3` while dead.
  - A feed of event lines (`Blue captured the intel`, `Green took your intel`, `Your
    intel was returned`), each kept for 5 s.
- **The scoreboard, while Tab is held.** Two columns, one per team; each player's kills,
  deaths and captures; sorted by captures and then kills; your row highlighted. Text and
  filled rectangles only.
- **While you are dead:** input stops and the camera stays where you fell until the
  respawn teleport.
- **Sounds:** new synthesised UI cues: your team captured, the enemy captured, your intel
  was taken, the intel was returned, match start, and match end. They are driven by the
  event history.
- **Keys:** F2 (switch team) and Tab (scoreboard), added to the README's controls.

### Tests (GameTests)

- The HUD's text for each phase and event, as pure functions in `game/Game/src`, so they
  are testable.
- The scoreboard's rows and their order.
- The team palette swap changes only entry 2.

### Checked by running

- A server and two clients on one machine, screenshotted in warmup, active and end, with
  an intel carried, and while dead.
- Scripted input does not reach the window, so temporary triggers in code force a phase,
  a pickup and a death on cue, and are removed before the commit.
- The user plays a real match, by hand.

## Error handling

- **State too large to encode:** refused and logged. The previous state stays on the
  wire, so a mode bug cannot take the protocol down.
- **Undecodable state bytes on the client:** logged once. The HUD shows `NO MATCH STATE`
  rather than stale numbers.
- **A command from a dead or unknown player:** it still reaches the mode, which decides.
  The CTF mode accepts a team switch from the dead.
- **No spawn found near a base:** the search widens to the map centre and logs. A player
  is never left unspawned.

## Out of scope

- D4 (forts that scale with the map).
- Map rotation (GAM-06).
- Menus (POL-03, B4a).
- Kill cams.
- Team chat.
- A real crouch pose or intel model (content).
