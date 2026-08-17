# Camera Aim API and Map-Aware Spawn — Design

**Date:** 2026-08-16
**Status:** Approved (pending spec review)

## Goal

Close the last engine gap on the roadmap: let a spawn choose where it stands and
which way it faces, without either value being hand-picked against a specific map.

Two independent capabilities, used together by the Sandbox:

1. **Aim the camera.** Set yaw and pitch on the controller, and derive them from a
   point to look at.
2. **Find open ground.** Resolve a coarse map hint into a position where the player
   box actually fits, above ground and out of the water.

## Context

`Sandbox.cpp:43` holds `SpawnPosition{240.5, 27.0, 300.5}` under a five-line comment
explaining that it is tied to `battlefield512.vox` specifically and has to be
re-picked whenever the map changes. The comment exists because getting it wrong is
not obvious: a spawn inside terrain renders a **black screen**, which reads as a
rendering bug rather than a bad constant. That cost real time when the map went from
256 to 512, since `TerrainGen` sizes features in absolute blocks and a 512 map is a
different landscape rather than an enlarged one.

Facing has the same problem in weaker form. The camera's default is `-90°` yaw, which
faces `-z`, so picking a spawn also means finding a spot whose view *along `-z`*
happens to be worth looking at.

`PerspectiveCamera::SetRotation(yaw, pitch)` already exists. The gap is one level up:
`PerspectiveCameraController` exposes only `SetPosition`, and it owns its own
`m_Yaw`/`m_Pitch`.

## Scope

The engine learns to find **safe ground near a hint**. It does not learn about teams,
forts, or map metadata. Deriving spawn points from map features is a gameplay
question — and `.vox` has nowhere to record them — so it stays out.

## Part 1 — Camera aim

### `PerspectiveCameraController::SetRotation(float yaw, float pitch)`

Sets `m_Yaw`/`m_Pitch`, clamps pitch to the same ±89° the mouse path uses, and calls
the existing `UpdateCamera()`. Plus `GetYaw()`/`GetPitch()`, for symmetry with
`GetPosition()`.

**Why it must go through the controller.** Yaw and pitch are stored twice — once on
the controller, once on the camera. Setting the camera directly appears to work until
the next mouse move, which recomputes from the controller's stale copy and snaps the
view back. This is the failure mode worth a test.

### `PerspectiveCamera::YawPitchToward(from, to) -> glm::vec2`

A static inverting the convention in `RecalculateViewMatrix`:

```
forward = (cos(yaw)·cos(pitch), sin(pitch), sin(yaw)·cos(pitch))

yaw   = degrees(atan2(d.z, d.x))
pitch = degrees(atan2(d.y, length(d.xz)))
```

It belongs on the camera because that is where the convention lives — it is the
reason `-90°` means `-z`. A caller doing its own `atan2` would be duplicating a
constant it does not own.

Degenerate `from == to` returns the camera's default `{-90, 0}` rather than a NaN.

## Part 2 — `SpawnFinder`

A new engine unit, `Cubit/include/Cubit/Voxel/SpawnFinder.h` and its `src` pair,
factored like `VoxelRaycast` and `VoxelCollision`.

```cpp
std::optional<glm::vec3> FindSpawn(
    const World& world, const glm::ivec2& hintXZ, const glm::vec3& halfExtents);
```

Returns the **centre of the player box**, matching `VoxelMoveResult::Position`, so
the result assigns straight into a position the collision system already understands.

### Per-column rule

1. Scan `y` from `world.GetHeight() - 1` down to 0 for the first **solid** block.
   None found means an unusable column — pure air, or pure water.

There are exactly two rejections, then: **no surface at all**, and **a surface under
water**. Everything else in a voxel world is a place you can stand.
2. Candidate centre is `(x + 0.5, surfaceY + 1 + halfExtents.y, z + 0.5)`. Feet rest
   exactly on the surface, so the first frame reports grounded rather than falling.
3. Reject if `VoxelCollision::OverlapsFluid` — this is what keeps a spawn out of the
   river. It needs no water-level constant: standing on the riverbed puts the box in
   water, so the existing predicate already answers it.

**There is deliberately no solid-overlap check.** It would be dead code: the scan takes
the *first* solid block from the top, so every cell above it is non-solid by
construction, and the 0.6-wide box centred in a 1.0 cell never crosses into a
neighbouring column. Nothing can be in the way. Adding a `VoxelCollision::Overlaps`
call would look prudent while being unreachable, and any test written for it could
not fail.

