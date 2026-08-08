# Transparency, Phase 2: Solidity — Design

**Date:** 2026-08-08
**Status:** Approved (pending spec review)

## Goal

Make water behave like water: the player falls through it and stands on the
riverbed, rather than walking on its surface. Clicking water still breaks it.

This completes the split that
[Phase 1](2026-07-31-transparency-opacity-design.md) began.

## Context

Phase 1 shipped opacity on 2026-08-06: `Palette` carries alpha, `World` derives
an `m_Opaque` table from it, and the mesher and sky light ask opacity instead of
solidity. Water renders see-through with a lit riverbed beneath it.

Solidity was deliberately left untouched, so `IsSolid(block)` is still
`block != 0` (`Cubit/include/Cubit/Voxel/Block.h:24`) and water is still
concrete: you walk on the river's surface, and `VoxelCollision` and
`VoxelRaycast` both route through that predicate.

Listed in [engine-roadmap.md](../../engine-roadmap.md) as the unfinished half of
the transparency item.

### There are three properties, not two

Phase 1 described `IsSolid` as conflating two properties. Reading the call sites
for this phase turned up a third, and it is the one that makes the change
dangerous if done naively:

| property | means | rule | asked by |
|---|---|---|---|
| **present** | emits geometry at all | `block != 0` | mesher, raycast, terrain gen |
| **opaque** | hides faces, blocks light | `alpha >= 1.0` | mesher, sky light |
| **solid** | blocks movement | `alpha >= 1.0` | collision |

`ChunkMesher.cpp:315` uses `IsSolid` to mean *present* — "is there a block here
to draw" — and then immediately branches on opacity at `:320` to choose the pass:

```cpp
if (!cells.IsSolid(cell))
    continue;                                  // present?

MeshGeometry& target = cells.IsOpaque(cell)    // which pass?
    ? mesh.Opaque : mesh.Transparent;
```

Redefining `IsSolid` in terms of alpha without splitting *present* out first
would make water emit no geometry at all, silently undoing Phase 1. Phase 2's
real work is separating *present* from *solid*; water floating free of collision
is the consequence.

## The solidity rule

A block is **solid** when its palette alpha satisfies `alpha >= 1.0f` — the same
rule as opacity.

```cpp
// Block.h
constexpr bool IsPresent(BlockId block) { return block != 0; }  // was IsSolid
constexpr bool IsOpaqueColor(const glm::vec4& c) { return c.a >= 1.0f; }
```

There is deliberately no `IsSolidColor` beside `IsOpaqueColor`. The two rules are
the same expression, and the table below is shared, so a second constexpr would
have no caller.

`World` exposes solidity beside the opacity it already has:

```cpp
bool World::IsIdSolid(BlockId block) const;          // table lookup
bool World::IsBlockSolid(int x, int y, int z) const; // out of bounds -> false
bool World::IsBlockPresent(int x, int y, int z) const;
```

**`IsIdSolid` reads the existing `m_Opaque` table rather than a new `m_Solid`
one.** Deriving both properties from alpha makes a second array identical to the
first on all 256 entries, so it would buy nothing but an opportunity for the two
to drift apart. The *names* stay distinct so call sites state what they mean;
sharing the storage is an implementation detail behind them.

`Chunk::IsBlockSolid` becomes `Chunk::IsBlockPresent`. A chunk holds no palette,
so *present* is the only one of the three it can answer — which is structural
confirmation the split is cut in the right place.

### Why alpha, and what it costs

Solidity could have had its own source of truth — a separate overridable table,
or MagicaVoxel `MATL` chunks parsed out of the `.vox`. Deriving it from alpha was
chosen instead: it needs no new data, no format work, and no content pipeline
change, and it makes water correct immediately.

The cost is precise and worth recording. Phase 1's spec anticipated glass as
"opaque off, solid on". **Under this rule glass is not expressible** — anything
see-through is also walk-through. Making glass work later means giving `m_Solid`
its own storage and its own source. Because every call site will already say
`IsIdSolid`, that change is confined to how the table is filled; no consumer
moves. That is the whole reason the names stay separate despite sharing storage.

### Existing behaviour is preserved by construction

`World`'s constructor installs `DefaultPalette()` and builds the derived table
(`World.cpp:11-14`). That palette gives alpha `1.0` to every entry except air,
which gets alpha `0`. So in any world that has not loaded a map palette, "solid"
under the new rule is true for exactly the ids `block != 0` was true for.

Every existing collision and raycast test therefore keeps its current behaviour
through the rename, which is what makes the mechanical half of this change safe.

## Call sites

