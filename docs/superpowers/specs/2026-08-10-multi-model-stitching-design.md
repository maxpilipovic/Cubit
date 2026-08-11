# Multi-Model Stitching — Design

**Date:** 2026-08-10
**Status:** Approved (pending spec review)

## Goal

Load and save maps larger than a single `.vox` model can address, so Cubit can
ship a true Ace-of-Spades-scale 512×64×512 battlefield instead of the current
256×64×256 one.

## Context

A `.vox` model stores each voxel coordinate in one byte, so 256 is a hard cap per
axis. Larger scenes are expressed as **several models placed by a scene graph**
(`nTRN` transform, `nGRP` group, `nSHP` shape nodes). Cubit understands none of
that today:

- `VoxLoader::Parse` (`Cubit/src/Voxel/VoxLoader.cpp`) walks `MAIN`'s children but
  only reads `SIZE`, `XYZI` and `RGBA`. On a multi-model file it appends **every**
  model's voxels into one list and keeps only the **last** `SIZE` — silently
  wrong rather than an error.
- `VoxWriter::Write` and `ToVoxModel` both call `RequireWritableSize`, which
  throws above 256. `VoxWriter.h` says so in as many words: splitting such a world
  "is multi-model stitching, which does not exist yet."
- `MapGen/src/MapGen.cpp` hardcodes `glm::ivec3(256, 64, 256)`.

This is item 5 — the last — in the [engine roadmap](../../engine-roadmap.md)'s
finish-the-engine arc.

## Approach: the scene graph never escapes the loader and writer

The multi-model structure is a **file format detail**, not a fact about Cubit's
world. `Parse` flattens however many models a file contains into one dense
`VoxModel`; `Write` splits a dense `VoxModel` into as many models as it needs.

Everything downstream is unchanged: `BuildWorld`, `ToVoxModel`, `TerrainGen`,
`MapGen` and the Sandbox keep their current signatures. `MaxDimension = 256`
stops being a limit on the **world** and becomes a limit on a single **model**.

Two alternatives were considered and rejected:

- **A `VoxScene` type** carried through the API (`LoadScene`, `BuildWorld(const
  VoxScene&)`, `ToVoxScene`). More faithful to the format and preserves the
  author's tiling, but it adds a type and touches every caller to buy fidelity
  nothing in Cubit reads. `TerrainGen` would still return one model that gets
  split immediately anyway.
- **Bypassing the intermediate** (`LoadWorld`/`SaveWorld` straight to `World`).
  Lowest memory, but it destroys the pure-data seam the current tests rely on —
  `Parse(Write(m)) == m` — and `TerrainGen` produces a `VoxModel` regardless.

Cost of the chosen approach: a 16.7 MB dense intermediate alongside the 33.5 MB
`World` while loading (irrelevant), and model identity is not preserved across a
round trip — a map authored as five overlapping models comes back as our 256
grid. The blocks are identical; nothing reads model identity.

## Read path

`Parse` collects three things while walking `MAIN`'s children:

- **Models, in file order.** The Nth `SIZE`/`XYZI` pair *is* model index N — that
  is what `nSHP` references.
- **Scene nodes** (`nTRN`, `nGRP`, `nSHP`) by node id, fields unresolved.
- **`RGBA`**, unchanged: one palette per file, shared by every model.

It then walks the node graph from the root, accumulating translations to give each
model an origin. Four rules, each a place where the wrong choice produces a
misplaced map rather than an error:

1. **`_t` is the model's centre, not its min corner.**
   `origin = translation - size/2`, floor division. The writer encodes
   `translation = origin + size/2` under the same convention, making the pair an
   exact inverse for even and odd sizes alike.
2. **`_t` is in vox axes**, so it converts like voxels do:
   `cubit(x, y, z) = vox(x, z, y)`. Converting the voxels and forgetting the
   translation is the easy mistake.
3. **Rotation is rejected, not ignored.** The `_r` byte packs a signed permutation
   matrix: bits 0–1 and 2–3 select which column is non-zero in rows 0 and 1, bits
   4–6 are sign bits, so identity encodes as `4`. A missing `_r` means identity and
   is the common case. Any other value throws — ignoring a rotation misplaces
   geometry silently, the worst available outcome. *The implementation verifies
   that constant against a MagicaVoxel-authored file before relying on it.*
4. **Extent is normalised.** Translations may be negative. The world is the union
   of placed model AABBs, and every origin shifts by `-unionMin` so the world's
   corner is `(0, 0, 0)`.

Models blit into one dense `VoxModel` sized to the union. Where models overlap,
the one resolved later wins, where "later" means later in **graph traversal
order** — not file order, which can differ. Overlap is not expected in files we
write; the rule exists so a hand-authored file has one defined answer.