Columns outside the world are unusable, so a hint just off the edge walks itself back
in rather than needing a bounds check at the call site.

### Search

Spiral outward in rings of increasing Chebyshev distance from the hint, nearest ring
first, scanning within a ring in a fixed order so the result is **deterministic and
testable**. Bounded at a radius of 64 columns (a 129×129 window) — ample on a 512 map,
while stopping a broken hint from quietly scanning the whole world.

### Two accepted consequences

- **A tree canopy is a valid spawn**, and so is any other floating surface. The
  top-down scan reaches leaves before ground. The engine has no notion of "leaves",
  and standing on a canopy is stable and visible — not the failure this design exists
  to fix. This is the same property that makes a solid-overlap check unnecessary:
  the topmost surface is always standable.
- **Caves are never spawned into**, for free: the scan finds the cave's *roof* first
  and stands the player on top of it.

### Failure

Exhausting the radius returns `std::nullopt`. The Sandbox logs `CB_ERROR` naming the
hint and the radius searched, then falls back to dropping the player from above the
hint column.

The log line is the point. A black screen is anonymous today; afterwards it says
which hint failed and how far the search looked.

## Part 3 — Sandbox integration

The `SpawnPosition` constant becomes a `SpawnHintXZ` constant (`glm::ivec2`) plus a
resolved `glm::vec3 m_Spawn`.

`ResolveSpawn()` runs inside `LoadWorld`, after `SkyLight::PropagateAll` and
**before** `LiftPlayerClearOfTerrain`, so **F9 re-resolves against the restored
terrain**. The order matters: the lift's last-resort fallback *is* the spawn, so the
spawn has to be valid for the new world before the lift can fall back on it. That
also fixes an existing weakness at `Sandbox.cpp:417`, where a column solid to the sky
drops the player at a constant the reloaded map may have since built over.

All three present uses of the constant become `m_Spawn`: the initial position, the
fall-out-of-the-world reset at `Sandbox.cpp:215`, and that fallback.

**Facing is applied once, at construction — not on reload.** F9 exists to iterate on
terrain you are standing in front of; re-aiming the view on every reload would be a
regression.

The aim target is the map centre **at the spawn's own eye height**, so pitch comes out
level and yaw does the work. Aiming at the literal centre of a 512×64×512 box would
tilt the view about 3° into the ground.

## Testing

Headless doctest cases:

**Camera**
- `YawPitchToward`: `-z → -90°`, `+x → 0°`, degenerate `from == to → {-90, 0}`.
- `YawPitchToward` round-tripped through `SetRotation`: the resulting forward vector
  points at the target. This is the case that would catch a sign or axis error.
- `SetRotation` on the controller: the camera follows, **and a subsequent mouse move
  continues from the new yaw instead of snapping back**. Nothing else catches the
  two-copies-of-yaw bug.

**SpawnFinder**
- Flat floor: the expected centre, feet on the surface.
- A hill under the hint: lands on top, not inside — the exact 512 failure.
- Water under the hint: spirals to the bank rather than the riverbed.
- A hint outside the world: the spiral walks back in.
- A floating lid above the hint: stands **on** the lid — pinning the canopy behaviour
  as intended rather than leaving it to be "fixed" later.
- A column solid to the top of the world: stands on its top face, box extending into
  the open air above the world. Also not a rejection.
- A world of nothing but air, and one of nothing but water: `std::nullopt` for both.
  Water alone is not standable, and that is a different code path from empty.
- Determinism: the same world and hint give the same answer.

**Against `battlefield512.vox`**
- The returned spawn overlaps neither solid nor fluid, and has solid ground directly
  beneath it.

This case needs **both candidate asset paths** — the suite runs from the repo root by
hand and from `Tests/` as a post-build step — plus an assertion that the asset was
actually found. A path-guarded test that silently no-ops looks green while proving
nothing; watch the assertion count, not the case count.

**On screen**

Build, run the Sandbox, screenshot, and confirm a lit view down the map with the HUD
`POS` matching the resolved spawn and its `y` **not drifting upward**. Upward drift is
collision pushing the player out of solid ground — the tell for a spawn still buried.

## Out of scope

- Team spawns, fort-derived spawn points, or any map metadata.
- Scaling `TerrainGen`'s absolute-sized features to map size. The forts being two
  10-block specks 496 apart on the 512 map is a real problem, but it is a question
  about how the game plays, not about where a player starts.
- A spawn that picks its own hint with no caller input.
