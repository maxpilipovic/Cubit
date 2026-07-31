# World Save — Design

**Date:** 2026-07-31
**Status:** Approved (pending spec review)

## Goal

Write an edited `World` back out as a MagicaVoxel `.vox` file, closing the loop the
load path opened. The purpose is **map authoring**: load `battlefield.vox`, fix the
map by playing it, save it back, and reload. `MapGen` becomes the first draft of a
map rather than its only source.

## Context

The load path already exists end to end:

```
file -> VoxLoader::LoadFile -> VoxModel -> BuildWorld -> World
```

`VoxWriter::Write(model) -> bytes` also exists, added for
[the battlefield map](2026-07-25-battlefield-map-design.md), and is tested as the
exact inverse of `VoxLoader::Parse`. What is missing is the two ends: turning a
`World` back into a `VoxModel`, and putting bytes on disk.

Listed in [engine-roadmap.md](../../engine-roadmap.md) as item 7, "World
persistence — nearly free now that `VoxWriter` exists".

## Architecture

Two new pieces, each mirroring its opposite number on the load path. No existing
code changes shape.

```
LOAD   file  -> VoxLoader::LoadFile    -> VoxModel -> BuildWorld  -> World
SAVE   World -> ToVoxModel             -> VoxModel -> VoxWriter::WriteFile -> file
```

### 1. `ToVoxModel` (new — free function, `VoxWriter.h`)

A free function, not a member, because `BuildWorld` is a free function in
`VoxLoader.h` and this is its inverse. Pure: no file I/O, so it is testable in
memory the way every existing `VoxWriter` test already is.

```cpp
CB_API VoxModel ToVoxModel(const World& world);
```

- **Size:** `{ world.GetWidth(), world.GetHeight(), world.GetDepth() }` — the
  chunk-padded size the world actually holds (see *Padding* below).
- **Voxels:** dense, in `VoxModel::At` order, `x + Size.x * (y + Size.y * z)`.
- **Colors:** `world.GetPalette()`, assigned directly.
- **Sky light is not saved.** The `.vox` format has nowhere to put it, and
  `SkyLight::PropagateAll` recomputes it on load, which the Sandbox already does.

### 2. `VoxWriter::WriteFile` (new — `VoxWriter.h`)

```cpp
static void WriteFile(const VoxModel& model, const std::string& path);
```

Mirrors `VoxLoader::LoadFile`. Calls the existing `Write` and puts the bytes on
disk in binary mode. Throws when the path cannot be opened.

### 3. Sandbox wiring

`F5` saves to `assets/maps/saved.vox` beside the executable and logs the resolved
absolute path through `CB_INFO`.

## Padding

A `World` is always a whole number of chunks. `BuildWorld` rounds up, so a model
whose size is not a multiple of 16 loads into a larger world:

| Map | On disk | Loads into | Saves as |
|---|---|---|---|
| `battlefield.vox` | 256×64×256 | 256×64×256 | 256×64×256 — identical |
| `starter.vox` | 16×6×16 | 16×16×16 | 16×16×16 — ten layers of air added |

**Decision: save the padded world as-is.** The shipped map is chunk-aligned, so it
round-trips byte-for-byte today. Trimming trailing air was rejected because the
saved size would then follow content — one block placed high would restore the full
height — and recording the source size on `World` was rejected because it adds a
field only the writer reads, and leaves the question of what happens to a block
placed outside the remembered box.

Padding costs nothing on disk: `Write` emits `XYZI` sparsely, so added air layers
contribute no voxels.

## The 256-per-axis guard

`VoxWriter::Write` stores voxel coordinates as single bytes
(`VoxWriter.cpp:51-53`). A world larger than 256 on any axis wraps silently and
produces a file that loads back as garbage. `World` has no such limit — its chunk
grid is arbitrary — so nothing stops this today.

`ToVoxModel` throws `std::runtime_error` when any axis exceeds 256, with a message
naming the axis and prefixed `vox:`, matching `VoxLoader`'s convention and its own
`MaxDimension` check on the read side. Size 256 is legal: coordinates run 0–255,
which is exactly a byte.

This is the honest placeholder for multi-model stitching. When that lands, it
replaces the throw.

## Error handling

Two failure modes, both `std::runtime_error`, consistent with the loader:

- **Oversized world** — `"vox: world is too large for a single .vox model (x = 272)"`
- **Unopenable path** — `"vox: cannot open file for writing: <path>"`, worded after
  `LoadFile`'s existing `"vox: cannot open file: <path>"`.

**The Sandbox handler must catch both.** `OnKeyPressed` runs inside a GLFW C
callback, and propagating a C++ exception across a C frame is undefined behaviour.
The `F5` handler wraps the save in try/catch and reports failure with `CB_ERROR`, so
a failed save is a log line rather than a crash.

## Testing

Doctest, per project convention. The conversion is pure, so most of it needs no
filesystem.

### `ToVoxModel` (Tests/src/WorldSaveTests.cpp — new)

- **Size is the padded world size:** a `2×1×3`-chunk world reports `32×16×48`.
- **Blocks land at matching positions:** blocks set at known world coordinates read
  back at the same coordinates through `model.At`.
- **Air stays air:** untouched positions are 0.
- **Palette carries over:** a world with a custom palette produces a model whose
  `Colors` match.
- **Round-trip through `BuildWorld`:** `BuildWorld(ToVoxModel(world))` reproduces
  every block and the palette. *This is the property the feature exists to provide*
  — it fails loudly if either half drifts.
- **Round-trip through bytes:** `VoxLoader::Parse(VoxWriter::Write(ToVoxModel(world)))`
  reproduces the world, exercising the axis swap and palette off-by-one as well.
- **Oversized world throws:** a 17-chunk-wide world (272 blocks) throws rather than
  wrapping to a corrupt file.

### `VoxWriter::WriteFile`

- **Write then load:** writing a model to a temporary path and loading it back
  reproduces it. The temporary file is removed afterwards.
- **Unopenable path throws:** writing into a directory that does not exist throws
  `std::runtime_error`.

### Rendering verification

Per project convention: build, run the Sandbox, edit a few blocks, press `F5`, and
confirm the logged path exists. Then copy `saved.vox` over
`Sandbox/assets/maps/battlefield.vox`, rebuild, run again, and confirm the edits are
present in the loaded map. Close via `WM_CLOSE` so the log survives.

## Build wiring

New sources land in the existing `Cubit/src/**.cpp` and `Tests/src/**.cpp` globs, but
**premake must be regenerated** (`GenerateProjects.bat`) after adding the new test
file so it joins the project.

## Out of scope

- Save slots, timestamped files, or any overwrite of the source map in
  `Sandbox/assets` — promoting a save into the source tree stays a deliberate copy.
- A HUD confirmation. The Sandbox already reports edits through the log, and the HUD
  has no message line; adding one is a separate change.
- Saving sky light, or any data the `.vox` format cannot hold.
- Splitting a world larger than 256 across several models — that is multi-model
  stitching, and the throw above is the placeholder for it.
