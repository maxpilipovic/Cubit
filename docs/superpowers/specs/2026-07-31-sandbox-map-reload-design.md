# Sandbox Map Reload (F9) — Design

**Date:** 2026-07-31
**Status:** Approved (pending spec review)

## Goal

Bind `F9` in the Sandbox to reloading `saved.vox`, so `F5` and `F9` form a
checkpoint pair usable entirely within one session: edit, save, keep editing,
restore when an edit turns out badly.

## Context

[World save](2026-07-31-world-save-design.md) added `F5`, which writes the edited
world to `assets/maps/saved.vox` beside the executable. Nothing reads that file.
Seeing a save again means copying it over `Sandbox/assets/maps/battlefield.vox`
and rebuilding — two steps outside the game, which leaves the authoring loop
half-closed.

`F9` closes it without any file copying.

Reverting to the shipped `battlefield.vox` was considered and rejected: it does
not pair with `F5`, so the save would stay unreachable and the loop would stay
half-closed.

## Architecture

Reloading is exactly what the constructor already does, so the setup is extracted
into one method rather than duplicated.

```cpp
//Replaces the world with the map at this path and settles the player into it.
void LoadWorld(const char* path)
{
    m_World = BuildWorld(VoxLoader::LoadFile(path));
    SkyLight::PropagateAll(m_World);
    LiftPlayerClearOfTerrain();
    m_VerticalVelocity = 0.0f;
    UpdateCameraPosition();
}
```

- The constructor calls `LoadWorld(MapPath)`, replacing its inline setup.
- `F9` calls `ReloadWorld()`, which wraps `LoadWorld(SavePath)` in a try/catch.
- The battlefield path becomes a `MapPath` constant beside the existing
  `SavePath`, rather than a string literal in the constructor.

Sky light must be propagated before anything meshes, or the first frames bake a
fully dark world into their vertex colours — the same reason the constructor
already orders it this way.

## Player handling

A reload can restore terrain where the player was standing. `VoxelCollision`
only pushes a box out of a block on a move it detects, so a player who starts
embedded stays embedded, with no escape but falling out of the world.

```cpp
void LiftPlayerClearOfTerrain()
{
    const float top = static_cast<float>(m_World.GetHeight());
    while (m_PlayerPosition.y < top &&
           VoxelCollision::Overlaps(m_World, m_PlayerPosition, PlayerHalfExtents))
        m_PlayerPosition.y += 1.0f;

    //A column solid to the sky has nowhere to stand.
    if (VoxelCollision::Overlaps(m_World, m_PlayerPosition, PlayerHalfExtents))
        m_PlayerPosition = SpawnPosition;
}
```

Keeping x and z preserves the part of the map being worked on, which is the point
of a fast reload. Resetting to spawn on every reload was rejected for that reason:
authoring a far corner of the map would mean walking back after each one.

At construction this is a no-op — spawn is in mid-air, so nothing overlaps.

## Error handling

`VoxLoader::LoadFile` throws before the assignment to `m_World`, so a missing or
malformed `saved.vox` leaves the current world untouched. Exception safety comes
from the order of operations, not from cleanup.

`ReloadWorld` catches `std::exception` and reports it with `CB_ERROR`, for the
same reason `SaveWorld` does: the handler runs inside a GLFW key callback, which
is C code, and propagating an exception across a C frame is undefined behaviour.

Pressing `F9` before ever pressing `F5` produces one error line and no other
effect.

The constructor does **not** catch. A sandbox that cannot load its map has
nothing to do, so that failure should propagate.

## Cost, and what the user sees

`F9` costs about what launching the application costs, minus process startup:

- `BuildWorld` and `SkyLight::PropagateAll` are synchronous. The window is frozen
  for several seconds in a debug build (the flood alone is ~5.2 s per
  [performance.md](../../performance.md)), roughly a second in an optimised one.
- All 1024 chunks are then re-meshed through `WorldRenderer`'s 4 ms per-frame
  budget, visible as the HUD's `PENDING` counter draining.
- `WorldRenderer` keys its cached meshes by chunk coordinate and a fresh `World`
  marks every chunk dirty, so stale geometry is replaced progressively. The old
  map is briefly visible while the new one rebuilds. No renderer change is needed.

This is accepted rather than hidden behind a loading screen: the same cost is
already paid silently at startup, and threading the load is a separate,
already-identified engine item.

## Testing

No automated tests. `Tests` links `Cubit`, not `Sandbox`, so this code is not
reachable from the suite — the same situation as `HudLayer` and the player
controller.

Moving `LiftPlayerClearOfTerrain` into the engine to make it testable was
considered and rejected: what happens to a player on reload is sandbox policy,
and the engine has no concept of a player.

Manual verification: dig a pit, `F5`, fill it back in, `F9`. The pit returns and
the player stands on solid ground rather than inside it. Then press `F9` again
after deleting `saved.vox` and confirm a single error line with the world intact.

## Out of scope

- A progress indicator or loading screen.
- Async or threaded loading.
- A key that reverts to the shipped `battlefield.vox`.
- Save slots or multiple checkpoints.
