# Match Rules: Capture the Intel — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task by task. Steps use
> checkbox (`- [ ]`) syntax for tracking. Record per-task deviations IN this plan, under the
> task, as B8's plan did.

**Goal:** Add a capture-the-intel match to Cubit: two teams, warmup, active and end phases,
an intel to steal, timed respawns and a scoreboard. The match rules live in the game, behind
a rule-free engine hook.

**Architecture:** The engine's `MatchServer` takes an optional game-supplied `GameMode`, which
it calls at join, leave, damage, kill, command and tick. Death becomes a real state that lasts
until the mode respawns the player. The mode's state crosses the wire as opaque bytes, in a
new `GameState` message and in `Welcome`. The game implements `CtfMode`, its encoded
`CtfState`, and the client's HUD, scoreboard, team colours, intel models and sounds.

**Tech Stack:** C++20, MSVC (VS 2026), premake5, doctest, OpenGL, ENet, miniaudio. Windows.

**Spec:** `docs/superpowers/specs/2026-09-27-match-rules-ctf-design.md`

## Global Constraints

- The engine (`Cubit/`) must never name a team, the intel, a phase, or anything else in
  `game/`. That is B8's rule.
- A `MatchServer` constructed without a mode behaves exactly as today. The existing tests,
  and the about 105 construction sites, stay unchanged and green.
- The protocol version becomes **8**. Snapshot entries stay **37 bytes**; the alive state is
  bit 2 of the existing flag byte.
- State bytes are at most **4096**; command bytes are at most **64**.
- All CTF numbers live in `CtfRules`, in ticks at 60 Hz (`FrameClock::FixedStepSeconds`).
- Commit and push after every task, on `master`. **No Claude co-author trailers or
  attribution** in commits.

### Build and test commands (used by every task)

- MSBuild: `"/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe"`
- Build everything and run both suites (post-build):
  `"$MSB" Cubit.slnx -m -v:m -nologo -p:Configuration=Debug -p:Platform=x64 2>&1 | grep -E " error |error C|test cases:" | grep -v MESSAGE | sort -u`
- **A new .cpp or .h file needs `/c/dev/premake/premake5 vs2026` first.** Never use
  `GenerateProjects.bat`, which deletes `bin/`.
- Run one engine test:
  `./bin/Debug-windows-x86_64/Tests/Tests.exe -tc="*part of name*"`.
  Run one game test the same way with `GameTests/GameTests.exe`.
- **Always build the whole solution before running a test binary.** The post-build step is
  what copies `Cubit.dll` next to `Tests.exe`; building one project with it switched off
  tests an OLD DLL.
- doctest's reported line numbers are sometimes wrong. Identify failures by test name.
- `sed -i` on a CRLF file does not match a trailing `$`. Use the Edit tool for multi-line
  edits.

---

## Stage 1 — Engine

### Task 1: Dead is a state in `MatchState`

**Files:**
- Modify: `Cubit/include/Cubit/Voxel/MatchState.h`, `Cubit/src/Voxel/MatchState.cpp`
- Test: `Tests/src/MatchStateTests.cpp` (append)

**Interfaces:**
- Produces: `bool MatchState::IsAlive(PlayerId) const` (false for an unknown id) and
  `void MatchState::SetAlive(PlayerId, bool)`. `Step` and `StepPlayer` skip dead players, and
  `RemovePlayer` forgets the flag.

- [ ] **Step 1: Write the failing tests** (append to `Tests/src/MatchStateTests.cpp`)

```cpp
TEST_CASE("A dead player is not stepped, and is again once alive")
{
    World world(2, 2, 2);
    for (int z = 0; z < world.GetDepth(); ++z)
        for (int x = 0; x < world.GetWidth(); ++x)
            world.SetBlock(x, 0, z, BlockId{ 1 });

    MatchState match(std::move(world));
    const PlayerId player = match.AddPlayer(glm::vec3(8.0f, 10.0f, 8.0f));
    CHECK(match.IsAlive(player));

    match.SetAlive(player, false);
    CHECK_FALSE(match.IsAlive(player));

    //In the air: a stepped player would fall.
    match.Step({}, 1.0f / 60.0f);
    match.StepPlayer(player, CharacterInput{}, 1.0f / 60.0f);
    CHECK(match.Player(player).Position().y == doctest::Approx(10.0f));

    match.SetAlive(player, true);
    match.Step({}, 1.0f / 60.0f);
    CHECK(match.Player(player).Position().y < 10.0f);
}

TEST_CASE("An unknown player is not alive, and removing a dead one forgets it")
{
    MatchState match(World(1, 1, 1));
    CHECK_FALSE(match.IsAlive(42));

    const PlayerId player = match.AddPlayer(glm::vec3(4.0f));
    match.SetAlive(player, false);
    match.RemovePlayer(player);
    CHECK_FALSE(match.IsAlive(player));
}
```

- [ ] **Step 2: Build and confirm it fails.** The compile error is "IsAlive is not a member".

- [ ] **Step 3: Implement.** In `MatchState.h`, add `#include <set>`, and in the public section:

```cpp
    //A dead player keeps their place in the roster - their id, their last
    //position - but is not stepped: no gravity, no input. Only a game rule
    //(through the server's GameModeHost) kills or revives. Unknown ids are not alive.
    bool IsAlive(PlayerId player) const;
    void SetAlive(PlayerId player, bool alive);
```

Add the private member `std::set<PlayerId> m_Dead;`. In `MatchState.cpp`:

```cpp
bool MatchState::IsAlive(PlayerId player) const
{
    return HasPlayer(player) && m_Dead.count(player) == 0;
}

void MatchState::SetAlive(PlayerId player, bool alive)
{
    if (!HasPlayer(player))
        return;

    if (alive)
        m_Dead.erase(player);
    else
        m_Dead.insert(player);
}
```

In `Step`'s loop, before `entry.second.Step(...)`, add
`if (m_Dead.count(player) != 0) continue;`. In `StepPlayer`, add
`if (m_Dead.count(player) != 0) return;` after the find. In `RemovePlayer`, add
`m_Dead.erase(player);`.

- [ ] **Step 4: Build.** Both suites pass: 649 engine cases (647 + 2) and 44 game cases.

- [ ] **Step 5: Commit** — `git add -A && git commit -m "Let a player be dead: kept in the roster, not stepped" && git push`

---

### Task 2: Protocol 8 — the alive bit, `GameState`, `GameCommand`, and state in `Welcome`

**Files:**
- Modify: `Cubit/include/Cubit/Net/ByteWriter.h`, `Cubit/include/Cubit/Net/ByteReader.h`,
  `Cubit/include/Cubit/Net/Protocol.h`, `Cubit/src/Net/Protocol.cpp`
- Test: `Tests/src/ProtocolTests.cpp` (append)

**Interfaces:**
- Produces:
  - `ByteWriter::Blob(std::span<const std::uint8_t>)`, which writes a u16 length and the
    bytes, and `std::vector<std::uint8_t> ByteReader::Blob()`.
  - `PlayerSnapshot::Alive` (defaults to `true`).
  - `struct GameStateMessage { std::vector<std::uint8_t> Bytes; }` and
    `struct GameCommandMessage { std::vector<std::uint8_t> Bytes; }`.
  - `constexpr std::size_t MaxGameStateBytes = 4096;` and
    `constexpr std::size_t MaxGameCommandBytes = 64;`.
  - `MessageId::GameState = 10` and `MessageId::GameCommand = 11`.
  - `WelcomeMessage::GameState` (bytes).
  - `Encode`/`Decode` overloads for both new messages.
  - `ProtocolVersion == 8`.

- [ ] **Step 1: Write the failing tests** (append to `Tests/src/ProtocolTests.cpp`)

```cpp
TEST_CASE("The alive bit rides the snapshot flag byte without growing it")
{
    SnapshotMessage sent;
    sent.Tick = 9;
    for (int i = 0; i < 8; ++i)
    {
        PlayerSnapshot player;
        player.Player = static_cast<PlayerId>(i + 1);
        player.Grounded = (i & 1) != 0;
        player.Crouched = (i & 2) != 0;
        player.Alive = (i & 4) != 0;
        sent.Players.push_back(player);
    }

    CHECK(Encode(sent).size() == Encode(SnapshotMessage{ 9, {} }).size() + 8 * 37);

    SnapshotMessage received;
    REQUIRE(Decode(Encode(sent), received));
    for (int i = 0; i < 8; ++i)
    {
        CAPTURE(i);
        CHECK(received.Players[i].Grounded == sent.Players[i].Grounded);
        CHECK(received.Players[i].Crouched == sent.Players[i].Crouched);
        CHECK(received.Players[i].Alive == sent.Players[i].Alive);
    }
}

TEST_CASE("Game state and game commands round-trip their bytes untouched")
{
    GameStateMessage state;
    state.Bytes = { 0, 1, 2, 250, 255 };
    GameStateMessage stateBack;
    REQUIRE(Decode(Encode(state), stateBack));
    CHECK(stateBack.Bytes == state.Bytes);

    GameCommandMessage command;
    command.Bytes = { 1 };
    GameCommandMessage commandBack;
    REQUIRE(Decode(Encode(command), commandBack));
    CHECK(commandBack.Bytes == command.Bytes);

    //Empty is legal: a mode with nothing to say.
    GameStateMessage empty;
    GameStateMessage emptyBack;
    REQUIRE(Decode(Encode(empty), emptyBack));
    CHECK(emptyBack.Bytes.empty());
}

TEST_CASE("An oversized game command is refused on decode")
{
    GameCommandMessage command;
    command.Bytes.assign(MaxGameCommandBytes + 1, 7);
    GameCommandMessage back;
    CHECK_FALSE(Decode(Encode(command), back));
}

TEST_CASE("Welcome carries the game state for a late joiner")
{
    WelcomeMessage sent;
    sent.You = 3;
    sent.MapName = "m.vox";
    sent.GameState = { 9, 8, 7 };

    WelcomeMessage back;
    REQUIRE(Decode(Encode(sent), back));
    CHECK(back.GameState == sent.GameState);
}

TEST_CASE("The protocol is version 8")
{
    CHECK(ProtocolVersion == 8u);
}
```

Also extend the existing "Every message truncated at every length is refused without
crashing" case. Add a `GameStateMessage` with 3 bytes and a `GameCommandMessage` with 1 byte
to whatever list of encoded messages that case iterates, following its existing pattern.

- [ ] **Step 2: Build and confirm the new cases fail to compile.**

- [ ] **Step 3: Implement.**

`ByteWriter.h`, beside the string writer:

```cpp
    //A length-prefixed run of bytes the protocol carries without reading - a
    //game mode's state or command. u16 length: nothing that uses it may exceed
    //65535, and every user has a far smaller cap of its own.
    void Blob(std::span<const std::uint8_t> bytes)
    {
        U16(static_cast<std::uint16_t>(bytes.size()));
        m_Bytes.insert(m_Bytes.end(), bytes.begin(), bytes.end());
    }
```

`ByteReader.h`, beside the string reader. It uses the reader's existing sticky-failure
convention; read the file for the exact name of its bounds helper and member names:

```cpp
    std::vector<std::uint8_t> Blob()
    {
        const std::uint16_t length = U16();
        if (!m_Ok || Remaining() < length)
        {
            m_Ok = false;
            return {};
        }

        std::vector<std::uint8_t> out(m_Bytes.begin() + m_Offset, m_Bytes.begin() + m_Offset + length);
        m_Offset += length;
        return out;
    }
```

(Add `#include <span>` and `#include <vector>` where they are missing.)

`Protocol.h`:
- Add `GameState = 10, GameCommand = 11` to `MessageId`.
- Add the version comment `//8: game modes. GameState and GameCommand carry a game's own bytes, Welcome carries the current state, and the snapshot's flag byte gains an alive bit. On the per-tick path.`
  and set `ProtocolVersion = 8`.
- Add `bool Alive = true;` to `PlayerSnapshot`, with a comment: dead players stay in the
  roster, and a client hides them and does not predict its own while dead.
- Add `std::vector<std::uint8_t> GameState;` to `WelcomeMessage`.
- Add the two constants and the two structs, plus Encode/Decode declarations beside the
  others.

`Protocol.cpp`:
- Add `constexpr std::uint8_t SnapshotAliveBit = 1u << 2;`.
- Snapshot encode: OR in `(player.Alive ? SnapshotAliveBit : 0u)`.
- Snapshot decode: `player.Alive = (flags & SnapshotAliveBit) != 0;`.
- Welcome encode: `writer.Blob(message.GameState);` after everything else.
- Welcome decode: `message.GameState = reader.Blob();` at the same position.
- Add the new encoders and decoders:

```cpp
std::vector<std::uint8_t> Encode(const GameStateMessage& message)
{
    ByteWriter writer;
    writer.U8(static_cast<std::uint8_t>(MessageId::GameState));
    writer.Blob(message.Bytes);
    return writer.Bytes();
}

bool Decode(std::span<const std::uint8_t> bytes, GameStateMessage& out)
{
    ByteReader reader(bytes);
    if (!OpenAs(reader, MessageId::GameState))
        return false;

    GameStateMessage message;
    message.Bytes = reader.Blob();
    if (!reader.Ok() || message.Bytes.size() > MaxGameStateBytes)
        return false;

    out = std::move(message);
    return true;
}
```

