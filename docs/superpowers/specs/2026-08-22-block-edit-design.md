# BlockEdit — Design

**Date:** 2026-08-22
**Status:** Approved (pending spec review)

## Goal

Make a block edit a piece of **data** that can be applied, sent, and replayed,
instead of something the Sandbox performs by calling `World::SetBlock` directly.

## Why

This is the seam replication, undo, and demo recording all key off. Right now an
edit is not a thing — it is a sequence of four steps that happen to sit together
in `SandboxLayer::OnMouseButtonPressed` (`Sandbox.cpp:435-450`): bounds-check,
`World::SetBlock`, `SkyLight::Repropagate`, log. Nothing else can perform an edit
without repeating that sequence, and nothing can record, reverse, or transmit one
at all.

It is a small change now against a refactor of every call site later. Today there
is exactly **one** gameplay call site. The other `SetBlock` callers — `VoxLoader`
and `TerrainGen` — build worlds rather than edit them, and are deliberately left
alone.

It also pairs with the fixed timestep that shipped earlier today: fixed ticks give
you the numbering, and this gives you the thing to number. The numbering itself is
out of scope here.

## Context

- `World::SetBlock` (`Cubit/src/Voxel/World.cpp:47`) **throws** `std::out_of_range`
  on a bad coordinate, and marks the affected chunks dirty on success.
- `SkyLight::Repropagate(World&, int, int, int)` relights the region an edit could
  have affected and marks whatever changed dirty.
- `Cubit/src/Voxel/` includes no GL headers, which the fixed-timestep design
  records as load-bearing for an eventual headless server.
- `BlockId` is a `std::uint8_t` palette index; `0` is air (`Voxel/Block.h`).
- The `EventBus` exists but has no unsubscribe, which the roadmap flags as a
  dangling-callback hazard.

## Part 1 — The type

`Cubit/include/Cubit/Voxel/BlockEdit.h`, implementation in
`Cubit/src/Voxel/BlockEdit.cpp`.

```cpp
//One block changing at one position. An intent, not a record: it says what the
//world should become, not what it was.
struct BlockEdit
{
    glm::ivec3 Position{ 0 };
    BlockId    Block = 0;
};

//Applies an edit and returns the edit that would undo it, or nothing if the
//world did not change.
//
//Relights the affected region as part of applying, so an edit is one operation
//that leaves the world consistent rather than a ritual every caller has to know
//the second half of.
std::optional<BlockEdit> ApplyBlockEdit(World& world, const BlockEdit& edit);
```

Three outcomes:

1. **Out of bounds** — returns `std::nullopt`, changes nothing.
2. **No-op**, the block already equals `edit.Block` — returns `std::nullopt`,
   changes nothing.
3. **Applied** — reads the previous id, calls `World::SetBlock`, calls
   `SkyLight::Repropagate`, and returns `BlockEdit{ edit.Position, previous }`.

**The inverse is the return value, not a second type.** Undo is applying what you
were handed back. A client sending an edit to a server cannot honestly fill in a
"previous" field — it only believes it knows — so carrying one in the intent would
be a lie waiting to desync.

**Out of bounds returns rather than throws, and that is a deliberate divergence
from `World::SetBlock`.** Throwing is right when a bad coordinate means a
programming error in the caller. Once an edit is data that can arrive from a file
or a socket, an out-of-range position is malformed *input*. A server that throws
on a malformed packet is a server that a malformed packet can kill.

**Rejecting no-ops is not tidiness — it is what keeps undo meaningful.** If
placing a block where that block already sits returned an inverse, the undo stack
would accumulate entries that visibly do nothing when popped, and the user would
press undo twice to see one change.

## Part 2 — Sandbox integration

`OnMouseButtonPressed` keeps its raycast and its two target rules (break the block
hit, place against the entry face, refuse to place when the ray began inside a
block). It then builds a `BlockEdit` and applies it.

It **loses** two things, both of which now belong to `ApplyBlockEdit`:

- the explicit `m_World.IsInBounds(...)` guard, and
- the `SkyLight::Repropagate(...)` call.

That migration is the point of the change: the ritual moves inside the unit, so no
future caller has to know it. The log line is derived from the result rather than
from which mouse button was pressed.

It **gains** an undo stack:

```cpp
//Inverses of applied edits, newest last. Capped so a long session cannot creep.
static constexpr std::size_t MaxUndoDepth = 256;
std::vector<BlockEdit> m_Undo;
```

An applied edit pushes its inverse, discarding the oldest entry past the cap.
`U` pops one and applies it. `U` rather than `Ctrl+Z` because `Input` has no
modifier support today, and `U` is unbound.

