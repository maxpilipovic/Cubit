# Greedy Meshing — Design

**Date:** 2026-08-06
**Status:** Approved (pending spec review)

## Goal

Merge coplanar, identically shaded faces into larger quads, so flat terrain costs
a handful of quads instead of thousands. Nothing about the rendered image
changes.

## Context

`ChunkMesher::Build` walks blocks and emits one quad per exposed face
(`Cubit/src/Voxel/ChunkMesher.cpp`, `AddExposedFaces`). A 16×16 lit floor emits
256 quads — 1024 vertices — where one quad would look identical. The battlefield
map meshes to roughly 480k quads, most of it flat ground and water surface.

Catalogued as **P3** in [performance.md](../../performance.md) and the last
remaining perf item in the [engine roadmap](../../engine-roadmap.md)'s finish-the-
engine arc.

### Why this comes after ambient occlusion and transparency

Both of those changed what a face's shading means, and greedy meshing has to
respect the result. The roadmap deliberately ordered them this way so the merge
criterion could be written against the finished shading model rather than
retrofitted twice.

Faces now carry per-vertex colour — palette colour × face shade × AO ×
sky light, with alpha passed through untouched. Merging two faces into one quad
means the GPU interpolates that colour linearly across the whole merged
rectangle. Two faces with different corner values cannot merge without
misrepresenting at least one of them.

## The merge rule

A face is **uniform** when its four corners agree:

```
ao[0] == ao[1] == ao[2] == ao[3]  &&  light[0] == light[1] == light[2] == light[3]
```

A uniform face collapses to one `glm::vec4` computed once. Two uniform faces
merge iff their **block ids match and their colours match on all four
components**.

Non-uniform faces — the shading gradients beside walls and under overhangs — are
emitted as 1×1 quads exactly as today, keeping their per-corner colours and their
diagonal flip. They are never merged with anything.

This is the conservative rule, and it is chosen deliberately: it is the only rule
under which the merged image is *identical* to the current one rather than merely
close. Open lit terrain — the case that motivated the work — is uniform almost
everywhere, so the reduction lands where it matters.

### On comparing the light floats

`CornerLight` returns `total / (counted * SkyLight::Max)`. Four corners with
identical integer inputs evaluate the same expression to bitwise-identical
results, so equality is meaningful here rather than incidental.

The failure mode is one-sided by construction. If two mathematically equal floats
ever compared unequal, the cost is a missed merge — never a wrong pixel. There is
no comparison whose failure produces incorrect shading.

Block id is part of the key even though equal colours would suffice visually. It
costs one integer compare and keeps the rule debuggable.

### Rejected alternatives

**Edge-matching strips** — merge along one axis while the shared edge's corners
match, letting gradients run. Absorbs more faces, but the interior of a merged
run is then linearly interpolated where the truth was piecewise, so a gradient
across three faces reads wrong in the middle. Rejected: it trades a guaranteed-
identical image for a modest extra reduction.

**Merging on block id alone**, resampling shading at the merged corners. The
largest reduction by far, and it discards the per-vertex AO shipped 2026-07-26 —
wall/floor junctions would read flat. Rejected outright.

**Strips only (merge along U, never V)** — simpler by roughly 25 lines, but a
16×16 lit floor goes to 16 quads rather than 1, leaving most of the win on the
table for exactly the flat terrain that dominates the map.

## Algorithm

`Build` inverts its loop. Instead of walking blocks and asking which faces are
exposed, it walks face planes:

```
for each of the 6 face directions d:
    for each slice s along d's normal (0..15):
        fill a 16x16 mask over d's two tangent axes
        consume the mask into maximal rectangles
        emit one quad per rectangle
```

A mask cell holds one of three states:

| State | Meaning |
|---|---|
| `None` | no face here |
| `Alone` | a face exists, but its shading varies across its corners |
| key | a face exists and is uniform: `BlockId` + `glm::vec4` colour |

`Alone` cells emit immediately through today's `AddFace` path. Only key cells
enter the rectangle pass, which is the standard greedy sweep: find an unconsumed
cell, extend along U while keys match, extend along V while entire rows match,
emit, mark consumed.

One 256-entry mask array is allocated per `Build` and reused across all 96
slices.

### What does not change

Face *existence* and face *shading* keep exactly the rules they have now.
`IsOpaque(neighbour) || Block(neighbour) == self` still decides whether a cell
has a face at all, and `CornerAo` / `CornerLight` still produce the shading.
Greedy meshing decides only how the resulting faces are grouped into quads, so it
can merge faces but never invent or suppress one.

Three consequences fall out rather than needing design:

