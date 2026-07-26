# Ambient Occlusion + Sky Lighting — Design

**Date:** 2026-07-26
**Status:** Approved (pending spec review)

## Goal

Replace Cubit's fixed per-face shading with real lighting: per-vertex ambient
occlusion so crevices and contact points darken, and a propagated sky-light grid so
enclosed spaces genuinely go dark. This is step 2 of the "finish the engine" arc in
[engine-roadmap.md](../../engine-roadmap.md) — the single biggest step up in how real
the world looks.

## Context

Today `ChunkMesher` shades each face with one of six constants (`TopShade` = 1.00
through `BottomShade` = 0.60) and bakes the result into the vertex colour. The
fragment shader in `Sandbox.cpp` just passes `v_Color` through. The consequence is
that every top face in the map is *exactly* the same brightness: the floor of a
trench, the ground under an overhang, and an open field are indistinguishable. Nothing
tells the eye that terrain has depth, and interiors are as bright as open sky.

Two decisions from brainstorming shape everything below:

1. **Both AO and sky lighting in one slice.** They share the per-vertex neighbourhood
   lookup — the same four cells touching a face corner supply both the occlusion count
   and the light average. Splitting them would mean writing that sampling code twice.
2. **Box repropagation** for edit-time relighting, rather than the Minecraft-style
   incremental add/remove flood. The removal pass is the subtlest code in a classic
   voxel lighting system and the usual source of bugs; a bounded from-scratch reflood
   is provably correct given light's finite travel distance, and cheap enough at this
   map size.

Shading stays **baked CPU-side into the vertex colour**. `VoxelVertex` keeps its
`{ vec3 Position, vec3 Color }` layout, the vertex/fragment shaders are untouched, and
this feature adds no GL code at all. The cost is that AO strength and the light curve
cannot be retuned without remeshing — which costs nothing in practice, since
relighting already forces a remesh.

## Architecture

Five pieces across three existing files and one new one. Everything is pure CPU and
doctest-able; only the final visual result needs the sandbox.

### 1. `Chunk` — light storage

A second array parallel to `m_Blocks`:

```cpp
std::array<std::uint8_t, BlockCount> m_SkyLight;  // 0..15
```

- One byte per block, not a packed nibble: 4 MB for the full 256×64×256 map, and the
  index math reuses `Chunk::GetIndex` unchanged.
- `GetSkyLight(x, y, z)` / `SetSkyLight(x, y, z, value)` in chunk-local coordinates.
- Out-of-bounds reads return **15** (full daylight), mirroring how `GetBlock` treats
  out-of-bounds as air.
- Constructor initialises to 0; an unpropagated world is dark, not accidentally lit.

### 2. `World` — light access in world coordinates

`GetSkyLight(x, y, z)` / `SetSkyLight(x, y, z, value)`, delegating through the chunk
grid exactly as `GetBlock`/`SetBlock` already do. Out-of-world reads return 15, which
makes the sky above the map a light source for free and stops edge columns going
artificially dark.

Light is stored in `Chunk` rather than as one flat buffer on `World` because every
other voxel system in this codebase addresses blocks through `World` → `Chunk`;
a separate flat array would mean two addressing conventions for the same coordinates.

### 3. `SkyLight` (new — `Cubit/…/Voxel/SkyLight.{h,cpp}`)

Flood-fill propagation. Static methods only, matching `ChunkMesher`'s shape. No GL, no
renderer knowledge.

```cpp
class CB_API SkyLight
{
public:
    SkyLight() = delete;

    //Floods sky light through the whole world. Called once after a map loads.
    static void PropagateAll(World& world);

    //Re-floods the bounded region an edit at this position can affect, marking
    //every chunk whose light changed dirty.
    static void Repropagate(World& world, int x, int y, int z);
};
```

**Propagation rule.** Breadth-first from the sky. Solid blocks hold light 0 and do not
propagate. A step **downward into air preserves the value** (sky light falls without
attenuation); every other direction costs 1. A cell is enqueued when a neighbour can
raise its value.

**`PropagateAll`** seeds every air cell in the world's top layer to 15 and floods.
Called once at map load, ~4.19M cells — a load-time cost, not a per-frame one.

**`Repropagate`** clears light inside a box of **±15 blocks horizontally and the full
column height**, clamped to world bounds, then refloods it seeded from two sources: the
sky above, and the ring of cells immediately *outside* the box, which keep their
existing values and act as boundary conditions.

The box bounds are exact, not a heuristic. Light attenuates 1 per horizontal step from
a maximum of 15, so no cell more than 15 blocks away horizontally can be affected by
an edit. Vertically, sky light falls without attenuation, so a single broken block can
change an entire column below it — hence full height rather than a vertical radius.
That is ~31 × 64 × 31 ≈ 61k cells per edit, on the order of a millisecond.