**Compatibility.** A file with no scene graph at all — which is what Cubit's own
writer emits today, and what `starter.vox` and `battlefield.vox` are — keeps
working as one model at the origin.

**Guard.** `MaxDimension = 256` still applies per model. A new
`MaxWorldDimension` rejects an absurd union extent, so a malformed file throws
instead of attempting a multi-gigabyte allocation.

## Write path

`RequireWritableSize` moves off the world and onto the tile. `ToVoxModel` drops
its size check and produces a dense model of whatever the world is; `Write`
decides how to store it.

- **All dimensions ≤ 256** — emit exactly today's bytes: `SIZE`, `XYZI`, `RGBA`,
  no scene graph. Byte-identical output keeps existing tests valid and leaves
  `starter.vox` unchanged in shape.
- **Any dimension > 256** — tile on a uniform 256 grid, `ceil(size / 256)` per
  axis. Each tile becomes its own `SIZE`+`XYZI` pair. **All-air tiles are skipped
  entirely**, so an empty corner costs nothing and no degenerate model is emitted.
  Then one scene graph places the survivors.

The graph is the minimal shape MagicaVoxel accepts: a root `nTRN` at identity → an
`nGRP` listing every tile → per tile an `nTRN` carrying `_t` → an `nSHP`
referencing that tile's model index. Layer id `-1`, one frame per transform.

**The property that must hold:** `Parse(Write(m)) == m` for a model larger than
256, exactly as it already holds below 256. Tiles never overlap and the
translation encode/decode pair is an exact inverse, so the flatten reproduces the
dense buffer — including the odd-sized remainder tiles at the far edge, which is
where a floor-division mistake would surface.

## The 512 map

`MapGen` takes an optional `--size W H D` in **Cubit axes** (H is the Y-up
height, matching `TerrainConfig::Size`), defaulting to `256 64 256` so
regenerating the existing battlefield stays a no-op. The new map is
`512 64 512`; height 64 fits one tile, so it writes as a **2×1×2 grid of four
tiles**, three 256-wide and the far edge exact. The Sandbox loads it.

`TerrainGen` needs no changes. It scales structurally with `config.Size` — the
fold-based symmetry, the ridge profile and the fort mirror all derive from it —
and its noise frequencies are in absolute block units, so a 512 map gets twice as
much terrain at the same feature scale. That is what "AoS scale" should mean.

**Recorded, deliberately not fixed here:** the river stays 6–12 blocks wide, which
is correct — rivers do not scale with maps. But the forts stay `FortEdgeOffset = 8`
from the edge at `FortRadius = 5`, so on a 512 map they are two small specks 496
apart. That is gameplay tuning, not stitching. Follow-up.

## Testing

New cases in the existing doctest suite:

- **Placement** — decode `_t` for even and odd model sizes. The floor-division
  convention is where an off-by-one hides.
- **Multi-model flatten** — a **hand-authored byte fixture** of two small models at
  different offsets, asserting blocks land at the right world coordinates. Hand
  authored specifically so the read tests are not merely agreeing with our own
  writer.
- **Negative translations** normalise so the world corner is `(0, 0, 0)`.
- **Non-identity `_r` throws.** **Union extent past `MaxWorldDimension` throws.**
  **A single model past 256 still throws.**
- **Round trip across the boundary** — a 260×64×260 world (cheap, but crosses 256
  and exercises remainder tiles) through `ToVoxModel → Write → Parse`, asserting
  block-for-block equality. Plus the real 512 once.
- **All-air tiles are skipped** — assert the emitted model count, not just that the
  file loads.
- **Existing single-model output stays byte-identical** — pinned, since that is
  what keeps `starter.vox` and the current tests valid.

Errors throw `std::runtime_error` with the existing `"vox: "` prefix. The
Sandbox's load path needs no change: it already assigns only after `LoadFile`
returns, so a bad file leaves the old world intact.

## Verification

Doctest covers the logic. The real check is running the Sandbox on the 512 map:
screenshot it, walk to a tile seam to confirm there is no wall or gap at x=256 and
z=256, then `F5`-save and reload. That seam is what unit tests are least likely to
catch and the most likely thing to be wrong.

## Out of scope

**Load time.** A 512×64×512 world is 4096 chunks. Extrapolating from
[performance.md](../../performance.md), a Debug build faces roughly 21 s of
blocking `SkyLight::PropagateAll` and ~27 s of wall-clock meshing at the 4 ms
per-frame slice. Real numbers get **measured** as the last step of this project and
written into `performance.md` to motivate a separate threading project — the
extrapolation above is not to be trusted as a result.

Also out of scope: `nTRN` rotation support, preserving authored model identity, and
the fort-scale tuning noted above.