`GameCommandMessage` is the same with `MessageId::GameCommand` and `MaxGameCommandBytes`.
`MatchServer::HandleMessage` and `MatchClient`'s switch must name the new ids, or the
compiler warns about an unhandled enum. For now, add `case MessageId::GameState: case
MessageId::GameCommand: return;` to the server's list of ignored ids, and `break;` in the
client. Tasks 3 and 5 replace these.

- [ ] **Step 4: Build.** Every protocol case passes, including the truncation sweep.

- [ ] **Step 5: Commit** — `"Protocol 8: an alive bit, and bytes for a game mode's state and commands"`

---

### Task 3: `GameMode` and `GameModeHost`, wired into `MatchServer`

**Files:**
- Create: `Cubit/include/Cubit/Net/GameMode.h`
- Modify: `Cubit/include/Cubit/Net/MatchServer.h`, `Cubit/src/Net/MatchServer.cpp`
- Test: create `Tests/src/GameModeTests.cpp`

**Interfaces:**
- Consumes: Task 1 (`MatchState::IsAlive`/`SetAlive`) and Task 2's messages.
- Produces: `GameMode.h`, exactly as below; `class MatchServer : public GameModeHost`; and
  a constructor gaining a trailing `GameMode* mode = nullptr`.

- [ ] **Step 1: Create `Cubit/include/Cubit/Net/GameMode.h`**

```cpp
#pragma once

#include "Cubit/Core.h"
#include "Cubit/Voxel/MatchState.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <vector>

//What a game mode may ask of the server. MatchServer implements it; a game's
//tests implement it with no network at all.
class GameModeHost
{
public:
    virtual ~GameModeHost() = default;

    virtual std::uint64_t Tick() const = 0;

    //Positions, the world, the roster.
    virtual const MatchState& Match() const = 0;

    virtual bool IsAlive(PlayerId player) const = 0;

    //Teleports, clears vertical velocity, restores starting health, and makes
    //the player alive. Also how a joiner first appears.
    virtual void Respawn(PlayerId player, const glm::vec3& position) = 0;

    //Dead, with no kill ruled and OnKilled not called: a team switch.
    virtual void Kill(PlayerId player) = 0;

    //Every edited cell back to the map's block, broadcast as one batch.
    virtual void ResetWorld() = 0;
};

//A game's rules, called by the server at fixed moments. The engine knows none
//of what a mode means: its state crosses the wire as bytes the engine carries
//without reading, and its commands arrive the same way.
class GameMode
{
public:
    virtual ~GameMode() = default;

    //The player exists and is DEAD. The mode decides when and where they appear.
    virtual void OnJoin(GameModeHost& host, PlayerId player) = 0;

    //Called before the player is removed.
    virtual void OnLeave(GameModeHost& host, PlayerId player) = 0;

    //Asked before any damage is applied. False means no damage and no kill.
    virtual bool CanDamage(const GameModeHost& host, PlayerId shooter, PlayerId victim) const = 0;

    //The rules killed `victim`: they are already dead and out of the rewind history.
    virtual void OnKilled(GameModeHost& host, PlayerId victim, PlayerId killer) = 0;

    //Bytes a client sent, at most MaxGameCommandBytes. From any joined player,
    //dead or alive: the mode decides what it accepts.
    virtual void OnCommand(GameModeHost& host, PlayerId player,
        std::span<const std::uint8_t> bytes) = 0;

    //Once per tick, after every player has stepped and before the snapshot.
    virtual void Step(GameModeHost& host) = 0;

    //True once after the state changed, so the server sends it to everybody.
    virtual bool TakeStateChanged() = 0;

    virtual std::vector<std::uint8_t> EncodeState() const = 0;
};
```

- [ ] **Step 2: Write the failing tests** in `Tests/src/GameModeTests.cpp`

```cpp
#include <doctest.h>

#include "Cubit/FrameClock.h"
#include "Cubit/Net/GameMode.h"
#include "Cubit/Net/LoopbackTransport.h"
#include "Cubit/Net/MatchClient.h"
#include "Cubit/Net/MatchServer.h"
#include "Cubit/Net/Protocol.h"

#include <optional>
#include <string>
#include <vector>

namespace
{
    constexpr std::uint64_t MapHash = 0xFEEDFACEull;
    const glm::vec3 Spawn{ 8.0f, 2.0f, 8.0f };
    const glm::vec3 Elsewhere{ 20.0f, 2.0f, 20.0f };

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

    //Records every call, and does what a test tells it to.
    struct RecordingMode final : GameMode
    {
        std::vector<PlayerId> Joined, Left, Killed, Killers;
        std::vector<std::vector<std::uint8_t>> Commands;
        int Steps = 0;
        bool AllowDamage = true;
        bool RespawnOnJoin = true;
        std::vector<std::uint8_t> State{ 1 };
        bool Changed = false;

        void OnJoin(GameModeHost& host, PlayerId player) override
        {
            Joined.push_back(player);
            if (RespawnOnJoin)
                host.Respawn(player, Spawn);
        }
        void OnLeave(GameModeHost&, PlayerId player) override { Left.push_back(player); }
        bool CanDamage(const GameModeHost&, PlayerId, PlayerId) const override { return AllowDamage; }
        void OnKilled(GameModeHost&, PlayerId victim, PlayerId killer) override
        {
            Killed.push_back(victim);
            Killers.push_back(killer);
        }
        void OnCommand(GameModeHost&, PlayerId, std::span<const std::uint8_t> bytes) override
        {
            Commands.emplace_back(bytes.begin(), bytes.end());
        }
        void Step(GameModeHost&) override { ++Steps; }
        bool TakeStateChanged() override { const bool was = Changed; Changed = false; return was; }
        std::vector<std::uint8_t> EncodeState() const override { return State; }
    };

    struct Rig
    {
        LoopbackNetwork Network;
        RecordingMode Mode;
        MatchServer Server;

        explicit Rig(bool withMode = true)
            : Server(FlatWorld(), "flat.vox", MapHash, Spawn, Network.Server(),
                MatchRules{}, withMode ? &Mode : nullptr) {}

        void Step(std::vector<MatchClient*> clients, int ticks = 1)
        {
            for (int t = 0; t < ticks; ++t)
            {
                for (MatchClient* client : clients)
                {
                    client->SetInput(CharacterInput{});
                    client->Step(FrameClock::FixedStepSeconds);
                }
                Server.Step(FrameClock::FixedStepSeconds);
            }
        }
    };
}

TEST_CASE("A mode hears the join, and the joiner appears where the mode says")
{
    Rig rig;
    PeerId peer = InvalidPeer;
    MatchClient client(rig.Network.AddClient(peer), GoodLoader());
    rig.Step({ &client }, 10);

    REQUIRE(rig.Mode.Joined.size() == 1);
    CHECK(rig.Mode.Joined[0] == client.LocalPlayer());
    CHECK(rig.Server.Match().IsAlive(client.LocalPlayer()));
    CHECK(rig.Mode.Steps >= 5);
}

TEST_CASE("A joiner the mode does not respawn stays dead and unstepped")
{
    Rig rig;
    rig.Mode.RespawnOnJoin = false;
    PeerId peer = InvalidPeer;
    MatchClient client(rig.Network.AddClient(peer), GoodLoader());
    rig.Step({ &client }, 30);

    const PlayerId player = client.LocalPlayer();
    CHECK_FALSE(rig.Server.Match().IsAlive(player));
    CHECK_FALSE(client.IsAlive(player));
}

TEST_CASE("A mode's state reaches every client when it changes, and a late joiner's welcome")
{
    Rig rig;
    PeerId peerA = InvalidPeer;
    MatchClient a(rig.Network.AddClient(peerA), GoodLoader());
    rig.Step({ &a }, 10);

    rig.Mode.State = { 4, 5, 6 };
    rig.Mode.Changed = true;
    rig.Step({ &a }, 3);
    CHECK(a.GameState() == std::vector<std::uint8_t>{ 4, 5, 6 });
    const std::uint64_t serial = a.GameStateSerial();

    PeerId peerB = InvalidPeer;
    MatchClient b(rig.Network.AddClient(peerB), GoodLoader());
    rig.Step({ &a, &b }, 10);
    CHECK(b.GameState() == std::vector<std::uint8_t>{ 4, 5, 6 });

    //Unchanged state is not resent.
    CHECK(a.GameStateSerial() == serial);
}

TEST_CASE("A client's command reaches the mode with its bytes")
{
    Rig rig;
    PeerId peer = InvalidPeer;
    MatchClient client(rig.Network.AddClient(peer), GoodLoader());
    rig.Step({ &client }, 10);

    const std::uint8_t switchTeam[] = { 1, 2 };
    client.SendCommand(switchTeam);
    rig.Step({ &client }, 3);

    REQUIRE(rig.Mode.Commands.size() == 1);
    CHECK(rig.Mode.Commands[0] == std::vector<std::uint8_t>{ 1, 2 });
}

TEST_CASE("Leaving tells the mode")
{
    Rig rig;
    PeerId peer = InvalidPeer;
    {
        MatchClient client(rig.Network.AddClient(peer), GoodLoader());
        rig.Step({ &client }, 10);
    }
    rig.Network.RemoveClient(peer);
    rig.Step({}, 3);
    CHECK(rig.Mode.Left.size() == 1);
}

TEST_CASE("Respawn and Kill through the host")
{
    Rig rig;
    PeerId peer = InvalidPeer;
    MatchClient client(rig.Network.AddClient(peer), GoodLoader());
    rig.Step({ &client }, 10);
    const PlayerId player = client.LocalPlayer();

    rig.Server.Kill(player);
    CHECK_FALSE(rig.Server.IsAlive(player));
    CHECK(rig.Mode.Killed.empty());   //Kill is not a ruled death

    rig.Server.Respawn(player, Elsewhere);
    CHECK(rig.Server.IsAlive(player));
    CHECK(rig.Server.Match().Player(player).Position() == Elsewhere);
    CHECK(rig.Server.HealthOf(player) == MatchRules{}.StartingHealth);

    rig.Step({ &client }, 20);
    CHECK(client.IsAlive(player));
}

TEST_CASE("Without a mode, nothing changes: joiners appear at the spawn, alive")
{
    Rig rig(false);
    PeerId peer = InvalidPeer;
    MatchClient client(rig.Network.AddClient(peer), GoodLoader());
    rig.Step({ &client }, 10);
    CHECK(rig.Server.Match().IsAlive(client.LocalPlayer()));
    CHECK(client.GameState().empty());
}
```

The kill, damage and dead-player refusal cases need a shot to land, and they are written in
Step 6 below, reusing the existing `LagCompensationTests`/`MatchServerTests` shooting helpers
(read them for how a test aims a shot at a standing target).

- [ ] **Step 3: Implement `MatchServer`.**
  - `class CB_API MatchServer final : public GameModeHost`. Include
    `"Cubit/Net/GameMode.h"`.
  - The constructor gains `GameMode* mode = nullptr`, stored as `m_Mode`.
  - Declare the host overrides in the public section, each `override`:
    - `std::uint64_t Tick() const { return m_Match.Tick(); }`;
    - the existing `const MatchState& Match() const` (add `override`);
    - `bool IsAlive(PlayerId) const`, `void Respawn(PlayerId, const glm::vec3&)`,
      `void Kill(PlayerId)` and `void ResetWorld()`. `ResetWorld` is implemented in Task 4;
      declare it now with an empty body so this task links.

  Changes in `MatchServer.cpp`:
  1. **Hello:** after `client->Player = m_Match.AddPlayer(m_Spawn);`, add
     `if (m_Mode != nullptr) { m_Match.SetAlive(client->Player, false); m_Mode->OnJoin(*this, client->Player); }`.
     Set `welcome.GameState = m_Mode != nullptr ? m_Mode->EncodeState() : std::vector<std::uint8_t>{};`
     before encoding.
  2. **HandleDisconnected:** before `m_Match.RemovePlayer`, add
     `if (m_Mode != nullptr) m_Mode->OnLeave(*this, found->Player);`.
  3. **HandleMessage:** add a case:

```cpp
    case MessageId::GameCommand:
    {
        GameCommandMessage command;
        if (!Decode(data, command) || client->Player == InvalidPlayer || m_Mode == nullptr)
            return;

        m_Mode->OnCommand(*this, client->Player, command.Bytes);
        return;
    }
```

     Keep `case MessageId::GameState:` among the ignored server-to-client ids.
  4. **Step:** after `m_Match.Step(commands, ...)`, add
     `if (m_Mode != nullptr) m_Mode->Step(*this);`. In the history loop, record only
     `if (m_Match.IsAlive(player))`. After the loop and before `SendSnapshots()`:

