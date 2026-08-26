# Scope Profiler — Design

**Date:** 2026-08-25
**Status:** Designed

## Goal

Make the engine able to measure itself, so a performance figure comes from a tool
that is still there next time rather than from timing code written by hand and
deleted afterwards.

One capability: name a scope, get how long it took, written as a file a trace
viewer can draw.

## Why

Every number in [performance.md](../../performance.md) was produced the same way.
The P8 investigation says so outright:

> *Method: temporary `TEST_CASE` probe appended to `Tests/src/SkyLightTests.cpp`,
> timing real code paths with `std::chrono::steady_clock`; temporary counters
> inside `SkyLight.cpp`'s `Flood`. All instrumentation reverted afterwards.*

That has happened three times — P6/P7, greedy meshing, P8 — and the roadmap already
records that it is about to happen a fourth, because `parse` + `BuildWorld` at 49%
of Debug load is the next target and has never been costed at a finer grain than
those two names.

Rebuilding the rig each time costs more than the code. The numbers stop being
comparable between investigations, because each rig measured slightly different
boundaries; and a phase only gets a number if someone thought to wrap it, which is
how `parse` + `BuildWorld` went unmeasured through three investigations that were
each looking for the biggest cost.

There is a second reason, specific to what comes next. Threading is the largest
remaining structural item, and a threaded phase cannot be understood from a column
of totals — the question is what overlapped what. That is a picture, not a number.

## Context

- Three build configurations exist already: `CB_DEBUG`, `CB_RELEASE`, `CB_DIST`
  (`premake5.lua`). `performance.md` reports a **Debug and a Release column for
  every measurement**, so both must stay measurable; `Dist` is the shipping config
  and is the only one that should pay nothing.
- `Cubit` is a `SharedLib`. `Sandbox`, `MapGen`, and `Tests` all link it and
  include only `Cubit/include`. Public types are exported with `CB_API`.
- `DebugDraw` (`Cubit/include/Cubit/Renderer/DebugDraw.h`) established the pattern
  for "callable from anywhere without holding a reference": static methods over
  state owned inside the DLL.
- `FrameClock` and `Application::Run` already use `std::chrono::steady_clock`.
- There is no `<thread>`, `<mutex>`, or `<atomic>` anywhere in the engine today.
  This design introduces the first two.
- Tests that touch the filesystem use `std::filesystem::temp_directory_path()`
  and remove the file afterwards (`Tests/src/WorldSaveTests.cpp:165-170`).
- `ChunkMesherTests:323` is already intermittently flaky under load, which is the
  reason §Testing forbids wall-clock threshold assertions.

## Options considered

### A — Static `Profiler`, state owned by the DLL — **CHOSEN**

Mirrors `DebugDraw`. A macro can be dropped into `VoxLoader.cpp` with no change to
any signature.

### B — An injected profiler instance — rejected

More orthodox and trivially testable, but every call site needs a reference passed
to it, which would mean changing `BuildWorld`'s signature to measure `BuildWorld`.
Avoiding exactly that is the point of the macro.

### C — Header-only — rejected, and worth recording why

Tempting, because it needs no export plumbing. It is wrong here. `Cubit` is a
`SharedLib` linked by three executables, so header-owned session state would give
the DLL and the exe **separate copies**: a scope inside `VoxLoader.cpp` and a scope
inside `Sandbox.cpp` would append to different buffers, and the load trace would
silently be missing whichever half you were not looking at. State lives in
`Profiler.cpp` behind `CB_API`.

## Part 1 — The types

`Cubit/include/Cubit/Profiler.h`.