Chunks whose light values actually changed are marked dirty via the existing
`World` dirty-tracking, so the amortized mesher picks them up.

### 4. `ChunkMesher` — per-vertex sampling

Where AO and lighting merge into one lookup. For each of a face's 4 corners, inspect
the 3 cells diagonally surrounding that corner on the **air** side of the face —
`side1`, `side2`, `corner`:

- **AO level** = `(side1 && side2) ? 0 : 3 - (side1 + side2 + corner)`, giving 0–3.
  The `side1 && side2` special case is what stops two walls meeting at a right angle
  from reading wrong.
- **Smooth light** = mean sky light of the air cells among
  `{faceAdjacent, side1, side2, corner}`, skipping solid ones. Averaging the same four
  cells is what makes light gradients smooth across a surface rather than stepping
  per block.

Final vertex colour:

```
color = blockColor * FaceShade * AoTable[aoLevel] * LightCurve(avgLight)
```

- `FaceShade` — the existing `TopShade`/`RightShade`/… constants, **kept**. They are
  what keeps a cube readable when it is uniformly lit.
- `AoTable` — 4 constants, starting at `{ 0.55, 0.70, 0.85, 1.00 }`.
- `LightCurve` — maps light 0–15 onto `[0.15, 1.0]`. The 0.15 floor means an unlit
  bunker reads as dark but stays navigable, rather than becoming a black void.

### 5. Triangulation flip

`AddFaceIndices` currently hardcodes `0,1,2 / 2,3,0`. With per-corner AO, a quad whose
opposite corners disagree develops a visible seam along the diagonal — the standard AO
anisotropy artifact. When `ao[0] + ao[2] > ao[1] + ao[3]`, emit `1,2,3 / 3,0,1`
instead.

Vertex and index *counts* are unchanged — only the winding differs — so face counts,
buffer sizing, and the existing mesher tests are unaffected.

## Data flow

**At map load:** `VoxLoader` → `World` → `SkyLight::PropagateAll` → first
`WorldRenderer::Update`. Propagation runs synchronously in `Sandbox`'s `BuildWorld`,
before any meshing, so the amortized mesher never sees an unlit world.

**On a block edit:** the *caller* sequences `World::SetBlock` then
`SkyLight::Repropagate` — `World` does not call `SkyLight` itself. Keeping the
dependency pointing one way (`SkyLight` → `World`, never the reverse) is what lets
`World` stay a storage-and-dirty-tracking type and lets the flood-fill be tested
against a plain `World` with no lighting hooks in it.

Both calls mark chunks dirty through the existing tracking, and the current
`m_Pending` budget remeshes them over the following frames. No renderer changes are
needed.

## Testing

All doctest, all pure CPU — no GPU required.

**`SkyLight`:**
- An open column reads 15 from top to bottom.
- A column with a roof goes dark below the roof.
- Horizontal spread falls off exactly 1 per step.
- **Equivalence:** after an edit, `Repropagate` produces the same grid as a full
  `PropagateAll` from scratch. This is the test that proves the bounded box is sound,
  and is the most important one in the feature.

**AO:**
- An isolated face has all 4 corners at maximum.
- Adding one diagonal neighbour darkens exactly one corner.
- The two-sides-solid case pins that corner to 0.

**Mesher:**
- Existing tests keep passing, with vertex and index counts unchanged.

**Rendering** is verified the usual way — build, run the sandbox, screenshot at ~7s
(Discord's overlay covers the HUD before then), close via `WM_CLOSE` so the stdout
buffer flushes.

## Implementation phases

The plan splits in two so there is something visible and verifiable partway through:

1. **Phase 1 — AO.** Per-vertex occlusion, the AO table, the triangulation flip.
   Shippable and observable on its own: trenches, craters and overhangs gain depth.
2. **Phase 2 — Sky lighting.** Light storage, `SkyLight` propagation, box
   repropagation on edits, and folding the light average into the vertex colour.

## Out of scope

- **Emissive / block light sources.** Nothing in the battlefield map glows, and a
  second light channel doubles propagation work.
- **Day/night cycle.** Sky light is a fixed 15.
- **Greedy meshing.** Deliberately sequenced *after* this work: per-vertex AO means
  two coplanar faces can only merge when their AO and light values match, so the
  greedy pass should be written AO-aware from the start rather than retrofitted. See
  the ordering note in [engine-roadmap.md](../../engine-roadmap.md).
- **Threaded propagation.** `PropagateAll` is a one-shot load cost; `Repropagate` is
  bounded to ~1 ms. Neither justifies a worker thread yet.