```cpp
    if (m_Mode != nullptr && m_Mode->TakeStateChanged())
    {
        GameStateMessage state;
        state.Bytes = m_Mode->EncodeState();

        //Refused rather than sent: a state past the cap is a mode bug, and
        //the previous state staying on the wire is better than a protocol error.
        if (state.Bytes.size() > MaxGameStateBytes)
            CB_ERROR("Game mode state is " + std::to_string(state.Bytes.size())
                + " bytes, over the " + std::to_string(MaxGameStateBytes) + " allowed; not sent");
        else
            SendToJoined(Encode(state), Channel::Reliable);
    }
```

  5. **SendSnapshots:** `entry.Alive = m_Match.IsAlive(player);`.
  6. **ApplyInputEdit:** refuse an edit from the dead. Change the legality test to
     `if (m_Match.IsAlive(player) && IsEditLegal(...))`.
  7. **HandleFire:**
     - At the top: `if (!m_Match.IsAlive(shooter.Player)) return;`.
     - In the candidate loop: `if (!m_Match.IsAlive(player)) continue;`.
     - In the victim branch, before damage:
       `if (m_Mode != nullptr && !m_Mode->CanDamage(*this, shooter.Player, victim.Player)) { resolved.Victim = InvalidPlayer; }`
       followed by an `else` block wrapping the existing damage code.
     - Replace the kill block's teleport with:

```cpp
            if (resolved.Killed)
            {
                m_History.Forget(victim.Player);

                if (m_Mode == nullptr)
                {
                    m_Match.TeleportPlayer(victim.Player, m_Spawn);
                    m_Match.PlayerForWrite(victim.Player).SetVerticalVelocity(0.0f);
                    victim.Health = m_Rules.StartingHealth;
                }
                else
                {
                    m_Match.SetAlive(victim.Player, false);
                    m_Mode->OnKilled(*this, victim.Player, shooter.Player);
                }
            }
```

     Keep the existing "THE HISTORY GOES TOO" comment above the Forget.
  8. **The host methods:**

```cpp
bool MatchServer::IsAlive(PlayerId player) const
{
    return m_Match.IsAlive(player);
}

void MatchServer::Respawn(PlayerId player, const glm::vec3& position)
{
    if (!m_Match.HasPlayer(player))
        return;

    m_Match.TeleportPlayer(player, position);
    m_Match.PlayerForWrite(player).SetVerticalVelocity(0.0f);
    m_Match.SetAlive(player, true);

    //A fresh life: nothing from before it may be rewound into.
    m_History.Forget(player);

    for (Client& client : m_Clients)
        if (client.Player == player)
            client.Health = m_Rules.StartingHealth;
}

void MatchServer::Kill(PlayerId player)
{
    m_Match.SetAlive(player, false);
    m_History.Forget(player);
}
```

- [ ] **Step 4: Stub the client side so the tests compile.** Task 5 does the real work. Add
  these to `MatchClient` now, in the same shape Task 5 finishes:
  - `bool IsAlive(PlayerId player) const { return m_Match.IsAlive(player); }`;
  - `const std::vector<std::uint8_t>& GameState() const { return m_GameState; }`;
  - `std::uint64_t GameStateSerial() const { return m_GameStateSerial; }`;
  - `void SendCommand(std::span<const std::uint8_t> bytes);`.

  Then add members `m_GameState` and `m_GameStateSerial`, and:
  - **HandleWelcome:** `m_GameState = welcome.GameState; ++m_GameStateSerial;`.
  - **A GameState handler:** decode it, `m_GameState = message.Bytes; ++m_GameStateSerial;`.
  - **HandleSnapshot, for remote entries:** `m_Match.SetAlive(entry.Player, entry.Alive);`.
  - **SendCommand:** `if (!m_Connected || bytes.size() > MaxGameCommandBytes) return;` then
    send `Encode(GameCommandMessage{ ... })` on `Channel::Reliable`.

- [ ] **Step 5: Regenerate premake and build.** The new cases pass, and every existing case
  still passes. That is the null-mode guarantee.

- [ ] **Step 6: Add the shooting cases** to `GameModeTests.cpp`. Base them on the helper
  that `MatchServerTests.cpp` uses to land a hit: search it for `Fire(`, and copy its
  approach for placing two players and aiming. Required cases:
  - `"A ruled kill makes the victim dead and tells the mode who killed them"`: after enough
    hits, `!rig.Server.IsAlive(victim)`, `rig.Mode.Killed == {victim}`,
    `rig.Mode.Killers == {shooter}`, and the victim's position is unchanged by further
    steps. It is not stepped.
  - `"A mode that refuses damage leaves the target unhurt"`: with `AllowDamage = false`,
    `HealthOf(victim) == StartingHealth` after several hits, and the `ShotReport` names no
    victim.
  - `"The dead cannot shoot or edit"`: after `Kill(player)`, that player's `Fire` changes
    nobody's health, and their `RequestEdit` is refused. The block stays, and the
    `EditResult` restores it.

- [ ] **Step 7: Build.** Everything passes. **Commit** —
  `"Let a game mode run the match: joins, kills, commands and state behind a rule-free hook"`

---

### Task 4: `MatchServer::ResetWorld`

**Files:**
- Modify: `Cubit/src/Net/MatchServer.cpp`
- Test: `Tests/src/GameModeTests.cpp` (append)

**Interfaces:**
- Produces: `void MatchServer::ResetWorld()`, declared in Task 3.

- [ ] **Step 1: Failing test**

```cpp
TEST_CASE("Resetting the world restores every edited cell, for clients and joiners")
{
    Rig rig;
    PeerId peerA = InvalidPeer;
    MatchClient a(rig.Network.AddClient(peerA), GoodLoader());
    rig.Step({ &a }, 10);

    const BlockEdit dug{ glm::ivec3(4, 0, 4), BlockId{ 0 } };
    const BlockEdit built{ glm::ivec3(6, 1, 6), BlockId{ 3 } };
    rig.Server.ApplyEdits(std::vector<BlockEdit>{ dug, built });
    rig.Step({ &a }, 3);
    REQUIRE(a.Match().GetWorld().GetBlock(4, 0, 4) == BlockId{ 0 });

    rig.Server.ResetWorld();
    rig.Step({ &a }, 3);

    CHECK(rig.Server.Match().GetWorld().GetBlock(4, 0, 4) == BlockId{ 1 });
    CHECK(rig.Server.Match().GetWorld().GetBlock(6, 1, 6) == BlockId{ 0 });
    CHECK(a.Match().GetWorld().GetBlock(4, 0, 4) == BlockId{ 1 });
    CHECK(a.Match().GetWorld().GetBlock(6, 1, 6) == BlockId{ 0 });
    CHECK(rig.Server.EditLog().empty());

    //A joiner after the reset is told of no edits at all.
    PeerId peerB = InvalidPeer;
    MatchClient b(rig.Network.AddClient(peerB), GoodLoader());
    rig.Step({ &a, &b }, 10);
    CHECK(b.Match().GetWorld().GetBlock(4, 0, 4) == BlockId{ 1 });
}
```

- [ ] **Step 2: Build; the test fails.** The block stays dug, because `ResetWorld` is empty.

- [ ] **Step 3: Implement**

```cpp
void MatchServer::ResetWorld()
{
    //Every cell the log has touched, back to what the map held there. The map's
    //own blocks cannot hang unsupported, so no collapse check follows.
    std::vector<BlockEdit> restore;
    restore.reserve(m_MapBlock.size());
    for (const auto& [cell, block] : m_MapBlock)
        restore.push_back(BlockEdit{ cell, block });

    std::vector<BlockEdit> changed;
    changed.reserve(restore.size());
    for (const BlockEdit& edit : restore)
        if (ApplyBlockEdit(m_Match.GetWorld(), edit).has_value())
            changed.push_back(edit);

    m_EditLog.clear();
    m_LogIndex.clear();
    m_MapBlock.clear();

    Broadcast(changed);
}
```

- [ ] **Step 4: Build.** Passes. **Commit** — `"Let the server put the map back as it loaded"`

---

### Task 5: The client when dead, and an end-to-end respawn

**Files:**
- Modify: `Cubit/include/Cubit/Net/MatchClient.h`, `Cubit/src/Net/MatchClient.cpp`
- Test: `Tests/src/GameModeTests.cpp` (append)

**Interfaces:**
- Produces: `std::uint64_t MatchClient::ServerTick() const`, the server tick of the newest
  snapshot, which the HUD's countdowns need. It also changes `Reconcile` so a respawn is
  taken as a teleport.

- [ ] **Step 1: Failing tests**

```cpp
TEST_CASE("A client neither predicts nor corrects while dead, and a respawn is a teleport")
{
    Rig rig;
    PeerId peer = InvalidPeer;
    MatchClient client(rig.Network.AddClient(peer), GoodLoader());
    rig.Step({ &client }, 60);
    const PlayerId player = client.LocalPlayer();

    rig.Server.Kill(player);
    rig.Step({ &client }, 10);
    CHECK_FALSE(client.IsAlive(player));

    //Walking while dead moves nothing on either side.
    const glm::vec3 where = client.Match().Player(player).Position();
    for (int i = 0; i < 30; ++i)
    {
        CharacterInput walk;
        walk.Move = glm::vec2(0.0f, 1.0f);
        client.SetInput(walk);
        client.Step(FrameClock::FixedStepSeconds);
        rig.Server.Step(FrameClock::FixedStepSeconds);
    }
    CHECK(client.Match().Player(player).Position() == where);

    const std::uint64_t correctionsBefore = client.Corrections().Count;
    rig.Server.Respawn(player, Elsewhere);
    rig.Step({ &client }, 10);

    CHECK(client.IsAlive(player));
    CHECK(glm::distance(client.Match().Player(player).Position(), Elsewhere) < 0.5f);
    CHECK(client.Corrections().Count == correctionsBefore);
}

TEST_CASE("The client knows the server's tick")
{
    Rig rig;
    PeerId peer = InvalidPeer;
    MatchClient client(rig.Network.AddClient(peer), GoodLoader());
    rig.Step({ &client }, 20);
    CHECK(client.ServerTick() == rig.Server.Tick());
}
```

- [ ] **Step 2: Build; the tests fail.** The respawn counts as a correction, and
  `ServerTick` does not exist.

- [ ] **Step 3: Implement.**
  - Add `std::uint64_t ServerTick() const { return m_ServerTick; }`.
  - In `HandleSnapshot`'s local branch, before `Reconcile(entry)`:

```cpp
            //Dead is the server's to say. While dead there is nothing to
            //predict: take its position and stop. The first snapshot alive
            //again is a respawn - a teleport, not a correction to smooth.
            const bool wasAlive = m_Match.IsAlive(entry.Player);
            m_Match.SetAlive(entry.Player, entry.Alive);

            if (!entry.Alive || !wasAlive)
            {
                m_Match.PlayerForWrite(entry.Player).SetState(entry.Position, entry.Position,
                    entry.VerticalVelocity, entry.Grounded, entry.Crouched);

                while (!m_Unacked.empty() && m_Unacked.front().Tick <= entry.LastInputTick)
                    m_Unacked.pop_front();
                continue;
            }
```

  Unacknowledged inputs above `LastInputTick` stay queued. The next reconcile replays them
  on top of the respawn position as usual.

- [ ] **Step 4: Build.** Passes, and `PredictionTests`/`PredictedEditTests` are unchanged.
  **Commit** — `"A dead client stops predicting, and takes its respawn as a teleport"`

---

### Task 6: `TerrainGen::FortCentres`

**Files:**
- Modify: `Cubit/include/Cubit/Voxel/TerrainGen.h`, `Cubit/src/Voxel/TerrainGen.cpp`
- Test: `Tests/src/TerrainGenTests.cpp` (append)

**Interfaces:**
- Produces: `static std::array<glm::ivec2, 2> TerrainGen::FortCentres(const glm::ivec3& size);`,
  as (x, z) columns, low x first.

- [ ] **Step 1: Failing test**

```cpp
TEST_CASE("The fort centres are where the forts are built, mirrored across the map")
{
    TerrainConfig config;
    const auto centres = TerrainGen::FortCentres(config.Size);

    CHECK(centres[0].x < config.Size.x / 2);
    CHECK(centres[1].x == config.Size.x - 1 - centres[0].x);
    CHECK(centres[0].y == config.Size.z / 2);
    CHECK(centres[1].y == config.Size.z / 2);

    //A fort is built there: its base block sits in the column somewhere.
    const VoxModel model = TerrainGen::Generate(config);
    for (const glm::ivec2& centre : centres)
    {
        bool fortBlock = false;
        for (int y = 0; y < config.Size.y; ++y)
        {
            const std::uint8_t block = model.At(centre.x, y, centre.y);
            fortBlock = fortBlock || block == MapBlocks::RedBase || block == MapBlocks::BlueBase;
        }
        CHECK(fortBlock);
    }
}
```

Check `VoxModel`'s accessor name in `VoxLoader.h` (`At` or similar), and how `BuildForts`
colours the forts, before relying on `RedBase`/`BlueBase`. Adjust the check to whatever the
fort's floor is actually made of.

- [ ] **Step 2: Build; the test fails.** `FortCentres` does not exist.

- [ ] **Step 3: Implement.** Declare it in `TerrainGen.h` (add `#include <array>`) with the
  comment: "Where the two forts stand, as (x, z) columns, low x first. The game puts each
  team's base here, so the two cannot disagree." Define it in `TerrainGen.cpp` from
  `FortEdgeOffset`:
  `return { glm::ivec2(FortEdgeOffset, size.z / 2), glm::ivec2(size.x - 1 - FortEdgeOffset, size.z / 2) };`.
  Make `BuildForts` and `InFortFootprint` use it instead of their own copies of the
  arithmetic.

- [ ] **Step 4: Build.** Passes, and the map hash tests are unchanged: the terrain is
  byte-identical. **Commit** — `"Say where the forts are, for the game to put bases there"`

---

## Stage 2 — The rules (game side)

