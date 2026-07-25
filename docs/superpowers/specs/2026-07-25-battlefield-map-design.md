# Ace-of-Spades-Style Battlefield Map — Design

**Date:** 2026-07-25
**Status:** Approved (pending spec review)

## Goal

Produce a large, playable, Ace-of-Spades-style map — a symmetric two-team
battlefield with a central river, forests, and mountains framing the flanks —
generated offline into a committed `.vox` file that the Sandbox loads through the
existing map-loading pipeline. The map should read as a place players would enjoy
flying around and building in, not a test fixture.

## Context

The prior slice ([vox map loading](2026-07-23-vox-map-loading-design.md)) added the
load path: `VoxLoader::Parse` / `LoadFile` → `BuildWorld` → a `World` the Sandbox
renders. The engine is a free-fly sandbox with block place/break and no game modes
yet, so team "bases" are **cosmetic** structures here, positioned so real
spawns/objectives can be attached later (the deferred `.json` sidecar work).

A single `.vox` model caps at 256 per axis; full AoS 512×512 needs the deferred
multi-model stitching. This map stays within the cap.

## Scale

- **Dimensions:** 128 (x) × 48 (y, up) × 128 (z) blocks, in Cubit (Y-up) space.
- **Chunks:** 8 × 3 × 8 = **192 chunks** (chunk = 16³).
- **Data:** ~300–400k solid voxels, ~1.5 MB `.vox` on disk.
- **Symmetry:** **geometrically** mirrored across the center plane (`x` pairs with
  `127 - x`), so both team halves have identical terrain shape. Block *colors*
  mirror too, with one deliberate exception: the two forts are colored by team side
  (red on the left, blue on the right). So the invariant is solidity-symmetric
  everywhere, and color-symmetric everywhere except the fort blocks.

## Architecture

Four components; the reusable, tested work lives in the Cubit library, and a thin
tool runs it offline.

### 1. `VoxWriter` (new — Cubit library)

The exact inverse of `VoxLoader`. Serializes a Cubit-space `VoxModel` into `.vox`
bytes.

- **Interface:**
  - `static std::vector<std::uint8_t> VoxWriter::Write(const VoxModel& model);`
- **Axis mapping (inverse of the loader):** the loader reads
  `cubit(x, y, z) = vox(x, z, y)`. So the writer stores a Cubit voxel `(cx, cy, cz)`
  at vox position `(cx, cz, cy)` and writes the vox `SIZE` as
  `(model.Size.x, model.Size.z, model.Size.y)`. This makes
  `Parse(Write(model))` reproduce `model`.
- **Palette (inverse off-by-one):** the loader sets `palette[j + 1] = RGBA[j]`, so
  the writer emits `RGBA[j] = model.Colors[j + 1]` for `j` in 0..254 (RGBA entry
  255 unused; alpha 255). Round-trip identity holds for palette indices **1..255**;
  index 0 is air and is not stored (a round-tripped model has `Colors[0]` equal to
  `DefaultPalette()[0]`, i.e. black).
- **Voxels:** sparse `XYZI` — only non-air voxels (`id != 0`) are emitted, each as
  `(x, y, z, id)` bytes. Coordinates fit in a byte because every axis ≤ 128 < 256.
- **Chunk layout written:** `VOX ` + version 150, `MAIN`, then `SIZE`, `XYZI`,
  `RGBA`, matching what `VoxLoader::Parse` already accepts.

### 2. `TerrainGen` (new — Cubit library)

Pure generation: config in, `VoxModel` out. No file I/O. Deterministic given a seed.

- **Interface:**
  ```cpp
  struct TerrainConfig
  {
      glm::ivec3 Size{ 128, 48, 128 };
      unsigned Seed = 1337;
      int   WaterLevel = 10;      // y at/below which the river holds water
      int   GroundBase = 13;      // baseline grass height before noise
      int   MountainHeight = 30;  // extra height added toward the z flanks
      int   SnowLine = 34;        // grass/stone above this y gets snow caps
      float ForestDensity = 0.06f;// per-eligible-column tree probability
  };
  VoxModel TerrainGen::Generate(const TerrainConfig& config);
  ```
- **Palette:** installs the map palette (below) into `model.Colors`.

### 3. `MapGen` (new — console project, `MapGen/`)

Thin offline tool. Builds a `TerrainConfig`, calls `TerrainGen::Generate`, passes
the model to `VoxWriter::Write`, and writes the bytes to a path (default
`Sandbox/assets/maps/battlefield.vox`, overridable by a CLI arg). Not run on every
build — invoked manually to (re)produce the committed asset. Prints the output path
and byte/voxel counts.

### 4. Sandbox wiring

- Load `assets/maps/battlefield.vox` as the new default map (`starter.vox` stays in
  the repo as the small test asset).
- Spawn the player on top of **Fort A** (near `x≈8, z≈64`) looking down the field
  toward Fort B. Adjust `WorldOffset`, `SpawnPosition`, and `FallResetHeight`
  together to sensible values for a 128×48×128 map (minimal tuning, not a spawn
  system).

## Generation algorithm

All heights are in Cubit Y. The map is generated for `x` in `[0, 63]` and mirrored
to `[64, 127]` (`x' = 127 - x`), guaranteeing symmetry. The mirror copies geometry
and colors, except that a mirrored **red** fort block is rewritten to **blue** so
the two sides read as opposing teams.

1. **Height field** — fractal value noise (a small self-contained hash-based value
   noise summed over ~4 octaves; no external library), seeded from `config.Seed`.
   Produces rolling ground around `GroundBase` ± a few blocks.
