# Block-edit FPS drop — investigation

_2026-07-28. Fixed in `7487e39` (relighting) and the mesher/budget commit that
follows it._

The short version lives in [performance.md](../../performance.md) as **P6** and
**P7**. This is the long version: what the symptom was, how each cause was found,
what the numbers were, and — the part worth keeping — the three optimisations that
turned out to be **slower** and why.

---

## The symptom

> "It is dropping fps every time a block is broken or placed."

Placing or breaking a block stalled the frame that handled the click. Everything
else ran fine: walking around, looking about, the 145 FPS the HUD reported with
`DRAWN 232/602`. Only edits hurt.

Both causes were introduced by the ambient-occlusion + sky-light work
(2026-07-26). Neither was a bug in what that work computed — the light values and
the shading were correct throughout. Both were about what it *cost*.

## How it was measured

No profiler. A temporary probe appended to an existing test file, timing the real
code paths against the real `battlefield.vox`, run through the normal build (the
test suite runs as a post-build step, so its stdout comes out in the build log).
Reverted afterwards.

Worth repeating that trick: it needs no tooling, it runs on the shipped map rather
than a synthetic one, and because the probe is a `TEST_CASE` it has the whole
engine already linked.

---

## Issue 1 — relighting cost the size of the box, not the size of the change

`SkyLight::Repropagate` had one strategy for every edit: blank a region and flood
it again from scratch. The region was fixed — radius `Max` (15) in X and Z,
because light cannot travel further than that sideways, and the **full world
height**, because full-strength light falls without dimming.

That reasoning is sound. The box genuinely cannot miss anything. The problem is it
is the box of everything an edit *might* affect, and it was paid whether or not
the edit affected any of it.

On the 256×64×256 battlefield that is 31 × 64 × 31 = **61,504 cells**, every
click. The probe:

```
[PROBE] box = 31 x 64 x 31 = 61504 cells (radius 15, full height)
[PROBE] Repropagate 161.0 ms to relight 1 of 61504 cells (0.0016%)
[PROBE] dirty chunks: 4 of 1024
```

**161 ms to change one cell.** Phase breakdown:

```
snapshot   8.05 ms
clear      2.91 ms
seed       3.69 ms
FLOOD    141.71 ms   <- 88%
compare    3.52 ms
```

The bookkeeping scans were nearly free. The re-flood was everything — seeding
~7,200 border cells to rebuild light that was already correct before the clear.
Shrinking the box or skipping the snapshot would have bought almost nothing; the
strategy itself was the cost.

### The fix

Work outward from the edit, stop where the light stops moving.

- **Breaking** a block — light only ever *increases*, so there is nothing to
  clear. Seed a flood from the neighbours that already have light. It stops after
  a few cells because everything else is already at least as bright.
- **Placing** a block — light decreases, which is harder: you have to know which
  lit cells were lit *by* the cell you just blocked. `Repropagate` runs after
  `SetBlock`, and `SetBlock` does not touch light, so the now-solid cell still
  holds the brightness it had while open. That is exactly the light just cut off.
  Walk outward: a neighbour *dimmer* than the current cell was lit by it, so clear
  it and keep walking; a neighbour as bright or brighter has its own source, so
  leave it and add it to a re-add list. Then flood from that list.

A `LightRecorder` remembers each touched cell's original value and marks dirty
only where the final value differs — preserving the "blanked and refilled to the
same value costs no remeshing" guarantee the old before/after compare provided.

### The subtle part

The textbook removal pass chases *dimmer* neighbours. That is **wrong** for sky
light, because of the free-fall rule: light at full strength falls without
dimming, so the cell below a free-falling one holds the *same* level, not a lower
one. A removal that only looks for dimmer neighbours skips straight over it and
leaves a lit column hanging under a block you just placed.

`Unflood` special-cases it: going downward, at `Max`, an equally-bright neighbour
also counts as lit-by-this-cell. Pinned by the test
`"Placing a block darkens the whole column it shades"`.

**Result: 173 ms → 0.03 ms** (debug) for a broken surface block.

---

## Issue 2 — meshing resolved every AO and light sample through `World`

With relighting fixed, the remaining 26 ms stall was entirely remeshing. AO had
made a chunk build far more expensive: each corner sample read through `World`,
which converts a world position into a chunk and an offset — bounds check, three
divisions, three modulos — and a face does roughly 40 of those (4 corners × ~10),
against **1** for the fixed per-face shading it replaced.

Measured split, rather than assumed:

```
[PROBE] chunk has 653 faces
[PROBE] full Build            6.73 ms
[PROBE] world lookups only    5.69 ms  (85% of Build)
[PROBE] bare 4096-cell walk   0.44 ms
[PROBE] => vector building    1.03 ms
```

**85% world lookups.** So: hoist them.

### The fix

`Build` copies the chunk plus its one-block shell (18³) into a flat
`Neighbourhood` once, then addresses cells by **flat index** — a sample becomes an
integer add and an array read. Face normals and tangent axes are converted to flat
offsets once per mesh, so a corner is two integer additions away.

The AO and light rules stayed a **single** implementation: they are templates over
the cell source, so the cached path and the public
`CornerAoLevel`/`CornerLightShade` (which the tests drive against a bare `World`)
share one copy of the rule instead of drifting apart as two copies of something
subtle.

**Result: 6.4 ms → 1.6 ms** per chunk (debug).

---

## What did NOT work

