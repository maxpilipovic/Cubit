# Transparency, Phase 1: Opacity — Design

**Date:** 2026-07-31
**Status:** Approved (pending spec review)

## Goal

Make water render as water: see-through, with a visible and correctly lit
riverbed beneath it. Water stays **solid** this phase — the player still walks on
its surface, exactly as today.

## Context

The engine has no transparency. `IsSolid(block)` is `block != 0`
(`Cubit/include/Cubit/Voxel/Block.h:15`), a pure function of the block id with no
access to a palette, and nine call sites across the mesher, sky light, collision
and raycast route through it. Water (`MapBlocks::Water`, index 7) is therefore
solid to everything: it renders as an opaque blue slab, blocks sky light, and
hides the riverbed under it.

Listed in [engine-roadmap.md](../../engine-roadmap.md) as the last item before
greedy meshing.

### Why this is Phase 1 of two

Two independent properties are conflated in `IsSolid`:

| | Blocks light, hides faces | Blocks movement |
|---|---|---|
| Stone | opaque | solid |
| Water | no | no |
| Glass (future) | no | solid |
| Air | no | no |

Water needs both flipped, but they are separate axes.

- **Phase 1 (this spec) — opacity.** Rendering and sky light. Water looks right;
  behaviour is unchanged.
- **Phase 2 (later) — solidity.** Collision and raycast, and whether entering
  water means sinking or swimming.

Ordering matters: doing solidity first would leave the player falling through
opaque water, which is worse than the current state. Phase 1 alone is a complete
visual improvement with no behavioural regression.

## The opacity mechanism

`Palette` becomes `std::array<glm::vec4, 256>`. A block is **opaque** when its
palette alpha satisfies `alpha >= 1.0f`, non-opaque below that.

`DefaultPalette()` gives alpha `1.0` to every entry except air (index 0), which
gets alpha `0`. Air therefore satisfies the same rule as everything else rather
than needing a special case.

`World::GetBlockColor` returns `glm::vec4` rather than `glm::vec3`, since the
mesher needs the alpha to write into the vertex. `VoxLoader::Parse` reads the
alpha byte it currently discards (`VoxLoader.cpp:115-118`), and `VoxWriter::Write`
emits the palette's alpha rather than the hardcoded 255 it writes today.

`World` derives a `std::array<bool, 256>` opacity table whenever its palette is
assigned, and exposes:

```cpp
bool World::IsBlockOpaque(int x, int y, int z) const;   // out of bounds -> false
bool World::IsIdOpaque(BlockId block) const;            // table lookup
```

A table, not a float comparison through the palette: the mesher samples opacity
tens of thousands of times per chunk, and [performance.md](../../performance.md)
P7 records what per-sample cost does to a chunk build.

Out of bounds reads as **not opaque**, matching `GetBlock` returning air and
`GetSkyLight` returning open sky.

`IsSolid` is not touched this phase. Collision and raycast keep their current
behaviour.

### Existing maps are unaffected

Verified directly against the shipped asset: every used entry in
`Sandbox/assets/maps/battlefield.vox` carries alpha 255 (index 7, water, is
`55 110 200 255`). Loading it after this change looks identical. Water becomes
transparent only once `TerrainGen` sets its alpha and the map is regenerated.

Any `.vox` whose palette carries alpha below 255 will render those blocks
transparent — the mechanism needs no new file format and no side-car table.

## The meshing rule

For a rendered block `B` with neighbour `N`, emit the face when **`N` is not
opaque and `N` differs from `B`**:

| B | N | Emit | Why |
|---|---|---|---|
| stone | air | yes | unchanged |
| stone | water | **yes** | the riverbed — new geometry |
| water | air | yes | the water surface |
| water | water | no | internal faces would double-blend |
| water | stone | no | hidden behind the stone's own face |
| stone | grass | no | `N` is opaque |

The outer loop still skips air, so *what is rendered* does not change; only *what
hides a face* does.

Ambient occlusion and corner light both switch from solidity to opacity:

- `CornerAo` — water must not cast occlusion into the riverbed.
- `CornerLight` — water cells now hold light, so they belong in the corner
  average rather than being skipped as solid.

`Neighbourhood` caches a `bool` opacity array alongside its block array, filled
during construction, so a sample stays one array read.

## Geometry split

```cpp
struct MeshGeometry
{
    std::vector<VoxelVertex> Vertices;
    std::vector<std::uint32_t> Indices;
};

struct ChunkMeshData
{
    MeshGeometry Opaque;
    MeshGeometry Transparent;
};
```

