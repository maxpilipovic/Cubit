# Where Load Time Actually Goes — P8 Investigation

**Date:** 2026-08-16
**Map:** `Sandbox/assets/maps/battlefield512.vox`, 512x64x512 = 16,777,216 cells
**Method:** temporary `TEST_CASE` probe appended to `Tests/src/SkyLightTests.cpp`,
timing real code paths with `std::chrono::steady_clock`; temporary counters inside
`SkyLight.cpp`'s `Flood`. All instrumentation reverted afterwards.

## Why this was measured

`docs/performance.md` quoted `SkyLight::PropagateAll` at **~5.2 s in a debug build**
and framed P8 as "load floods light and meshes the whole world on one thread — fix is
threading". Both halves turned out to be wrong, and the second one would have sent the
work in the wrong direction.

## The breakdown

| Phase | Debug | Release | Share |
|---|---:|---:|---:|
| parse `.vox` | 3,606 ms | 127 ms | 11% |
| `BuildWorld` | 4,911 ms | 211 ms | 15% |
| **`SkyLight::PropagateAll`** | **19,578 ms** | **1,003 ms** | **59%** |
| — clear every cell | 807 ms | 55 ms | 2% |
| — seed top layer | 34 ms | 3 ms | 0.1% |
| — **the flood** | **18,736 ms** | **945 ms** | **57%** |
| mesh all 4,096 chunks | 4,985 ms | 289 ms | 15% |
| **Total** | **33,079 ms** | **1,631 ms** | |

Debug/Release ratio is 20.3x, matching the ~20x multiplier recorded elsewhere.

A control loop reading all 16.8M cells through `World::GetSkyLight` took 920 ms in
Debug (55 ns a read), which is the figure the flood's cost is built from.

## Three findings that change the plan

**1. The 5.2 s figure was a 256-map measurement.** On the shipped 512 map the flood is
19.6 s, and load as a whole is 33 s in Debug.

**2. Meshing is the smallest of the three costs, not the biggest.** At 4,985 ms it is
15% of load. The "world visibly builds itself for the first half minute" that P1's
notes describe is the deliberate 4 ms per-frame `MeshBudgetMilliseconds` slice
spreading 5 s of work across frames — it is not 30 s of work. The cost that actually
blocks, before a single frame renders, is the flood.

Threading the mesher — P8's stated fix — therefore targets 15% of load.

**3. `parse` + `BuildWorld` is 8.5 s in Debug (26%)**, larger than meshing, and had
never been costed.

## Why the flood is slow

Counters over the 512 map, taken as deltas from the cumulative totals so the small
worlds in earlier test cases do not pollute them:

| | count |
|---|---:|
| dequeues (cell visits) | 11,347,219 |
| neighbour tests | 68,083,158 |
| light writes | 11,085,069 |
| of which full-strength free-fall | 9,854,640 (**88.9%**) |
| peak queue size | 262,143 |

**The BFS is not thrashing.** Visits ~= writes ~= air-cell count, so each cell is
written about once. There is no redundant-revisit bug to find. Neighbour tests are
exactly 6 per visit.

**The cost is per-operation addressing.** Each neighbour test does `IsInBounds` +
`IsBlockOpaque` + `GetSkyLight` — three `World`-addressed calls, each a bounds check
plus three divides and three modulos. That is roughly 200M addressed lookups, which at
the measured 55 ns accounts for the time.

**Nearly nine-tenths of the work is filling open sky columns one queue entry at a
time.** 88.9% of writes are light falling straight down through open air without
dimming — a case a sequential downward scan per column handles with no queue and no
neighbour tests at all.

## What was decided

Rewrite the open-column part of the flood as a downward column scan, seeding the
remaining sideways BFS from the boundary between lit and unlit columns. Design and the
rejected alternatives:
[specs/2026-08-16-sky-light-column-scan-design.md](../specs/2026-08-16-sky-light-column-scan-design.md).

Threading is deliberately *not* the first move. It was the documented plan, and the
measurements do not support it.

## Reproducing this

Append a `TEST_CASE` probe to an existing test file rather than adding a new one — a
new file needs `premake5 vs2026` re-run. The suite runs as a post-build step, so probe
stdout lands in the build log. Revert afterwards.