```cpp
//One timed scope, as it will be written out. Name is a string literal owned by
//the caller's translation unit, so recording allocates nothing.
struct ProfileResult
{
    const char* Name = nullptr;
    std::int64_t StartMicroseconds = 0;     // relative to session start
    std::int64_t DurationMicroseconds = 0;
    std::uint32_t ThreadId = 0;             // small dense id, not an OS id
};

class CB_API Profiler
{
public:
    //Starts recording. Scopes entered before this are dropped rather than
    //buffered, so a capture contains what it says it contains.
    static void BeginSession(const char* name, const std::string& outputPath);

    //Stops recording, merges every thread's buffer, writes the trace file, and
    //returns the merged results sorted by thread then start time.
    //
    //Returns the data as well as writing it so tests can assert on values
    //rather than scraping JSON, which is what keeps a JSON *parser* out of
    //this project.
    static std::vector<ProfileResult> EndSession();

private:
    friend class ProfileTimer;
    static void Record(const char* name,
        std::chrono::steady_clock::time_point start,
        std::chrono::steady_clock::time_point end);
};

//Times the scope it is declared in. Constructed at entry, destroyed at exit.
class CB_API ProfileTimer
{
public:
    explicit ProfileTimer(const char* name);
    ~ProfileTimer();

    ProfileTimer(const ProfileTimer&) = delete;
    ProfileTimer& operator=(const ProfileTimer&) = delete;

private:
    const char* m_Name;
    std::chrono::steady_clock::time_point m_Start;
};
```

The macros:

```cpp
#ifdef CB_DIST
    #define CB_PROFILE_SCOPE(name)
    #define CB_PROFILE_FUNCTION()
#else
    #define CB_PROFILE_SCOPE(name) \
        ProfileTimer CB_PROFILE_NAME(cbProfileTimer, __LINE__)(name)
    #define CB_PROFILE_FUNCTION() CB_PROFILE_SCOPE(__FUNCSIG__)
#endif
```

Two-level concatenation (`CB_PROFILE_NAME`/`CB_PROFILE_JOIN`) so `__LINE__`
expands before it is pasted. `__FUNCSIG__` is MSVC; the project is
`CB_PLATFORM_WINDOWS`-only, so it is used directly rather than wrapped in a
portability shim that has no second case to serve.

In `CB_DIST` the macros expand to nothing and `BeginSession`/`EndSession` are
empty — `EndSession` returns an empty vector. The API still exists, so the
Sandbox's call sites compile in every configuration.

## Part 2 — Thread-local buffers

The part carrying the complexity, and the reason the design is not three lines
around a `std::vector`.

Each thread appends to its own buffer with no lock, so recording never contends
and never distorts the timings it is taking. A mutex on the hot path would make
the profiler's own contention part of the measurement under exactly the parallel
workload it exists to measure.

```cpp
//Owned by one thread. Registers itself the first time that thread records, and
//flushes on the way out.
struct ThreadBuffer
{
    ThreadBuffer();   // takes the mutex once, appends itself to the registry
    ~ThreadBuffer();  // takes the mutex once, moves Results into the merged store

    std::vector<ProfileResult> Results;
    std::uint32_t ThreadId = 0;
};

thread_local ThreadBuffer t_Buffer;
```

Three things this gets right that the obvious version does not:

**The mutex is taken twice per thread per session, not twice per scope.**
Registration happens on the thread's first record and nowhere else.

**The destructor flushes rather than merely unregistering.** A worker that exits
before `EndSession` would otherwise take its samples with it, and the samples from
a short-lived worker are the ones a threading investigation most wants.

**Thread ids are a dense counter assigned at registration, not a hash of
`std::thread::id`.** The main thread registers first and is 0, workers are 1, 2,
3. The trace viewer draws one lane per id, and small integers make that readable;
a hashed OS id produces lanes labelled with ten-digit numbers in arbitrary order.

Recording checks an `std::atomic<bool>` before touching the buffer:

```cpp
void Profiler::Record(const char* name, time_point start, time_point end)
{
    if (!s_Active.load(std::memory_order_relaxed))
        return;   // before t_Buffer is odr-used, so no session means no registration

    t_Buffer.Results.push_back(/* ... */);
}
```

`EndSession` takes the mutex, drains every registered buffer plus the merged store,
sorts by `(ThreadId, StartMicroseconds)`, writes, and clears each buffer's results
while leaving it registered — so a second session on the same threads costs no
re-registration and starts empty.

**`BeginSession` while a session is active** ends the previous one first, writing
its file, and logs `CB_CORE_WARN`. Nothing is lost, and the warning names the
mistake. It does not throw: following `ApplyBlockEdit`'s precedent, a misuse that
has an honest answer gets the answer rather than an exception.

## Part 3 — Output

Chrome trace JSON, which both `chrome://tracing` and `ui.perfetto.dev` open:

```json
{"otherData":{},"traceEvents":[
{"cat":"function","dur":4911000,"name":"BuildWorld","ph":"X","pid":0,"tid":0,"ts":3606000}
]}
```