- **The transparency split stays correct.** Merging requires equal block ids, and
  opacity is a function of block id, so a merged quad can never straddle the
  opaque and transparent passes.
- **The same-block rule survives.** Two water cells still share no face; the mask
  simply has no entry there.
- **The diagonal flip resolves itself.** A uniform quad satisfies
  `ao[0] + ao[2] == ao[1] + ao[3]`, so `flip` is false for every merged quad. The
  existing flip logic keeps applying to the 1×1 faces that stay unmerged — which
  are precisely the ones with a gradient across the quad.

### Merged quad geometry

Reuses the existing `FaceGeometry` table rather than hand-writing six cases. For
a rectangle spanning `w` along U and `h` along V, corner *i* sits at:

```
Corner[i] + U * (CornerU[i] > 0 ? w - 1 : 0)
          + V * (CornerV[i] > 0 ? h - 1 : 0)
```

The unit corner, pushed out by the extra extent on whichever sides face +U and
+V. `CornerU` / `CornerV` already encode each vertex's sign along the tangent
axes — the same data that lets a corner's two occluding neighbours be found
without a per-face switch.

All four vertices of a merged quad carry the same colour, alpha included.

## Structure

`Cubit/src/Voxel/ChunkMesher.cpp` is 443 lines and gains roughly 120 net. The
`Neighbourhood` cache (lines 24–106) moves to a new engine-internal header
`Cubit/src/Voxel/Neighbourhood.h`. It is self-contained, has a genuinely
separable job, and the move leaves `ChunkMesher.cpp` near its current size rather
than pushing past 550.

`ChunkMesher.h`'s public surface does not change. `Build` still returns one
`ChunkMeshData`; `CornerAoLevel`, `CornerLightShade`, `AoShade` and `LightFloor`
are all untouched.

## Testing

Doctest, per project convention.

### The oracle changes from count to area

`CountExposedFaces` (`Tests/src/ChunkMesherTests.cpp`) stays as the independent
oracle, but the identity it feeds changes. Quad count no longer equals exposed-
face count; **covered area** still does, under any merge rule:

```cpp
CHECK(CoveredArea(mesh) == CountExposedFaces(world, 0, 0, 0));
```

`CoveredArea` sums, per quad, the product of its two non-zero bounding-box
extents. 26 references across `ChunkMesherTests.cpp` derive or assert quad counts
today and are rewritten against this invariant.

`RequireWellFormed` is unchanged: 4 vertices and 6 indices per quad, every index
in range.

### Targeted merge tests

The area invariant alone would pass a mesher that merged nothing, so it is paired
with cases whose answers are hand-checkable:

| Case | Expectation |
|---|---|
| Lone block | 6 quads, area 6 |
| Filled 16³ chunk | 6 quads, area 1536 |
| Fully lit floor slab, top plane | 1 quad |
| Floor beside a wall | gradient strip stays 1×1, so >1 quad |
| Two touching transparent blocks | still no shared face |
| Mixed opaque and transparent blocks | no quad spans both passes |

### Existing AO tests

They keep working. `QuadsInPlane` reads quads as groups of four consecutive
vertices, which merging preserves, and the gradient faces those tests probe are
exactly the ones that stay 1×1.

## Measurement

Per [performance.md](../../performance.md) discipline, no win is claimed without
a Debug measurement. A temporary probe `TEST_CASE` against
`Sandbox/assets/maps/battlefield.vox` records quads, vertices, and whole-map mesh
time before and after. The real numbers go into P3; the probe is removed before
the final commit.

**Accepted trade-off:** the mask sweep may make per-chunk meshing *slower* in
Debug even as it cuts vertices, because Debug cost tracks total function-call
count (three cost-per-call optimisations measured slower there on 2026-07-28).
That trade is accepted: meshing happens once per chunk against a 4 ms per-frame
budget, while drawing happens every frame with no budget at all. The existing
`CHECK(ms < 50.0)` bound stays as a guard against an order-of-magnitude
regression.

Rendering is verified by running the sandbox: the map must look pixel-identical,
with the HUD quad count sharply down.

## HUD

`HudLayer.h:187` prints `FACES` from `WorldRenderer::TotalFaceCount`, which is
`indices / 6` — quads, not faces, once this lands. Relabelled `QUADS`.

## Out of scope

- Strip-merging faces with shading gradients.
- GPU buffer reuse (P4), draw-call batching (P5), threading the mesher — all
  separately catalogued.
- Merging across chunk boundaries. Chunks mesh independently by design, and that
  is what makes a single-chunk remesh cheap after an edit.
- Any change to how AO or sky light are computed.