**The trap this must design around: F9 reloads the world from disk.** Every
inverse on the stack then describes a world state that no longer exists, so
popping one would write a block that was never there. `LoadWorld` clears the
stack. This is the same shape as the teleport bug from the fixed-timestep branch —
state that silently outlives the thing it described.

Undoing an undo is not redo: applying an inverse is itself an edit, but its own
inverse is deliberately **not** pushed. Pushing it would make `U` alternate
between two states forever instead of walking back through history.

`U` pops the entry whether or not applying it changes anything. An inverse that
comes back `nullopt` describes a cell some other edit has since overwritten, so
the entry is stale and keeping it would only stall the stack on the same dead
entry every press.

## Part 3 — HUD

`HudState` gains an undo-depth field, drawn as `UNDO <n>`. `SandboxLayer` writes
it in `OnFrameUpdate`, beside the steps-per-frame field that already publishes
there — one place that mirrors state onto the HUD each frame, rather than a write
at every site that touches the stack.

Keyboard input cannot be delivered to the window by a screenshot script
(`glfwGetKey` needs real focus), so a readout is the only way undo is visible on
screen at all. The real proof stays in the tests; this is what makes the feature
observable when someone is actually holding the keyboard.

`DebugFont::Order` is currently `0123456789-.: ACDEFGNOPST` and lacks `U`. It gains
one glyph. `Order` and the `Glyphs` table are positionally matched and `IndexOf`
falls back to a blank silently, so a mismatch draws the wrong glyph for every
character after the insertion point with no error — append `U` to the end of both.

## Testing

New `Tests/src/BlockEditTests.cpp`. Adding a test file requires re-running
`premake5 vs2026` directly — **not** `GenerateProjects.bat`, which deletes `bin/`
and ends in a blocking `pause`. Create every new source file before the single
regeneration: premake expands its `files` globs at generation time, so a file
created afterwards is invisible to the build and the resulting link error is
indistinguishable from a genuine red state.

`World` and `SkyLight` need no GL context, so every case below is a real unit test.

- Applying an edit changes the block at the position.
- The returned inverse carries the position and the **previous** id.
- Applying the inverse restores the previous block.
- An out-of-bounds position returns `nullopt` and leaves the world unchanged —
  and does **not** throw, which is the divergence from `World::SetBlock` worth
  pinning.
- Setting a block to the value it already holds returns `nullopt` and leaves the
  world unchanged.
- Applying marks the containing chunk dirty, and a face edit also marks the
  neighbour (`WorldDirtyTests.cpp` is the precedent for how to assert this).
- A sequence of edits, undone in reverse, restores every block.

### The property that carries the design

**Apply an edit, apply its inverse, and the world must be identical — including
every sky-light value.**

Build a world with terrain, `PropagateAll`, snapshot every cell's sky light, dig a
block out of a surface so a light shaft opens, apply the inverse, then compare
cell by cell. Report the **first differing cell's position**, never a bool: a
lighting disagreement hides in one corner out of millions.

This is the test that proves relighting-inside-apply is correct rather than merely
present. A half-correct `Repropagate` — one that lights the new hole but fails to
un-light it on the way back — passes every other case in the list.

The digging direction matters: opening a shaft and closing it exercises both the
flood and the unflood. Filling a hole and reopening it would too, but digging is
what the Sandbox actually does first.

## Verification on screen

Build, run the Sandbox, screenshot, close via `WM_CLOSE`.

- `UNDO 0` appears in the readout at startup, with all five glyphs solid — a blank
  where `U` should be means the font insert is wrong.
- Breaking and placing blocks still works, and lighting still updates around an
  edit.

Pressing `U` cannot be scripted, so the undo behaviour itself is verified by the
tests rather than on screen, and by hand if desired.

## Out of scope

- **Redo.** It needs a second stack and a rule for what a fresh edit does to it.
- **Recording edits for demo playback.** A list nothing reads yet is speculation;
  undo has a user pressing a key, recording has nobody.
- **Publishing edits on the `EventBus`.** Nothing needs to react, and the bus has
  no unsubscribe.
- **Tick numbering.** `FrameClock` counts steps per frame, not total ticks, so a
  tick field would have nothing honest to fill it.
- **Batching.** Nothing replays a bulk sequence yet, and a flag meaning "skip the
  relight that keeps you correct" is an API that gets passed wrong.
- **Routing `VoxLoader` or `TerrainGen` through `BlockEdit`.** They build worlds
  rather than edit them; relighting per block during a build would be absurd.