2. **Mountains on the flanks** — add a ridge term that grows toward the `z = 0` and
   `z = 127` edges (e.g. proportional to `MountainHeight * edgeFactor(z)`), so the
   flanks wall up while the central lanes (mid-z) stay open and passable.
3. **Column fill** — for each column, fill `y = 0` up to its height: **stone** at
   depth, a **dirt** band beneath the surface, **grass** on top. Above `SnowLine`,
   surface becomes **snow** and exposed rock becomes **stone-dark**.
4. **River** — a channel centered on the mirror line (`x = 63/64`), meandering in
   `x` by a low-frequency noise of `z` (amplitude a few blocks), width ~6–10. Within
   the channel, carve the ground down and fill with **water** up to `WaterLevel`;
   line the banks with **sand**. Because the channel is generated to include the
   `x = 63` edge and then mirrored, it forms one continuous river straddling the
   contested center line.
5. **Forests** — for each eligible surface column (grass, above water level, away
   from the river banks and forts), a per-column hash compared against
   `ForestDensity` places a **tree**: a **wood** trunk 4–7 tall plus a rounded
   **leaves** canopy (with **leaves-light** highlights). Trees are placed on the
   `[0,63]` half and mirrored.
6. **Forts** — at `x≈8` and `x≈119` (mirror pair), centered near `z≈64`: a small
   symmetric rampart (walls + floor + an opening facing the field). Fort A (left) is
   built in **red**; its mirror image is recolored to **blue** to form Fort B.
7. **Dithering** — grass and leaves use their paired light/dark indices chosen by
   the column hash, so large areas read as natural rather than flat sheets.

### Palette

| # | Block | Approx RGB (0–255) | Use |
|---|-------|--------------------|-----|
| 1 | Grass | (70, 145, 55) | terrain surface |
| 2 | Grass-dark | (55, 115, 45) | surface dithering |
| 3 | Dirt | (120, 85, 50) | sub-surface band |
| 4 | Stone | (130, 130, 135) | rock / mountains |
| 5 | Stone-dark | (80, 80, 85) | deep/exposed rock |
| 6 | Sand | (210, 195, 140) | river banks |
| 7 | Water | (55, 110, 200) | river (opaque, solid) |
| 8 | Wood | (95, 65, 40) | tree trunks |
| 9 | Leaves | (45, 110, 45) | canopy |
| 10 | Leaves-light | (70, 140, 60) | canopy highlight |
| 11 | Snow | (235, 240, 245) | mountain caps |
| 12 | Red base | (190, 45, 45) | Fort A |
| 13 | Blue base | (55, 80, 200) | Fort B |

(Index 0 is air. Exact values may be nudged during implementation for readability.)

**Note on water:** the engine has no transparency; `IsSolid` treats any non-air
block as solid, so water renders as an opaque blue surface and is collidable — an
acceptable AoS-style approximation for now.

## Testing

Doctest, per project convention (Tests project runs as a postbuild step). Behavioral
invariants, not golden bytes.

### `VoxWriter` (Tests/src/VoxWriterTests.cpp)
- **Round-trip identity:** build a small `VoxModel` (e.g. 3×4×5) with a handful of
  voxels and a few custom palette entries; `Parse(Write(model))` reproduces `Size`,
  every `At(x, y, z)`, and `Colors[1..255]`.
- **Air is skipped:** a model with only air produces a valid file whose `XYZI` count
  is 0 and which parses back to an all-air model of the same size.
- **Header sanity:** output begins with `VOX ` + version 150 and a `MAIN` chunk.

### `TerrainGen` (Tests/src/TerrainGenTests.cpp)
- **Size:** generated model size equals the configured size.
- **Geometric mirror symmetry:** for all `x, y, z`,
  `IsSolid(At(x, y, z)) == IsSolid(At(Size.x - 1 - x, y, z))` — both halves have the
  same terrain shape.
- **Color mirror away from forts:** for all `x, y, z` where neither voxel is a team
  color (12/13), `At(x, y, z) == At(Size.x - 1 - x, y, z)`.
- **River water at center:** at least one **water** voxel exists on the center
  columns (`x = 63` and `x = 64`), and no water sits above `WaterLevel`.
- **Trees only on grass:** every **wood** trunk's base voxel rests directly on a
  **grass** (or grass-dark) voxel — never on water, sand, or stone.
- **Forts present and team-colored:** **red** (12) voxels appear only in the
  `x < 64` half and **blue** (13) only in the `x ≥ 64` half, near the fort positions.
- **Snow caps:** no snow below `SnowLine`.

### Rendering verification
Build, run `MapGen` to produce `battlefield.vox`, run the Sandbox, screenshot, and
close via `WM_CLOSE` (project convention). Confirm the frame reads as a symmetric
battlefield: central river, flank mountains with snow, forests, two colored forts.

## Build wiring

- New sources land in the Cubit library globs (`Cubit/src/**.cpp`) and Tests globs
  automatically, but **premake must be regenerated** (`premake5 vs2026`) after adding
  files and the new `MapGen` project.
- Add a `project "MapGen"` (ConsoleApp, C++20, links `Cubit`, includes
  `Cubit/include` and `vendor/GLM`) to `premake5.lua`.
- `battlefield.vox` is generated by running `MapGen` once and is committed. The
  Sandbox's existing `{COPYDIR} assets` postbuild copies it beside the exe, and
  `debugdir` makes the relative load path resolve.
- Document the regenerate command (run `MapGen.exe`) near the asset.

## Out of scope (deferred)

- Multi-model stitching for maps larger than 256³.
- Functional gameplay: real spawns, team logic, objectives (the `.json` sidecar).
- Water transparency / fluid behavior.
- Caves/overhangs (the height-field fill is solid-below-surface).