Microseconds throughout, timestamps relative to session start.

**Nesting needs no representation.** Complete events (`"ph":"X"`) on one `tid` nest
by containment, so the viewer derives the flame graph from `ts` and `dur` alone.
That is why results are sorted before writing rather than emitted in completion
order — a destructor fires innermost-first, so completion order is the reverse of
what reads naturally, and some viewers require ascending `ts`.

Names are escaped for `"` and `\` on the way out. `__FUNCSIG__` will not normally
contain either, but it is unsanitised compiler output being pasted into JSON, and
one template argument containing a quote would produce a file that silently fails
to open.

## Part 4 — Call sites

Six scopes, chosen to reproduce exactly the table `performance.md` P8 publishes, so
the trace is directly comparable against the figures already recorded there:

| Scope | File |
|---|---|
| `VoxLoader::LoadFile` | `Cubit/src/Voxel/VoxLoader.cpp` |
| `VoxLoader::Parse` | `Cubit/src/Voxel/VoxLoader.cpp` |
| `BuildWorld` | `Cubit/src/Voxel/VoxLoader.cpp` |
| `SkyLight::PropagateAll` | `Cubit/src/Voxel/SkyLight.cpp` |
| `ChunkMesher::Build` | `Cubit/src/Voxel/ChunkMesher.cpp` |
| `WorldRenderer::Update` | `Cubit/src/Renderer/WorldRenderer.cpp` |

`LoadFile` and `Parse` are separate deliberately: P8 reports them as one row named
"parse", and whether the cost is reading the bytes or interpreting them is the
first thing the next investigation needs to know.

The Sandbox wraps its constructor's `LoadWorld(MapPath)` in a session writing
`profile-load.json` beside the executable.

## Testing

`Tests/src/ProfilerTests.cpp`. No GPU and no window, like the rest of
`Cubit/src/Voxel/`.

1. **One scope yields one result with that name.**
2. **Nested scopes yield two, with the inner contained in the outer** — the inner's
   `ts` at or after the outer's, and its end at or before the outer's end. This is
   the assertion that proves nesting works, since nesting has no representation of
   its own to check.
3. **Two threads yield results with distinct `ThreadId`s, both present after the
   merge** — the assertion that proves Part 2. A variant where the worker joins
   before `EndSession` proves the destructor flush specifically, since that is the
   path where the buffer is gone by the time the merge runs.
4. **A scope entered with no session active is dropped** — proves the `s_Active`
   gate, and that a stray macro in engine code costs nothing when nobody is
   measuring.
5. **A session with no scopes writes a valid empty trace** rather than a truncated
   or malformed file.

Files go to `std::filesystem::temp_directory_path()` and are removed afterwards,
matching `WorldSaveTests.cpp:165-170`.

**No assertion of the form "this took at least N milliseconds."** `ChunkMesherTests:323`
is already flaky under load and wall-clock thresholds are how a suite acquires more
of that. Duration is asserted `> 0` and by containment only.

Adding a new test file means re-running `premake5 vs2026`, since the file globs in
`premake5.lua` expand at generation time.

## Documentation to update

- `docs/engine-roadmap.md` — move **Profiling instrumentation** out of "Let the
  game pull these", since it is no longer waiting on a game to ask for it.
- `docs/performance.md` — note that figures from here on are reproducible with
  `CB_PROFILE_SCOPE` rather than an ad-hoc rig.
- `README.md` — a line under the engine's capabilities.

## Out of scope

- **Counters.** `CB_PROFILE_COUNT` was considered and deliberately deferred. The
  P8 lesson argues for them — timing said "the flood is slow", counters said
  "88.9% of writes are free-fall down open columns", and the second is what
  produced the fix. But whether timing alone localises the 8.5 s is exactly what
  the first trace will show, and the follow-up design gets to be written knowing.
- **Sampling.** No stack walking, no symbols, no Windows API.
- **A HUD widget.** The HUD already reports a smoothed frame rate; this is for
  offline analysis of load and edits.
- **Always-on telemetry.** `Dist` compiles it out entirely.
- **Fixing the load cost.** This design measures. What it indicts gets its own
  design, deliberately written after the measurement — P8 spent months naming a
  fix ("thread the mesher") for a cost nobody had measured, and the measurement
  contradicted both the size and the location of the problem.