A face goes to `Transparent` when its own block `B` is non-opaque, otherwise to
`Opaque`.

**Accepted cost:** every existing mesher and AO test refers to `mesh.Vertices`
and `mesh.Indices` and becomes `mesh.Opaque.Vertices` / `mesh.Opaque.Indices`.
The churn spans `Tests/src/ChunkMesherTests.cpp` and `Tests/src/ChunkAoTests.cpp`
and is purely mechanical — those tests build only opaque blocks, so their
assertions hold unchanged against the `Opaque` set.

Rejected alternative: adding `TransparentVertices` / `TransparentIndices` to the
existing struct. No test churn, but it leaves a worse-named type in the codebase
permanently.

## Vertex format

`VoxelVertex::Color` becomes `glm::vec4`, so one shader and one buffer layout
serve both passes.

**Accepted cost:** opaque geometry carries an alpha it never varies — roughly
7.6 MB of additional GPU memory across the map's ~1.9M vertices.

Rejected alternatives: a second shader and vertex layout (more moving parts), or
a per-draw uniform alpha (breaks as soon as a second transparent block type
exists).

## Rendering

`WorldRenderer::ChunkMesh` holds two sets of GPU buffers. A chunk's entry is
dropped only when **both** sets are empty.

`Render` becomes two passes:

1. **Opaque** — depth write on, frustum culled, any order.
2. **Transparent** — frustum culled, collected and sorted **back to front** by
   distance from the camera, drawn with depth *write* off and depth *test* still
   on, so water does not paint over terrain standing in front of it.

Per-chunk sort granularity is deliberate: within one chunk the water surface is a
single flat plane with no self-overlap, so finer sorting would buy nothing.

Two supporting changes:

- `Renderer::SetDepthWrite(bool)` — new. `SetDepthTest` exists; the depth mask
  does not.
- `WorldRenderer::Render` gains a camera-position parameter for the sort key. It
  must be in the same space as the chunk transforms — that is, the value
  `PerspectiveCamera::GetPosition()` returns, which the sandbox already offsets
  by `WorldOffset`.

`DrawnChunkCount` counts chunks submitted across both passes; `TotalFaceCount`
sums both geometry sets.

The sandbox's fragment shader takes `vec4` and emits `color = v_Color` rather
than `vec4(v_Color, 1.0)`.

## Sky light

One predicate, in three places in `Cubit/src/Voxel/SkyLight.cpp`: `IsBlockSolid`
becomes `IsBlockOpaque`. Non-opaque blocks then hold and pass light on exactly
the same rules as air, including free fall at full strength, and the riverbed
ends up as bright as the open ground beside it.

The propagation and removal rules themselves are deliberately untouched.
`SkyLight::Unflood`'s "was this cell lit by that one" test depends on the
attenuation rule, and that is where P6's free-fall bug lived. Per-block light
absorption — deeper water reading darker — is a plausible later refinement and is
explicitly **out of scope** here.

## Content

`TerrainGen::MapPalette` returns `vec4` and gives water alpha `0.55`. Every other
entry gets alpha `1.0`. `MapGen` regenerates `Sandbox/assets/maps/battlefield.vox`
and the regenerated asset is committed.

## Testing

Doctest, per project convention.

**New:**
- Palette alpha survives a round trip through `VoxWriter::Write` and
  `VoxLoader::Parse`.
- A loaded palette with alpha below 255 produces a non-opaque block;
  `World::IsIdOpaque` and `IsBlockOpaque` agree with the palette.
- Out-of-bounds positions read as not opaque.
- The six meshing cases in the table above, each asserted on face counts.
- A transparent block contributes no ambient occlusion to its neighbour.
- A transparent block's cells are included in the corner light average.
- Sky light passes through a non-opaque block and reaches the cell below it.
- Faces of a non-opaque block land in `Transparent`; faces of an opaque block
  land in `Opaque`.

**Mechanical:** existing mesher and AO tests renamed to `mesh.Opaque.*`.

**Not unit tested:** the two-pass draw, the depth-write toggle, and the sort.
Rendering is verified by running the sandbox — build, look at the river, close
via `WM_CLOSE` so the log survives. Success looks like: the water surface is
see-through, the riverbed is visible and lit through it, and terrain standing in
front of the river is not painted over by it.

## Out of scope

- Solidity: collision, raycast, sinking or swimming. That is Phase 2.
- Per-block light absorption (deeper water reading darker).
- Sorting individual faces rather than chunks.
- Any transparent block other than water; glass is a palette entry away once this
  lands, but nothing in this phase adds one.
- Refraction, caustics, animated water surfaces.