### Task 7: `CtfRules` and `CtfState` with its encoding

**Files:**
- Create: `game/Game/src/CtfRules.h`, `game/Game/src/CtfState.h`,
  `game/Game/src/CtfState.cpp`
- Test: create `game/GameTests/src/CtfStateTests.cpp`

**Interfaces:**
- Produces: the `CubitGame::` names below, used by every later task.

- [ ] **Step 1: Create `CtfRules.h`**

```cpp
#pragma once

#include <cstdint>

//Cubit's own game: the capture-the-intel numbers. Tuning, not logic.
namespace CubitGame
{
    constexpr std::uint64_t TicksPerSecond = 60;

    struct CtfRules
    {
        int CapturesToWin = 3;
        std::uint64_t TimeLimitTicks = 20 * 60 * TicksPerSecond;
        std::uint64_t CountdownTicks = 10 * TicksPerSecond;
        std::uint64_t EndScreenTicks = 15 * TicksPerSecond;
        std::uint64_t RespawnDelayTicks = 5 * TicksPerSecond;
        std::uint64_t SpawnProtectionTicks = 2 * TicksPerSecond;
        std::uint64_t DroppedReturnTicks = 30 * TicksPerSecond;

        //Metres from the intel to the nearest point of a player's box.
        float PickupRadius = 1.5f;

        //Around the carrier's own intel home spot, horizontally and vertically.
        float BaseRadius = 4.0f;

        std::size_t EventHistory = 8;
    };
}
```

(Add `#include <cstddef>` for `std::size_t`.)

- [ ] **Step 2: Create `CtfState.h`**

```cpp
#pragma once

#include "Cubit/Voxel/MatchState.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <vector>

//Cubit's own game: the match as the server rules it, and as every client reads it.
namespace CubitGame
{
    enum class Team : std::uint8_t { None = 0, Blue = 1, Green = 2 };

    //0 for Blue, 1 for Green. Team::None has no index.
    inline int IndexOf(Team team) { return team == Team::Green ? 1 : 0; }
    inline Team TeamAt(int index) { return index == 1 ? Team::Green : Team::Blue; }
    inline Team Other(Team team) { return team == Team::Blue ? Team::Green : Team::Blue; }

    enum class Phase : std::uint8_t { Warmup = 0, Countdown = 1, Active = 2, Ended = 3 };

    enum class IntelStatus : std::uint8_t { Home = 0, Carried = 1, Dropped = 2 };

    enum class EventKind : std::uint8_t
    {
        MatchStarted = 1, PickedUp = 2, Captured = 3, Returned = 4, MatchWon = 5, Draw = 6
    };

    struct PlayerStats
    {
        PlayerId Player = InvalidPlayer;
        Team Side = Team::None;
        std::uint16_t Kills = 0;
        std::uint16_t Deaths = 0;
        std::uint16_t Captures = 0;

        //The server tick this player respawns at, or 0 when not waiting to.
        std::uint64_t RespawnTick = 0;
    };

    //One team's intel. `Owner` is the team it belongs to.
    struct Intel
    {
        IntelStatus Status = IntelStatus::Home;
        PlayerId Carrier = InvalidPlayer;
        glm::vec3 Position{ 0.0f };   //bottom centre, in world space
        glm::vec3 HomeSpot{ 0.0f };
    };

    struct MatchEvent
    {
        std::uint64_t Serial = 0;     //counts up from 1; NewEvents reads by it
        EventKind Kind = EventKind::MatchStarted;
        Team Side = Team::None;       //the team the event is about (who scored, whose intel)
        PlayerId Player = InvalidPlayer;
    };

    struct CtfState
    {
        Phase Stage = Phase::Warmup;
        std::uint64_t StageEndTick = 0;   //0 when the stage has no end
        std::array<std::uint8_t, 2> Captures{};
        Team Winner = Team::None;         //set in Ended; None there means a draw
        std::vector<PlayerStats> Players;
        std::array<Intel, 2> Intels{};    //indexed by IndexOf(owner)
        std::deque<MatchEvent> Events;    //oldest first

        const PlayerStats* Find(PlayerId player) const;
        PlayerStats* Find(PlayerId player);
        int TeamSize(Team team) const;
    };

    //First byte is this format's version, so an old client refuses a new layout
    //rather than misreading it.
    constexpr std::uint8_t CtfStateVersion = 1;

    std::vector<std::uint8_t> Encode(const CtfState& state);
    std::optional<CtfState> DecodeCtfState(std::span<const std::uint8_t> bytes);

    //The one command a client sends.
    constexpr std::uint8_t SwitchTeamCommand = 1;
}
```

- [ ] **Step 3: Write the failing tests** in `game/GameTests/src/CtfStateTests.cpp`

```cpp
#include <doctest.h>

#include "CtfState.h"

using namespace CubitGame;

namespace
{
    CtfState Sample()
    {
        CtfState state;
        state.Stage = Phase::Active;
        state.StageEndTick = 123456;
        state.Captures = { 2, 1 };
        state.Winner = Team::None;
        state.Players.push_back(PlayerStats{ 3, Team::Blue, 4, 1, 2, 0 });
        state.Players.push_back(PlayerStats{ 7, Team::Green, 0, 5, 0, 999 });
        state.Intels[0] = Intel{ IntelStatus::Carried, 7, glm::vec3(1.5f, 2.0f, 3.5f), glm::vec3(8.5f, 20.0f, 256.5f) };
        state.Intels[1] = Intel{ IntelStatus::Dropped, InvalidPlayer, glm::vec3(100.5f, 12.0f, 40.5f), glm::vec3(503.5f, 20.0f, 256.5f) };
        state.Events.push_back(MatchEvent{ 11, EventKind::Captured, Team::Blue, 3 });
        state.Events.push_back(MatchEvent{ 12, EventKind::PickedUp, Team::Blue, 7 });
        return state;
    }
}

TEST_CASE("The match state round-trips every field")
{
    const CtfState sent = Sample();
    const std::optional<CtfState> back = DecodeCtfState(Encode(sent));
    REQUIRE(back.has_value());

    CHECK(back->Stage == sent.Stage);
    CHECK(back->StageEndTick == sent.StageEndTick);
    CHECK(back->Captures == sent.Captures);
    CHECK(back->Winner == sent.Winner);
    REQUIRE(back->Players.size() == 2);
    CHECK(back->Players[1].Player == 7);
    CHECK(back->Players[1].Side == Team::Green);
    CHECK(back->Players[1].Deaths == 5);
    CHECK(back->Players[1].RespawnTick == 999);
    CHECK(back->Intels[0].Status == IntelStatus::Carried);
    CHECK(back->Intels[0].Carrier == 7);
    CHECK(back->Intels[1].Position == sent.Intels[1].Position);
    CHECK(back->Intels[1].HomeSpot == sent.Intels[1].HomeSpot);
    REQUIRE(back->Events.size() == 2);
    CHECK(back->Events[0].Serial == 11);
    CHECK(back->Events[0].Kind == EventKind::Captured);
    CHECK(back->Events[1].Player == 7);
}

TEST_CASE("A different version, a short buffer, or an unknown enum is refused")
{
    std::vector<std::uint8_t> bytes = Encode(Sample());

    std::vector<std::uint8_t> wrongVersion = bytes;
    wrongVersion[0] = CtfStateVersion + 1;
    CHECK_FALSE(DecodeCtfState(wrongVersion).has_value());

    for (std::size_t length = 0; length < bytes.size(); ++length)
        CHECK_FALSE(DecodeCtfState(std::span(bytes.data(), length)).has_value());

    std::vector<std::uint8_t> badPhase = bytes;
    badPhase[1] = 9;   //the phase byte follows the version
    CHECK_FALSE(DecodeCtfState(badPhase).has_value());
}

TEST_CASE("A full match's state stays well under the wire's cap")
{
    CtfState state = Sample();
    state.Players.clear();
    for (PlayerId p = 1; p <= 32; ++p)
        state.Players.push_back(PlayerStats{ p, Team::Blue, 999, 999, 99, 1 });
    for (std::uint64_t s = 1; s <= 8; ++s)
        state.Events.push_back(MatchEvent{ s, EventKind::PickedUp, Team::Green, 1 });

    CHECK(Encode(state).size() < 4096);
}

TEST_CASE("Finding a player and counting a team")
{
    CtfState state = Sample();
    REQUIRE(state.Find(3) != nullptr);
    CHECK(state.Find(3)->Kills == 4);
    CHECK(state.Find(99) == nullptr);
    CHECK(state.TeamSize(Team::Blue) == 1);
    CHECK(state.TeamSize(Team::Green) == 1);
}
```

- [ ] **Step 4: Implement `CtfState.cpp`** with `ByteWriter`/`ByteReader` (from
  `Cubit/Net/`). Layout, in order:
  - the version (u8), the phase (u8), `StageEndTick` (u64), the two capture counts (u8
    each), and the winner (u8);
  - the player count (u16), then for each player: id (u16), team (u8), kills, deaths and
    captures (u16 each), and respawn tick (u64);
  - for each of the two intels: status (u8), carrier (u16), position (Vec3), and home spot
    (Vec3);
  - the event count (u8), then for each event: serial (u64), kind (u8), team (u8) and
    player (u16).

  The decoder refuses:
  - a version mismatch;
  - a phase above 3;
  - a team above 2;
  - an intel status above 2;
  - an event kind of 0 or above 6;
  - `!reader.Ok()` at the end;
  - leftover bytes.

  `Find` and `TeamSize` are linear scans. Check `ByteReader`'s constructor and method names
  before using them.

- [ ] **Step 5: Regenerate premake and build.** 4 new game cases pass. **Commit** —
  `"The capture-the-intel state, and the bytes it crosses the wire as"`

---

### Task 8: `CtfMode` — teams, spawning, protection, and a fake host

**Files:**
- Create: `game/Game/src/CtfMode.h`, `game/Game/src/CtfMode.cpp`,
  `game/GameTests/src/FakeHost.h`
- Test: create `game/GameTests/src/CtfModeTests.cpp`

**Interfaces:**
- Consumes: `GameMode`/`GameModeHost` (Task 3), `TerrainGen::FortCentres` (Task 6),
  `FindSpawn` (`Cubit/Voxel/SpawnFinder.h`: `std::optional<glm::vec3> FindSpawn(const World&, const glm::ivec2& hintXZ, const glm::vec3& halfExtents)`),
  and Task 7's types.
- Produces: `class CtfMode final : public GameMode` with
  `CtfMode(const CtfRules& rules, glm::ivec3 mapSize, std::uint32_t seed)` and
  `const CtfState& State() const`. Also `FakeHost`, used by Tasks 9 and 10.

- [ ] **Step 1: Create `FakeHost.h`**

```cpp
#pragma once

#include "Cubit/Net/GameMode.h"
#include "Cubit/Voxel/MatchState.h"

#include <glm/glm.hpp>

#include <utility>

//A GameModeHost with no network: a real MatchState, a tick the test sets, and
//a count of the calls that have no state of their own to check.
class FakeHost final : public GameModeHost
{
public:
    explicit FakeHost(World world) : m_Match(std::move(world)) {}

    std::uint64_t Now = 1;
    int Resets = 0;

    std::uint64_t Tick() const override { return Now; }
    const MatchState& Match() const override { return m_Match; }
    bool IsAlive(PlayerId player) const override { return m_Match.IsAlive(player); }

    void Respawn(PlayerId player, const glm::vec3& position) override
    {
        m_Match.TeleportPlayer(player, position);
        m_Match.SetAlive(player, true);
    }

    void Kill(PlayerId player) override { m_Match.SetAlive(player, false); }
    void ResetWorld() override { ++Resets; }

    //What the server does on a join: add the player dead, then ask the mode.
    PlayerId Join(GameMode& mode)
    {
        const PlayerId player = m_Match.AddPlayer(glm::vec3(0.0f));
        m_Match.SetAlive(player, false);
        mode.OnJoin(*this, player);
        return player;
    }

    void Leave(GameMode& mode, PlayerId player)
    {
        mode.OnLeave(*this, player);
        m_Match.RemovePlayer(player);
    }

    //What the server does on a ruled kill.
    void RuleKill(GameMode& mode, PlayerId victim, PlayerId killer)
    {
        m_Match.SetAlive(victim, false);
        mode.OnKilled(*this, victim, killer);
    }

    void Place(PlayerId player, const glm::vec3& centre) { m_Match.TeleportPlayer(player, centre); }

    void Run(GameMode& mode, std::uint64_t ticks)
    {
        for (std::uint64_t i = 0; i < ticks; ++i)
        {
            ++Now;
            mode.Step(*this);
        }
    }

    World& Terrain() { return m_Match.GetWorld(); }

private:
    MatchState m_Match;
};
```

- [ ] **Step 2: Write the failing tests** in `CtfModeTests.cpp`. The test world is
  64 × 16 × 32 blocks: `World(4, 1, 2)` with a floor at y 0. `FortCentres` puts the bases at
  (8, 16) and (55, 16).

