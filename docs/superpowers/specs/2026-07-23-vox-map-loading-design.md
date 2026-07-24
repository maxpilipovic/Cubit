# Load a `.vox` Map Into the World (Map Loading, Slice 1)

Load a single MagicaVoxel `.vox` model from disk into the `World`, apply its
palette so blocks render in the file's colours, and have the Sandbox load a map
file instead of building test terrain in code. This is the first slice of the
map-loading arc that follows the completed four-step multi-chunk plan.

Maps are loaded from files rather than generated. A `.vox` model carries both the
voxels and a colour palette, which is why block colour has to stop being a
hardcoded `constexpr` switch and become per-map data.

## Scope

In scope:

- A `.vox` parser for a single model: `SIZE`, `XYZI`, and `RGBA` chunks.
- Blocks become palette indices; the `World` owns the palette; block colour is a
  palette lookup.
- The Sandbox loads `assets/maps/starter.vox` instead of `BuildTestTerrain`.

Out of scope (deferred to later slices):

- Multi-model stitching. `.vox` caps at 256^3 per model; AoS-scale maps need
  several models stitched together. This slice loads exactly one model.
- The companion `.json` sidecar for spawn points and objectives.
- Centring `WorldOffset` and choosing a real spawn/boundary. The Sandbox keeps its
  current offset and spawn constants.

## The palette refactor

`.vox` gives each voxel a palette index (1-255; 0 means empty) plus a 256-entry
RGBA palette that travels with the map. Colour therefore becomes per-world data.

- `Block.h` replaces the `BlockType` enum with `using BlockId = std::uint8_t`,
  where `0` is empty/air. `IsSolid(BlockId)` becomes `id != 0`. The
  `constexpr GetBlockColor` switch is deleted.
- `Block.h` adds `using Palette = std::array<glm::vec3, 256>`.
- `World` owns a `Palette` and exposes `glm::vec3 GetBlockColor(BlockId) const`.
  The mesher already holds a `const World&`, so it calls `world.GetBlockColor(id)`
  in place of the free function.
- `Chunk` stores `std::array<BlockId, BlockCount>` instead of
  `std::array<BlockType, BlockCount>`. Both are one byte, so storage is unchanged.
- The Sandbox's number keys 1-8 select palette indices 1-8, whatever colours the
  loaded map assigns them.

## Components

### Engine (`Cubit`)

**`VoxLoader`** (new, `Cubit/Voxel/`) parses a single-model `.vox`, split so the
byte-walking is testable without touching the filesystem:

- `VoxModel Parse(std::span<const std::uint8_t> bytes)` walks the RIFF chunk tree
  (`MAIN` containing `SIZE`, `XYZI`, `RGBA`) and returns a `VoxModel`.
- `VoxModel LoadFile(const std::string& path)` reads the file into a buffer and
  calls `Parse`.

**`VoxModel`** is plain parsed data, already in Cubit space:

- `glm::ivec3 Size` — dimensions in blocks (Cubit axes).
- `std::vector<std::uint8_t> Voxels` — dense `Size.x * Size.y * Size.z` palette
  indices, `0` for empty.
- `Palette Colors` — 256 colours, index-aligned to `BlockId`.

**`World BuildWorld(const VoxModel&)`** sizes a `World` to `ceil(dim / 16)` chunks
per axis, writes every non-empty voxel with `SetBlock`, and installs the palette.
Parsing and world-construction stay independently testable.

### Sandbox

- Delete `BuildTestTerrain`, `GetTerrainHeight`, and `GetTerrainBlock`.
- Load `assets/maps/starter.vox` through `VoxLoader::LoadFile` + `BuildWorld`,
  replacing the hardcoded `World m_World{ 4, 1, 4 }`.
- Set `debugdir` on the Sandbox project in `premake5.lua` to the Sandbox folder so
  the working directory is stable and `assets/maps/starter.vox` resolves.
- Number keys 1-8 select palette indices 1-8.

The engine loader takes a path and knows nothing about `assets/`. The Sandbox owns
the `assets/maps/` convention and passes the relative path in.

## Data flow

`starter.vox` bytes -> `VoxLoader::LoadFile` -> `VoxModel` (Cubit-space voxels +
palette) -> `BuildWorld` -> `World` (every chunk starts dirty) -> the existing
`WorldRenderer` meshes it, and the mesher reads colour through
`world.GetBlockColor(id)`.

## Axis swap and sizing

`.vox` is Z-up; Cubit is Y-up. The mapping is `cubit(x, y, z) = vox(x, z, y)`: vox
Z (up) becomes Cubit Y (up), and vox Y becomes Cubit Z. The swap is applied once,
inside `Parse`, so every consumer downstream is already in Cubit space.

World dimensions round up to whole chunks. Voxels that fall outside the model's
extent within the last chunk of an axis stay air (`0`).

## Palette detail

`.vox` stores 256 RGBA entries, but voxel colour index `i` maps to RGBA entry
`i - 1` — the format's well-known 1-based off-by-one. `VoxLoader` resolves this so
a `BlockId` stored in the world indexes `Palette` directly. RGBA bytes (0-255)
convert to the `glm::vec3` 0-1 range the mesher expects. A file with no `RGBA`
chunk falls back to MagicaVoxel's default palette.

## Error handling

`LoadFile` and `Parse` throw on a missing or unreadable file, bad magic or an
unsupported version, a model dimension greater than 256, or a truncated or
malformed chunk. This matches the project's existing exception style. The Sandbox
lets the exception propagate: while there is exactly one map, a missing or broken
map is a hard startup failure, which is the behaviour we want.

## Testing

Tests use doctest in the `Tests/` project, one file per subject.

- **`VoxLoaderTests`** — an in-test helper builds a byte buffer for a known model
  (a few coloured voxels plus a palette). Assert the parsed `Size`, the axis swap,
  specific voxel indices, and palette conversion including the off-by-one.
  Malformed-input cases (bad magic, oversize dimension, truncated chunk) assert
  that `Parse` throws.
- **`BuildWorldTests`** — from a hand-built `VoxModel`, assert the world's chunk
  dimensions (rounding up), a few block positions, out-of-extent air, and that
  `GetBlockColor` returns the palette colour for a placed block.
- **`starter.vox`** — the same byte-building helper generates a small designed map
  (a floor plus a few coloured pillars). Its bytes are committed to
  `Sandbox/assets/maps/starter.vox` so the build never depends on running a
  generator.

Rendering is verified by building the Sandbox, running it, screenshotting the
loaded map, and closing it via `WM_CLOSE`.
