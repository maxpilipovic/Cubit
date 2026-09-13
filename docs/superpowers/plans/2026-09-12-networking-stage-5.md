# Networking Stage 5 (Predicted Edits) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** At 150 ms, a player pillar-jumping by placing blocks under themselves, or digging down by breaking the block underfoot, gets zero corrections; a placed block appears the instant it is clicked, and an edit the rules forbid does nothing on the client that tried it.

**Architecture:** Edits ride the input stream. Each input tick carries at most one edit inside the existing redundant three-input bundle; the server checks it against shared reach and overlap rules and applies it when it steps that tick, before anybody moves, then tells the editor the outcome with a reliable `EditResult` and everyone else with `EditApplied`. The client runs the same rules before predicting, keeps a confirmed layer beneath its pending predictions so server changes never overwrite a visible prediction, and replays pending edits tick by tick as block writes that neither relight nor mark chunks dirty.

**Tech Stack:** C++20, MSVC (Visual Studio 18 / vs2026), premake5, doctest, GLM, ENet.

**Spec:** `docs/superpowers/specs/2026-09-12-predicted-edits-design.md` (read it; Stage 4's spec is `docs/superpowers/specs/2026-09-08-networking-stage-4-design.md`, Stage 3's is `docs/superpowers/specs/2026-09-03-networking-stage-3-design.md`)

## Global Constraints

- **C++20**, `cppdialect "C++20"` in `premake5.lua`. `std::span` and `std::optional` are available.
- **`Cubit/src/Voxel/`, `Cubit/include/Cubit/Voxel/`, `Cubit/src/Net/` and `Cubit/include/Cubit/Net/` must stay GL-free.** No `glad`, `GLFW`, or `gl*`.
- **Any exported class (`CB_API`) with a `std::` or `glm::` member needs the 4251 pragma guard**, matching the file it lives in.
- **`CB_API` is `dllexport`. An exported class must define every member it declares.** Never declare a member and leave its body for a later commit — that is LNK2019, not a TODO.
- **Every new `.cpp` under `Cubit/src/` must `#include "cub.h"` as its first line.**
- **Premake globs expand at generation time.** After adding any new file, run `/c/dev/premake/premake5.exe vs2026` from the repo root. Do **not** run `GenerateProjects.bat`.
- **Tests include `<doctest.h>`**, not `<doctest/doctest.h>`.
- **Build command** (repo root):
  ```bash
  MSB="/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe"
  "$MSB" Cubit.slnx -p:Configuration=Debug -p:Platform=x64 -m -v:minimal -nologo
  ```
  The suite runs as a post-build step, so a failing test fails the build. One case on its own: `./bin/Debug-windows-x86_64/Tests/Tests.exe -tc="the case name"`.
- **Every commit leaves the suite green.** Tasks are ordered so that nothing is removed before its replacement exists: `EditRequest` survives until Task 7.
- **`ProtocolVersion` becomes 4 in Task 3 and stays 4.** Message ids are never reused: `EditRequest = 5` is retired in Task 7, and `EditResult` is `9`.
- **`NetworkSim::Latency` is ONE-WAY seconds.** Every gate in this plan uses `5 * FrameClock::FixedStepSeconds` — 166.7 ms RTT — because 150 ms is not a whole number of ticks.
- **The rules, verbatim from the spec:** an edit is legal when it is in bounds and changes something; the editor's eye (`Position() + EyeOffset`) is within `ReachDistance` (12) of the nearest point of the target cell; and a non-air placement's unit cell does not *strictly* overlap any player's box (touching is allowed; breaking has no overlap rule). Evaluated against the state at the start of the step that carries the edit.
- **At most one edit per input tick.** The client queues at most `MaxQueuedEdits` (4) clicks.
- **The bar:** zero corrections from a player's own legal edits.
- **Wire fields are fixed-width little-endian.**
- **Never add Claude co-author trailers or attribution to commits.** End each commit with the `Claude-Session:` trailer the session provides.
- **Commit messages are a subject plus a wrapped prose body.** The `git commit` lines below give the subject; write the body.
- **Single-player must not change.** `Sandbox.exe` with no flags reports `POS 240.500000 26.900099 300.500000` and `FACES 1927774`, with `FACES` sampled when `PendingCount() == 0`.
- **Try to make every new invariant test fail before trusting it, and make its oracle what the consumer sees.** Stage 4's hit-rate gate passed while every rewind was a tick short. Every mutation this plan names is a *prediction* until it is run: if one will not turn its test red, stop and work out why before going on.
- **The arithmetic in this plan is estimated, not measured.** Jump apex (~1.69 blocks, ~22 ticks) and byte counts were worked out by hand. Where a test depends on a number, it derives it from the running code rather than from this document.

---

## File Structure

| File | Responsibility |
|---|---|
| `Cubit/include/Cubit/Voxel/EditRules.h` `Cubit/src/Voxel/EditRules.cpp` | **Create.** `ReachDistance`, and the legality check both ends run. No networking, no GL. |
| `Tests/src/EditRulesTests.cpp` | **Create.** Reach boundary, strict overlap, breaking exempt, other players optional. |
| `Cubit/include/Cubit/Voxel/World.h` `Cubit/src/Voxel/World.cpp` | **Modify.** `SetBlockUnmarked`: a block write that marks nothing dirty, for replay. |
| `Tests/src/WorldDirtyTests.cpp` | **Modify.** `SetBlockUnmarked` marks nothing. |
| `Cubit/include/Cubit/Cubit.h` | **Modify.** Include `EditRules.h`. |
| `Cubit/include/Cubit/Net/Protocol.h` `Cubit/src/Net/Protocol.cpp` | **Modify.** Version 4: optional edit per input entry, `EditResultMessage`; Task 7 retires `EditRequest`. |
| `Tests/src/ProtocolTests.cpp` | **Modify.** Round trips, re-pinned sizes, truncation, id recognition. |
| `Cubit/include/Cubit/Net/MatchServer.h` `Cubit/src/Net/MatchServer.cpp` | **Modify.** Queue edits with inputs; check and apply at the input's tick; `EditResult` to the editor, `EditApplied` to others. |
| `Tests/src/MatchServerTests.cpp` | **Modify.** Edits at their tick, refusals, results. |
| `Cubit/include/Cubit/Net/MatchClient.h` `Cubit/src/Net/MatchClient.cpp` | **Modify.** Edit queue, prediction, confirmed layer, `EditResult`, replay. |
| `Tests/src/PredictedEditTests.cpp` | **Create.** Client prediction, confirmed layer, pillar and dig gates, no-remesh, loss, refusal. |
| `Tests/src/WireOracleTests.cpp` | **Modify.** Replace the round-trip contract; extend the conflict test. |
| `Sandbox/src/Sandbox.cpp` | **Modify.** Use the shared `ReachDistance`; correct the edit-path comment. |
| `docs/superpowers/specs/2026-09-12-predicted-edits-design.md` `docs/engine-roadmap.md` | **Modify.** Record what shipped. |

## Task Order

1. The rules (`EditRules`), and `ReachDistance` leaves the Sandbox.
2. `World::SetBlockUnmarked`.
3. Protocol version 4: edits in input entries, `EditResult`. `EditRequest` stays.
4. The server applies input edits at their tick.
5. The client predicts edits, with the confirmed layer.
6. Replay with edits: the pillar and dig gates, and no remeshing.
7. Retire `EditRequest`.
8. Loss, refusal end to end, and conflicts.
9. The Sandbox, and single-player unchanged.
10. The live run, and writing down what happened.

---

## Task 1: The rules