```cpp
#include <doctest.h>

#include "CtfMode.h"
#include "FakeHost.h"

#include "Cubit/Voxel/TerrainGen.h"

using namespace CubitGame;

namespace
{
    World Floor()
    {
        World world(4, 1, 2);   //64 x 16 x 32
        for (int z = 0; z < world.GetDepth(); ++z)
            for (int x = 0; x < world.GetWidth(); ++x)
                world.SetBlock(x, 0, z, BlockId{ 1 });
        return world;
    }

    const glm::ivec3 Size{ 64, 16, 32 };

    glm::vec2 Base(Team team)
    {
        const auto centres = TerrainGen::FortCentres(Size);
        return glm::vec2(centres[IndexOf(team)]) + glm::vec2(0.5f);
    }

    float DistanceFromBase(const FakeHost& host, PlayerId player, Team team)
    {
        const glm::vec3 p = host.Match().Player(player).Position();
        return glm::distance(glm::vec2(p.x, p.z), Base(team));
    }

    Team TeamOf(const CtfMode& mode, PlayerId player)
    {
        return mode.State().Find(player)->Side;
    }
}

TEST_CASE("Joiners are balanced onto the smaller team and spawn at their own base")
{
    CtfMode mode(CtfRules{}, Size, 1);
    FakeHost host(Floor());

    const PlayerId a = host.Join(mode);
    const PlayerId b = host.Join(mode);
    const PlayerId c = host.Join(mode);

    CHECK(TeamOf(mode, a) != TeamOf(mode, b));
    CHECK(mode.State().TeamSize(Team::Blue) + mode.State().TeamSize(Team::Green) == 3);
    CHECK(std::abs(mode.State().TeamSize(Team::Blue) - mode.State().TeamSize(Team::Green)) == 1);

    for (PlayerId p : { a, b, c })
    {
        CHECK(host.IsAlive(p));
        CHECK(DistanceFromBase(host, p, TeamOf(mode, p)) < 8.0f);
    }
}

TEST_CASE("The same seed balances the same way")
{
    CtfMode one(CtfRules{}, Size, 7), two(CtfRules{}, Size, 7);
    FakeHost hostOne(Floor()), hostTwo(Floor());
    const PlayerId a = hostOne.Join(one);
    const PlayerId b = hostTwo.Join(two);
    CHECK(TeamOf(one, a) == TeamOf(two, b));
}

TEST_CASE("A switch is granted only when it keeps the teams within one")
{
    CtfMode mode(CtfRules{}, Size, 1);
    FakeHost host(Floor());
    const PlayerId a = host.Join(mode);
    const PlayerId b = host.Join(mode);
    const Team before = TeamOf(mode, a);

    //1 v 1: switching would make it 2 v 0. Refused.
    const std::uint8_t command[] = { SwitchTeamCommand };
    mode.OnCommand(host, a, command);
    CHECK(TeamOf(mode, a) == before);
    CHECK(host.IsAlive(a));

    //2 v 1 after a third joins the smaller side... make the joiner's team the
    //bigger one and switch them back.
    const PlayerId c = host.Join(mode);
    const Team big = TeamOf(mode, c);
    PlayerId mover = TeamOf(mode, a) == big ? a : (TeamOf(mode, b) == big ? b : c);
    mode.OnCommand(host, mover, command);
    CHECK(TeamOf(mode, mover) == Other(big));

    //Switching kills without a death, and respawns at the new base later.
    CHECK_FALSE(host.IsAlive(mover));
    CHECK(mode.State().Find(mover)->Deaths == 0);
    host.Run(mode, CtfRules{}.RespawnDelayTicks + 1);
    CHECK(host.IsAlive(mover));
    CHECK(DistanceFromBase(host, mover, Other(big)) < 8.0f);
}

TEST_CASE("A killed player respawns after the delay, at their own base")
{
    CtfMode mode(CtfRules{}, Size, 1);
    FakeHost host(Floor());
    const PlayerId a = host.Join(mode);
    const PlayerId b = host.Join(mode);

    host.RuleKill(mode, a, b);
    CHECK(mode.State().Find(a)->RespawnTick == host.Now + CtfRules{}.RespawnDelayTicks);

    host.Run(mode, CtfRules{}.RespawnDelayTicks - 1);
    CHECK_FALSE(host.IsAlive(a));
    host.Run(mode, 1);
    CHECK(host.IsAlive(a));
    CHECK(mode.State().Find(a)->RespawnTick == 0);
    CHECK(DistanceFromBase(host, a, TeamOf(mode, a)) < 8.0f);
}

TEST_CASE("A spawn still works after the base has been dug out")
{
    CtfMode mode(CtfRules{}, Size, 1);
    FakeHost host(Floor());

    //Dig a pit at both bases.
    for (const glm::ivec2 centre : TerrainGen::FortCentres(Size))
        for (int dz = -3; dz <= 3; ++dz)
            for (int dx = -3; dx <= 3; ++dx)
                host.Terrain().SetBlock(centre.x + dx, 0, centre.y + dz, BlockId{ 0 });

    const PlayerId a = host.Join(mode);
    CHECK(host.IsAlive(a));
    CHECK(host.Match().Player(a).Position().y > 0.0f);
}

TEST_CASE("No friendly fire, and no damage during spawn protection")
{
    CtfMode mode(CtfRules{}, Size, 1);
    FakeHost host(Floor());
    const PlayerId a = host.Join(mode);
    const PlayerId b = host.Join(mode);
    const PlayerId c = host.Join(mode);
    const PlayerId mate = TeamOf(mode, c) == TeamOf(mode, a) ? a : b;

    CHECK_FALSE(mode.CanDamage(host, c, mate));

    const PlayerId enemy = TeamOf(mode, c) == TeamOf(mode, a) ? b : a;
    CHECK_FALSE(mode.CanDamage(host, c, enemy));   //just spawned
    host.Run(mode, CtfRules{}.SpawnProtectionTicks);
    CHECK(mode.CanDamage(host, c, enemy));
}

TEST_CASE("Leaving removes the player from the roster")
{
    CtfMode mode(CtfRules{}, Size, 1);
    FakeHost host(Floor());
    const PlayerId a = host.Join(mode);
    host.Leave(mode, a);
    CHECK(mode.State().Find(a) == nullptr);
}

TEST_CASE("Every change is reported once")
{
    CtfMode mode(CtfRules{}, Size, 1);
    FakeHost host(Floor());
    host.Join(mode);
    CHECK(mode.TakeStateChanged());
    CHECK_FALSE(mode.TakeStateChanged());
}
```

- [ ] **Step 3: Implement `CtfMode`** (`CtfMode.h`):

```cpp
#pragma once

#include "CtfRules.h"
#include "CtfState.h"

#include "Cubit/Net/GameMode.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <map>

namespace CubitGame
{
    class CtfMode final : public GameMode
    {
    public:
        CtfMode(const CtfRules& rules, const glm::ivec3& mapSize, std::uint32_t seed);

        void OnJoin(GameModeHost& host, PlayerId player) override;
        void OnLeave(GameModeHost& host, PlayerId player) override;
        bool CanDamage(const GameModeHost& host, PlayerId shooter, PlayerId victim) const override;
        void OnKilled(GameModeHost& host, PlayerId victim, PlayerId killer) override;
        void OnCommand(GameModeHost& host, PlayerId player, std::span<const std::uint8_t> bytes) override;
        void Step(GameModeHost& host) override;
        bool TakeStateChanged() override;
        std::vector<std::uint8_t> EncodeState() const override;

        const CtfState& State() const { return m_State; }

    private:
        Team SmallerTeam();
        void Spawn(GameModeHost& host, PlayerId player);
        void QueueRespawn(GameModeHost& host, PlayerId player);
        void Changed() { m_Changed = true; }

        CtfRules m_Rules;
        glm::ivec3 m_MapSize;
        std::array<glm::ivec2, 2> m_Bases;
        std::uint32_t m_Random;
        CtfState m_State;
        bool m_Changed = false;

        //Server-only: never sent.
        std::map<PlayerId, std::uint64_t> m_ProtectedUntil;
    };
}
```

  `CtfMode.cpp` for this task. Phases and the intel come in Tasks 9 and 10; `Step` only
  handles respawns here:

```cpp
#include "CtfMode.h"

#include "Cubit/Logger.h"
#include "Cubit/Voxel/CharacterController.h"
#include "Cubit/Voxel/SpawnFinder.h"
#include "Cubit/Voxel/TerrainGen.h"

#include <algorithm>

namespace CubitGame
{
    CtfMode::CtfMode(const CtfRules& rules, const glm::ivec3& mapSize, std::uint32_t seed)
        : m_Rules(rules), m_MapSize(mapSize), m_Bases(TerrainGen::FortCentres(mapSize)),
          m_Random(seed * 2654435761u + 1u)
    {
    }

    Team CtfMode::SmallerTeam()
    {
        const int blue = m_State.TeamSize(Team::Blue);
        const int green = m_State.TeamSize(Team::Green);
        if (blue != green)
            return blue < green ? Team::Blue : Team::Green;

        //xorshift: the same seed balances the same way on every build.
        m_Random ^= m_Random << 13;
        m_Random ^= m_Random >> 17;
        m_Random ^= m_Random << 5;
        return (m_Random & 1u) ? Team::Green : Team::Blue;
    }

    void CtfMode::OnJoin(GameModeHost& host, PlayerId player)
    {
        m_State.Players.push_back(PlayerStats{ player, SmallerTeam() });
        Spawn(host, player);
        Changed();
    }

    void CtfMode::OnLeave(GameModeHost&, PlayerId player)
    {
        std::erase_if(m_State.Players, [player](const PlayerStats& s) { return s.Player == player; });
        m_ProtectedUntil.erase(player);
        Changed();
    }

    bool CtfMode::CanDamage(const GameModeHost& host, PlayerId shooter, PlayerId victim) const
    {
        const PlayerStats* a = m_State.Find(shooter);
        const PlayerStats* b = m_State.Find(victim);
        if (a == nullptr || b == nullptr || a->Side == b->Side)
            return false;

        const auto protectedUntil = m_ProtectedUntil.find(victim);
        return protectedUntil == m_ProtectedUntil.end() || host.Tick() >= protectedUntil->second;
    }

    void CtfMode::OnKilled(GameModeHost& host, PlayerId victim, PlayerId killer)
    {
        (void)killer;   //scored in Task 9, once phases exist
        QueueRespawn(host, victim);
        Changed();
    }

    void CtfMode::OnCommand(GameModeHost& host, PlayerId player, std::span<const std::uint8_t> bytes)
    {
        PlayerStats* stats = m_State.Find(player);
        if (stats == nullptr || bytes.empty() || bytes[0] != SwitchTeamCommand)
            return;

        //After the move the new team has `after` and the old one `left`; the
        //switch may not leave them more than one apart. 1 v 1 gives 2 v 0:
        //refused. 2 v 1, moving off the bigger side, gives 1 v 2: allowed.
        const Team to = Other(stats->Side);
        const int after = m_State.TeamSize(to) + 1;
        const int left = m_State.TeamSize(stats->Side) - 1;
        if (after - left > 1)
            return;

        stats->Side = to;
        host.Kill(player);
        QueueRespawn(host, player);
        Changed();
    }

    void CtfMode::QueueRespawn(GameModeHost& host, PlayerId player)
    {
        if (PlayerStats* stats = m_State.Find(player))
            stats->RespawnTick = host.Tick() + m_Rules.RespawnDelayTicks;
    }

    void CtfMode::Spawn(GameModeHost& host, PlayerId player)
    {
        PlayerStats* stats = m_State.Find(player);
        if (stats == nullptr)
            return;

        const World& world = host.Match().GetWorld();
        const glm::vec3 half = CharacterConfig{}.HalfExtents;
        const glm::ivec2 base = m_Bases[IndexOf(stats->Side)];

        std::optional<glm::vec3> at = FindSpawn(world, base, half);
        if (!at.has_value())
        {
            CB_WARN("No spawn near the base at " + std::to_string(base.x) + "," + std::to_string(base.y)
                + "; trying the map centre");
            at = FindSpawn(world, glm::ivec2(m_MapSize.x / 2, m_MapSize.z / 2), half);
        }

        //Never leave a player unspawned: the top of the centre column is still
        //somewhere to stand, if a bad one.
        const glm::vec3 position = at.value_or(glm::vec3(m_MapSize.x / 2 + 0.5f,
            static_cast<float>(m_MapSize.y) + half.y, m_MapSize.z / 2 + 0.5f));

        host.Respawn(player, position);
        stats->RespawnTick = 0;
        m_ProtectedUntil[player] = host.Tick() + m_Rules.SpawnProtectionTicks;
    }

    void CtfMode::Step(GameModeHost& host)
    {
        for (PlayerStats& stats : m_State.Players)
        {
            if (stats.RespawnTick != 0 && host.Tick() >= stats.RespawnTick)
            {
                Spawn(host, stats.Player);
                Changed();
            }
        }
    }

    bool CtfMode::TakeStateChanged()
    {
        const bool was = m_Changed;
        m_Changed = false;
        return was;
    }

    std::vector<std::uint8_t> CtfMode::EncodeState() const
    {
        return Encode(m_State);
    }
}
```

  Check that `FindSpawn` returns a box *centre*, as `Respawn` expects. Read
  `SpawnFinder.h`'s comment. If it returns feet, add `half.y`.

- [ ] **Step 4: Regenerate premake and build.** The new cases pass. **Commit** —
  `"Capture the intel, part one: teams, balancing, switching and spawning"`

---

### Task 9: `CtfMode` — phases and scoring