Three attempts measured slower. All three are the same lesson.

### 1. The cache, addressed by coordinate — 98 → **134 ms** (worse)

The first version of `Neighbourhood` took a `glm::ivec3` and converted it to a
flat offset internally: subtract the origin, assert in range, index a
`std::array`. That is a glm subtraction, a `CB_CORE_ASSERT`, and a bounds-checked
`std::array::operator[]` — **none of which inline in a debug build**. Each cache
read cost about what the `World` lookup it replaced cost, and the unconditional
5,832-cell fill was added on top.

A cache only pays if the read becomes genuinely cheaper. Addressing by flat index
(no glm, no assert, raw arrays) is what turned it into a win: **98 → 30 ms**.

### 2. Sharing each face's 3×3 tangent plane — 30.3 → **32.7 ms** (worse)

A face's four corners sample only 9 distinct cells but read ~40 times. Gathering
those 9 once and combining per corner ought to be a clear ~2× reduction.

It was not, because it reduced the *cost per read* while leaving the *number of
calls* alone: 18 gather calls **plus** the same ~40 per-corner calls, now into a
smaller array. In a debug build nothing inlines, so total call count is what
matters. A win in an optimised build; not in the one being run.

### 3. Pre-counting faces to `reserve()` both vectors — 30.3 → **31.3 ms** (worse)

With sampling fixed, vector growth was over half the remaining per-chunk cost, so
sizing the vectors once looked obvious. But the counting pass is itself ~16,000
non-inlined calls, which cost more than the reallocations it saved.

### The lesson

**In a debug build, total function-call count dominates; cost-per-call
optimisations do not show up.** The one change that worked (flat indices) worked
because it removed genuinely expensive *work* — divisions, modulos, bounds
checks — not because it shuffled where the work happened.

This matters here specifically because **Cubit is developed and run in Debug**
(only `bin/Debug-windows-x86_64` existed). An optimisation that only helps Release
does not fix the problem actually being experienced.

---

## Debug vs Release

Worth knowing the multiplier, since it shapes which fixes are worth making:

| | Debug | Release |
|---|---|---|
| `Repropagate`, before | 173 ms | 8.2 ms |
| chunk build, before | 6.37 ms | 0.37 ms |
| chunk build, after | 1.64 ms | 0.30 ms |
| `PropagateAll` at load | 5.2 s | 254 ms |

Roughly 20×. Release alone would have masked issue 1 into a ~9 ms hiccup — real,
but not the thing being complained about. Both problems were genuine algorithmic
faults; Debug just made them impossible to ignore.

---

## The per-frame budget

`MeshBudgetPerFrame = 16` was a fixed **count**, chosen when a chunk build was
cheap. A count cannot hold this job: chunks differ several-fold in geometry, and
the same count costs wildly different amounts in debug and optimised builds, so
any count is a stall in one of them or needless dawdling in the other.

It is now a time slice, `MeshBudgetMilliseconds = 4.0`, checked *after* each build
so at least one chunk always completes and the pending set always drains.

**The trade:** at ~1.6 ms a chunk in debug that is ~2 chunks a frame instead of
16, so the initial 1024-chunk mesh spreads over ~8 s of smooth frames rather than
~1 s of stuttering ones. It is a one-line constant. Threading is the real fix for
load time and is still deferred.

---

## Where we are now

A break on open ground, 256×64×256 battlefield, debug build:

| | at the start | after issue 1 | after both |
|---|---|---|---|
| relight | 173.3 ms | 0.03 ms | 0.03 ms |
| remesh 4 chunks | 26.1 ms | 26.1 ms | 7.06 ms |
| **worst case in one frame** | **199 ms (≈5 fps)** | 26 ms | **~4 ms** (capped by the slice) |

Optimised build: 0.93 ms of work per click.

### Tests added

- `"Relighting an edit costs what the edit changed, not what it might have"` —
  failed at **18,098 ms** for 100 single-cell edits, now passes with ~80×
  headroom.
- `"Meshing a chunk costs little more than reading its blocks"` — failed at
  **98 ms** for 16 chunks, now ~31 ms.
- `"Repropagation over open ground matches a full propagation"` (4 subcases) and
  `"Placing a block darkens the whole column it shades"` — the free-fall removal
  rule.
- `"Occlusion samples blocks across a chunk boundary"` and `"Corner light samples
  cells across a chunk boundary"` — the exact case a neighbourhood cache gets
  wrong by an off-by-one. Written *before* the cache existed, so they passed
  against the old `World`-reading code and then guarded the change.

**A note on the perf thresholds.** They are deliberately loose, and set clear of
the run-to-run spread rather than against it (the mesher one measures 30–32 ms and
the bound is 50 ms). They exist to catch a return to the old *order of magnitude*,
not to hold a stopwatch to the current number on whatever machine runs them.

### Still open

- `SkyLight::PropagateAll` floods the whole world once at load: **~5.2 s debug**,
  254 ms optimised. Same flood, one-off, not yet touched.
- **P3 greedy meshing** — still the biggest remaining mesher win, and now the
  natural next one. The AO interaction still applies: two coplanar faces can only
  merge when their AO *and* light values match.
- **P4 GPU buffer reuse** — every remesh still allocates fresh
  `VertexArray`/`VertexBuffer`/`IndexBuffer` and frees the old ones. Minor at 4
  chunks an edit; matters once meshing is threaded.
- **Threading** the mesher, which is what would fix load time.