**Files:**
- Create: `Cubit/include/Cubit/Voxel/EditRules.h`, `Cubit/src/Voxel/EditRules.cpp`, `Tests/src/EditRulesTests.cpp`
- Modify: `Cubit/include/Cubit/Cubit.h` (include the new header in the existing `Voxel/` block), `Sandbox/src/Sandbox.cpp:52-53` (delete the Sandbox's own `ReachDistance`)

**Interfaces:**
- Produces:
  - `constexpr float ReachDistance = 12.0f;`
  - `enum class OtherPlayers { Check, Ignore };`
  - `CB_API bool IsCellWithinReach(const glm::vec3& eye, const glm::ivec3& cell);`
  - `CB_API bool BoxOverlapsCell(const glm::vec3& centre, const glm::vec3& halfExtents, const glm::ivec3& cell);`
  - `CB_API bool IsEditLegal(const MatchState& match, PlayerId editor, const BlockEdit& edit, OtherPlayers others);`

**Why `ReachDistance` moves in this task.** `Cubit.h` is included by the Sandbox, and the Sandbox declares its own `constexpr float ReachDistance` in an anonymous namespace. With both visible, every unqualified use in `Sandbox.cpp` is ambiguous and the Sandbox stops compiling. The value is the same, so the Sandbox's copy is deleted.

- [ ] **Step 1: Write the failing tests**

`Tests/src/EditRulesTests.cpp`:

```cpp
#include <doctest.h>

#include "Cubit/Voxel/EditRules.h"
#include "Cubit/Voxel/MatchState.h"
#include "Cubit/Voxel/World.h"

#include <glm/glm.hpp>
#include <utility>

namespace
{
    World FloorWorld()
    {
        World world(2, 2, 2);

        for (int z = 0; z < world.GetDepth(); ++z)
            for (int x = 0; x < world.GetWidth(); ++x)
                world.SetBlock(x, 0, z, BlockId{ 1 });

        return world;
    }
}

TEST_CASE("Reach includes a cell whose nearest point is exactly ReachDistance away")
{
    //Nearest point of cell (12,0,0) to this eye is (12, 0.5, 0.5): exactly 12.
    const glm::vec3 eye(0.0f, 0.5f, 0.5f);
    CHECK(IsCellWithinReach(eye, glm::ivec3(12, 0, 0)));

    //One cell further is 13 away.
    CHECK_FALSE(IsCellWithinReach(eye, glm::ivec3(13, 0, 0)));
}

TEST_CASE("Reach measures to the nearest point of the cell, not its centre")
{
    //Centre of cell (12,0,0) is 12.5 away; its near face is 12.
    const glm::vec3 eye(0.0f, 0.5f, 0.5f);
    CHECK(glm::distance(eye, glm::vec3(12.5f, 0.5f, 0.5f)) > ReachDistance);
    CHECK(IsCellWithinReach(eye, glm::ivec3(12, 0, 0)));
}

TEST_CASE("A box overlapping a cell overlaps, and one only touching it does not")
{
    //Half extent 1 on y puts the box's bottom at exactly y = 1.0, the top of
    //cell (8,0,8). Values chosen to be exact in binary floating point.
    const glm::vec3 half(0.25f, 1.0f, 0.25f);
    const glm::vec3 standing(8.5f, 2.0f, 8.5f);

    CHECK_FALSE(BoxOverlapsCell(standing, half, glm::ivec3(8, 0, 8)));
    CHECK(BoxOverlapsCell(standing, half, glm::ivec3(8, 1, 8)));
    CHECK_FALSE(BoxOverlapsCell(standing, half, glm::ivec3(9, 1, 8)));
}

TEST_CASE("An edit is legal in reach, and illegal when it changes nothing or leaves the world")
{
    MatchState match(FloorWorld());
    const PlayerId editor = match.AddPlayer(glm::vec3(8.5f, 2.0f, 8.5f));

    CHECK(IsEditLegal(match, editor, BlockEdit{ glm::ivec3(10, 0, 8), BlockId{ 0 } }, OtherPlayers::Check));
    CHECK_FALSE(IsEditLegal(match, editor, BlockEdit{ glm::ivec3(10, 0, 8), BlockId{ 1 } }, OtherPlayers::Check));
    CHECK_FALSE(IsEditLegal(match, editor, BlockEdit{ glm::ivec3(10, -1, 8), BlockId{ 0 } }, OtherPlayers::Check));
    CHECK_FALSE(IsEditLegal(match, editor, BlockEdit{ glm::ivec3(31, 0, 31), BlockId{ 0 } }, OtherPlayers::Check));
    CHECK_FALSE(IsEditLegal(match, PlayerId{ 99 }, BlockEdit{ glm::ivec3(10, 0, 8), BlockId{ 0 } }, OtherPlayers::Check));
}

TEST_CASE("A placement may not overlap the editor, and may fill the cell under their feet")
{
    MatchState match(FloorWorld());

    //Airborne, feet at y = 2.1: cell (8,1,8) spans y 1..2 and is clear of the box.
    const PlayerId editor = match.AddPlayer(glm::vec3(8.5f, 3.0f, 8.5f));

    CHECK(IsEditLegal(match, editor, BlockEdit{ glm::ivec3(8, 1, 8), BlockId{ 2 } }, OtherPlayers::Check));
    CHECK_FALSE(IsEditLegal(match, editor, BlockEdit{ glm::ivec3(8, 2, 8), BlockId{ 2 } }, OtherPlayers::Check));
}

TEST_CASE("A placement into another player is checked only when asked to, and breaking never is")
{
    MatchState match(FloorWorld());
    const PlayerId editor = match.AddPlayer(glm::vec3(8.5f, 2.0f, 8.5f));
    const PlayerId other = match.AddPlayer(glm::vec3(11.5f, 2.0f, 8.5f));
    REQUIRE(other != editor);

    const BlockEdit intoOther{ glm::ivec3(11, 1, 8), BlockId{ 2 } };
    CHECK_FALSE(IsEditLegal(match, editor, intoOther, OtherPlayers::Check));
    CHECK(IsEditLegal(match, editor, intoOther, OtherPlayers::Ignore));

    //The floor under the other player: breaking has no overlap rule.
    CHECK(IsEditLegal(match, editor, BlockEdit{ glm::ivec3(11, 0, 8), BlockId{ 0 } }, OtherPlayers::Check));
}
```

- [ ] **Step 2: Run the build to verify it fails**

Run premake (new files), then the build command.
Expected: compile failure — `EditRules.h` does not exist.

- [ ] **Step 3: Write the header**

`Cubit/include/Cubit/Voxel/EditRules.h`:

```cpp
#pragma once

#include "Cubit/Core.h"
#include "Cubit/Voxel/BlockEdit.h"
#include "Cubit/Voxel/MatchState.h"

#include <glm/glm.hpp>

//How far a player can reach to edit a block, in blocks, measured from the eye
//to the nearest point of the cell.
//
//One number for the Sandbox's aim ray, the client's prediction and the server's
//ruling. Copies would drift, and a client whose reach is a hair longer than the
//server's predicts edits the server refuses - a correction on every click at
//the edge of reach.
constexpr float ReachDistance = 12.0f;

//Whether the overlap rule looks at players other than the editor.
//
//The server always checks them. The client checks them when it predicts,
//against where it last saw them; replay does not re-check them, because a newer
//snapshot could flip an edit the server will accept and flicker it off and on.
enum class OtherPlayers
{
    Check,
    Ignore
};

//True when the nearest point of the unit cell is within ReachDistance of the eye.
CB_API bool IsCellWithinReach(const glm::vec3& eye, const glm::ivec3& cell);

//True when the box and the unit cell share volume. Touching is not overlapping,
//which is what lets a player place the cell their feet rest on top of.
CB_API bool BoxOverlapsCell(const glm::vec3& centre, const glm::vec3& halfExtents,
    const glm::ivec3& cell);

//Whether `editor` may make `edit` in this match, as it stands right now.
//
//Called by the server when it applies an edit and by the client before it
//predicts one - identical code, so identical answers for identical state. The
//match passed in must be in the state it has at the start of the step that
//carries the edit.
CB_API bool IsEditLegal(const MatchState& match, PlayerId editor, const BlockEdit& edit,
    OtherPlayers others);
```

- [ ] **Step 4: Write the implementation**

`Cubit/src/Voxel/EditRules.cpp`:

```cpp
#include "cub.h"

#include "Cubit/Voxel/EditRules.h"

#include "Cubit/Voxel/CharacterController.h"
#include "Cubit/Voxel/World.h"

bool IsCellWithinReach(const glm::vec3& eye, const glm::ivec3& cell)
{
    const glm::vec3 min(cell);
    const glm::vec3 nearest = glm::clamp(eye, min, min + glm::vec3(1.0f));
    return glm::distance(eye, nearest) <= ReachDistance;
}

bool BoxOverlapsCell(const glm::vec3& centre, const glm::vec3& halfExtents,
    const glm::ivec3& cell)
{
    const glm::vec3 boxMin = centre - halfExtents;
    const glm::vec3 boxMax = centre + halfExtents;
    const glm::vec3 cellMin(cell);
    const glm::vec3 cellMax = cellMin + glm::vec3(1.0f);

    //Strict on every axis: a box resting exactly on a cell's top face does not
    //overlap it.
    return boxMin.x < cellMax.x && boxMax.x > cellMin.x
        && boxMin.y < cellMax.y && boxMax.y > cellMin.y
        && boxMin.z < cellMax.z && boxMax.z > cellMin.z;
}

bool IsEditLegal(const MatchState& match, PlayerId editor, const BlockEdit& edit,
    OtherPlayers others)
{
    if (!match.HasPlayer(editor))
        return false;

    const World& world = match.GetWorld();
    const glm::ivec3& at = edit.Position;

    //ApplyBlockEdit's own conditions, checked here so an edit that would do
    //nothing is refused rather than reported as accepted.
    if (!world.IsInBounds(at.x, at.y, at.z) || world.GetBlock(at.x, at.y, at.z) == edit.Block)
        return false;

    const CharacterController& character = match.Player(editor);
    const glm::vec3 eye = character.Position() + glm::vec3(0.0f, character.Config().EyeOffset, 0.0f);

    if (!IsCellWithinReach(eye, at))
        return false;

    //Breaking never traps anybody.
    if (edit.Block == BlockId{ 0 })
        return true;

    for (const auto& [player, body] : match.Players())
    {
        if (player != editor && others == OtherPlayers::Ignore)
            continue;

        if (BoxOverlapsCell(body.Position(), body.Config().HalfExtents, at))
            return false;
    }

    return true;
}
```

Add `#include "Cubit/Voxel/EditRules.h"` to `Cubit/include/Cubit/Cubit.h` in the `Voxel/` block, keeping it alphabetical. In `Sandbox/src/Sandbox.cpp`, delete these two lines from the anonymous namespace:

```cpp
    //How far the player can reach to edit terrain, in blocks.
    constexpr float ReachDistance = 12.0f;
```

- [ ] **Step 5: Build and run**

Expected: build succeeds and the whole suite passes, including the six new cases.

- [ ] **Step 6: Prove the tests can fail**

One at a time, rebuild and run `-tc="Reach includes*,A box overlapping*,A placement into another*"`, then revert:
1. `<= ReachDistance` → `< ReachDistance`. Expected red: "Reach includes a cell whose nearest point is exactly ReachDistance away".
2. `boxMin.y < cellMax.y` → `boxMin.y <= cellMax.y`. Expected red: "A box overlapping a cell overlaps, and one only touching it does not".
3. Delete the `others == OtherPlayers::Ignore` skip, so everyone is always checked. Expected red: "A placement into another player is checked only when asked to, and breaking never is".

If a mutation stays green, stop and report it before continuing.

- [ ] **Step 7: Commit**

```bash
git add Cubit/include/Cubit/Voxel/EditRules.h Cubit/src/Voxel/EditRules.cpp Tests/src/EditRulesTests.cpp Cubit/include/Cubit/Cubit.h Sandbox/src/Sandbox.cpp
git commit -m "Give edits a reach and an overlap rule both ends can run"
```

---

## Task 2: A block write that marks nothing dirty

**Files:**
- Modify: `Cubit/include/Cubit/Voxel/World.h` (declare beside `SetBlockAssumingDirty`), `Cubit/src/Voxel/World.cpp` (define beside it), `Tests/src/WorldDirtyTests.cpp`

**Interfaces:**
- Produces: `void World::SetBlockUnmarked(int x, int y, int z, BlockId block);`

**Why not reuse `SetBlockAssumingDirty`.** Its body is the same, but its documented contract is "legal only when every chunk this could affect is already dirty". Replay's contract is different — the caller restores the block before anything renders or relights — and a second function with its own comment keeps both contracts true instead of stretching one.

- [ ] **Step 1: Write the failing test**

Append to `Tests/src/WorldDirtyTests.cpp`:

```cpp
TEST_CASE("An unmarked block write changes the block and marks nothing")
{
    //Replay writes and restores blocks on every snapshot while edits are
    //pending. If those writes marked chunks dirty, each snapshot would remesh
    //about four chunks at ~6.4 ms each (docs/performance.md) - so this is a
    //cost test in the shape of a correctness test.
    World world = MakeWorld();
    world.ClearDirty();

    world.SetBlockUnmarked(20, 20, 20, BlockId{ 3 });

    CHECK(world.GetBlock(20, 20, 20) == BlockId{ 3 });
    CHECK(world.DirtyChunks().empty());
}
```

- [ ] **Step 2: Build to verify it fails**

Expected: compile failure — `SetBlockUnmarked` is not a member of `World`.

- [ ] **Step 3: Implement**

In `World.h`, directly after the `SetBlockAssumingDirty` declaration:

```cpp
    //Changes a block without marking anything dirty and without relighting;
    //throws when the position is outside the world.
    //
    //For replay only. Replay undoes and redoes a client's pending edits so the
    //character collides against the world as it stood at each replayed tick,
    //then restores every block before returning - so the world it leaves is the
    //world it found, and nothing needs remeshing or relighting. Used anywhere
    //that does not restore, it leaves a stale mesh and stale light. Collision
    //reads IsBlockSolid and IsBlockFluid only, never light, which is what makes
    //skipping the relight safe while the writes are in place.
    void SetBlockUnmarked(int x, int y, int z, BlockId block);
```

In `World.cpp`, directly after `SetBlockAssumingDirty`, with the same body (`WriteBlock(x, y, z, block);`) and a one-line comment pointing at the header for the contract.

- [ ] **Step 4: Build and run**

Expected: the suite passes.

- [ ] **Step 5: Prove it can fail**

Change the body to `SetBlock(x, y, z, block);`, rebuild, run `-tc="An unmarked block write*"`. Expected: red on `DirtyChunks().empty()`. Revert.

- [ ] **Step 6: Commit**

```bash
git add Cubit/include/Cubit/Voxel/World.h Cubit/src/Voxel/World.cpp Tests/src/WorldDirtyTests.cpp
git commit -m "Let replay write a block without dirtying its chunk"
```

---

## Task 3: Protocol version 4 — edits in inputs, and `EditResult`

**Files:**
- Modify: `Cubit/include/Cubit/Net/Protocol.h`, `Cubit/src/Net/Protocol.cpp`, `Tests/src/ProtocolTests.cpp`

**Interfaces:**
- Produces:
  - `InputMessage::Edits` — `std::vector<std::optional<BlockEdit>>`. Entry `i` carries an edit exactly when `i < Edits.size() && Edits[i].has_value()`. The encoder accepts an empty `Edits` (no edits at all); the decoder always fills one element per input.
  - `enum class MessageId { ..., EditResult = 9 };`
  - `struct EditResultMessage { std::uint64_t ClientTick = 0; bool Accepted = false; BlockEdit Edit; };` — `Edit.Block` is the block the server has at `Edit.Position` after ruling.
  - `CB_API std::vector<std::uint8_t> Encode(const EditResultMessage& message);`
  - `CB_API bool Decode(std::span<const std::uint8_t> bytes, EditResultMessage& out);`
  - `ProtocolVersion == 4`

`EditRequest` and `EncodeEditRequest` are **not** touched here — the server and client still use them until Task 7.

- [ ] **Step 1: Write the failing tests**

Append to `Tests/src/ProtocolTests.cpp`:

```cpp
TEST_CASE("An input entry carries its edit, and an entry without one comes back without one")
{
    InputMessage sent;
    sent.FirstTick = 77;
    sent.Inputs.assign(3, CharacterInput{});
    sent.Edits = {
        std::nullopt,
        BlockEdit{ glm::ivec3(-3, 40, 1000), BlockId{ 513 } },
        std::nullopt
    };

    InputMessage received;
    REQUIRE(Decode(Encode(sent), received));

    REQUIRE(received.Edits.size() == 3);
    CHECK_FALSE(received.Edits[0].has_value());
    REQUIRE(received.Edits[1].has_value());
    CHECK(received.Edits[1]->Position == glm::ivec3(-3, 40, 1000));
    CHECK(received.Edits[1]->Block == BlockId{ 513 });
    CHECK_FALSE(received.Edits[2].has_value());
}

TEST_CASE("A bundle sent with no edits decodes one empty edit per input")
{
    //The encoder takes an empty Edits as "none"; the decoder never hands back a
    //shorter list than Inputs, so a caller can index the two together.
    InputMessage sent;
    sent.FirstTick = 5;
    sent.Inputs.assign(3, CharacterInput{});

    InputMessage received;
    REQUIRE(Decode(Encode(sent), received));

    REQUIRE(received.Edits.size() == 3);
    for (const std::optional<BlockEdit>& edit : received.Edits)
        CHECK_FALSE(edit.has_value());
}

TEST_CASE("An edit result round-trips, accepted and refused")
{
    for (const bool accepted : { true, false })
    {
        CAPTURE(accepted);

        EditResultMessage sent;
        sent.ClientTick = 4294967302ull;   //Past a u32, so a narrowed field shows up.
        sent.Accepted = accepted;
        sent.Edit = BlockEdit{ glm::ivec3(12, -4, 7), BlockId{ 2 } };

        const std::vector<std::uint8_t> bytes = Encode(sent);

        //1 id + 8 tick + 1 accepted + 12 position + 2 block.
        CHECK(bytes.size() == 24);

        MessageId id = MessageId::Hello;
        REQUIRE(PeekMessageId(bytes, id));
        CHECK(id == MessageId::EditResult);

        EditResultMessage received;
        REQUIRE(Decode(bytes, received));
        CHECK(received.ClientTick == sent.ClientTick);
        CHECK(received.Accepted == accepted);
        CHECK(received.Edit.Position == sent.Edit.Position);
        CHECK(received.Edit.Block == sent.Edit.Block);
    }
}
```

Replace the whole of `TEST_CASE("A three-input bundle is 61 bytes")` with:

```cpp
TEST_CASE("A three-input bundle is 64 bytes, and 14 more for each edit it carries")
{
    //Pinned because it is the number the stage's upstream cost is quoted from:
    //1 id + 1 count + 8 tick + 3 x (17 input + 1 edit flag) = 64 bytes,
    //3,840 B/s per client at 60 Hz. Version 3 was 61; the three flag bytes are
    //this stage's whole standing cost.
    InputMessage message;
    message.FirstTick = 1;
    message.Inputs.assign(InputBundleSize, CharacterInput{});

    CHECK(Encode(message).size() == 64);

    message.Edits = { std::nullopt, BlockEdit{ glm::ivec3(1, 2, 3), BlockId{ 4 } }, std::nullopt };
    CHECK(Encode(message).size() == 78);
}
```

In `TEST_CASE("Every message truncated at every length is refused without crashing")`, add to the `messages` block:

```cpp
        InputMessage inputWithEdit;
        inputWithEdit.FirstTick = 10;
        inputWithEdit.Inputs.assign(InputBundleSize, CharacterInput{});
        inputWithEdit.Edits = { BlockEdit{ glm::ivec3(3, 3, 3), BlockId{ 1 } }, std::nullopt, std::nullopt };
        messages.push_back(Encode(inputWithEdit));

        EditResultMessage result;
        result.ClientTick = 3;
        result.Accepted = true;
        result.Edit = BlockEdit{ glm::ivec3(4, 4, 4), BlockId{ 2 } };
        messages.push_back(Encode(result));
```

declare `EditResultMessage editResult;` beside the other decoder outputs in the loop body (`HelloMessage hello;` and the rest), and add a case to the switch:

```cpp
            case MessageId::EditResult:  CHECK_FALSE(Decode(truncated, editResult)); break;
```

In `TEST_CASE("Every message id the wire carries is recognised")`, add the row:

```cpp
        { Encode(EditResultMessage{}),              MessageId::EditResult },
```

Add `#include <optional>` to the includes of `ProtocolTests.cpp`.

- [ ] **Step 2: Build to verify it fails**

Expected: compile failure — `InputMessage` has no member `Edits`, `EditResultMessage` is undeclared.

- [ ] **Step 3: Change the header**

In `Protocol.h`:

1. Add `#include <optional>` to the includes.
2. In `enum class MessageId`, after `ShotResolved = 8`, add `EditResult = 9`. Update the comment above the enum: it says "Eight, and deliberately not nine" — make it "Nine" and keep the rest of its reasoning.
3. Change `ProtocolVersion` to `4` and add to the version history comment:

```cpp
//4: predicted edits. An input entry may carry one edit, and the editor hears
//its fate from EditResult. On the per-tick path.
```

4. Add to `InputMessage`, after `Inputs`:

```cpp
    //At most one edit per input, riding with the tick it was made on so the
    //server applies it at exactly that step - the whole of what makes a
    //predicted edit agree with the server.
    //
    //Entry i carries an edit exactly when i < Edits.size() and Edits[i] has a
    //value, so a sender with no edits may leave this empty. Decode always
    //fills one element per input, so a receiver can index the two together.
    std::vector<std::optional<BlockEdit>> Edits;
```

5. After `ShotResolvedMessage`, add:

```cpp
//The server's ruling on one of a client's own edits, sent to that client only.
//
//Reliable, because a lost refusal would leave a block on one client that the
//server never had - a permanent desync, not a correction. Tagged with the
//client's own tick, which is how the client finds the prediction it answers.
struct EditResultMessage
{
    std::uint64_t ClientTick = 0;
    bool Accepted = false;

    //The position, and the block the server has there AFTER ruling: the
    //requested block when accepted, the unchanged one when refused. Always
    //the server's truth, so the client never has to work it out.
    BlockEdit Edit;
};
```

6. Declare `CB_API std::vector<std::uint8_t> Encode(const EditResultMessage& message);` after the `ShotResolvedMessage` encoder, and `CB_API bool Decode(std::span<const std::uint8_t> bytes, EditResultMessage& out);` after its decoder. Update the `PeekMessageId` comment's "one of the eight" to "one of the nine".

- [ ] **Step 4: Change the encoding**

In `Protocol.cpp`:

1. After `CharacterInputBytes`, add:

```cpp
    //The smallest an input entry can be on the wire: the input and its edit
    //flag, with no edit. The guard in Decode(InputMessage&) divides by this -
    //see the note on PlayerSnapshotBytes for why a guard constant must be the
    //true minimum width and never larger.
    constexpr std::size_t InputEntryMinBytes = CharacterInputBytes + 1;
```

2. In `Encode(const InputMessage&)`, replace the loop with:

```cpp
    for (std::size_t i = 0; i < message.Inputs.size(); ++i)
    {
        const CharacterInput& input = message.Inputs[i];
        writer.F32(input.Move.x);
        writer.F32(input.Move.y);
        writer.F32(input.Yaw);
        writer.F32(input.Pitch);
        writer.Bool(input.Jump);

        const bool hasEdit = i < message.Edits.size() && message.Edits[i].has_value();
        writer.Bool(hasEdit);
        if (hasEdit)
            WriteEdit(writer, *message.Edits[i]);
    }
```

3. In `Decode(..., InputMessage&)`, change the guard's divisor from `CharacterInputBytes` to `InputEntryMinBytes`, reserve `message.Edits` beside `message.Inputs`, and extend the loop body after `message.Inputs.push_back(input);`:

```cpp
        std::optional<BlockEdit> edit;
        if (reader.Bool())
            edit = ReadEdit(reader);
        message.Edits.push_back(edit);
```

4. Add the encoder after `Encode(const ShotResolvedMessage&)`:

```cpp
std::vector<std::uint8_t> Encode(const EditResultMessage& message)
{
    ByteWriter writer;
    writer.U8(static_cast<std::uint8_t>(MessageId::EditResult));
    writer.U64(message.ClientTick);
    writer.Bool(message.Accepted);
    WriteEdit(writer, message.Edit);
    return writer.Bytes();
}
```

5. Add the decoder after `Decode(..., ShotResolvedMessage&)`:

```cpp
bool Decode(std::span<const std::uint8_t> bytes, EditResultMessage& out)
{
    ByteReader reader(bytes);
    if (!OpenAs(reader, MessageId::EditResult))
        return false;

    EditResultMessage message;
    message.ClientTick = reader.U64();
    message.Accepted = reader.Bool();
    message.Edit = ReadEdit(reader);

    //Fixed width, no count: nothing to reserve on a hostile packet's word.
    if (!reader.Ok())
        return false;

    out = message;
    return true;
}
```

6. In `PeekMessageId`, change the upper bound from `MessageId::ShotResolved` to `MessageId::EditResult`.

- [ ] **Step 5: Build and run**

Expected: the whole suite passes. Nothing outside `ProtocolTests` notices: every existing sender leaves `Edits` empty, and every existing receiver ignores it.

- [ ] **Step 6: Prove the new cases can fail**

One at a time, rebuild, run `-tc="An input entry carries*,A three-input bundle*,An edit result round-trips*"`, and revert:
1. In `Decode(InputMessage&)`, push `std::nullopt` instead of `edit` (still reading the bytes). Expected red: "An input entry carries its edit, and an entry without one comes back without one".
2. In `Encode(InputMessage&)`, write the flag only when `hasEdit` (drop the `writer.Bool(hasEdit)` for entries without one). Expected red: the 64-byte check. (The round trip may also go red — record which.)
3. In `Encode(EditResultMessage&)`, write `ClientTick` as `U32`. Expected red: the 24-byte check and the tick comparison.

- [ ] **Step 7: Commit**

```bash
git add Cubit/include/Cubit/Net/Protocol.h Cubit/src/Net/Protocol.cpp Tests/src/ProtocolTests.cpp
git commit -m "Carry an edit in an input, and its ruling back, in protocol 4"
```

---

## Task 4: The server applies an input's edit at that input's tick

**Files:**
- Modify: `Cubit/include/Cubit/Net/MatchServer.h`, `Cubit/src/Net/MatchServer.cpp`, `Tests/src/MatchServerTests.cpp`

**Interfaces:**
- Consumes: `IsEditLegal`, `OtherPlayers` (Task 1); `InputMessage::Edits`, `EditResultMessage`, `Encode(const EditResultMessage&)` (Task 3).
- Produces (behaviour later tasks rely on):
  - An edit carried by input tick T is checked with `IsEditLegal(match, player, edit, OtherPlayers::Check)` and, if legal, applied **inside the `MatchServer::Step` that takes input T off the queue, before `m_Match.Step`**. Several clients' edits on one step are applied in player-id order.
  - Every carried edit produces exactly one reliable `EditResultMessage` to its editor: `ClientTick` = T, `Accepted`, and `Edit.Block` = the server's block at that cell right after ruling.
  - An accepted edit is appended to `EditLog()` and sent as `EditApplied` to every joined client **except** the editor.
  - Private: `void SendToJoined(const std::vector<std::uint8_t>& payload, Channel channel, PeerId except = InvalidPeer);`

The legacy `EditRequest` path (`m_PendingEdits`, `ApplyPendingEdits`) is left exactly as it is until Task 7.

- [ ] **Step 1: Write the failing tests**

In `Tests/src/MatchServerTests.cpp`, add `#include <optional>` if absent, and add to the first anonymous namespace (after `SendInput`):

```cpp
    void SendInputWithEdit(Transport& client, std::uint64_t tick, const CharacterInput& input,
        const BlockEdit& edit)
    {
        InputMessage message;
        message.FirstTick = tick;
        message.Inputs.push_back(input);
        message.Edits.push_back(edit);
        client.Send(LoopbackNetwork::ServerPeer, Encode(message), Channel::Unreliable);
    }

    //Everything edit-shaped waiting on an endpoint. One drain for both kinds,
    //because Poll consumes whatever it reads.
    struct EditTraffic
    {
        std::vector<EditResultMessage> Results;
        std::vector<EditMessage> Applied;
    };

    EditTraffic DrainEdits(Transport& transport)
    {
        EditTraffic heard;

        NetEvent event;
        while (transport.Poll(event))
        {
            if (event.Type != NetEventType::Message)
                continue;

            MessageId id = MessageId::Hello;
            if (!PeekMessageId(event.Data, id))
                continue;

            if (id == MessageId::EditResult)
            {
                EditResultMessage result;
                if (Decode(event.Data, result))
                    heard.Results.push_back(result);
            }
            else if (id == MessageId::EditApplied)
            {
                EditMessage applied;
                if (Decode(event.Data, applied))
                    heard.Applied.push_back(applied);
            }
        }

        return heard;
    }

    //Steps until the joined player is standing on the floor, so a test starts
    //from rest rather than mid-fall.
    void Settle(MatchServer& server, PlayerId player)
    {
        for (int i = 0; i < 60 && !server.Match().Player(player).Grounded(); ++i)
            server.Step(FrameClock::FixedStepSeconds);
    }
```

Check that `CharacterController` exposes `Grounded()`; it is read as `Player_().Grounded()` in `Sandbox.cpp`.

Append these cases:

```cpp
TEST_CASE("An input's edit is applied on the step that takes that input, before anybody moves")
{
    //THE ORDER THE CLIENT WILL COPY. The client applies its edit and then
    //steps; if the server stepped first and edited after, a player breaking
    //the block underfoot would stand on it for one more server tick than on
    //their own screen - a correction on every dig.
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& client = network.AddClient(peer);
    const PlayerId player = Join(server, client);
    REQUIRE(player != InvalidPlayer);

    Settle(server, player);
    REQUIRE(server.Match().Player(player).Grounded());
    const float restingY = server.Match().Player(player).Position().y;

    //Spawn (8, 2, 8) is a block corner, so the 0.6-wide box stands on FOUR
    //floor cells. Breaking one would not drop it and this test would pass
    //whichever order the server used. Three go first, on their own ticks,
    //while the player is still held up by the fourth.
    const glm::ivec3 supports[] = { { 7, 0, 7 }, { 7, 0, 8 }, { 8, 0, 7 } };
    std::uint64_t tick = 1;
    for (const glm::ivec3& cell : supports)
    {
        SendInputWithEdit(client, tick++, CharacterInput{}, BlockEdit{ cell, BlockId{ 0 } });
        server.Step(FrameClock::FixedStepSeconds);
    }

    REQUIRE(server.Match().Player(player).Grounded());
    REQUIRE(server.Match().Player(player).Position().y == restingY);

    //The last support.
    SendInputWithEdit(client, tick, CharacterInput{}, BlockEdit{ glm::ivec3(8, 0, 8), BlockId{ 0 } });
    server.Step(FrameClock::FixedStepSeconds);

    CHECK(server.Match().GetWorld().GetBlock(8, 0, 8) == BlockId{ 0 });

    //Already falling on this very step: the floor went before the move. Had
    //the move come first, the player would still be standing on it here.
    CHECK(server.Match().Player(player).Position().y < restingY);
}

TEST_CASE("An accepted edit answers its editor with a result and everyone else with EditApplied")
{
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId firstPeer = InvalidPeer;
    Transport& first = network.AddClient(firstPeer);
    REQUIRE(Join(server, first) != InvalidPlayer);

    PeerId secondPeer = InvalidPeer;
    Transport& second = network.AddClient(secondPeer);
    REQUIRE(Join(server, second) != InvalidPlayer);

    DrainEdits(first);
    DrainEdits(second);

    const BlockEdit edit{ glm::ivec3(4, 0, 4), BlockId{ 0 } };
    SendInputWithEdit(first, 1, CharacterInput{}, edit);
    server.Step(FrameClock::FixedStepSeconds);

    const EditTraffic editor = DrainEdits(first);
    REQUIRE(editor.Results.size() == 1);
    CHECK(editor.Results[0].ClientTick == 1);
    CHECK(editor.Results[0].Accepted);
    CHECK(editor.Results[0].Edit.Position == edit.Position);
    CHECK(editor.Results[0].Edit.Block == BlockId{ 0 });

    //Not its own EditApplied: the result is the editor's answer, and an
    //untagged EditApplied arriving too would be applied over a newer
    //prediction on the same cell.
    CHECK(editor.Applied.empty());

    const EditTraffic other = DrainEdits(second);
    CHECK(other.Results.empty());
    REQUIRE(other.Applied.size() == 1);
    CHECK(other.Applied[0].Edit.Position == edit.Position);

    REQUIRE(server.EditLog().size() == 1);
    CHECK(server.EditLog()[0].Position == edit.Position);
}

TEST_CASE("An edit beyond reach is refused, changes nothing, and the input still moves the player")
{
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId firstPeer = InvalidPeer;
    Transport& first = network.AddClient(firstPeer);
    const PlayerId player = Join(server, first);
    REQUIRE(player != InvalidPlayer);

    PeerId secondPeer = InvalidPeer;
    Transport& second = network.AddClient(secondPeer);
    REQUIRE(Join(server, second) != InvalidPlayer);

    Settle(server, player);
    DrainEdits(first);
    DrainEdits(second);

    const glm::vec3 before = server.Match().Player(player).Position();

    CharacterInput walking;
    walking.Move = glm::vec2(0.0f, 1.0f);

    //The far corner of a 32-block world from a spawn at (8, 2, 8): about 32
    //blocks away, well past ReachDistance.
    SendInputWithEdit(first, 1, walking, BlockEdit{ glm::ivec3(31, 0, 31), BlockId{ 0 } });
    server.Step(FrameClock::FixedStepSeconds);

    const EditTraffic editor = DrainEdits(first);
    REQUIRE(editor.Results.size() == 1);
    CHECK_FALSE(editor.Results[0].Accepted);
    CHECK(editor.Results[0].Edit.Block == BlockId{ 1 });

    CHECK(server.Match().GetWorld().GetBlock(31, 0, 31) == BlockId{ 1 });
    CHECK(server.EditLog().empty());
    CHECK(DrainEdits(second).Applied.empty());

    CHECK(server.Match().Player(player).Position() != before);
}

TEST_CASE("A placement into a player's box is refused")
{
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId firstPeer = InvalidPeer;
    Transport& first = network.AddClient(firstPeer);
    const PlayerId player = Join(server, first);
    REQUIRE(player != InvalidPlayer);

    Settle(server, player);
    DrainEdits(first);

    //Cell (8,1,8) holds the standing player's legs.
    SendInputWithEdit(first, 1, CharacterInput{}, BlockEdit{ glm::ivec3(8, 1, 8), BlockId{ 2 } });
    server.Step(FrameClock::FixedStepSeconds);

    const EditTraffic editor = DrainEdits(first);
    REQUIRE(editor.Results.size() == 1);
    CHECK_FALSE(editor.Results[0].Accepted);
    CHECK(editor.Results[0].Edit.Block == BlockId{ 0 });
    CHECK(server.Match().GetWorld().GetBlock(8, 1, 8) == BlockId{ 0 });
}

TEST_CASE("Edits taken on one step are applied in player-id order")
{
    //Arrival order is socket scheduling. Player-id order is the same every run,
    //which is what lets every client predict and still converge.
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId firstPeer = InvalidPeer;
    Transport& first = network.AddClient(firstPeer);
    const PlayerId firstId = Join(server, first);

    PeerId secondPeer = InvalidPeer;
    Transport& second = network.AddClient(secondPeer);
    const PlayerId secondId = Join(server, second);

    REQUIRE(firstId != InvalidPlayer);
    REQUIRE(secondId != InvalidPlayer);
    REQUIRE(firstId < secondId);

    const glm::ivec3 contested(4, 1, 4);

    //The HIGHER id sends first, so arrival order and id order disagree.
    SendInputWithEdit(second, 1, CharacterInput{}, BlockEdit{ contested, BlockId{ 3 } });
    SendInputWithEdit(first, 1, CharacterInput{}, BlockEdit{ contested, BlockId{ 2 } });
    server.Step(FrameClock::FixedStepSeconds);

    //Lower id first, higher id last: the higher id's block stands.
    CHECK(server.Match().GetWorld().GetBlock(4, 1, 4) == BlockId{ 3 });
    CHECK(server.EditLog().size() == 2);
}
```

- [ ] **Step 2: Build to verify it fails**

Expected: the build compiles and the new cases fail (the server ignores `Edits`), so the post-build suite run fails. Record which cases failed and on which assertions.

- [ ] **Step 3: Carry the edit in the queue**

In `MatchServer.h`, add `#include "Cubit/Voxel/EditRules.h"` and `#include <optional>`. Extend `QueuedInput`:

```cpp
        struct QueuedInput
        {
            std::uint64_t Tick = 0;
            CharacterInput Input;

            //The edit made on this tick, if any. Queued with the input rather
            //than applied on arrival, so it lands on exactly the step the
            //client predicted it on.
            std::optional<BlockEdit> Edit;
        };
```

Declare, after `ApplyPendingEdits`:

```cpp
    //Rules on one client's edit, applies it if legal, answers the editor with
    //EditResult and tells everyone else with EditApplied.
    void ApplyInputEdit(PlayerId player, PeerId peer, std::uint64_t clientTick,
        const BlockEdit& edit);
```

Change the `SendToJoined` declaration to take `PeerId except = InvalidPeer` as a third parameter, and add one sentence to its comment: "`except` skips one peer — the editor, for an edit it already knows the fate of."

In `MatchServer.cpp`, in `case MessageId::Input:`, replace the `push_back` with:

```cpp
            client->Queue.push_back(Client::QueuedInput{ tick, input.Inputs[i],
                i < input.Edits.size() ? input.Edits[i] : std::nullopt });
```

- [ ] **Step 4: Apply edits at their tick**

In `MatchServer::Step`, declare before the per-client loop:

```cpp
    //One client's edit, taken off the queue with its input.
    struct TakenEdit
    {
        PlayerId Player = InvalidPlayer;
        PeerId Peer = InvalidPeer;
        std::uint64_t ClientTick = 0;
        BlockEdit Edit;
    };
    std::vector<TakenEdit> takenEdits;
```

Inside the loop, directly after `commands.push_back(...)`:

```cpp
        if (queued.Edit.has_value())
            takenEdits.push_back(TakenEdit{ client.Player, client.Peer, queued.Tick, *queued.Edit });
```

After the `std::sort` of `commands` and before `m_Match.Step(...)`:

```cpp
    //BEFORE the step, and in player-id order - the order the client copies.
    //A client predicts its edit and then steps; applying edits after the
    //step here would leave a player standing on a block they have already
    //broken on their own screen.
    std::stable_sort(takenEdits.begin(), takenEdits.end(),
        [](const TakenEdit& a, const TakenEdit& b) { return a.Player < b.Player; });

    for (const TakenEdit& taken : takenEdits)
        ApplyInputEdit(taken.Player, taken.Peer, taken.ClientTick, taken.Edit);
```

Add the definition after `ApplyPendingEdits`:

```cpp
void MatchServer::ApplyInputEdit(PlayerId player, PeerId peer, std::uint64_t clientTick,
    const BlockEdit& edit)
{
    EditResultMessage result;
    result.ClientTick = clientTick;
    result.Edit.Position = edit.Position;

    if (IsEditLegal(m_Match, player, edit, OtherPlayers::Check)
        && ApplyBlockEdit(m_Match.GetWorld(), edit).has_value())
    {
        result.Accepted = true;
        m_EditLog.push_back(edit);

        EditMessage applied;
        applied.Edit = edit;
        SendToJoined(EncodeEditApplied(applied), Channel::Reliable, peer);
    }

    //The server's truth either way, so the client never has to work it out.
    const glm::ivec3& at = edit.Position;
    result.Edit.Block = m_Match.GetWorld().GetBlock(at.x, at.y, at.z);

    m_Transport.Send(peer, Encode(result), Channel::Reliable);
}
```

In `SendToJoined`'s definition, add the parameter and skip it:

```cpp
        if (client.Player == InvalidPlayer || client.Peer == except)
            continue;
```

(`InvalidPeer` is never a real client's peer, so the default skips nobody.)

- [ ] **Step 5: Build and run**

Expected: the whole suite passes.

- [ ] **Step 6: Prove the new cases can fail**

One at a time, rebuild, run `-tc="An input's edit*,An accepted edit answers*,An edit beyond reach*,A placement into a player's box*,Edits taken on one step*"`, and revert:
1. Move the `ApplyInputEdit` loop to after `m_Match.Step(...)`. Expected red: "An input's edit is applied on the step that takes that input, before anybody moves".
2. Pass `InvalidPeer` instead of `peer` to `SendToJoined` in `ApplyInputEdit`. Expected red: "An accepted edit answers its editor with a result and everyone else with EditApplied" (`editor.Applied.empty()`).
3. Replace the `IsEditLegal(...)` call with `true`. Expected red: both refusal cases.
4. Reverse the `stable_sort` comparison. Expected red: "Edits taken on one step are applied in player-id order".

- [ ] **Step 7: Commit**

```bash
git add Cubit/include/Cubit/Net/MatchServer.h Cubit/src/Net/MatchServer.cpp Tests/src/MatchServerTests.cpp
git commit -m "Apply an edit at the tick it rides in on"
```

---

## Task 5: The client predicts its own edits, with a confirmed layer beneath

**Files:**
- Create: `Tests/src/PredictedEditTests.cpp`
- Modify: `Cubit/include/Cubit/Net/MatchClient.h`, `Cubit/src/Net/MatchClient.cpp`, `Tests/src/WireOracleTests.cpp` (delete one case)

**Interfaces:**
- Consumes: `IsEditLegal`, `OtherPlayers` (Task 1); `InputMessage::Edits`, `EditResultMessage`, `Decode(..., EditResultMessage&)` (Task 3); the server behaviour of Task 4.
- Produces:
  - `constexpr std::size_t MaxQueuedEdits = 4;`
  - `void MatchClient::RequestEdit(const BlockEdit& edit);` — queues; no longer sends.
  - `std::size_t MatchClient::PendingEditCount() const;` — predictions still waiting for their `EditResult`.
  - Private, used by Task 6:
    - `struct PendingInput { std::uint64_t Tick; CharacterInput Input; std::optional<BlockEdit> Edit; };`
    - `struct PredictedEdit { std::uint64_t Tick = 0; BlockEdit Edit; BlockId Beneath = 0; };`
    - `std::deque<PredictedEdit> m_Predicted;` — oldest first.
    - `void ApplyConfirmedBlock(const glm::ivec3& cell, BlockId block);`

**What this task does not do.** Replay is untouched: `Reconcile` still replays movement against the world as it now stands, including edits made on later ticks. Under latency that can still cost corrections while pillaring — Task 6 fixes it and gates it. The tests here are about what the *world* shows, not about corrections.

**Why a WireOracle case is deleted.** "An edit takes a round trip and is not applied locally first" asserts that one tick after `RequestEdit` the requester's world is unchanged. That is the old contract, and this task makes it false on purpose. Its replacement is "A client's own edit shows on the step it is made, before the server has heard of it" below.

- [ ] **Step 1: Write the failing tests**

`Tests/src/PredictedEditTests.cpp`:

```cpp
#include <doctest.h>

#include "Cubit/FrameClock.h"
#include "Cubit/Net/LoopbackTransport.h"
#include "Cubit/Net/MatchClient.h"
#include "Cubit/Net/MatchServer.h"
#include "Cubit/Net/Protocol.h"
#include "Cubit/Net/SimulatedTransport.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <optional>
#include <string>

namespace
{
    constexpr std::uint64_t MapHash = 0xFEEDFACEull;
    const glm::vec3 Spawn{ 8.0f, 2.0f, 8.0f };

    //166.7 ms RTT: five ticks each way, the figure every gate in this stage uses.
    constexpr double OneWayLatency = 5 * FrameClock::FixedStepSeconds;

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

    void StepBoth(MatchClient& client, MatchServer& server)
    {
        client.SetInput(CharacterInput{});
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }

    //Connected, and standing: the local player exists (a snapshot has named
    //it) and has landed.
    void ConnectAndSettle(MatchClient& client, MatchServer& server)
    {
        for (int i = 0; i < 200; ++i)
        {
            StepBoth(client, server);

            if (client.Connected() && client.Match().HasPlayer(client.LocalPlayer())
                && client.Match().Player(client.LocalPlayer()).Grounded() && i > 60)
                return;
        }
    }

    BlockId ClientBlock(const MatchClient& client, const glm::ivec3& at)
    {
        return client.Match().GetWorld().GetBlock(at.x, at.y, at.z);
    }

    BlockId ServerBlock(const MatchServer& server, const glm::ivec3& at)
    {
        return server.Match().GetWorld().GetBlock(at.x, at.y, at.z);
    }
}

TEST_CASE("A client's own edit shows on the step it is made, before the server has heard of it")
{
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    SimulatedTransport serverNet(network.Server(), sim);

    PeerId peer = InvalidPeer;
    SimulatedTransport clientNet(network.AddClient(peer), sim);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, serverNet);
    MatchClient client(clientNet, GoodLoader());

    ConnectAndSettle(client, server);
    REQUIRE(client.Connected());

    const glm::ivec3 cell(4, 0, 4);
    client.RequestEdit(BlockEdit{ cell, BlockId{ 0 } });

    //One client step, no server step.
    client.SetInput(CharacterInput{});
    client.Step(FrameClock::FixedStepSeconds);

    CHECK(ClientBlock(client, cell) == BlockId{ 0 });
    CHECK(ServerBlock(server, cell) == BlockId{ 1 });
    CHECK(client.PendingEditCount() == 1);

    for (int i = 0; i < 60 && client.PendingEditCount() > 0; ++i)
        StepBoth(client, server);

    CHECK(client.PendingEditCount() == 0);
    CHECK(ServerBlock(server, cell) == BlockId{ 0 });
    CHECK(ClientBlock(client, cell) == BlockId{ 0 });
}

TEST_CASE("An edit the rules forbid is never predicted and never sent")
{
    LoopbackNetwork network;
    PeerId peer = InvalidPeer;
    Transport& raw = network.AddClient(peer);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, network.Server());
    MatchClient client(raw, GoodLoader());

    ConnectAndSettle(client, server);
    REQUIRE(client.Connected());

    //Beyond reach, then into the player's own legs.
    const glm::ivec3 far(31, 0, 31);
    const glm::ivec3 legs(8, 1, 8);

    client.RequestEdit(BlockEdit{ far, BlockId{ 0 } });
    StepBoth(client, server);
    client.RequestEdit(BlockEdit{ legs, BlockId{ 2 } });
    StepBoth(client, server);

    CHECK(ClientBlock(client, far) == BlockId{ 1 });
    CHECK(ClientBlock(client, legs) == BlockId{ 0 });
    CHECK(client.PendingEditCount() == 0);

    for (int i = 0; i < 20; ++i)
        StepBoth(client, server);

    CHECK(server.EditLog().empty());
}

TEST_CASE("A server change beneath a pending prediction does not show until the prediction resolves")
{
    //THE CONFIRMED LAYER. The server's messages are hand-built and the server is
    //not stepped after the edit, so the order in which things reach the client
    //is exactly the order this test says.
    LoopbackNetwork network;
    PeerId peer = InvalidPeer;
    Transport& raw = network.AddClient(peer);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, network.Server());
    MatchClient client(raw, GoodLoader());

    ConnectAndSettle(client, server);
    REQUIRE(client.Connected());

    const glm::ivec3 cell(4, 1, 4);
    client.RequestEdit(BlockEdit{ cell, BlockId{ 2 } });
    client.SetInput(CharacterInput{});
    client.Step(FrameClock::FixedStepSeconds);

    const std::uint64_t predictedTick = client.Match().Tick();
    REQUIRE(ClientBlock(client, cell) == BlockId{ 2 });

    //Somebody else's edit to the same cell reaches this client first.
    EditMessage theirs;
    theirs.Edit = BlockEdit{ cell, BlockId{ 3 } };
    network.Server().Send(peer, EncodeEditApplied(theirs), Channel::Reliable);

    client.SetInput(CharacterInput{});
    client.Step(FrameClock::FixedStepSeconds);

    //Still this client's prediction on screen.
    CHECK(ClientBlock(client, cell) == BlockId{ 2 });

    //Then the server refuses the prediction: the cell is theirs.
    EditResultMessage refused;
    refused.ClientTick = predictedTick;
    refused.Accepted = false;
    refused.Edit = BlockEdit{ cell, BlockId{ 3 } };
    network.Server().Send(peer, Encode(refused), Channel::Reliable);

    client.SetInput(CharacterInput{});
    client.Step(FrameClock::FixedStepSeconds);

    CHECK(ClientBlock(client, cell) == BlockId{ 3 });
    CHECK(client.PendingEditCount() == 0);
}

TEST_CASE("A quick place-then-break never shows the placed block again")
{
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    SimulatedTransport serverNet(network.Server(), sim);

    PeerId peer = InvalidPeer;
    SimulatedTransport clientNet(network.AddClient(peer), sim);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, serverNet);
    MatchClient client(clientNet, GoodLoader());

    ConnectAndSettle(client, server);
    REQUIRE(client.Connected());

    const glm::ivec3 cell(4, 1, 4);
    client.RequestEdit(BlockEdit{ cell, BlockId{ 2 } });
    StepBoth(client, server);
    REQUIRE(ClientBlock(client, cell) == BlockId{ 2 });

    client.RequestEdit(BlockEdit{ cell, BlockId{ 0 } });
    StepBoth(client, server);
    REQUIRE(ClientBlock(client, cell) == BlockId{ 0 });

    //The placement's accepted result arrives while the break is still pending.
    //Applied straight to the world, it would put the block back for a round
    //trip.
    int steps = 0;
    for (; steps < 80 && client.PendingEditCount() > 0; ++steps)
    {
        StepBoth(client, server);
        CAPTURE(steps);
        CHECK(ClientBlock(client, cell) == BlockId{ 0 });
    }

    CHECK(client.PendingEditCount() == 0);
    CHECK(ServerBlock(server, cell) == BlockId{ 0 });
}
```

Delete `TEST_CASE("An edit takes a round trip and is not applied locally first")` from `Tests/src/WireOracleTests.cpp`, whole.

**Two more WireOracle cases need an input set before every client `Step`.** `MatchClient::Step` returns before it predicts when no input has been set since the last step (`if (!m_Connected || !m_HasInput) return;`). "A client joining late gets a world matching everyone else's, block for block" and "Two clients editing the same block on the same tick converge" call `RequestEdit` and then `Step` without ever calling `SetInput` — which was fine while `RequestEdit` sent immediately, and after this task would leave every edit sitting in the queue for ever. In both cases, add `client.SetInput(CharacterInput{});` (with that case's client variable names: `first`, `second`) immediately before **every** `Step` call on a `MatchClient`, including the handshake loops. Change nothing else in them.

- [ ] **Step 2: Build to verify it fails**

Run premake (new file), then build. Expected: compile failure — `PendingEditCount` is not a member of `MatchClient`.

- [ ] **Step 3: Change the header**

In `MatchClient.h`:

1. After `MaxUnackedInputs`, add:

```cpp
//How many clicked edits may wait for an input tick. One edit rides each tick,
//so this is the backlog a burst of clicks can build before extras are dropped.
constexpr std::size_t MaxQueuedEdits = 4;
```

2. Replace `RequestEdit`'s comment:

```cpp
    //Asks for a block to change. Queued for the next Step, which checks it
    //against the same rules the server uses: an illegal edit is dropped there
    //and never sent, and a legal one is shown immediately and sent with that
    //tick's input. At most one edit rides each tick.
    void RequestEdit(const BlockEdit& edit);
```

3. After `RoundTripTime()`, add:

```cpp
    //Predicted edits still waiting for the server's EditResult. For tests and
    //diagnostics.
    std::size_t PendingEditCount() const { return m_Predicted.size(); }
```

4. Declare privately, after `HandleShotResolved`:

```cpp
    void HandleEditResult(std::span<const std::uint8_t> data);

    //A block the server has confirmed. Written underneath the oldest pending
    //prediction on that cell when there is one, so what shows stays this
    //client's prediction until that prediction resolves; written to the world
    //directly otherwise.
    void ApplyConfirmedBlock(const glm::ivec3& cell, BlockId block);
```

5. Add `std::optional<BlockEdit> Edit;` to `PendingInput`, commented "The edit this tick carried, if any - resent with the input and replayed at its tick."

6. After `m_Unacked`, add:

```cpp
    //One of this client's edits the server has not ruled on yet.
    struct PredictedEdit
    {
        std::uint64_t Tick = 0;
        BlockEdit Edit;

        //What the cell held beneath this prediction. For the oldest prediction
        //on a cell, the server-confirmed value; for a newer one, the older
        //prediction's block.
        BlockId Beneath = 0;
    };

    //Oldest first. Removed when their EditResult arrives, which can be after
    //the snapshot that acknowledged their input: results are reliable and
    //snapshots are not, so they arrive in either order.
    std::deque<PredictedEdit> m_Predicted;

    //Clicks waiting for a tick to ride on.
    std::deque<BlockEdit> m_EditQueue;
```

- [ ] **Step 4: Predict, send and confirm**

In `MatchClient.cpp`, add `#include "Cubit/Voxel/EditRules.h"`.

Replace `RequestEdit`'s body:

```cpp
void MatchClient::RequestEdit(const BlockEdit& edit)
{
    if (!m_Connected)
        return;

    //Dropped past the cap rather than queued without bound: a client that
    //clicks faster than 60 a second for long enough has asked for edits it
    //will not see for seconds.
    if (m_EditQueue.size() >= MaxQueuedEdits)
        return;

    m_EditQueue.push_back(edit);
}
```

In `Step`'s message switch, add `case MessageId::EditResult: HandleEditResult(event.Data); break;` beside `ShotResolved`.

In `Step`, replace:

```cpp
    const std::uint64_t tick = m_Match.Tick() + 1;

    m_Unacked.push_back(PendingInput{ tick, m_Input });
```

with:

```cpp
    const std::uint64_t tick = m_Match.Tick() + 1;

    //PREDICTING AN EDIT, before the step - the order the server applies it in.
    //Checked against the state this tick steps from, with the same function
    //the server will run, so a legal edit here is a legal edit there unless
    //another player has moved into the cell since this client last saw them.
    std::optional<BlockEdit> edit;
    if (!m_EditQueue.empty())
    {
        const BlockEdit requested = m_EditQueue.front();
        m_EditQueue.pop_front();

        if (IsEditLegal(m_Match, m_LocalPlayer, requested, OtherPlayers::Check))
        {
            World& world = m_Match.GetWorld();
            const glm::ivec3& at = requested.Position;

            PredictedEdit predicted;
            predicted.Tick = tick;
            predicted.Edit = requested;
            predicted.Beneath = world.GetBlock(at.x, at.y, at.z);

            //For real - relit and remeshed, the cost EditApplied used to pay
            //a round trip later.
            ApplyBlockEdit(world, requested);

            m_Predicted.push_back(predicted);

            //The same bound as m_Unacked, for the same reason: only a silent
            //server grows this, and it forgets the bookkeeping, not the block.
            if (m_Predicted.size() > MaxUnackedInputs)
                m_Predicted.pop_front();

            edit = requested;
        }
    }

    m_Unacked.push_back(PendingInput{ tick, m_Input, edit });
```

In the bundle loop, beside `message.Inputs.push_back(m_Unacked[i].Input);`, add `message.Edits.push_back(m_Unacked[i].Edit);`.

Replace `HandleEditApplied`'s last line (`ApplyBlockEdit(m_Match.GetWorld(), message.Edit);`) with `ApplyConfirmedBlock(message.Edit.Position, message.Edit.Block);`.

Add, after `HandleEditApplied`:

```cpp
void MatchClient::HandleEditResult(std::span<const std::uint8_t> data)
{
    if (!m_Connected)
        return;

    EditResultMessage result;
    if (!Decode(data, result))
        return;

    const auto found = std::find_if(m_Predicted.begin(), m_Predicted.end(),
        [&result](const PredictedEdit& predicted)
        {
            return predicted.Tick == result.ClientTick
                && predicted.Edit.Position == result.Edit.Position;
        });

    //Resolved either way: accepted, its block is confirmed; refused, the
    //server's block is. Both are result.Edit.Block. A result matching no
    //prediction - a duplicate, or one already forgotten - still carries the
    //server's truth, so it goes through the same path.
    if (found != m_Predicted.end())
        m_Predicted.erase(found);

    ApplyConfirmedBlock(result.Edit.Position, result.Edit.Block);
}

void MatchClient::ApplyConfirmedBlock(const glm::ivec3& cell, BlockId block)
{
    //The OLDEST prediction on this cell sits directly on the confirmed layer.
    const auto bottom = std::find_if(m_Predicted.begin(), m_Predicted.end(),
        [&cell](const PredictedEdit& predicted) { return predicted.Edit.Position == cell; });

    if (bottom != m_Predicted.end())
    {
        bottom->Beneath = block;
        return;
    }

    //Nothing predicted here: the server's block is what shows. A no-op when it
    //already does, which is the accepted-prediction case.
    ApplyBlockEdit(m_Match.GetWorld(), BlockEdit{ cell, block });
}
```

- [ ] **Step 5: Build and run**

Expected: the whole suite passes, including the four new cases and the two WireOracle cases given inputs in Step 1, which now go through prediction.

Before adding those `SetInput` calls, it is worth running once without them: both cases should go red with the edits stuck in the queue. That confirms the reason given in Step 1 rather than taking it on trust; record what you saw.

If a PredictionTests correction gate goes red, stop: nothing in those tests edits, so a red there means prediction changed something it should not have.

- [ ] **Step 6: Prove the new cases can fail**

One at a time, rebuild, run `-tc="A client's own edit shows*,An edit the rules forbid*,A server change beneath*,A quick place-then-break*"`, and revert:
1. In `Step`, remove the `ApplyBlockEdit(world, requested);` line (still record and send). Expected red: "A client's own edit shows on the step it is made, before the server has heard of it".
2. Replace the client's `IsEditLegal(...)` with `true`. Expected red: "An edit the rules forbid is never predicted and never sent".
3. Make `ApplyConfirmedBlock` always call `ApplyBlockEdit` (delete the `bottom` branch). Expected red: "A server change beneath a pending prediction does not show until the prediction resolves" and "A quick place-then-break never shows the placed block again".

- [ ] **Step 7: Commit**

```bash
git add Cubit/include/Cubit/Net/MatchClient.h Cubit/src/Net/MatchClient.cpp Tests/src/PredictedEditTests.cpp Tests/src/WireOracleTests.cpp
git commit -m "Show a player's own edit the moment they make it"
```

---

## Task 6: Replay against the world as it stood at each tick

**Files:**
- Modify: `Cubit/include/Cubit/Net/MatchClient.h`, `Cubit/src/Net/MatchClient.cpp`, `Tests/src/PredictedEditTests.cpp`

**Interfaces:**
- Consumes: `World::SetBlockUnmarked` (Task 2); `IsEditLegal`, `OtherPlayers::Ignore` (Task 1); `PendingInput::Edit`, `PredictedEdit`, `m_Predicted`, `ApplyConfirmedBlock` (Task 5).
- Produces:
  - `PredictedEdit` gains `bool Withdrawn = false;` — set when a replay re-check finds the edit illegal; a withdrawn prediction is not shown, not undone, not replayed, and not a layer for `ApplyConfirmedBlock`, and still waits for its `EditResult`.
  - Private: `void ReplayEdit(std::uint64_t tick, std::vector<glm::ivec3>& changed);`

**The failure this task removes.** After Task 5, `Reconcile` resets the player to the server's state at acknowledged tick A and replays the unacknowledged inputs against the world *as it is now* — which already holds blocks placed on ticks after A. A player pillar-jumping replays the ticks before a placement with that block already under them, lands early, and snaps. The gates below are written first and must be seen red against Task 5's `Reconcile` before anything is changed.

- [ ] **Step 1: Write the gates and the replay tests**

Append to the anonymous namespace in `Tests/src/PredictedEditTests.cpp`:

```cpp
    //A world tall enough to pillar thirty blocks: 32 x 64 x 32, floor at y = 0.
    World TallWorld()
    {
        World world(2, 4, 2);

        for (int z = 0; z < world.GetDepth(); ++z)
            for (int x = 0; x < world.GetWidth(); ++x)
                world.SetBlock(x, 0, z, BlockId{ 1 });

        return world;
    }

    MatchClient::MapLoader TallLoader()
    {
        return [](const std::string&) -> std::optional<LoadedMap>
        {
            return LoadedMap{ TallWorld(), MapHash };
        };
    }

    //A solid 4 x 4 column, 21 blocks deep, centred under the spawn corner.
    //Spawn (8, 2, 8) is a block corner, so the player stands on the four cells
    //x, z in {7, 8} and a dig has to take all four to drop them.
    constexpr int ColumnTop = 20;
    const glm::vec3 ColumnSpawn{ 8.0f, 23.0f, 8.0f };

    World ColumnWorld()
    {
        World world(2, 2, 2);

        for (int y = 0; y <= ColumnTop; ++y)
            for (int z = 6; z <= 9; ++z)
                for (int x = 6; x <= 9; ++x)
                    world.SetBlock(x, y, z, BlockId{ 1 });

        return world;
    }

    MatchClient::MapLoader ColumnLoader()
    {
        return [](const std::string&) -> std::optional<LoadedMap>
        {
            return LoadedMap{ ColumnWorld(), MapHash };
        };
    }

    //The feet of the client's own predicted player - what the person playing
    //sees, and so what they react to.
    float PredictedFeet(const MatchClient& client)
    {
        const CharacterController& self = client.Match().Player(client.LocalPlayer());
        return self.Position().y - self.Config().HalfExtents.y;
    }
```

Add `#include "Cubit/Voxel/CharacterController.h"` and `#include <cmath>` to the includes.

Append these cases:

```cpp
TEST_CASE("Pillar-jumping at 166.7 ms costs no corrections")
{
    //THE STAGE'S GATE. Hold jump, and each time the predicted feet clear the
    //top of the next cell up, place a block in it - the way a person pillars.
    //Every edit is legal, so the bar is exactly zero.
    //
    //The oracle is the correction count the player would see, plus the server's
    //world: a pillar that never reached the server would also cost no
    //corrections.
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    SimulatedTransport serverNet(network.Server(), sim);

    PeerId peer = InvalidPeer;
    SimulatedTransport clientNet(network.AddClient(peer), sim);

    MatchServer server(TallWorld(), "tall.vox", MapHash, Spawn, serverNet);
    MatchClient client(clientNet, TallLoader());

    ConnectAndSettle(client, server);
    REQUIRE(client.Connected());

    const std::uint64_t correctionsBefore = client.Corrections().Count;

    CharacterInput jumping;
    jumping.Jump = true;

    constexpr int Height = 30;
    int placed = 0;          //Cells (8, 1..placed, 8) requested so far.

    for (int tick = 0; tick < Height * 60 && placed < Height; ++tick)
    {
        //The next cell is y = placed + 1, whose top is placed + 2.
        if (PredictedFeet(client) > static_cast<float>(placed + 2))
        {
            ++placed;
            client.RequestEdit(BlockEdit{ glm::ivec3(8, placed, 8), BlockId{ 2 } });
        }

        client.SetInput(jumping);
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }

    //Let the last results and snapshots land, standing still.
    for (int i = 0; i < 60; ++i)
        StepBoth(client, server);

    REQUIRE(placed == Height);

    for (int y = 1; y <= Height; ++y)
    {
        CAPTURE(y);
        CHECK(ServerBlock(server, glm::ivec3(8, y, 8)) == BlockId{ 2 });
        CHECK(ClientBlock(client, glm::ivec3(8, y, 8)) == BlockId{ 2 });
    }

    CHECK(client.PendingEditCount() == 0);

    const MatchClient::CorrectionStats stats = client.Corrections();
    MESSAGE("pillar " << Height << " blocks at 166.7 ms: corrections " << (stats.Count - correctionsBefore)
        << ", max " << stats.Max);
    CHECK(stats.Count - correctionsBefore == 0);
}

TEST_CASE("Digging straight down at 166.7 ms costs no corrections")
{
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    SimulatedTransport serverNet(network.Server(), sim);

    PeerId peer = InvalidPeer;
    SimulatedTransport clientNet(network.AddClient(peer), sim);

    MatchServer server(ColumnWorld(), "column.vox", MapHash, ColumnSpawn, serverNet);
    MatchClient client(clientNet, ColumnLoader());

    ConnectAndSettle(client, server);
    REQUIRE(client.Connected());
    REQUIRE(client.Match().Player(client.LocalPlayer()).Grounded());

    const std::uint64_t correctionsBefore = client.Corrections().Count;
    const float startFeet = PredictedFeet(client);

    constexpr int Levels = 10;
    int dug = 0;
    int requestedLevel = -1;

    for (int tick = 0; tick < Levels * 120 && dug < Levels; ++tick)
    {
        const CharacterController& self = client.Match().Player(client.LocalPlayer());

        //Standing: take the four cells underfoot, one per tick through the queue.
        if (self.Grounded())
        {
            const int level = static_cast<int>(std::floor(PredictedFeet(client) + 0.01f)) - 1;

            if (level != requestedLevel && level > 0)
            {
                requestedLevel = level;
                for (const glm::ivec2 xz : { glm::ivec2(7, 7), glm::ivec2(7, 8), glm::ivec2(8, 7), glm::ivec2(8, 8) })
                    client.RequestEdit(BlockEdit{ glm::ivec3(xz.x, level, xz.y), BlockId{ 0 } });
                ++dug;
            }
        }

        StepBoth(client, server);
    }

    for (int i = 0; i < 120; ++i)
        StepBoth(client, server);

    REQUIRE(dug == Levels);
    CHECK(PredictedFeet(client) <= startFeet - static_cast<float>(Levels) + 0.01f);
    CHECK(client.PendingEditCount() == 0);

    const MatchClient::CorrectionStats stats = client.Corrections();
    MESSAGE("dig " << Levels << " levels at 166.7 ms: corrections " << (stats.Count - correctionsBefore)
        << ", max " << stats.Max);
    CHECK(stats.Count - correctionsBefore == 0);
}

TEST_CASE("Replaying a pending edit remeshes nothing")
{
    //Replay undoes and redoes pending edits on every snapshot. Through the
    //ordinary write path that would mark chunks dirty sixty times a second.
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    SimulatedTransport serverNet(network.Server(), sim);

    PeerId peer = InvalidPeer;
    SimulatedTransport clientNet(network.AddClient(peer), sim);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, serverNet);
    MatchClient client(clientNet, GoodLoader());

    ConnectAndSettle(client, server);
    REQUIRE(client.Connected());

    client.RequestEdit(BlockEdit{ glm::ivec3(4, 0, 4), BlockId{ 0 } });
    StepBoth(client, server);
    REQUIRE(client.PendingEditCount() == 1);

    //The prediction itself remeshed, once. Everything from here is replay.
    client.MatchForWrite().GetWorld().ClearDirty();
    const std::uint64_t snapshotsBefore = client.Corrections().Snapshots;

    for (int i = 0; i < 60 && client.PendingEditCount() > 0; ++i)
    {
        StepBoth(client, server);
        CAPTURE(i);
        CHECK(client.Match().GetWorld().DirtyChunks().empty());
    }

    CHECK(client.PendingEditCount() == 0);

    //Replay actually ran while the edit was pending, or this proved nothing.
    CHECK(client.Corrections().Snapshots >= snapshotsBefore + 5);
}

TEST_CASE("An edit that a correction puts out of reach stops showing, and the server's answer decides")
{
    //Replay re-checks the editor's own conditions. A snapshot that moves the
    //player three blocks back puts a cell at the edge of reach out of it, so
    //the replayed edit is withdrawn: this client stops showing a block it can
    //no longer justify, and the server's EditResult settles the cell.
    LoopbackNetwork network;
    PeerId peer = InvalidPeer;
    Transport& raw = network.AddClient(peer);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, network.Server());
    MatchClient client(raw, GoodLoader());

    ConnectAndSettle(client, server);
    REQUIRE(client.Connected());

    //About 11.1 from the spawn eye, inside reach.
    const glm::ivec3 edge(19, 0, 8);
    client.RequestEdit(BlockEdit{ edge, BlockId{ 0 } });
    client.SetInput(CharacterInput{});
    client.Step(FrameClock::FixedStepSeconds);

    const std::uint64_t predictedTick = client.Match().Tick();
    REQUIRE(ClientBlock(client, edge) == BlockId{ 0 });

    //The server says the player is at x = 5, and has not yet applied the tick
    //the edit rode on. From x = 5 the cell is about 14 away.
    PlayerSnapshot mine;
    mine.Player = client.LocalPlayer();
    mine.Position = glm::vec3(5.0f, client.Match().Player(client.LocalPlayer()).Position().y, 8.0f);
    mine.Grounded = true;
    mine.LastInputTick = predictedTick - 1;
    mine.Health = StartingHealth;

    SnapshotMessage snapshot;
    snapshot.Tick = server.Match().Tick() + 100;
    snapshot.Players = { mine };
    network.Server().Send(peer, Encode(snapshot), Channel::Unreliable);

    client.MatchForWrite().GetWorld().ClearDirty();
    client.SetInput(CharacterInput{});
    client.Step(FrameClock::FixedStepSeconds);

    CHECK(ClientBlock(client, edge) == BlockId{ 1 });
    CHECK(client.PendingEditCount() == 1);

    //Withdrawn is a real change on screen, so it does remesh.
    CHECK_FALSE(client.Match().GetWorld().DirtyChunks().empty());

    //The server accepted it anyway - from where it believed the player stood.
    EditResultMessage accepted;
    accepted.ClientTick = predictedTick;
    accepted.Accepted = true;
    accepted.Edit = BlockEdit{ edge, BlockId{ 0 } };
    network.Server().Send(peer, Encode(accepted), Channel::Reliable);

    client.SetInput(CharacterInput{});
    client.Step(FrameClock::FixedStepSeconds);

    CHECK(ClientBlock(client, edge) == BlockId{ 0 });
    CHECK(client.PendingEditCount() == 0);
}
```

- [ ] **Step 2: Run the build and record which cases fail**

Expected: it compiles, and the suite fails. Record, with the printed MESSAGE lines, the correction counts both gates report against Task 5's `Reconcile`. **Both gates must be red here.** If either reports zero corrections before any change, the gate cannot see the failure this task exists to fix: stop and report, do not continue. "An edit that a correction puts out of reach…" is also expected red (no withdrawal yet). "Replaying a pending edit remeshes nothing" may already be green, because Task 5's replay writes nothing — record whichever it is.

- [ ] **Step 3: Add withdrawal to the header**

In `MatchClient.h`, add to `PredictedEdit`:

```cpp
        //Set when replay, after a correction, finds this edit no longer legal
        //for the editor where the server now has them. A withdrawn prediction
        //is not shown and not replayed, and waits only for its EditResult.
        bool Withdrawn = false;
```

and declare privately, after `ApplyConfirmedBlock`:

```cpp
    //Re-applies the prediction made on `tick` during replay, as a block write
    //only. If the editor's own conditions no longer hold, withdraws it instead
    //and records the cell in `changed`, which needs a real relight and remesh.
    void ReplayEdit(std::uint64_t tick, std::vector<glm::ivec3>& changed);
```

Add `#include <vector>` to the header's includes.

- [ ] **Step 4: Undo, replay, restore**

In `MatchClient.cpp`, add `#include "Cubit/Voxel/SkyLight.h"`.

In `ApplyConfirmedBlock`, make the `bottom` search skip withdrawn predictions: `return predicted.Edit.Position == cell && !predicted.Withdrawn;`.

In `Reconcile`, directly before `character.SetState(entry.Position, ...)`:

```cpp
    World& world = m_Match.GetWorld();

    //UNDO every edit this client predicted after the acknowledged tick, newest
    //first, so replay starts from the world as it stood at that tick. Block
    //writes only - no relight, nothing marked dirty - because the loop below
    //puts every one of them back.
    for (auto it = m_Predicted.rbegin(); it != m_Predicted.rend(); ++it)
    {
        if (it->Tick <= entry.LastInputTick || it->Withdrawn)
            continue;

        const glm::ivec3& at = it->Edit.Position;
        world.SetBlockUnmarked(at.x, at.y, at.z, it->Beneath);
    }
```

Replace the replay loop:

```cpp
    for (const PendingInput& pending : m_Unacked)
        m_Match.StepPlayer(m_LocalPlayer, pending.Input, m_StepSeconds);
```

with:

```cpp
    //REDO, tick by tick: each tick's edit, then that tick's step - the order
    //the client predicted in and the server applies in.
    std::vector<glm::ivec3> changed;
    for (const PendingInput& pending : m_Unacked)
    {
        if (pending.Edit.has_value())
            ReplayEdit(pending.Tick, changed);

        m_Match.StepPlayer(m_LocalPlayer, pending.Input, m_StepSeconds);
    }

    //Every cell is back as it was, except those a withdrawal left different.
    //Those really changed, so they get the relight and remesh ApplyBlockEdit
    //would have given them.
    for (const glm::ivec3& at : changed)
    {
        world.MarkChunkDirtyAt(at.x, at.y, at.z);
        SkyLight::Repropagate(world, at.x, at.y, at.z);
    }
```

Add, after `ApplyConfirmedBlock`:

```cpp
void MatchClient::ReplayEdit(std::uint64_t tick, std::vector<glm::ivec3>& changed)
{
    const auto found = std::find_if(m_Predicted.begin(), m_Predicted.end(),
        [tick](const PredictedEdit& predicted) { return predicted.Tick == tick && !predicted.Withdrawn; });

    //Already resolved by its EditResult, which has written the server's block,
    //or already withdrawn. Nothing to replay.
    if (found == m_Predicted.end())
        return;

    const glm::ivec3& at = found->Edit.Position;

    //The editor's own conditions only. Other players were checked once, when
    //this was predicted; re-checking them against a newer snapshot could flip
    //an edit the server will accept, hide it, and show it again when its
    //result arrives - a flicker the server never caused.
    if (IsEditLegal(m_Match, m_LocalPlayer, found->Edit, OtherPlayers::Ignore))
    {
        m_Match.GetWorld().SetBlockUnmarked(at.x, at.y, at.z, found->Edit.Block);
        return;
    }

    //Left showing what was beneath it, which the undo pass already wrote.
    found->Withdrawn = true;
    changed.push_back(at);
}
```

`SkyLight::Repropagate`'s signature is `static void Repropagate(World& world, int x, int y, int z);` in `Cubit/include/Cubit/Voxel/SkyLight.h`.

- [ ] **Step 5: Build and run**

Expected: the whole suite passes. Record the two gates' printed correction counts — both must read 0.

If a gate still reports corrections, do not raise a threshold or loosen the check. Print, for each correction, the client tick, the acknowledged tick, the predicted and replayed positions, and the edits pending, and find the tick where the two machines' worlds differed. The likeliest causes, in order: an edit applied after the step on one side; `Beneath` recorded after `ApplyBlockEdit` instead of before; a prediction erased by its `EditResult` while its input was still unacknowledged (see the note in Task 8).

- [ ] **Step 6: Prove the gates can fail**

One at a time, rebuild, run `-tc="Pillar-jumping*,Digging straight down*,Replaying a pending edit*,An edit that a correction*"`, and revert:
1. **Apply-on-arrival**, the pre-stage behaviour: in `MatchServer.cpp`'s `case MessageId::Input:`, call `ApplyInputEdit(client->Player, peer, tick, *input.Edits[i])` for every carried edit as the bundle is decoded, and queue the input with `std::nullopt` instead. Expected red: "Pillar-jumping at 166.7 ms costs no corrections".
2. Delete the UNDO loop in `Reconcile`. Expected red: both gates.
3. In the UNDO loop, use `world.SetBlock(...)` instead of `SetBlockUnmarked`. Expected red: "Replaying a pending edit remeshes nothing".
4. In `ReplayEdit`, write the block whether or not `IsEditLegal` passes. Expected red: "An edit that a correction puts out of reach stops showing, and the server's answer decides".

Record, for mutation 1, how many corrections the pillar gate reported: that number is the measured cost this stage removes, and Task 10 records it.

- [ ] **Step 7: Commit**

```bash
git add Cubit/include/Cubit/Net/MatchClient.h Cubit/src/Net/MatchClient.cpp Tests/src/PredictedEditTests.cpp
git commit -m "Replay pending edits at their own ticks, without remeshing"
```

---

## Task 7: Retire `EditRequest`

**Files:**
- Modify: `Cubit/include/Cubit/Net/Protocol.h`, `Cubit/src/Net/Protocol.cpp`, `Cubit/include/Cubit/Net/MatchServer.h`, `Cubit/src/Net/MatchServer.cpp`, `Cubit/src/Net/MatchClient.cpp`, `Tests/src/ProtocolTests.cpp`, `Tests/src/MatchServerTests.cpp`

**Interfaces:**
- Consumes: `SendInputWithEdit`, `DrainEdits` (Task 4's test helpers).
- Removes: `MessageId::EditRequest`, `EncodeEditRequest`, `MatchServer::PendingEdit`, `m_PendingEdits`, `ApplyPendingEdits`.
- Produces: `PeekMessageId` recognises exactly `Hello, Welcome, Input, Snapshot, EditApplied, Fire, ShotResolved, EditResult`; id 5 is refused. `Decode(..., EditMessage&)` accepts only `EditApplied`.

Nothing has sent `EditRequest` since Task 5; this task deletes the dead path so that nothing can start sending it again.

- [ ] **Step 1: Rewrite the tests that name `EditRequest`**

In `Tests/src/ProtocolTests.cpp`:

1. Replace the whole of `TEST_CASE("Edit messages round-trip and keep their own identity")` with:

```cpp
TEST_CASE("An applied edit round-trips")
{
    EditMessage sent;
    sent.Edit = BlockEdit{ glm::ivec3(300, 40, -12), BlockId{ 3 } };

    const std::vector<std::uint8_t> applied = EncodeEditApplied(sent);

    MessageId id = MessageId::Hello;
    REQUIRE(PeekMessageId(applied, id));
    CHECK(id == MessageId::EditApplied);

    EditMessage received;
    REQUIRE(Decode(applied, received));
    CHECK(received.Edit.Position == glm::ivec3(300, 40, -12));
    CHECK(received.Edit.Block == BlockId{ 3 });
}

TEST_CASE("A retired message id is not recognised")
{
    //5 was EditRequest until version 4. Ids are never reused, and a packet
    //carrying one must vanish at dispatch rather than reach a decoder.
    const std::vector<std::uint8_t> retired{ 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

    MessageId id = MessageId::Hello;
    CHECK_FALSE(PeekMessageId(retired, id));

    EditMessage edit;
    CHECK_FALSE(Decode(retired, edit));

    CHECK_FALSE(PeekMessageId(std::vector<std::uint8_t>{ 0 }, id));
    CHECK_FALSE(PeekMessageId(std::vector<std::uint8_t>{ 10 }, id));
}
```

2. In the truncation sweep, replace `messages.push_back(EncodeEditRequest(edit));` with `messages.push_back(EncodeEditApplied(edit));`, and delete the `case MessageId::EditRequest:` line (keep `case MessageId::EditApplied:` and its body).
3. In "Every message id the wire carries is recognised", delete the `EncodeEditRequest` row.

In `Tests/src/MatchServerTests.cpp`:

1. In `TEST_CASE("A peer that has not finished the handshake is sent nothing")`, replace the four lines that build `EditMessage edit` and send `EncodeEditRequest(edit)` with:

```cpp
    //A legal edit, clear of the players at the spawn, so the server does send
    //EditApplied to joined clients - and must not send it to this one.
    SendInputWithEdit(speaker, 1, CharacterInput{}, BlockEdit{ glm::ivec3(4, 1, 4), BlockId{ 1 } });
```

`SendInputWithEdit` is declared in the anonymous namespace *above* this case only if it was added before line 263; if Task 4 added it further down, move the helper up to the first anonymous namespace so it is in scope here.

2. Replace the whole of `TEST_CASE("An applied edit reaches every joined client and is remembered for the next one")` with:

```cpp
TEST_CASE("An accepted edit is remembered for the next client to join")
{
    //A client arriving after somebody dug a hole must see the hole, so the log
    //rides along in Welcome. The other half of the old case - who hears about
    //an edit - is "An accepted edit answers its editor with a result and
    //everyone else with EditApplied".
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId firstPeer = InvalidPeer;
    Transport& first = network.AddClient(firstPeer);
    REQUIRE(Join(server, first) != InvalidPlayer);

    const BlockEdit edit{ glm::ivec3(4, 1, 4), BlockId{ 1 } };
    SendInputWithEdit(first, 1, CharacterInput{}, edit);
    server.Step(FrameClock::FixedStepSeconds);

    REQUIRE(server.EditLog().size() == 1);
    CHECK(server.EditLog()[0].Position == edit.Position);

    PeerId latePeer = InvalidPeer;
    Transport& late = network.AddClient(latePeer);
    late.Send(LoopbackNetwork::ServerPeer, Encode(HelloMessage{}), Channel::Reliable);
    server.Step(FrameClock::FixedStepSeconds);

    const std::optional<WelcomeMessage> welcome = FindWelcome(late);
    REQUIRE(welcome.has_value());
    REQUIRE(welcome->Edits.size() == 1);
    CHECK(welcome->Edits[0].Position == edit.Position);
}
```

- [ ] **Step 2: Build to verify the retired-id case fails**

Expected: it compiles (`EncodeEditRequest` still exists but is now unused by the tests), and "A retired message id is not recognised" fails — `PeekMessageId` still accepts 5.

- [ ] **Step 3: Delete the path**

In `Protocol.h`: delete `EditRequest = 5` from the enum and add a line above `EditApplied = 6`: `//5 was EditRequest, retired in version 4. Never reuse it.` Delete the `EncodeEditRequest` declaration. Rewrite `EditMessage`'s comment to: "One edit the server has applied, sent to every joined client except the one that made it."

In `Protocol.cpp`: delete `EncodeEditRequest`. In `Decode(..., EditMessage&)`, replace the two-id check with `if (!OpenAs(reader, MessageId::EditApplied)) return false;` (creating the `ByteReader` first, as the other decoders do). Replace `PeekMessageId`'s range check with an explicit list:

```cpp
bool PeekMessageId(std::span<const std::uint8_t> bytes, MessageId& out)
{
    if (bytes.empty())
        return false;

    //A list, not a range: a retired id sits inside the range, and a range check
    //would wave it through to dispatch.
    switch (static_cast<MessageId>(bytes[0]))
    {
    case MessageId::Hello:
    case MessageId::Welcome:
    case MessageId::Input:
    case MessageId::Snapshot:
    case MessageId::EditApplied:
    case MessageId::Fire:
    case MessageId::ShotResolved:
    case MessageId::EditResult:
        out = static_cast<MessageId>(bytes[0]);
        return true;
    }

    return false;
}
```

In `MatchServer.h`: delete `struct PendingEdit`, `ApplyPendingEdits`'s declaration and comment, and `m_PendingEdits`.

In `MatchServer.cpp`: delete the `ApplyPendingEdits();` call and the comment above it in `Step`, the `case MessageId::EditRequest:` block in `HandleMessage`, and `ApplyPendingEdits`'s definition.

In `MatchClient.cpp`: delete `case MessageId::EditRequest:` from the list of client-to-server ids in `Step`'s switch.

Then `grep -rn "EditRequest\|PendingEdit\b\|ApplyPendingEdits" Cubit Server Sandbox Tests` must print nothing but the retirement comment.

- [ ] **Step 4: Build and run**

Expected: the whole suite passes.

- [ ] **Step 5: Prove the retirement test can fail**

Put the old range check back in `PeekMessageId` (`id < Hello || id > EditResult` refuses), rebuild, run `-tc="A retired message id*"`. Expected red. Revert.

- [ ] **Step 6: Commit**

```bash
git add Cubit/include/Cubit/Net/Protocol.h Cubit/src/Net/Protocol.cpp Cubit/include/Cubit/Net/MatchServer.h Cubit/src/Net/MatchServer.cpp Cubit/src/Net/MatchClient.cpp Tests/src/ProtocolTests.cpp Tests/src/MatchServerTests.cpp
git commit -m "Retire EditRequest now that edits ride inputs"
```

---

## Task 8: Loss, a real refusal, and conflicts

**Files:**
- Modify: `Tests/src/PredictedEditTests.cpp`, `Tests/src/WireOracleTests.cpp`

**Interfaces:**
- Consumes: everything from Tasks 1–7. Produces tests only.

- [ ] **Step 1: Extract the pillar run and add the loss case**

In `Tests/src/PredictedEditTests.cpp`, add to the anonymous namespace:

```cpp
    struct PillarOutcome
    {
        int Placed = 0;
        std::uint64_t Corrections = 0;
        float MaxCorrection = 0.0f;
        bool ServerHasPillar = false;
        bool ClientHasPillar = false;
        std::size_t PendingAfter = 0;
    };

    //Hold jump and fill the cell under the predicted feet each time they clear
    //it, `height` blocks up, over a link shaped by `sim`.
    PillarOutcome RunPillar(const NetworkSim& sim, int height)
    {
        LoopbackNetwork network;
        SimulatedTransport serverNet(network.Server(), sim);

        PeerId peer = InvalidPeer;
        SimulatedTransport clientNet(network.AddClient(peer), sim);

        MatchServer server(TallWorld(), "tall.vox", MapHash, Spawn, serverNet);
        MatchClient client(clientNet, TallLoader());

        ConnectAndSettle(client, server);
        REQUIRE(client.Connected());

        const std::uint64_t correctionsBefore = client.Corrections().Count;

        CharacterInput jumping;
        jumping.Jump = true;

        PillarOutcome outcome;

        for (int tick = 0; tick < height * 60 && outcome.Placed < height; ++tick)
        {
            if (PredictedFeet(client) > static_cast<float>(outcome.Placed + 2))
            {
                ++outcome.Placed;
                client.RequestEdit(BlockEdit{ glm::ivec3(8, outcome.Placed, 8), BlockId{ 2 } });
            }

            client.SetInput(jumping);
            client.Step(FrameClock::FixedStepSeconds);
            server.Step(FrameClock::FixedStepSeconds);
        }

        for (int i = 0; i < 120; ++i)
            StepBoth(client, server);

        outcome.ServerHasPillar = true;
        outcome.ClientHasPillar = true;
        for (int y = 1; y <= outcome.Placed; ++y)
        {
            outcome.ServerHasPillar = outcome.ServerHasPillar && ServerBlock(server, glm::ivec3(8, y, 8)) == BlockId{ 2 };
            outcome.ClientHasPillar = outcome.ClientHasPillar && ClientBlock(client, glm::ivec3(8, y, 8)) == BlockId{ 2 };
        }

        outcome.Corrections = client.Corrections().Count - correctionsBefore;
        outcome.MaxCorrection = client.Corrections().Max;
        outcome.PendingAfter = client.PendingEditCount();
        return outcome;
    }
```

Rewrite the body of "Pillar-jumping at 166.7 ms costs no corrections" to use it — same assertions, now read off the outcome:

```cpp
    NetworkSim sim;
    sim.Latency = OneWayLatency;

    const PillarOutcome outcome = RunPillar(sim, 30);

    MESSAGE("pillar 30 blocks at 166.7 ms: corrections " << outcome.Corrections << ", max " << outcome.MaxCorrection);
    REQUIRE(outcome.Placed == 30);
    CHECK(outcome.ServerHasPillar);
    CHECK(outcome.ClientHasPillar);
    CHECK(outcome.PendingAfter == 0);
    CHECK(outcome.Corrections == 0);
```

Keep that case's opening comment. Rebuild and confirm it still passes and still prints 0.

Then add:

```cpp
TEST_CASE("Pillar-jumping under 5% loss and jitter: the correction count, measured")
{
    //THE RECORDED NUMBER UNDER A BAD LINK. The spec expects zero - an edit is
    //lost only when its whole input is, and then the server does not step that
    //tick either - but that is a prediction, and this case exists to find out.
    NetworkSim sim;
    sim.Latency = OneWayLatency;
    sim.Jitter = FrameClock::FixedStepSeconds;
    sim.Loss = 0.05f;
    sim.Seed = 1;

    const PillarOutcome outcome = RunPillar(sim, 30);

    MESSAGE("pillar 30 blocks at 166.7 ms, 5% loss, jitter: corrections " << outcome.Corrections
        << ", max " << outcome.MaxCorrection);

    REQUIRE(outcome.Placed == 30);
    CHECK(outcome.ServerHasPillar);
    CHECK(outcome.ClientHasPillar);
    CHECK(outcome.PendingAfter == 0);
}
```

- [ ] **Step 2: Run it and decide the loss assertion from the measurement**

Run `-tc="Pillar-jumping under 5% loss*"` and read the printed count.

- **If it is 0:** add `CHECK(outcome.Corrections == 0);` with a comment recording that 0 was measured, and run seeds 2 and 3 by hand once (change `sim.Seed`, run, revert) and record their counts in the report.
- **If it is not 0:** do **not** add a bound and do not change anything else. Stop and report the count, the max, and — using a temporary per-correction log in `HandleSnapshot` like Stage 4's — for each correction: the client tick, the acknowledged tick, and whether an `EditResult` for an edit made *after* the acknowledged tick had already arrived. **Known candidate:** Task 5 erases a prediction the moment its `EditResult` arrives, even if a delayed older snapshot then replays ticks before that edit with the block already in place. Jitter reorders unreliable snapshots, and results are reliable, so this can happen here and cannot happen on the clean link. If that is what the log shows, it is a design question for the controller, not a threshold to set.

- [ ] **Step 3: Add the end-to-end refusal**

Append to `Tests/src/PredictedEditTests.cpp`:

```cpp
TEST_CASE("A placement into a player this client has not seen yet is refused and put right")
{
    //The one way honest play mispredicts: the client checks other players at
    //the positions it last saw them, and the server checks where they are. Here
    //the other player has joined on the server but no snapshot has told this
    //client yet, and they stand in the cell being filled.
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    SimulatedTransport serverNet(network.Server(), sim);

    PeerId builderPeer = InvalidPeer;
    SimulatedTransport builderNet(network.AddClient(builderPeer), sim);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, serverNet);
    MatchClient builder(builderNet, GoodLoader());

    ConnectAndSettle(builder, server);
    REQUIRE(builder.Connected());

    //Off the spawn, so the builder's own box is clear of the cell. Yaw 0 faces +x.
    CharacterInput walking;
    walking.Move = glm::vec2(0.0f, 1.0f);
    for (int i = 0; i < 60; ++i)
    {
        builder.SetInput(walking);
        builder.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }
    for (int i = 0; i < 30; ++i)
        StepBoth(builder, server);

    PeerId arrivalPeer = InvalidPeer;
    SimulatedTransport arrivalNet(network.AddClient(arrivalPeer), sim);
    MatchClient arrival(arrivalNet, GoodLoader());

    const glm::ivec3 spawnCell(8, 1, 8);
    bool requested = false;

    for (int i = 0; i < 200; ++i)
    {
        //The moment the server has the arrival and the builder does not.
        if (!requested && server.Match().Players().size() == 2 && builder.Match().Players().size() == 1)
        {
            builder.RequestEdit(BlockEdit{ spawnCell, BlockId{ 2 } });
            requested = true;
        }

        builder.SetInput(CharacterInput{});
        arrival.SetInput(CharacterInput{});
        builder.Step(FrameClock::FixedStepSeconds);
        arrival.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);

        if (requested && builder.PendingEditCount() == 0)
            break;
    }

    REQUIRE(requested);
    CHECK(builder.PendingEditCount() == 0);
    CHECK(ServerBlock(server, spawnCell) == BlockId{ 0 });
    CHECK(ClientBlock(builder, spawnCell) == BlockId{ 0 });
}
```

Run it. If `requested` never becomes true, the arrival's join and the builder's first snapshot of them landed on the same step: report it rather than changing the latency, because that would change what the case tests.

Mutation, then revert: in `HandleEditResult`, skip `ApplyConfirmedBlock` when `!result.Accepted`. Expected red on `ClientBlock(builder, spawnCell) == BlockId{ 0 }`.

- [ ] **Step 4: Extend the conflict case**

In `Tests/src/WireOracleTests.cpp`, at the end of `TEST_CASE("Two clients editing the same block on the same tick converge")`, append:

```cpp
    //Both predicted, so both must also have nothing left waiting.
    CHECK(first.PendingEditCount() == 0);
    CHECK(second.PendingEditCount() == 0);

    //And again, a tick apart, the second undoing the first while the first's
    //result is still in flight to both.
    first.RequestEdit(BlockEdit{ contested, BlockId{ 3 } });
    first.Step(FrameClock::FixedStepSeconds);
    second.Step(FrameClock::FixedStepSeconds);
    server.Step(FrameClock::FixedStepSeconds);

    second.RequestEdit(BlockEdit{ contested, BlockId{ 0 } });

    for (int i = 0; i < 40; ++i)
    {
        first.Step(FrameClock::FixedStepSeconds);
        second.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }

    CHECK(WorldsMatch(first.Match().GetWorld(), server.Match().GetWorld()));
    CHECK(WorldsMatch(second.Match().GetWorld(), server.Match().GetWorld()));
    CHECK(first.PendingEditCount() == 0);
    CHECK(second.PendingEditCount() == 0);
```

Task 5 already gave every client `Step` in this case an input. Give the new `Step` calls you are adding one too — `first.SetInput(CharacterInput{});` and `second.SetInput(CharacterInput{});` directly before each — or their edits never leave the queue.

- [ ] **Step 5: Build and run**

Expected: the whole suite passes, with the loss case's assertion settled by Step 2.

Mutation, then revert: make `ApplyConfirmedBlock` always call `ApplyBlockEdit`. Expected red: the conflict case's second round.

- [ ] **Step 6: Commit**

```bash
git add Tests/src/PredictedEditTests.cpp Tests/src/WireOracleTests.cpp
git commit -m "Measure predicted edits under loss, refusal and conflict"
```

---

## Task 9: The Sandbox, and single-player unchanged

**Files:**
- Modify: `Sandbox/src/Sandbox.cpp`

**Interfaces:**
- Consumes: `MatchClient::RequestEdit` (Task 5), `ReachDistance` (Task 1).

The Sandbox's connected edit path already calls `m_Client->RequestEdit(edit)` and needs no new code: prediction lives in `MatchClient`. What is wrong is its comment, which describes the round trip this stage removed.

- [ ] **Step 1: Correct the comment**

In `OnMouseButtonPressed`, under `// BRANCH POINT 3 OF 3.`, replace:

```cpp
            // Nothing happens locally. The block disappears when the server
            // says so, one round trip later - which is the most legible
            // demonstration of latency this app has.
```

with:

```cpp
            // Predicted, not waited for. MatchClient checks the edit against
            // the same rules the server runs and shows it on the next step if
            // it is legal; the server applies it on the same tick and only a
            // refusal ever takes it back. Single-player below still applies
            // edits directly and keeps its undo stack.
```

- [ ] **Step 2: Build**

Expected: the whole suite passes; `Sandbox.exe` builds.

- [ ] **Step 3: Verify single-player is unchanged**

Screen capture of this window is unreliable, and stdout is fully buffered when redirected, so use a temporary probe that writes to a flushed file. In `SandboxLayer::OnRender`, after the `m_HudState->PendingChunks = ...` line, add (marked for removal):

```cpp
        // PROBE-TEMP BEGIN
        static bool probeWritten = false;
        if (!probeWritten && !m_Client && m_WorldRenderer.PendingCount() == 0)
        {
            probeWritten = true;
            const glm::vec3 p = Player_().Position();
            std::ofstream probe("probe.txt", std::ios::app);
            probe << "POS " << std::to_string(p.x) << " " << std::to_string(p.y) << " "
                << std::to_string(p.z) << " FACES " << m_WorldRenderer.TotalFaceCount() << std::endl;
        }
        // PROBE-TEMP END
```

with `#include <fstream> // PROBE-TEMP`. Build, then launch `Sandbox.exe` **from `bin/Debug-windows-x86_64/Sandbox`** (the map path is relative) with no arguments, wait until `probe.txt` appears (about 15 s), and close the GLFW window with `PostMessage(hwnd, WM_CLOSE)` — find the window whose class is `GLFW30`, not `MainWindowHandle`. Tell the user first: it captures the mouse cursor while it runs.

Expected: `POS 240.500000 26.900099 300.500000 FACES 1927774`.

**If the numbers differ, run it again before believing them.** The launched window takes keyboard focus, and stray input moves the player; on 2026-09-12 a first run read `POS 242.627975 27.550098 299.849274 FACES 1927762` and the same binary then read the expected values exactly. Different numbers from the same binary mean input, not code.

Remove every `PROBE-TEMP` line, delete `probe.txt`, rebuild, and confirm `grep -n PROBE Sandbox/src/Sandbox.cpp` prints nothing.

- [ ] **Step 4: Commit**

```bash
git add Sandbox/src/Sandbox.cpp
git commit -m "Say what a connected edit does now"
```

---

## Task 10: The live run, and writing down what happened

**Files:**
- Modify: `docs/superpowers/specs/2026-09-12-predicted-edits-design.md`, `docs/engine-roadmap.md`

- [ ] **Step 1: Instrument corrections, temporarily**

In `MatchClient::HandleSnapshot`, around the local player's `Reconcile(entry);` call, log each correction as it happens (marked `DIAG-TEMP`): record `m_CorrectionCount` and `m_LocalHealth` before the call, and if the count changed, `CB_INFO` one line with the client tick, `entry.LastInputTick`, the error, `m_Predicted.size()`, and whether health rose (a respawn). The logger flushes every line, so the log survives a killed process.

- [ ] **Step 2: Run a real match with the user**

Build. Launch `Server.exe` from `bin/Debug-windows-x86_64/Server`, wait about 6 s for the map to load, then two `Sandbox.exe --connect 127.0.0.1 --latency 150` from `bin/Debug-windows-x86_64/Sandbox`, each with stdout redirected to its own file. Ask the user to pillar-jump and dig with one player for about a minute, and to Alt+Tab as they normally would. **Ask them to tell you when they are done rather than closing anything**, then close each client with `WM_CLOSE` to its `GLFW30` window so `OnDetach` writes `NETSTATS`, and stop the server (it has no clean shutdown). Closing the black console windows kills the clients before `NETSTATS` is written.

Expected: in the editing player's log, zero correction lines that are not respawns. Record `NETSTATS` for both clients and the count of each kind of correction line.

If there are non-respawn corrections, stop and report them with their log lines before writing any documentation: the in-process gates passing while the live run does not is exactly Stage 4's lesson, and it is a finding, not a formality.

- [ ] **Step 3: Remove the instrumentation**

`git checkout -- Cubit/src/Net/MatchClient.cpp` only if `git diff` shows nothing in it but the `DIAG-TEMP` lines; otherwise remove them by hand. Rebuild and confirm `grep -rn DIAG-TEMP Cubit` prints nothing.

- [ ] **Step 4: Write the Shipped section**

Append `## Shipped` to the spec, in the voice of Stage 4's. Numbers, not adjectives:

- The pillar and dig gates' correction counts (0 and 0), and — from Task 6 Step 6 mutation 1 — how many corrections the same pillar run cost with the server applying edits on arrival. That pair is the stage's result.
- The loss case's measured count, and seeds 2 and 3 if they were run.
- The live run: duration, what was done, `NETSTATS`, and correction lines by kind.
- **What turned out differently from the design.** Include at least: any mutation in this plan that did not turn its test red, and why; anything a task had to do that the plan did not say (for example, the `SetInput` calls Task 5 needed); and whether the known erase-on-result candidate in Task 8 ever showed up.
- **Still open**, carried forward: other players' edits still arrive a round trip late; a placement next to another player can still mispredict; the edit log still grows without bound; the undiagnosed session death under `--loss 80/90`.

- [ ] **Step 5: Update the roadmap**

In `docs/engine-roadmap.md`, after the Stage 4 paragraph under "An entity or actor concept", add a Stage 5 paragraph in the same voice: that a player's own edits are predicted and applied at their tick, the measured before-and-after correction counts, and what is still open.

- [ ] **Step 6: Commit**

```bash
git add docs/superpowers/specs/2026-09-12-predicted-edits-design.md docs/engine-roadmap.md
git commit -m "Record networking stage 5 as shipped"
```

---

## Self-Review Notes

**Spec coverage.** Rules → Task 1. Replay write path → Task 2. Protocol version 4, `EditResult`, retired id → Tasks 3 and 7. Server applies at the input's tick, player-id order, results to the editor, `EditApplied` to others → Task 4. Client queue, prediction, confirmed layer, results → Task 5. Replay undo/redo, re-checking only the editor's own conditions, withdrawal → Task 6. Spec gates 1 (pillar), 2 (dig), 4 (no remesh) → Task 6; 3 (loss), 5 (refusal), 7 (conflicts) → Tasks 4 and 8; 6 (rules) → Task 1; 8 (protocol) → Tasks 3 and 7; 9 (single-player) → Task 9; 10 (live) → Task 10. The spec's "tests replaced on purpose" → Tasks 5 and 7.

**Where this plan is most likely to be wrong**, in order:
1. **The gates' geometry and timing** — jump apex, the dig column, the moment the refusal test catches the arrival. All are derived from the running code rather than asserted, and each has a stated fallback, but none has been run.
2. **Task 8's erase-on-result candidate** under jitter, called out there with what to log.
3. **Mutation predictions.** Every "expected red" is a prediction; the plan says to stop when one stays green.
