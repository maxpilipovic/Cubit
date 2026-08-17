# Sky-Light Column Scan — Design

**Date:** 2026-08-16
**Status:** Approved (pending spec review)

## Goal

Cut the cost of `SkyLight::PropagateAll`, which is **57% of map load time** and the
only part of load that blocks before a single frame renders.

Measurements: [load-cost breakdown](../investigations/2026-08-16-load-cost-breakdown.md).
The short version: on `battlefield512.vox` the flood is 18,736 ms in Debug (945 ms
Release), it visits each air cell about once so it is not thrashing, and **88.9% of
its writes are full-strength light falling straight down open columns** — one BFS
queue entry at a time, six neighbour tests each.

## Options considered

### A — Column scan + boundary seeding — **CHOSEN**

One downward pass per column fills the open run with `Max` and the rest with `0`,
recording each column's lowest lit y. The BFS is then seeded only where a lit column
borders an unlit one, derived from those per-column heights.

Removes the ~9.85M queued free-fall writes *and* the ~59M neighbour tests that went
with them. Seed count scales with the terrain's **vertical relief** — summed height
differences between adjacent columns — instead of with air volume. It also absorbs the
existing 807 ms clear loop, because every cell is written exactly once.

### B — Column scan, then enqueue every `Max` cell — rejected

Same scan, but seed the BFS with all lit cells rather than just boundary ones. Saves
the 9.85M writes but keeps ~59M neighbour tests, since every `Max` cell is still
dequeued and tested six ways. Nearly all the implementation cost of A for a fraction
of the benefit. The seeding rule is the point, not the scan.

### C — Flat-index the existing BFS (the P7 treatment) — rejected *for now*

Keep the algorithm; replace `World`'s div/mod addressing with precomputed flat offsets
(±1, ±W, ±WH), exactly as `ChunkMesher` was fixed under P7. Expect roughly 4x on the
flood — 18.7 s to about 5 s — because it makes each of the 200M lookups cheaper rather
than removing them.

**Rejected as the first move, not rejected outright.** A removes ~89% of the
operations; C makes the remaining ~11% cheaper. Doing C first would optimise work that
A deletes. **C composes with A and stays on the table** — re-measure after A and decide
on evidence. Do not re-derive this from scratch.

## The algorithm

All of it lives inside `Cubit/src/Voxel/SkyLight.cpp`. **No public API change.**
`Repropagate`, `Unflood`, and `Flood` itself are untouched — only what
`PropagateAll` does before calling `Flood` changes.

### Pass 1 — the scan

Per column `(x, z)`, walking down from the top:

```
y = height - 1
while y >= 0 and not opaque(x, y, z):   SetSkyLight(x, y, z, Max);  --y
skyBottom[col] = y + 1        // lowest lit y; == height when nothing is lit
while y >= 0:                 SetSkyLight(x, y, z, 0);  --y
```

Every cell is written exactly once, so **the separate clear loop is deleted**, not
merely made cheaper. `skyBottom` is a `std::vector<int>` of `width * depth`.

A column that is opaque at the very top gets nothing, and `skyBottom` is `height` — an
empty lit range, which the seeding rule below reads correctly with no special case.

### Pass 2 — the seed set

```
deepest = max(skyBottom[c]) over the 4 in-bounds horizontal neighbours
for y in [skyBottom[col], deepest):   enqueue(x, y, z)
```

Taking the max over neighbours rather than looping each one gives the union of the
four ranges without enqueueing a cell up to four times — every range starts at
`skyBottom[col]`, so they nest. Out-of-bounds neighbours are excluded from the max:
they contribute no darkness, matching `Flood`'s existing `IsInBounds` skip.

Then `Flood(world, queue, nullptr)` runs exactly as it does today.

## Why this is exactly equivalent, not approximately

Three properties, each falling out of the scan rather than needing to be enforced:

1. **After the scan, the `Max` cells are precisely the sky-connected cells.** So the
   boundary seeds are precisely the `Max` cells that can raise a neighbour. An interior
   `Max` cell has every neighbour `Max` or opaque and can raise nothing, which is why
   dropping it from the queue changes no output.
2. **The free-fall branch can never fire again.** The cell below any `Max` cell is
   `Max` or opaque by construction, so `d == DownIndex && level == Max` either hits
   opaque or is rejected by the `>= value` check. The scan has already done every
   free-fall write. What remains is ordinary dimming flood-fill.
3. **A slightly over-inclusive seed set is harmless.** `skyBottom` says a neighbour
   column is *dark* at some y, which may mean opaque rather than merely unlit.
   Enqueueing for an opaque neighbour costs one skipped test in `Flood` and changes
   nothing.

Fully enclosed caves stay dark exactly as now: nothing lit borders them, so nothing
seeds them.

## Testing

**A naive reference oracle** in `Tests/src/`, effectively today's implementation copied
verbatim — seed the top layer, flood through `World` accessors, no cleverness. The fast
path is asserted to match it **cell for cell**. The duplication is deliberate and
isolated to the test suite.

Comparisons report the **first differing cell's position, not a bool**. A whole-world
equality check that fails with "not equal" is undiagnosable at this scale; the stitched
512 map test already learned this.

**Permanent cases** — hand-built worlds, each targeting one rule:

- flat floor
- an overhang casting a sideways shadow
- a fully enclosed cave (stays dark)
- a column opaque at the very top (empty lit range)
- a world of pure air (everything `Max`)
- a staircase-relief world, which specifically stresses the seed y-ranges
- one `TerrainGen`-generated map at a smaller size, for realistic structure — hills,
  river, forest, forts

**One-off, during implementation:** the full `battlefield512` cell-for-cell comparison
against the oracle, run once and the result recorded, then removed. Keeping it would
cost ~19 s of oracle time on every build, roughly doubling the suite, and **structure
is what breaks this rewrite, not scale** — a 512 map is the same shapes as a 128 map
with more of them.

The existing `SkyLight` cases stay untouched and must keep passing. They encode the
lighting rules, including the named "falls without dimming" case.

**Re-measure** Debug and Release after implementation with the same probe technique,
and record before/after.

## Documentation to update

- `docs/performance.md` — P8 gets a real section rather than a summary row, carrying
  the breakdown and the finding that threading the mesher targets 15% of load and is
  not the recommended first move. Its stale ~5.2 s figure is corrected.
- `docs/engine-roadmap.md` — P8's one-line description.
- The investigation write-up already records how the numbers were taken.

## Out of scope

- **Threading anything.** The measurements do not support it as the next move.
- **Option C, flat indexing** — deliberately deferred, see above.
- **`parse` + `BuildWorld`**, 26% of Debug load and never costed before. Real, and a
  separate piece of work.
- **`Repropagate`.** The edit path is already 0.03 ms and is not touched.