**Files:** modify `game/Game/src/CtfMode.h/.cpp`; add tests to `CtfModeTests.cpp`

**Interfaces:**
- Produces: private `void StartMatch(GameModeHost&)`, `void EndMatch(GameModeHost&, Team winner)`,
  `void ResetMatch(GameModeHost&)`, `void Record(EventKind, Team, PlayerId)` and
  `void PlaceIntelsHome(const World&)`. It also fills `Intels[i].HomeSpot`.

- [ ] **Step 1: Failing tests** (append)

```cpp
namespace
{
    CtfRules Quick()
    {
        CtfRules rules;
        rules.CountdownTicks = 10;
        rules.TimeLimitTicks = 1000;
        rules.EndScreenTicks = 20;
        return rules;
    }

    //Two players, one each side, run through the countdown into the match.
    struct Match
    {
        CtfMode Mode;
        FakeHost Host;
        PlayerId Blue = InvalidPlayer, Green = InvalidPlayer;

        explicit Match(const CtfRules& rules = Quick()) : Mode(rules, Size, 1), Host(Floor())
        {
            const PlayerId a = Host.Join(Mode);
            const PlayerId b = Host.Join(Mode);
            Blue = TeamOf(Mode, a) == Team::Blue ? a : b;
            Green = Blue == a ? b : a;
        }

        void Begin() { Host.Run(Mode, Quick().CountdownTicks + 2); }
    };
}

TEST_CASE("Warmup waits for both teams, then counts down into the match")
{
    CtfMode mode(Quick(), Size, 1);
    FakeHost host(Floor());
    host.Join(mode);
    host.Run(mode, 100);
    CHECK(mode.State().Stage == Phase::Warmup);

    host.Join(mode);
    host.Run(mode, 1);
    CHECK(mode.State().Stage == Phase::Countdown);
    host.Run(mode, Quick().CountdownTicks + 1);
    CHECK(mode.State().Stage == Phase::Active);
    CHECK(mode.State().Events.back().Kind == EventKind::MatchStarted);
}

TEST_CASE("The countdown starts over if a team empties")
{
    CtfMode mode(Quick(), Size, 1);
    FakeHost host(Floor());
    host.Join(mode);
    const PlayerId b = host.Join(mode);
    host.Run(mode, 3);
    host.Leave(mode, b);
    host.Run(mode, 1);
    CHECK(mode.State().Stage == Phase::Warmup);
}

TEST_CASE("Kills count in the match and not in warmup")
{
    Match m;
    m.Host.RuleKill(m.Mode, m.Green, m.Blue);
    CHECK(m.Mode.State().Find(m.Blue)->Kills == 0);

    m.Host.Run(m.Mode, 100);
    m.Begin();
    REQUIRE(m.Mode.State().Stage == Phase::Active);
    m.Host.RuleKill(m.Mode, m.Green, m.Blue);
    CHECK(m.Mode.State().Find(m.Blue)->Kills == 1);
    CHECK(m.Mode.State().Find(m.Green)->Deaths == 1);
}

TEST_CASE("The time limit ends the match on captures, a draw when level")
{
    Match m;
    m.Begin();
    m.Host.Run(m.Mode, Quick().TimeLimitTicks + 1);
    CHECK(m.Mode.State().Stage == Phase::Ended);
    CHECK(m.Mode.State().Winner == Team::None);
    CHECK(m.Mode.State().Events.back().Kind == EventKind::Draw);
}

TEST_CASE("After the end screen the world, scores and intels reset and warmup begins")
{
    Match m;
    m.Begin();
    m.Host.RuleKill(m.Mode, m.Green, m.Blue);
    m.Host.Run(m.Mode, Quick().TimeLimitTicks + 1);
    REQUIRE(m.Mode.State().Stage == Phase::Ended);

    m.Host.Run(m.Mode, Quick().EndScreenTicks + 1);
    CHECK(m.Host.Resets == 1);
    CHECK(m.Mode.State().Find(m.Blue)->Kills == 0);
    CHECK(m.Mode.State().Captures == std::array<std::uint8_t, 2>{ 0, 0 });
    CHECK(m.Host.IsAlive(m.Blue));
    CHECK(m.Host.IsAlive(m.Green));
    //Both teams are still here, so warmup goes straight to its countdown.
    CHECK((m.Mode.State().Stage == Phase::Warmup || m.Mode.State().Stage == Phase::Countdown));
}

TEST_CASE("The intel's home is on top of each fort")
{
    Match m;
    m.Begin();
    for (int i = 0; i < 2; ++i)
    {
        const Intel& intel = m.Mode.State().Intels[i];
        CHECK(intel.Status == IntelStatus::Home);
        CHECK(intel.HomeSpot.y == doctest::Approx(1.0f));   //on the floor at y 0
        CHECK(intel.Position == intel.HomeSpot);
    }
}
```

- [ ] **Step 2: Build; the tests fail.**

- [ ] **Step 3: Implement.** In `Step`, before the respawn loop:

```cpp
        const std::uint64_t now = host.Tick();
        const bool bothTeams = m_State.TeamSize(Team::Blue) > 0 && m_State.TeamSize(Team::Green) > 0;

        switch (m_State.Stage)
        {
        case Phase::Warmup:
            if (bothTeams)
            {
                m_State.Stage = Phase::Countdown;
                m_State.StageEndTick = now + m_Rules.CountdownTicks;
                Changed();
            }
            break;

        case Phase::Countdown:
            if (!bothTeams)
            {
                m_State.Stage = Phase::Warmup;
                m_State.StageEndTick = 0;
                Changed();
            }
            else if (now >= m_State.StageEndTick)
                StartMatch(host);
            break;

        case Phase::Active:
            if (now >= m_State.StageEndTick)
            {
                const auto [blue, green] = std::pair{ m_State.Captures[0], m_State.Captures[1] };
                EndMatch(host, blue == green ? Team::None : (blue > green ? Team::Blue : Team::Green));
            }
            break;

        case Phase::Ended:
            if (now >= m_State.StageEndTick)
                ResetMatch(host);
            break;
        }
```

  The helpers:

```cpp
    void CtfMode::Record(EventKind kind, Team side, PlayerId player)
    {
        const std::uint64_t serial = m_State.Events.empty() ? 1 : m_State.Events.back().Serial + 1;
        m_State.Events.push_back(MatchEvent{ serial, kind, side, player });
        while (m_State.Events.size() > m_Rules.EventHistory)
            m_State.Events.pop_front();
        Changed();
    }

    void CtfMode::PlaceIntelsHome(const World& world)
    {
        for (int i = 0; i < 2; ++i)
        {
            const glm::ivec2 base = m_Bases[i];
            int top = 0;
            for (int y = world.GetHeight() - 1; y >= 0; --y)
                if (world.IsBlockSolid(base.x, y, base.y)) { top = y + 1; break; }

            Intel& intel = m_State.Intels[i];
            intel.HomeSpot = glm::vec3(base.x + 0.5f, static_cast<float>(top), base.y + 0.5f);
            intel.Position = intel.HomeSpot;
            intel.Status = IntelStatus::Home;
            intel.Carrier = InvalidPlayer;
        }
    }

    void CtfMode::StartMatch(GameModeHost& host)
    {
        m_State.Stage = Phase::Active;
        m_State.StageEndTick = host.Tick() + m_Rules.TimeLimitTicks;
        m_State.Captures = { 0, 0 };
        m_State.Winner = Team::None;
        for (PlayerStats& stats : m_State.Players)
            stats.Kills = stats.Deaths = stats.Captures = 0;
        PlaceIntelsHome(host.Match().GetWorld());
        Record(EventKind::MatchStarted, Team::None, InvalidPlayer);
    }

    void CtfMode::EndMatch(GameModeHost& host, Team winner)
    {
        m_State.Stage = Phase::Ended;
        m_State.StageEndTick = host.Tick() + m_Rules.EndScreenTicks;
        m_State.Winner = winner;
        Record(winner == Team::None ? EventKind::Draw : EventKind::MatchWon, winner, InvalidPlayer);
    }

    void CtfMode::ResetMatch(GameModeHost& host)
    {
        host.ResetWorld();
        m_State.Captures = { 0, 0 };
        m_State.Winner = Team::None;
        for (PlayerStats& stats : m_State.Players)
        {
            stats.Kills = stats.Deaths = stats.Captures = 0;
            host.Kill(stats.Player);
            Spawn(host, stats.Player);
        }
        PlaceIntelsHome(host.Match().GetWorld());
        m_State.Stage = Phase::Warmup;
        m_State.StageEndTick = 0;
        Changed();
    }
```

  `OnKilled` now scores:

```cpp
    void CtfMode::OnKilled(GameModeHost& host, PlayerId victim, PlayerId killer)
    {
        if (m_State.Stage == Phase::Active)
        {
            if (PlayerStats* k = m_State.Find(killer)) ++k->Kills;
            if (PlayerStats* v = m_State.Find(victim)) ++v->Deaths;
        }
        DropIntelOf(host, victim);   //Task 10; declare it now as an empty private method
        QueueRespawn(host, victim);
        Changed();
    }
```

  Also call `PlaceIntelsHome(host.Match().GetWorld())` from the first `OnJoin`, so the home
  spots exist before the first match. Add `#include <utility>`.

- [ ] **Step 4: Build.** Every CTF case passes. **Commit** —
  `"Capture the intel, part two: warmup, the match, the end, and the reset"`

---

### Task 10: `CtfMode` — the intel

**Files:** modify `CtfMode.h/.cpp`; add tests to `CtfModeTests.cpp`

**Interfaces:**
- Produces: private `void DropIntelOf(GameModeHost&, PlayerId)`,
  `void StepIntels(GameModeHost&)`, and `bool Touches(const GameModeHost&, PlayerId, const glm::vec3&) const`.

- [ ] **Step 1: Failing tests** (append; these use `Match`, `Quick` and the `Base` helper)

