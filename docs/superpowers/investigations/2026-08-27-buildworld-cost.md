# Where `BuildWorld`'s Time Goes — P10 Investigation

**Date:** 2026-08-27
**Map:** `Sandbox/assets/maps/battlefield512.vox`, 512x64x512 = 16,777,216 cells,
5,952,784 of them solid, across 4,096 chunks
**Method:** temporary `TEST_CASE` probe appended to `Tests/src/VoxLoaderTests.cpp`,
timing real code paths with `std::chrono::steady_clock`. Reverted afterwards.
Load-phase figures alongside it come from `CB_PROFILE_SCOPE` (see
[performance.md](../../performance.md) P8/P9).

## Why this was measured

After the file read was fixed (P9), `BuildWorld` was the largest remaining line
item in load at **4,496 ms** in a debug build, and had never been costed at any
finer grain than its own name.

There was a plausible-looking culprit visible in the source — `World::SetBlock`
ends in `MarkChunkDirtyAt`, which runs a 3x3x3 loop and inserts into a
`std::set` — but that was a guess from reading code. This project has been
wrong about exactly that kind of guess before: P8 named "thread the mesher" as
its fix for months, and when the numbers finally arrived, meshing was 15% of load
while the flood was 57%. So the guess was measured rather than acted on.

## The breakdown

| Step | Debug |
|---|---:|
| `World` constructor — allocates 4,096 chunks **and** inserts all 4,096 coordinates into the dirty set | 10.8 ms |
| `SetPalette` | 0.0 ms |
| **The block loop — 5,952,784 `SetBlock` calls** | **4,685.4 ms** |
| `MarkChunkDirtyAt` alone, over the same 5,952,784 cells | **5,071.2 ms** |
| Scanning the model without writing anything (`At` over all 16.8M cells) | 123.1 ms |

## What it says

**Dirty-marking is essentially the whole cost.** `MarkChunkDirtyAt` called on its
own over the same cells costs as much as the entire loop that contains it — the
two figures differ by 8%, which is run-to-run variance, and the second loop ran
second. Everything else `SetBlock` does — a bounds check, three divides, three
modulos, a chunk index, an array write — is free by comparison, and iterating the
model at all is 123 ms of the 4,685.

**And every one of those marks is redundant.** The `World` constructor has already
inserted all 4,096 chunk coordinates into the dirty set before the first block is
written — that is what the 10.8 ms buys. So all 5.95M subsequent inserts find the
key already present and change nothing. `BuildWorld` spends 4.5 seconds
discovering, six million times, that the set it is inserting into is already full.

The cost per call is a 3x3x3 loop with its guard conditions, plus at least one
`std::set<glm::ivec3>::insert` — a red-black tree descent over 4,096 entries,
roughly twelve `IVec3Less` comparisons — for a result that is always "already
there".

## Why the guess was right but worth checking anyway

The predicted culprit was the right one. What the measurement added was the
*shape* of it: the expectation was that dirty-marking would be a large share of
`SetBlock`, and the answer is that it is very nearly all of it, with `SetBlock`'s
own addressing arithmetic — the thing P7 spent effort on for the mesher —
invisible at this scale. That changes the fix from "make marking cheaper" to
"do not mark at all here", which is a different piece of work.

It also priced the alternative that looks attractive from the source: the
constructor's blanket 4,096-entry fill, which reads like the wasteful part, costs
**10.8 ms**. It is not worth touching.

## What this implies for the fix

`BuildWorld` populates a world that is already entirely dirty. It needs a way to
write blocks without marking, and the ~4.5 s should follow. The shape of that
seam — a `SetBlock` overload, a separate bulk-fill entry point, or writing chunk
storage directly — is a design question, not a measurement one, and is
deliberately left to its own cycle.

Expected result if dirty-marking is removed from this path: `BuildWorld` falls
from ~4,500 ms to roughly the 130 ms the scan and the writes actually cost,
taking about **4.5 s off a debug load that stands at ~8.3 s** after P9.

## Reproducing this

Append a `TEST_CASE` probe to an existing test file rather than adding a new one —
a new file needs `premake5 vs2026` re-run. The suite runs as a post-build step, so
probe stdout lands in the build log. Revert afterwards.

Time `MarkChunkDirtyAt` separately rather than trying to instrument inside the hot
loop: it is public, and calling it over the same cells against an already-full
dirty set reproduces exactly the state `SetBlock` sees, without adding per-call
scope overhead that would distort the thing being measured.

## What the fix achieved

Shipped the same day. `World::SetBlockAssumingDirty` writes a block without
marking; `BuildWorld` uses it.

| `BuildWorld` | before | after | |
|---|---:|---:|---|
| Debug | 4,496.4 ms | **427.1 ms** | **10.5x** |
| Release | 219.6 ms | **43.4 ms** | 5.1x |

The prediction above was ~130 ms and the answer is ~427 ms. This probe showed
`SetBlock`'s own work was negligible *relative to marking*, and writing it up
rounded that to negligible outright. Six million of anything is not free — the
writes are ~300 ms of the 427. Direction right, magnitude off by 3x.

Total debug load is now **4,265 ms**, of which `SkyLight::PropagateAll` is
**79%**. Every other phase is under 10%. See [performance.md](../../performance.md)
P10.