| site | today | after | behaviour |
|---|---|---|---|
| `VoxelCollision.cpp:33` (`OverlapsSolid`) | `IsBlockSolid` = `!=0` | `IsBlockSolid` = alpha | **changes** — water stops blocking |
| `SkyLight.cpp:212` (`Repropagate`) | `IsBlockSolid` | `IsBlockOpaque` | **bug fix**, see below |
| `VoxelRaycast.cpp:96` | `IsBlockSolid` | `IsBlockPresent` | unchanged |
| `ChunkMesher.cpp:315` | `IsSolid` | `IsPresent` | unchanged |
| `Neighbourhood.h:89` | `IsSolid` | `IsPresent` | unchanged |
| `TerrainGen.cpp:155` | `IsSolid` | `IsPresent` | unchanged |
| `Chunk.cpp:31` | `IsBlockSolid` | `IsBlockPresent` | unchanged |

### The sky light straggler

`SkyLight::Repropagate` asks `IsBlockSolid` to decide whether an edit filled a
cell in or opened it up (`SkyLight.cpp:212`), while the three other lighting
sites moved to opacity in Phase 1 (`:99`, `:147`, `:194`). Placing a
non-opaque block therefore takes the *unflood* path — taking back light that
still passes straight through the block that was placed.

This is a Phase 1 defect, latent only because the shipped map's transparent id
is one nothing routinely places. It is in scope here because it is the same
predicate confusion, one line from the code being changed.

### Why the raycast keeps asking *present*

Collision and the editing raycast deliberately diverge: you pass through water
but you can still click it. This keeps water removable — `MapBlocks::Water` is
index 7 and `PlaceableBlocks` is `{1..8}` (`Sandbox.cpp:49`), so key `7` places
water, and a raycast that skipped it would hand the player a block they could
create but never destroy.

A future bullet trace wants solidity rather than presence, but no such trace
exists and adding a parameter for a hypothetical caller is speculative. The
raycast keeps one rule until a second caller genuinely needs a different one.

## Sandbox

No new movement state, no buoyancy, no drag. The river is two blocks deep
(`CarveRiver` puts the bed at `WaterLevel - 2` and fills y=9..10,
`TerrainGen.cpp:129,146`), so a player who falls in lands on the bed with their
head just under the surface. Buoyancy would be invisible at that depth and would
bake gameplay tuning constants into the engine.

One knock-on: `LiftPlayerClearOfTerrain` (`Sandbox.cpp:313`) steps the player up
until `VoxelCollision::Overlaps` reports clear, so after this change a player who
reloads standing in the river is no longer lifted out of the water. That is
correct — they should be in it — but the function's comment talks about being
lifted clear of *terrain* and needs a word to match.

## Testing

Doctest, per project convention. A test installs a palette with an alpha-`0.55`
entry to get a transparent id; no map asset is needed.

**New:**
- `World::IsIdSolid` / `IsBlockSolid` are false for a transparent id, true for an
  opaque one, false for air.
- `IsBlockPresent` is true for that same transparent id — pins the split itself.
- Out-of-bounds positions read as not solid, matching `GetBlock` reading as air.
- A falling box passes through a water column and lands on the bed beneath it.
- `VoxelCollision::Overlaps` is false for a box sitting inside water.
- A ray fired into water hits the **water** block, not the bed — pins the
  deliberate divergence from collision.
- Placing a non-opaque block does not darken the column beneath it. This is the
  `SkyLight.cpp:212` fix, and it rides on the "full-strength light falls for
  free" rule that P6 already has a named test for, so a wrong unflood shows up as
  a dark column rather than a single dark cell.

**Mechanical:** the `IsSolid` → `IsPresent` rename touches
`BuildWorldTests.cpp`, `ChunkMesherTests.cpp`, `ChunkTests.cpp`,
`TerrainGenTests.cpp`, `VoxelRaycastTests.cpp` and `WorldTests.cpp`. Their
assertions are unchanged — they are the regression net for every "unchanged" row
in the call-site table.

**Not unit tested:** that the river feels right to walk into. Verified by running
the sandbox — build, walk into the water, close via `WM_CLOSE` so the log
survives. Success looks like: walking off the bank drops the player to the
riverbed rather than onto the surface, the water still renders see-through from
below as well as above, and clicking the surface breaks a water block.

## Out of scope

- Swimming, buoyancy, sink rate, in-water drag, drowning or oxygen.
- Glass, or any block that is non-opaque but solid — see the cost recorded above.
- `MATL` parsing, or any per-block material data in the `.vox` format.
- Per-block light absorption (deeper water reading darker) — still deferred from
  Phase 1.
- A solidity parameter on `VoxelRaycast` for a future bullet trace.
- Keeping `IsSolid` as a deprecated alias. A name that meant two things is
  precisely what this phase removes.