```cpp
namespace
{
    //Stand a player's box so its feet are on the spot.
    void StandAt(FakeHost& host, PlayerId player, const glm::vec3& spot)
    {
        host.Place(player, spot + glm::vec3(0.0f, CharacterConfig{}.HalfExtents.y, 0.0f));
    }

    const Intel& IntelOf(const CtfMode& mode, Team owner) { return mode.State().Intels[IndexOf(owner)]; }
}

TEST_CASE("An enemy takes the intel by touching it, but not in warmup")
{
    Match m;
    StandAt(m.Host, m.Blue, IntelOf(m.Mode, Team::Green).HomeSpot);
    m.Host.Run(m.Mode, 1);
    CHECK(IntelOf(m.Mode, Team::Green).Status == IntelStatus::Home);

    m.Begin();
    StandAt(m.Host, m.Blue, IntelOf(m.Mode, Team::Green).HomeSpot);
    m.Host.Run(m.Mode, 1);
    CHECK(IntelOf(m.Mode, Team::Green).Status == IntelStatus::Carried);
    CHECK(IntelOf(m.Mode, Team::Green).Carrier == m.Blue);
    CHECK(m.Mode.State().Events.back().Kind == EventKind::PickedUp);
}

TEST_CASE("A player cannot take their own intel")
{
    Match m;
    m.Begin();
    StandAt(m.Host, m.Blue, IntelOf(m.Mode, Team::Blue).HomeSpot);
    m.Host.Run(m.Mode, 1);
    CHECK(IntelOf(m.Mode, Team::Blue).Status == IntelStatus::Home);
}

TEST_CASE("Carrying it home scores a capture and sends it back")
{
    Match m;
    m.Begin();
    StandAt(m.Host, m.Blue, IntelOf(m.Mode, Team::Green).HomeSpot);
    m.Host.Run(m.Mode, 1);

    StandAt(m.Host, m.Blue, IntelOf(m.Mode, Team::Blue).HomeSpot + glm::vec3(2.0f, 0.0f, 0.0f));
    m.Host.Run(m.Mode, 1);

    CHECK(m.Mode.State().Captures[IndexOf(Team::Blue)] == 1);
    CHECK(m.Mode.State().Find(m.Blue)->Captures == 1);
    CHECK(IntelOf(m.Mode, Team::Green).Status == IntelStatus::Home);
    CHECK(m.Mode.State().Events.back().Kind == EventKind::Captured);
}

TEST_CASE("Three captures win")
{
    Match m;
    m.Begin();
    for (int i = 0; i < 3; ++i)
    {
        StandAt(m.Host, m.Blue, IntelOf(m.Mode, Team::Green).HomeSpot);
        m.Host.Run(m.Mode, 1);
        StandAt(m.Host, m.Blue, IntelOf(m.Mode, Team::Blue).HomeSpot);
        m.Host.Run(m.Mode, 1);
    }
    CHECK(m.Mode.State().Stage == Phase::Ended);
    CHECK(m.Mode.State().Winner == Team::Blue);
    CHECK(m.Mode.State().Events.back().Kind == EventKind::MatchWon);
}

TEST_CASE("The carrier's death drops it where they fell; the owner's touch returns it")
{
    Match m;
    m.Begin();
    StandAt(m.Host, m.Blue, IntelOf(m.Mode, Team::Green).HomeSpot);
    m.Host.Run(m.Mode, 1);

    const glm::vec3 fell(30.5f, 1.0f, 16.5f);
    StandAt(m.Host, m.Blue, fell);
    m.Host.Run(m.Mode, 1);
    m.Host.RuleKill(m.Mode, m.Blue, m.Green);

    CHECK(IntelOf(m.Mode, Team::Green).Status == IntelStatus::Dropped);
    CHECK(glm::distance(IntelOf(m.Mode, Team::Green).Position, fell) < 0.01f);

    StandAt(m.Host, m.Green, fell);
    m.Host.Run(m.Mode, 1);
    CHECK(IntelOf(m.Mode, Team::Green).Status == IntelStatus::Home);
    CHECK(m.Mode.State().Events.back().Kind == EventKind::Returned);
}

TEST_CASE("A dropped intel an enemy touches is taken again")
{
    Match m;
    m.Begin();
    StandAt(m.Host, m.Blue, IntelOf(m.Mode, Team::Green).HomeSpot);
    m.Host.Run(m.Mode, 1);
    const glm::vec3 fell(30.5f, 1.0f, 16.5f);
    StandAt(m.Host, m.Blue, fell);
    m.Host.Run(m.Mode, 1);
    m.Host.RuleKill(m.Mode, m.Blue, m.Green);

    //Blue respawns and walks back to it.
    m.Host.Run(m.Mode, Quick().RespawnDelayTicks + 1);
    StandAt(m.Host, m.Blue, fell);
    m.Host.Run(m.Mode, 1);
    CHECK(IntelOf(m.Mode, Team::Green).Status == IntelStatus::Carried);
}

TEST_CASE("An untouched dropped intel goes home after the timeout")
{
    Match m;
    m.Begin();
    StandAt(m.Host, m.Blue, IntelOf(m.Mode, Team::Green).HomeSpot);
    m.Host.Run(m.Mode, 1);
    StandAt(m.Host, m.Blue, glm::vec3(30.5f, 1.0f, 16.5f));
    m.Host.Run(m.Mode, 1);
    m.Host.RuleKill(m.Mode, m.Blue, m.Green);
    StandAt(m.Host, m.Green, glm::vec3(50.5f, 1.0f, 2.5f));   //away from it

    m.Host.Run(m.Mode, Quick().DroppedReturnTicks - 1);
    CHECK(IntelOf(m.Mode, Team::Green).Status == IntelStatus::Dropped);
    m.Host.Run(m.Mode, 2);
    CHECK(IntelOf(m.Mode, Team::Green).Status == IntelStatus::Home);
}

TEST_CASE("Leaving or switching while carrying drops it")
{
    Match m;
    m.Begin();
    StandAt(m.Host, m.Blue, IntelOf(m.Mode, Team::Green).HomeSpot);
    m.Host.Run(m.Mode, 1);
    m.Host.Leave(m.Mode, m.Blue);
    CHECK(IntelOf(m.Mode, Team::Green).Status == IntelStatus::Dropped);
}

TEST_CASE("A drop below the world goes straight home")
{
    Match m;
    m.Begin();
    StandAt(m.Host, m.Blue, IntelOf(m.Mode, Team::Green).HomeSpot);
    m.Host.Run(m.Mode, 1);
    m.Host.Place(m.Blue, glm::vec3(30.5f, -20.0f, 16.5f));
    m.Host.Run(m.Mode, 1);
    m.Host.RuleKill(m.Mode, m.Blue, m.Green);
    CHECK(IntelOf(m.Mode, Team::Green).Status == IntelStatus::Home);
}

TEST_CASE("The dead cannot take or return an intel")
{
    Match m;
    m.Begin();
    m.Host.Kill(m.Blue);
    StandAt(m.Host, m.Blue, IntelOf(m.Mode, Team::Green).HomeSpot);
    m.Host.Run(m.Mode, 1);
    CHECK(IntelOf(m.Mode, Team::Green).Status == IntelStatus::Home);
}
```

(Add `#include "Cubit/Voxel/CharacterController.h"` to the test file.)

- [ ] **Step 2: Build; the tests fail.**

- [ ] **Step 3: Implement.** Call `StepIntels(host)` at the end of `Step`, only when
  `m_State.Stage == Phase::Active`. Then:

```cpp
    bool CtfMode::Touches(const GameModeHost& host, PlayerId player, const glm::vec3& point) const
    {
        if (!host.IsAlive(player))
            return false;

        const CharacterController& body = host.Match().Player(player);
        const glm::vec3 lo = body.Position() - body.HalfExtents();
        const glm::vec3 hi = body.Position() + body.HalfExtents();
        const glm::vec3 nearest = glm::clamp(point, lo, hi);
        return glm::distance(nearest, point) <= m_Rules.PickupRadius;
    }

    void CtfMode::DropIntelOf(GameModeHost& host, PlayerId player)
    {
        for (int i = 0; i < 2; ++i)
        {
            Intel& intel = m_State.Intels[i];
            if (intel.Status != IntelStatus::Carried || intel.Carrier != player)
                continue;

            const glm::vec3 feet = host.Match().HasPlayer(player)
                ? host.Match().Player(player).Position()
                    - glm::vec3(0.0f, host.Match().Player(player).HalfExtents().y, 0.0f)
                : intel.Position;

            intel.Carrier = InvalidPlayer;
            if (feet.y < 0.0f)
            {
                intel.Status = IntelStatus::Home;
                intel.Position = intel.HomeSpot;
                Record(EventKind::Returned, TeamAt(i), InvalidPlayer);
            }
            else
            {
                intel.Status = IntelStatus::Dropped;
                intel.Position = feet;
                m_DroppedAt[i] = host.Tick();
            }
            Changed();
        }
    }

    void CtfMode::StepIntels(GameModeHost& host)
    {
        for (int i = 0; i < 2; ++i)
        {
            Intel& intel = m_State.Intels[i];
            const Team owner = TeamAt(i);

            if (intel.Status == IntelStatus::Carried)
            {
                const CharacterController& carrier = host.Match().Player(intel.Carrier);
                intel.Position = carrier.Position() + glm::vec3(0.0f, carrier.HalfExtents().y, 0.0f);

                //Home: within the base radius of the carrier's own intel's spot.
                const glm::vec3 home = m_State.Intels[IndexOf(Other(owner))].HomeSpot;
                const glm::vec3 at = carrier.Position();
                const bool inBase = glm::distance(glm::vec2(at.x, at.z), glm::vec2(home.x, home.z)) <= m_Rules.BaseRadius
                    && std::abs(at.y - home.y) <= m_Rules.BaseRadius;

                if (inBase)
                {
                    const Team scorer = Other(owner);
                    ++m_State.Captures[IndexOf(scorer)];
                    if (PlayerStats* s = m_State.Find(intel.Carrier)) ++s->Captures;
                    Record(EventKind::Captured, scorer, intel.Carrier);
                    intel.Status = IntelStatus::Home;
                    intel.Carrier = InvalidPlayer;
                    intel.Position = intel.HomeSpot;

                    if (m_State.Captures[IndexOf(scorer)] >= m_Rules.CapturesToWin)
                    {
                        EndMatch(host, scorer);
                        return;
                    }
                }
                Changed();
                continue;
            }

            if (intel.Status == IntelStatus::Dropped && host.Tick() - m_DroppedAt[i] >= m_Rules.DroppedReturnTicks)
            {
                intel.Status = IntelStatus::Home;
                intel.Position = intel.HomeSpot;
                Record(EventKind::Returned, owner, InvalidPlayer);
                continue;
            }

            for (const PlayerStats& stats : m_State.Players)
            {
                if (!Touches(host, stats.Player, intel.Position))
                    continue;

                if (stats.Side != owner)
                {
                    intel.Status = IntelStatus::Carried;
                    intel.Carrier = stats.Player;
                    Record(EventKind::PickedUp, owner, stats.Player);
                    break;
                }

                if (intel.Status == IntelStatus::Dropped)
                {
                    intel.Status = IntelStatus::Home;
                    intel.Position = intel.HomeSpot;
                    Record(EventKind::Returned, owner, stats.Player);
                    break;
                }
            }
        }
    }
```

  Add the member `std::array<std::uint64_t, 2> m_DroppedAt{};`. Call `DropIntelOf` from
  `OnLeave` (before erasing the stats) and from `OnCommand` (before switching the team). In
  `ResetMatch`, `PlaceIntelsHome` already sends both intels home.

- [ ] **Step 4: Build.** Every CTF case passes. **Commit** —
  `"Capture the intel, part three: take it, carry it, drop it, return it, score it"`

---

### Task 11: The server runs capture the intel

**Files:** modify `game/Server/src/Server.cpp`

- [ ] **Step 1:** After the world is built and before `MatchServer` is constructed:

```cpp
        //The match's rules: capture the intel, in the game, behind the engine's
        //rule-free hook. Seeded from the clock so team ties do not always
        //break the same way between matches.
        CubitGame::CtfMode mode(CubitGame::CtfRules{},
            glm::ivec3(world.GetWidth(), world.GetHeight(), world.GetDepth()),
            static_cast<std::uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
```

  Pass `&mode` as the last `MatchServer` argument, with `ServerRules` before it. Take the
  width, height and depth before `std::move(world)`. Include `"CtfMode.h"`.

- [ ] **Step 2: Build, then run it.** Start the server with `--duration 20` and two
  `GameApp --connect 127.0.0.1` clients, using the scripted launch from
  `docs/superpowers/plans/2026-09-17-b8-engine-game-split.md` Task 7, or the WM_CLOSE
  pattern in memory. Check:
  - the server's log shows no errors;
  - both clients exit 0;
  - both clients reported snapshots with 0 corrections.

- [ ] **Step 3: Commit** — `"The server plays capture the intel"`

---

## Stage 3 — What the player sees and hears

### Task 12: Pure presentation — HUD text, scoreboard rows, team and intel models

**Files:**
- Create: `game/Game/src/MatchHud.h`, `game/Game/src/MatchModels.h`
- Test: create `game/GameTests/src/MatchHudTests.cpp`

**Interfaces:**
- Produces, in `CubitGame::`:
  - `std::string ScoreLine(const CtfState&)` gives `"BLUE 1 - 0 GREEN"`.
  - `std::string PhaseLine(const CtfState&, std::uint64_t serverTick)` gives `"WARMUP"`,
    `"STARTS IN 7"`, `"12:43"`, `"BLUE WINS"`, `"GREEN WINS"` or `"DRAW"`. Countdowns
    round up.
  - `std::string RespawnLine(const PlayerStats&, std::uint64_t serverTick)` gives
    `"RESPAWN IN 3"`, or `""` when not waiting.
  - `std::string EventLine(const MatchEvent&, Team local)`.
  - `struct ScoreRow { PlayerId Player; Team Side; std::uint16_t Kills, Deaths, Captures; bool Local; };`
  - `std::vector<ScoreRow> ScoreboardRows(const CtfState&, PlayerId local)`, sorted by team
    (Blue first), then captures and then kills descending, then id.
  - `VoxModel TeamModel(const VoxModel& player, Team)`, which sets palette entry 2 to
    `TeamColour`.
  - `glm::vec4 TeamColour(Team)`: Blue (0.25, 0.45, 0.95, 1) and Green (0.25, 0.75, 0.30, 1).
  - `VoxModel IntelModel(Team)`, a 6 × 6 × 6 solid cube of palette entry 1 in the team
    colour.

- [ ] **Step 1: Failing tests** (`MatchHudTests.cpp`)

```cpp
#include <doctest.h>

#include "MatchHud.h"
#include "MatchModels.h"

using namespace CubitGame;

TEST_CASE("The score line and every phase line")
{
    CtfState s;
    s.Captures = { 1, 0 };
    CHECK(ScoreLine(s) == "BLUE 1 - 0 GREEN");

    s.Stage = Phase::Warmup;
    CHECK(PhaseLine(s, 100) == "WARMUP");

    s.Stage = Phase::Countdown;
    s.StageEndTick = 1000;
    CHECK(PhaseLine(s, 1000 - 6 * 60 - 1) == "STARTS IN 7");   //rounds up

    s.Stage = Phase::Active;
    s.StageEndTick = 1000 + (12 * 60 + 43) * 60;
    CHECK(PhaseLine(s, 1000) == "12:43");
    CHECK(PhaseLine(s, s.StageEndTick + 50) == "0:00");

    s.Stage = Phase::Ended;
    s.Winner = Team::Green;
    CHECK(PhaseLine(s, 0) == "GREEN WINS");
    s.Winner = Team::None;
    CHECK(PhaseLine(s, 0) == "DRAW");
}

TEST_CASE("The respawn line counts down in whole seconds and is empty when alive")
{
    PlayerStats p;
    CHECK(RespawnLine(p, 50).empty());
    p.RespawnTick = 400;
    CHECK(RespawnLine(p, 400 - 2 * 60 - 30) == "RESPAWN IN 3");
}

TEST_CASE("Event lines speak from the local player's side")
{
    CHECK(EventLine(MatchEvent{ 1, EventKind::Captured, Team::Blue, 3 }, Team::Blue) == "Your team captured the intel");
    CHECK(EventLine(MatchEvent{ 1, EventKind::Captured, Team::Green, 3 }, Team::Blue) == "Green captured the intel");
    CHECK(EventLine(MatchEvent{ 1, EventKind::PickedUp, Team::Blue, 3 }, Team::Blue) == "Your intel was taken");
    CHECK(EventLine(MatchEvent{ 1, EventKind::PickedUp, Team::Green, 3 }, Team::Blue) == "Your team took the Green intel");
    CHECK(EventLine(MatchEvent{ 1, EventKind::Returned, Team::Blue, 0 }, Team::Blue) == "Your intel was returned");
    CHECK(EventLine(MatchEvent{ 1, EventKind::MatchStarted, Team::None, 0 }, Team::Blue) == "The match has started");
}

TEST_CASE("Scoreboard rows: Blue first, then by captures and kills, local marked")
{
    CtfState s;
    s.Players = {
        PlayerStats{ 1, Team::Green, 5, 0, 0 },
        PlayerStats{ 2, Team::Blue, 1, 2, 0 },
        PlayerStats{ 3, Team::Blue, 0, 0, 1 },
    };
    const std::vector<ScoreRow> rows = ScoreboardRows(s, 2);
    REQUIRE(rows.size() == 3);
    CHECK(rows[0].Player == 3);   //Blue, one capture
    CHECK(rows[1].Player == 2);
    CHECK(rows[1].Local);
    CHECK(rows[2].Player == 1);
}

TEST_CASE("A team model changes only the torso colour")
{
    VoxModel player;
    player.Size = { 2, 2, 2 };
    player.Voxels.assign(8, 1);
    player.Colors[1] = glm::vec4(0.1f);
    player.Colors[2] = glm::vec4(0.2f);
    player.Colors[3] = glm::vec4(0.3f);

    const VoxModel blue = TeamModel(player, Team::Blue);
    CHECK(blue.Colors[1] == player.Colors[1]);
    CHECK(blue.Colors[3] == player.Colors[3]);
    CHECK(blue.Colors[2] == TeamColour(Team::Blue));
    CHECK(blue.Voxels == player.Voxels);
}

TEST_CASE("The intel is a solid cube in its team's colour")
{
    const VoxModel intel = IntelModel(Team::Green);
    CHECK(intel.Size == glm::ivec3(6, 6, 6));
    CHECK(intel.Voxels.size() == 216);
    CHECK(intel.Colors[1] == TeamColour(Team::Green));
}
```

- [ ] **Step 2: Build; the tests fail.**

- [ ] **Step 3: Implement both headers** as inline functions. `EventLine` uses the local
  player's team to choose "Your team" or "Your intel" wording, as the test shows; for the
  other team it uses the name "Blue" or "Green". Seconds are
  `(ticks + TicksPerSecond - 1) / TicksPerSecond`. The clock is `M:SS`. Check `VoxModel`'s
  field names in `VoxLoader.h` (`Size`, `Voxels`, `Colors`) before writing them.

- [ ] **Step 4: Regenerate premake and build.** Passes. **Commit** —
  `"What the match says and looks like: HUD lines, scoreboard rows, team and intel models"`

---

### Task 13: GameApp shows the match

**Files:**
- Modify: `game/GameApp/src/GameApp.cpp`, `game/Game/src/GameHudLayer.h`,
  `Cubit/include/Cubit/Renderer/ScreenOverlay.h`, `Cubit/src/Renderer/ScreenOverlay.cpp`,
  `game/Game/src/SoundSynth.h/.cpp`, `game/Game/src/SoundCues.h/.cpp`,
  `game/GameTests/src/SoundCuesTests.cpp`

**Interfaces:**
- Consumes: everything above.
- Produces:
  - `void ScreenOverlay::FillRect(float x, float y, float width, float height, const glm::vec4& colour) const`.
  - Six new `Cue` values: `TeamCaptured, EnemyCaptured, IntelTaken, IntelReturned, MatchStart, MatchEnd`,
    making `CueCount` 14.
  - `void SoundCues::MatchEvents(const std::deque<MatchEvent>&, Team local, std::vector<CueToPlay>&)`.

- [ ] **Step 1: `FillRect`.** In `ScreenOverlay.cpp`, beside `FillScreen`, it is
  `DrawQuad(*m_White, x, y, width, height, glm::vec2(0.0f), glm::vec2(1.0f), colour);`.
  Declare it with the comment "A solid rectangle in pixel space, y up, for panels behind
  text."

- [ ] **Step 2: Sounds.**
  - **The cues:** add the six values to `Cue` and bump `CueCount`. Add names in `CueName`.
    Synthesise them from the existing helpers:
    - `TeamCaptured`: rising 800, 1200, 1600 Hz blips.
    - `EnemyCaptured`: falling 900, 600 Hz.
    - `IntelTaken`: two quick 1000 Hz pips.
    - `IntelReturned`: a single 700 Hz tone.
    - `MatchStart`: a 0.6 s 440 Hz horn at 0.8 peak.
    - `MatchEnd`: a 330/415/494 Hz chord, 0.8 s.

    `SoundSynthTests` covers every cue already, because it loops to `CueCount`.
  - **The test:** add to `SoundCuesTests.cpp`:

```cpp
TEST_CASE("Match events sound from the local team's side, each once")
{
    SoundCues cues;
    std::vector<CueToPlay> out;
    std::deque<CubitGame::MatchEvent> events{
        CubitGame::MatchEvent{ 1, CubitGame::EventKind::Captured, CubitGame::Team::Blue, 3 },
        CubitGame::MatchEvent{ 2, CubitGame::EventKind::Captured, CubitGame::Team::Green, 4 },
        CubitGame::MatchEvent{ 3, CubitGame::EventKind::PickedUp, CubitGame::Team::Blue, 4 } };

    cues.MatchEvents(events, CubitGame::Team::Blue, out);
    REQUIRE(out.size() == 3);
    CHECK(out[0].Sound == Cue::TeamCaptured);
    CHECK(out[1].Sound == Cue::EnemyCaptured);
    CHECK(out[2].Sound == Cue::IntelTaken);
    CHECK_FALSE(out[0].Positioned);

    out.clear();
    cues.MatchEvents(events, CubitGame::Team::Blue, out);
    CHECK(out.empty());
}
```

    Implement it with a third `NewEvents` member, `m_Match`. The mapping:
    - `Captured` → `TeamCaptured` or `EnemyCaptured`, by `Side == local`;
    - `PickedUp` → `IntelTaken` only when `Side == local` (your intel), nothing otherwise;
    - `Returned` → `IntelReturned`;
    - `MatchStarted` → `MatchStart`;
    - `MatchWon` or `Draw` → `MatchEnd`.

    Include `"CtfState.h"`. `SkipHistory` gains the match events as a third parameter; update
    its one caller and the existing "joining" test.

- [ ] **Step 3: GameApp — state, models and drawing.** In `PlayerLayer`:
  - **Members:** `std::optional<CubitGame::CtfState> m_Ctf;`,
    `std::uint64_t m_CtfSerial = 0;`, `bool m_CtfBroken = false;`,
    `std::array<std::unique_ptr<Mesh>, 2> m_TeamMeshes;`,
    `std::array<std::unique_ptr<Mesh>, 2> m_IntelMeshes;`, and a
    `std::deque<std::pair<std::string, std::uint64_t>> m_Feed;` of lines and their expiry
    tick.
  - **Constructor, after the player model loads:** for i in 0..1,
    `m_TeamMeshes[i] = std::make_unique<Mesh>(ModelMesher::Build(CubitGame::TeamModel(model, CubitGame::TeamAt(i))));`
    and `m_IntelMeshes[i] = std::make_unique<Mesh>(ModelMesher::Build(CubitGame::IntelModel(CubitGame::TeamAt(i))));`.
  - **In `OnFixedUpdate`, after the client steps:** if
    `m_Client->GameStateSerial() != m_CtfSerial`, decode with `DecodeCtfState`. On failure,
    log once, set `m_CtfBroken` and reset `m_Ctf`; on success, set `m_Ctf`. Update the
    serial either way.
  - **`DrawRemotePlayers`:**
    - skip `!m_Client->IsAlive(player)`;
    - choose `m_TeamMeshes[IndexOf(team)]` from `m_Ctf->Find(player)->Side`;
    - fall back to `m_PlayerMesh` when there is no state or no team.
  - **`DrawIntels(alpha)`**, called beside `DrawRemotePlayers`, for each intel in `m_Ctf`:
    - the position is `intel.Position`, but for a carried intel carried by a remote player,
      use `PoseOf(carrier).Position` plus half the height, so it moves smoothly;
    - carried by the local player, skip it: you know you have it;
    - draw with `glm::translate(WorldOffset + position - vec3(0.3, 0, 0.3)) * glm::scale(vec3(0.1f))`
      at brightness `BrightnessAt(position)`.

- [ ] **Step 4: GameApp — the dead, and keys.**
  - **While the local player is dead:**
    - In `ReadInput`, return a `CharacterInput` with only `Yaw`/`Pitch` set.
    - In the edit and fire handlers, return early.
    - `UpdateCameraPosition` keeps using the character's position. It does not move while
      dead, so the camera stays where the player fell.
  - **In `OnKeyPressed`:** `KeyCode::F2` with `m_Client` sends
    `const std::uint8_t cmd[] = { CubitGame::SwitchTeamCommand }; m_Client->SendCommand(cmd);`
    and logs `"Asked to switch team"`.
  - **Tab** is read held, not pressed: each frame,
    `m_HudState->ShowScoreboard = Input::IsKeyPressed(KeyCode::Tab);`.

- [ ] **Step 5: HUD.**
  - **`GameHudState` fields:** `std::string ScoreLine, PhaseLine, CentreLine, TeamLine;`,
    `std::vector<std::string> Feed;`, `std::vector<CubitGame::ScoreRow> Scoreboard;` and
    `bool ShowScoreboard = false;`.
  - **In `OnFrameUpdate`, when connected, fill them from `m_Ctf` and `ServerTick()`:**
    - `ScoreLine` and `PhaseLine`.
    - `CentreLine`, in priority order:
      1. `RespawnLine` while dead;
      2. `"YOU HAVE THE INTEL - RETURN TO BASE"` when an intel's carrier is the local
         player;
      3. otherwise empty.
    - `TeamLine = "TEAM BLUE"` or `"TEAM GREEN"`.
    - `Feed`: new events from `NewEvents`, as `EventLine`s kept for 5 s of server ticks, and
      at most 4.
    - `Scoreboard = ScoreboardRows(...)`.
    - With `m_CtfBroken`, `PhaseLine = "NO MATCH STATE"`.
  - **In `GameHudLayer::OnRender`, when these are non-empty:**
    - the score line centred at the top, with `MeasureText` for centring, and the phase line
      under it;
    - the centre line at mid-screen, above the crosshair;
    - the team line under the debug readout;
    - the feed at the top right, right-aligned.
  - **The scoreboard, while `ShowScoreboard`:**
    - a `FillRect` panel, 60% of the screen and centred, in (0, 0, 0, 0.6);
    - two columns with headers `BLUE` and `GREEN` in their team colours;
    - rows as `name  K  D  C`, where the name is `"PLAYER " + id`;
    - the local row drawn in yellow (1, 0.9, 0.3, 1).
  - **Sounds:** in `PlaySounds`, call `m_Cues.MatchEvents(m_Ctf->Events, localTeam, m_PendingCues)`
    when `m_Ctf` is set, and pass the events to `SkipHistory` on the first frame.

- [ ] **Step 6: Build.** Both suites pass. **Commit** —
  `"Show the match: team colours, the intels, the HUD, the scoreboard, and its sounds"`

---

### Task 14: Verify by running, and document everything

- [ ] **Step 1: A two-client run with temporary triggers.** Screenshot one client in each
  state. Scripted input does not reach the window (see memory), so add `// TEMP` code to
  `Server.cpp`: a `CtfRules` with `CountdownTicks = 60`, `TimeLimitTicks = 60 * 60` and
  `EndScreenTicks = 5 * 60`, plus a server-side forced pickup at tick 600, where the first
  Blue player is teleported onto the Green intel's home spot. Screenshot client A at
  roughly:
  - t = 3 s (warmup or countdown, the score line, team colours on the other player);
  - t = 12 s (a carried intel, `YOU HAVE THE INTEL` on the carrier's client);
  - t = 70 s (the end screen);
  - t = 78 s (the reset back to warmup).

  For the dead state, force a server-side kill at t = 20 s and screenshot `RESPAWN IN`. Check
  the client logs:
  - 0 corrections except at respawns;
  - no errors;
  - both clients exit 0.

  **Remove every TEMP line** and rebuild.

- [ ] **Step 2: Documentation.**
  - `docs/engine-roadmap.md`: tick **D1**, **D5** and **B3a** with how each was done, the
    test counts, and the screenshot evidence. Record that the team colours changed from the
    spec's tint design to a palette-swapped mesh per team.
  - `README.md`:
    - "What works" gains the match;
    - the controls gain F2 (switch team) and Tab (scoreboard);
    - the protocol version is 8;
    - Server runs capture the intel;
    - the architecture paragraph mentions `GameMode`.
  - The spec: mark it implemented, and list any deviations recorded in this plan.
  - Memory: a new `match-rules-ctf.md`, with the MEMORY.md line, recording:
    - the `GameMode` hook and the null-mode guarantee;
    - that dead is a `MatchState` state;
    - the respawn-as-teleport rule in `MatchClient`;
    - the TEMP-trigger technique for match screenshots;
    - that the user still owes a real hand-played match.

- [ ] **Step 3: Commit** — `"Document the match: roadmap D1, D5 and B3a, README, spec"`, and push.
