# Fixed Timestep and Render Interpolation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Advance the simulation in fixed 60 Hz steps independent of the frame rate, and interpolate the rendered camera between the last two simulated states.

**Architecture:** A new `FrameClock` value type converts one wall-clock frame delta into a whole number of fixed steps plus a leftover fraction (alpha). `Application::Run` runs the steps with the tick loop outer and the layer loop inner, then renders once with alpha. `Layer::OnUpdate` is removed and replaced by `OnFixedUpdate` (simulation) and `OnFrameUpdate` (per-frame), so every existing override must state which clock it wants. `SandboxLayer` keeps a previous player position and lerps the camera in the render path.

**Tech Stack:** C++20, premake5 (vs2026), doctest, GLM, OpenGL/GLFW.

**Spec:** `docs/superpowers/specs/2026-08-22-fixed-timestep-design.md`

## Global Constraints

- **Build (Tests):** `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64` — use FORWARD slashes in the `.vcxproj` path or MSBuild fails with MSB1009.
- **Build (Sandbox):** same command with `Sandbox/Sandbox.vcxproj`.
- The test suite runs as a post-build step, so its stdout appears in the build log. A failing test breaks the build.
- Run a single case: `bin/Debug-windows-x86_64/Tests/Tests.exe -tc="<case name>"`.
- **Adding any new source file requires re-running `premake5 vs2026` from the repo root.** Do NOT run `GenerateProjects.bat` — it deletes `bin/` and ends in a blocking `pause`. The `files` globs (`Cubit/src/**.cpp`, `Tests/src/**.cpp`) are expanded at generation time, so a new file is invisible until premake reruns.
- Every `.cpp` under `Cubit/src/` must begin with `#include "cub.h"` — it is the precompiled header (`pchheader "cub.h"`).
- Public engine classes carry `CB_API` (from `Cubit/Core.h`).
- Comment style in this codebase: `//` with no space before the text for doc comments on declarations; `// ` with a space for explanatory prose inside function bodies. Match the surrounding file.
- Commit after each task. Push to `master` directly — no feature branches. **Never add Claude co-author trailers or attribution.**

---

### Task 1: `FrameClock`

The pure arithmetic, under test, before anything else changes.

**Files:**
- Create: `Cubit/include/Cubit/FrameClock.h`
- Create: `Cubit/src/FrameClock.cpp`
- Test: `Tests/src/FrameClockTests.cpp`

**Interfaces:**
- Consumes: `Timestep` from `Cubit/Timestep.h`, `CB_API` from `Cubit/Core.h`.
- Produces: `class FrameClock` with `int Advance(double frameSeconds)`, `float Alpha() const`, `static constexpr Timestep Step()`, `static constexpr double FixedStepSeconds`, `static constexpr int MaxTicksPerFrame`.

- [ ] **Step 1: Create the header**

Create `Cubit/include/Cubit/FrameClock.h`:

```cpp
#pragma once

#include "Cubit/Core.h"
#include "Cubit/Timestep.h"

//Turns a variable wall-clock frame delta into a whole number of fixed
//simulation steps plus the fraction left over, so the simulation advances at a
//rate that does not depend on how long a frame took to draw.
class CB_API FrameClock
{
public:
    //Seconds each fixed simulation step advances the world by.
    static constexpr double FixedStepSeconds = 1.0 / 60.0;

    //Most steps one frame may run. Past this the surplus is discarded rather
    //than repaid, so a long stall skips wall-clock time instead of draining in
    //slow motion across the frames that follow.
    static constexpr int MaxTicksPerFrame = 5;

    //Absorbs one frame's wall-clock delta and returns how many fixed steps to
    //run before rendering.
    int Advance(double frameSeconds);

    //How far the accumulator sits between the last completed step and the next,
    //in [0, 1). Rendering interpolates by this.
    float Alpha() const;

    //The duration every fixed step advances the simulation by.
    static constexpr Timestep Step() { return Timestep(FixedStepSeconds); }

private:
    double m_Accumulator = 0.0;
};
```

- [ ] **Step 2: Create the test file with the failing tests**

Create `Tests/src/FrameClockTests.cpp`:

```cpp
#include <doctest.h>

#include "Cubit/FrameClock.h"

namespace
{
    constexpr double Step = FrameClock::FixedStepSeconds;
}

TEST_CASE("A frame shorter than a step runs nothing and banks the remainder")
{
    FrameClock clock;

    CHECK(clock.Advance(Step * 0.5) == 0);
    CHECK(clock.Alpha() == doctest::Approx(0.5f));
}

TEST_CASE("Exactly one step runs one step and leaves nothing over")
{
    FrameClock clock;

    CHECK(clock.Advance(Step) == 1);
    CHECK(clock.Alpha() == doctest::Approx(0.0f));
}

TEST_CASE("One and a half steps runs one step and keeps the half")
{
    FrameClock clock;

    CHECK(clock.Advance(Step * 1.5) == 1);
    CHECK(clock.Alpha() == doctest::Approx(0.5f));
}

TEST_CASE("A zero delta owes nothing")
{
    FrameClock clock;

    CHECK(clock.Advance(0.0) == 0);
    CHECK(clock.Alpha() == doctest::Approx(0.0f));
}

TEST_CASE("Short frames accumulate into a step on the frame that crosses")
{
    //The fencepost this unit exists for: two frames of 0.4 owe nothing, and the
    //third crosses the boundary rather than the second.
    FrameClock clock;

    CHECK(clock.Advance(Step * 0.4) == 0);
    CHECK(clock.Advance(Step * 0.4) == 0);
    CHECK(clock.Advance(Step * 0.4) == 1);
    CHECK(clock.Alpha() == doctest::Approx(0.2f));
}

TEST_CASE("A long stall runs at most the cap and discards the surplus")
{
    FrameClock clock;

    CHECK(clock.Advance(3.0) == FrameClock::MaxTicksPerFrame);

    //Whole steps still owed are dropped, so nothing is left to interpolate by.
    CHECK(clock.Alpha() == doctest::Approx(0.0f));
}

TEST_CASE("The frame after a stall carries no debt")
{
    FrameClock clock;
    clock.Advance(3.0);

    CHECK(clock.Advance(Step) == 1);
    CHECK(clock.Alpha() == doctest::Approx(0.0f));
}

TEST_CASE("A negative delta is ignored rather than subtracted")
{
    //A clock that ran backwards would hand back time the simulation already ran.
    FrameClock clock;
    clock.Advance(Step * 0.75);

    CHECK(clock.Advance(-1.0) == 0);
    CHECK(clock.Alpha() == doctest::Approx(0.75f));
}

TEST_CASE("A thousand awkward frames do not drift")
{
    FrameClock clock;

    int total = 0;
    for (int frame = 0; frame < 1000; ++frame)
        total += clock.Advance(0.0073);

    //7.3 seconds at 60 Hz is 438 steps. Allow one either side for the accumulated
    //floating-point error this test exists to bound.
    CHECK(total >= 437);
    CHECK(total <= 439);
}
```

- [ ] **Step 3: Regenerate the projects so both new files are compiled**

Run from the repo root: `premake5 vs2026`

Expected: premake prints the generated projects and exits 0. Do NOT run `GenerateProjects.bat`.

- [ ] **Step 4: Build and confirm the tests fail**

Run: `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64`

Expected: FAIL — a linker error, `unresolved external symbol` for `FrameClock::Advance` and `FrameClock::Alpha`. The header declares them and nothing defines them yet. This is the red state.

- [ ] **Step 5: Write the implementation**

Create `Cubit/src/FrameClock.cpp`:

```cpp
#include "cub.h"

#include "Cubit/FrameClock.h"

int FrameClock::Advance(double frameSeconds)
{
    // A backwards or absent delta owes nothing. Subtracting it would hand back
    // time the simulation has already run.
    if (frameSeconds > 0.0)
        m_Accumulator += frameSeconds;

    int ticks = 0;
    while (m_Accumulator >= FixedStepSeconds && ticks < MaxTicksPerFrame)
    {
        m_Accumulator -= FixedStepSeconds;
        ++ticks;
    }

    // The cap stopped the loop with whole steps still owed. Drop them: repaying
    // them over the following frames would turn one stall into a longer, slower
    // one. Tested against the remainder rather than against the tick count, so a
    // frame that happens to owe exactly the cap keeps its legitimate fraction.
    if (m_Accumulator >= FixedStepSeconds)
        m_Accumulator = 0.0;

    return ticks;
}

float FrameClock::Alpha() const
{
    return static_cast<float>(m_Accumulator / FixedStepSeconds);
}
```

- [ ] **Step 6: Build and confirm the tests pass**

Run: `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64`

Expected: build succeeds, and the post-build suite reports all cases passing with the total risen by 9.

- [ ] **Step 7: Commit**

```bash
git add Cubit/include/Cubit/FrameClock.h Cubit/src/FrameClock.cpp Tests/src/FrameClockTests.cpp
git commit -m "Add a fixed-step frame clock"
```

---

### Task 2: Split the layer update interface and rewire the loop

**This task is atomic by necessity.** Removing `Layer::OnUpdate` breaks every override at once, so the interface change, the `LayerStack` fan-out, the `Application::Run` loop, and both Sandbox layers must land in one commit or nothing compiles.

Behaviour here is a straight rename plus the new loop. The interpolation alpha is threaded through but deliberately unused — Task 3 consumes it.

**Files:**
- Modify: `Cubit/include/Cubit/Layer/Layer.h:19-22`
- Modify: `Cubit/include/Cubit/Layer/LayerStack.h:31-35`
- Modify: `Cubit/src/Layer/LayerStack.cpp:43-53`
- Modify: `Cubit/include/Cubit/Application.h:44-47`
- Modify: `Cubit/src/Application.cpp` — `Run` (lines ~63-92) and the default virtuals (lines ~136-143)
- Modify: `Sandbox/src/Sandbox.cpp:169` and `:239`
- Modify: `Sandbox/src/HudLayer.h:104` and `:118`
- Test: `Tests/src/LayerStackTests.cpp` (create)

**Interfaces:**
- Consumes: `FrameClock` from Task 1 (`Advance`, `Alpha`, `Step`).
- Produces: `Layer::OnFixedUpdate(Timestep)`, `Layer::OnFrameUpdate(Timestep)`, `Layer::OnRender(float alpha)`; the same three on `LayerStack`; the same three as protected virtuals on `Application`. Task 3 overrides `SandboxLayer::OnRender(float alpha)`; Task 4 overrides `SandboxLayer::OnFrameUpdate`.

- [ ] **Step 1: Write the failing LayerStack test**

Create `Tests/src/LayerStackTests.cpp`:

```cpp
#include <doctest.h>

#include "Cubit/FrameClock.h"
#include "Cubit/Layer/LayerStack.h"

#include <memory>

namespace
{
    //Counts what the stack forwarded, so a fan-out can be asserted without a
    //window or a GL context.
    class RecordingLayer final : public Layer
    {
    public:
        int FixedUpdates = 0;
        int FrameUpdates = 0;
        float LastAlpha = -1.0f;

        void OnFixedUpdate(Timestep step) override
        {
            (void)step;
            ++FixedUpdates;
        }

        void OnFrameUpdate(Timestep delta) override
        {
            (void)delta;
            ++FrameUpdates;
        }

        void OnRender(float alpha) override { LastAlpha = alpha; }
    };
}

TEST_CASE("Every layer receives every fixed step and one frame update")
{
    LayerStack stack;

    auto layer = std::make_unique<RecordingLayer>();
    auto overlay = std::make_unique<RecordingLayer>();
    RecordingLayer* layerPointer = layer.get();
    RecordingLayer* overlayPointer = overlay.get();

    stack.PushLayer(std::move(layer));
    stack.PushOverlay(std::move(overlay));

    stack.OnFixedUpdate(FrameClock::Step());
    stack.OnFixedUpdate(FrameClock::Step());
    stack.OnFrameUpdate(Timestep(0.033));

    //Two steps and one frame update reach both, not one each or two frames.
    CHECK(layerPointer->FixedUpdates == 2);
    CHECK(overlayPointer->FixedUpdates == 2);
    CHECK(layerPointer->FrameUpdates == 1);
    CHECK(overlayPointer->FrameUpdates == 1);
}

TEST_CASE("The interpolation alpha reaches every layer")
{
    LayerStack stack;

    auto layer = std::make_unique<RecordingLayer>();
    RecordingLayer* layerPointer = layer.get();
    stack.PushLayer(std::move(layer));

    stack.OnRender(0.25f);

    CHECK(layerPointer->LastAlpha == doctest::Approx(0.25f));
}
```

- [ ] **Step 2: Regenerate and build to confirm it fails**

Run: `premake5 vs2026` then the Tests build command.

Expected: FAIL to compile — `'OnFixedUpdate': is not a member of 'Layer'`, and `RecordingLayer` cannot override methods that do not exist. This is the red state.

- [ ] **Step 3: Split the `Layer` interface**

In `Cubit/include/Cubit/Layer/Layer.h`, replace the `OnUpdate` and `OnRender` declarations:

```cpp
    //Advances the layer's simulation by one fixed step. Called zero or more
    //times per frame, always with the same duration, so simulation does not
    //depend on how long a frame took to draw.
    virtual void OnFixedUpdate(Timestep step) { (void)step; }

    //Updates the layer once per frame with the real elapsed time. For work that
    //is about the frame itself rather than about the world.
    virtual void OnFrameUpdate(Timestep delta) { (void)delta; }

    //Renders the layer once per frame after updates finish. `alpha` is how far
    //the frame falls between the last completed fixed step and the next, for
    //interpolating a rendered position.
    virtual void OnRender(float alpha) { (void)alpha; }
```

There is deliberately no `OnUpdate`. Removing it rather than redefining it turns every existing override into a compile error that has to pick a clock, which is the entire point of the change.

- [ ] **Step 4: Split the `LayerStack` fan-out**

In `Cubit/include/Cubit/Layer/LayerStack.h`, replace the `OnUpdate` and `OnRender` declarations:

```cpp
    //Advances regular layers and overlays by one fixed step, in forward order.
    void OnFixedUpdate(Timestep step);

    //Updates regular layers and overlays once for the frame, in forward order.
    void OnFrameUpdate(Timestep delta);

    //Renders regular layers and overlays in forward order.
    void OnRender(float alpha);
```

In `Cubit/src/Layer/LayerStack.cpp`, replace `LayerStack::OnUpdate` and `LayerStack::OnRender`:

```cpp
void LayerStack::OnFixedUpdate(Timestep step)
{
    for (const auto& layer : m_Data->Layers)
        layer->OnFixedUpdate(step);
}

void LayerStack::OnFrameUpdate(Timestep delta)
{
    for (const auto& layer : m_Data->Layers)
        layer->OnFrameUpdate(delta);
}

void LayerStack::OnRender(float alpha)
{
    for (const auto& layer : m_Data->Layers)
        layer->OnRender(alpha);
}
```

- [ ] **Step 5: Split the `Application` virtuals**

In `Cubit/include/Cubit/Application.h`, replace the two protected declarations:

```cpp
    //Advances client simulation by one fixed step.
    virtual void OnFixedUpdate(Timestep step);

    //Updates client behavior once per frame with the real elapsed time.
    virtual void OnFrameUpdate(Timestep delta);

    //Renders client behavior for one frame, interpolated by `alpha`.
    virtual void OnRender(float alpha);
```

In `Cubit/src/Application.cpp`, replace the `Application::OnUpdate` and `Application::OnRender` definitions:

```cpp
void Application::OnFixedUpdate(Timestep step)
{
    (void)step;
}

void Application::OnFrameUpdate(Timestep delta)
{
    (void)delta;
}

void Application::OnRender(float alpha)
{
    (void)alpha;
}
```

- [ ] **Step 6: Rewrite the loop**

In `Cubit/src/Application.cpp`, add the include beside the existing ones:

```cpp
#include "Cubit/FrameClock.h"
```

Replace the body of `Application::Run` with:

```cpp
void Application::Run()
{
    CB_CORE_INFO("Engine running");

    using Clock = std::chrono::steady_clock;
    auto lastFrameTime = Clock::now();
    FrameClock frameClock;

    while (m_Data->Running)
    {
        m_Data->WindowInstance->PollEvents();

        if (!m_Data->Running || m_Data->WindowInstance->ShouldClose())
            break;

        const auto currentFrameTime = Clock::now();
        const std::chrono::duration<double> frameDuration = currentFrameTime - lastFrameTime;
        lastFrameTime = currentFrameTime;

        const Timestep timestep(frameDuration.count());

        // Every layer takes one step before any layer takes the next. Stepping a
        // layer to completion first would let a layer read another's state from
        // up to MaxTicksPerFrame steps in the future.
        constexpr Timestep step = FrameClock::Step();
        const int ticks = frameClock.Advance(frameDuration.count());
        for (int tick = 0; tick < ticks; ++tick)
        {
            m_Data->Layers.OnFixedUpdate(step);
            OnFixedUpdate(step);
        }

        // After the steps, so per-frame work observes their result.
        m_Data->Layers.OnFrameUpdate(timestep);
        OnFrameUpdate(timestep);

        const float alpha = frameClock.Alpha();

        Renderer::SetClearColor(0.08f, 0.10f, 0.15f, 1.0f);
        Renderer::Clear();
        m_Data->Layers.OnRender(alpha);
        OnRender(alpha);
        m_Data->WindowInstance->SwapBuffers();
    }
}
```

- [ ] **Step 7: Point the two Sandbox layers at the clock each one wants**

In `Sandbox/src/Sandbox.cpp` line ~169, rename the player update — physics belongs on the fixed step:

```cpp
    void OnFixedUpdate(Timestep timestep) override
```

Its body is unchanged in this task.

At line ~239, give `SandboxLayer::OnRender` the alpha and ignore it for now:

```cpp
    void OnRender(float alpha) override
    {
        (void)alpha;
        m_WorldRenderer.Update(m_World);
```

In `Sandbox/src/HudLayer.h` line ~104, the frame-rate readout is the one thing that genuinely wants the real delta — handed a fixed step it would report a constant 60:

```cpp
    //Tracks a smoothed frame rate for the readout.
    void OnFrameUpdate(Timestep timestep) override
```

Its body is unchanged. At line ~118:

```cpp
    void OnRender(float alpha) override
    {
        (void)alpha;
        Renderer::SetDepthTest(false);
```

- [ ] **Step 8: Build the tests and confirm they pass**

Run: `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Tests/Tests.vcxproj -p:Configuration=Debug -p:Platform=x64`

Expected: build succeeds, all cases pass, total risen by 2 over Task 1.

- [ ] **Step 9: Build the Sandbox and confirm it compiles**

Run: `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Sandbox/Sandbox.vcxproj -p:Configuration=Debug -p:Platform=x64`

Expected: build succeeds with no warnings about hidden virtuals. If the compiler reports an unreferenced or hidden `OnUpdate` anywhere, a `Layer` subclass was missed — there should be exactly two, `SandboxLayer` and `HudLayer`.

- [ ] **Step 10: Commit**

```bash
git add Cubit/include/Cubit/Layer/Layer.h Cubit/include/Cubit/Layer/LayerStack.h Cubit/src/Layer/LayerStack.cpp Cubit/include/Cubit/Application.h Cubit/src/Application.cpp Sandbox/src/Sandbox.cpp Sandbox/src/HudLayer.h Tests/src/LayerStackTests.cpp
git commit -m "Step simulation on a fixed clock, separate from the frame"
```

---

### Task 3: Interpolate the camera, and stop teleports smearing

**Files:**
- Modify: `Sandbox/src/Sandbox.cpp` — constructor (~line 114 and ~127), `OnFixedUpdate` (~169-235), `OnRender` (~239), `UpdateCameraPosition` (~329), `LoadWorld` tail (~414), `LiftPlayerClearOfTerrain` (~427), members (~539)

**Interfaces:**
- Consumes: `SandboxLayer::OnRender(float alpha)` and `OnFixedUpdate` from Task 2.
- Produces: `SandboxLayer::TeleportPlayer(const glm::vec3&)`, `SandboxLayer::UpdateCameraPosition(float alpha)`, member `m_PreviousPlayerPosition`.

No unit test: `SandboxLayer` needs a GL context and is untested by the suite today, as the spec records. Verification is Task 5, on screen.

**Leave `m_EyeInFluid` where it is.** It is computed inside `OnFixedUpdate` from the simulated position, not the interpolated eye, and it stays that way. A one-step lag on the underwater fog and HUD wash is invisible, and moving it into the render path would put a world query there for no gain.

- [ ] **Step 1: Add the previous-position member**

In `Sandbox/src/Sandbox.cpp`, beneath `glm::vec3 m_PlayerPosition{ 0.0f };` (~line 539):

```cpp
    // Where the player stood at the end of the previous fixed step. Rendering
    // interpolates between this and the current position, so motion stays smooth
    // when frames and steps do not line up.
    glm::vec3 m_PreviousPlayerPosition{ 0.0f };
```

- [ ] **Step 2: Add the teleport helper**

In `Sandbox/src/Sandbox.cpp`, beside `UpdateCameraPosition` (~line 329):

```cpp
    //Moves the player without interpolating through the space in between.
    //
    //Interpolating a respawn or a reload would smear the camera across the map
    //for a frame, so both positions are written together and the lerp that
    //follows is a no-op.
    void TeleportPlayer(const glm::vec3& position)
    {
        m_PlayerPosition = position;
        m_PreviousPlayerPosition = position;
    }
```

- [ ] **Step 3: Make the camera read the interpolated position**

Replace `UpdateCameraPosition` (~line 329) with:

```cpp
    //Places the camera at eye height above the player, in world space, at the
    //point the player occupied `alpha` of the way through the current step.
    void UpdateCameraPosition(float alpha)
    {
        const glm::vec3 position =
            glm::mix(m_PreviousPlayerPosition, m_PlayerPosition, alpha);

        m_CameraController.SetPosition(
            position + WorldOffset + glm::vec3(0.0f, EyeOffset, 0.0f));
    }
```

- [ ] **Step 4: Snapshot in the step, and drop the camera move out of it**

At the top of `OnFixedUpdate` (~line 171), before `const float seconds = ...`:

```cpp
        // The step is about to overwrite the position rendering interpolates
        // from, so keep it first.
        m_PreviousPlayerPosition = m_PlayerPosition;
```

At the end of the same method, delete the trailing `UpdateCameraPosition();` call — the camera is now placed in the render path instead.

In the same method (~line 231), the fall reset is a discontinuity:

```cpp
        if (m_PlayerPosition.y < FallResetHeight)
        {
            TeleportPlayer(m_Spawn);
            m_VerticalVelocity = 0.0f;
        }
```

- [ ] **Step 5: Place the camera in the render path**

At the top of `OnRender` (~line 239), replacing the `(void)alpha;` line from Task 2:

```cpp
    void OnRender(float alpha) override
    {
        UpdateCameraPosition(alpha);

        m_WorldRenderer.Update(m_World);
```

- [ ] **Step 6: Route the remaining three teleports through the helper**

In the constructor (~line 114):

```cpp
        TeleportPlayer(m_Spawn);
```

In the constructor (~line 127), the camera placement before the first render becomes an explicit "where the player is now":

```cpp
        UpdateCameraPosition(1.0f);
```

At the tail of `LoadWorld` (~line 414), the same:

```cpp
        LiftPlayerClearOfTerrain();
        m_VerticalVelocity = 0.0f;
        UpdateCameraPosition(1.0f);
```

Rewrite `LiftPlayerClearOfTerrain` (~line 427) so the lift has a single writer and ends in a teleport. The doc comment above it is unchanged:

```cpp
    void LiftPlayerClearOfTerrain()
    {
        const float top = static_cast<float>(m_World.GetHeight());

        glm::vec3 lifted = m_PlayerPosition;
        while (lifted.y < top &&
            VoxelCollision::Overlaps(m_World, lifted, PlayerHalfExtents))
            lifted.y += 1.0f;

        // A column solid to the sky has nowhere to stand.
        if (VoxelCollision::Overlaps(m_World, lifted, PlayerHalfExtents))
            lifted = m_Spawn;

        // A lift is a discontinuity, so it must not be interpolated through.
        TeleportPlayer(lifted);
    }
```

- [ ] **Step 7: Build the Sandbox**

Run: `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Sandbox/Sandbox.vcxproj -p:Configuration=Debug -p:Platform=x64`

Expected: build succeeds. If `glm::mix` is unresolved, add `#include <glm/glm.hpp>` to the Sandbox includes.

- [ ] **Step 8: Commit**

```bash
git add Sandbox/src/Sandbox.cpp
git commit -m "Interpolate the camera between simulation steps"
```

---

### Task 4: Show steps per frame on the HUD

Below roughly 12 fps the cap stops the loop running every step it owes and the simulation quietly slows down. That is correct behaviour, invisible without a readout, and indistinguishable from a physics bug when it happens — and Debug on the 512 map is the normal way this project runs.

**Files:**
- Modify: `Sandbox/src/DebugFont.h:24` and the glyph table (~line 52)
- Modify: `Sandbox/src/HudLayer.h:17-34` (`HudState`) and `DrawReadout` (~line 238)
- Modify: `Sandbox/src/Sandbox.cpp` — `OnFixedUpdate`, a new `OnFrameUpdate`, members

**Interfaces:**
- Consumes: `HudState` (`Sandbox/src/HudLayer.h`), `Layer::OnFrameUpdate` from Task 2.
- Produces: `HudState::StepsPerFrame` (`int`).

- [ ] **Step 1: Teach the debug font the letter T**

The font carries only `"0123456789-.: ACDEFGNOPS"`, and an unsupported character renders as a blank — a label written in letters it lacks would silently disappear. `STEPS` needs one new glyph.

In `Sandbox/src/DebugFont.h` line 24:

```cpp
    constexpr std::string_view Order = "0123456789-.: ACDEFGNOPST";
```

Append to the `Glyphs` table, after the `S` entry (the table order must match `Order`, so add a comma to the S line):

```cpp
        { ".####", "#....", "#....", ".###.", "....#", "....#", "####." }, // S
        { "#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.." }  // T
```

- [ ] **Step 2: Add the field to `HudState`**

In `Sandbox/src/HudLayer.h`, inside `HudState` after `PendingChunks`:

```cpp
    //Fixed simulation steps run during the last frame. Above 1 the renderer is
    //behind the simulation; at FrameClock::MaxTicksPerFrame the loop has stopped
    //running every step it owes and the simulation is running slow.
    int StepsPerFrame = 0;
```

- [ ] **Step 3: Draw it**

In `DrawReadout` (`Sandbox/src/HudLayer.h`, ~line 238), between the `PENDING` and `FPS` lines:

```cpp
        y -= lineHeight;
        DrawText("STEPS " + std::to_string(m_State->StepsPerFrame), TextMargin, y);
```

- [ ] **Step 4: Count the steps and publish them once a frame**

In `Sandbox/src/Sandbox.cpp`, add the member beside `m_PreviousPlayerPosition`:

```cpp
    // Counted across the current frame's steps and published by OnFrameUpdate.
    int m_StepsThisFrame = 0;
```

At the top of `OnFixedUpdate`, beside the previous-position snapshot:

```cpp
        ++m_StepsThisFrame;
```

Add `OnFrameUpdate` immediately after `OnFixedUpdate`:

```cpp
    //Publishes how many fixed steps ran this frame, then resets for the next.
    void OnFrameUpdate(Timestep timestep) override
    {
        (void)timestep;
        m_HudState->StepsPerFrame = m_StepsThisFrame;
        m_StepsThisFrame = 0;
    }
```

- [ ] **Step 5: Build the Sandbox**

Run: `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Sandbox/Sandbox.vcxproj -p:Configuration=Debug -p:Platform=x64`

Expected: build succeeds.

- [ ] **Step 6: Commit**

```bash
git add Sandbox/src/DebugFont.h Sandbox/src/HudLayer.h Sandbox/src/Sandbox.cpp
git commit -m "Show fixed steps per frame on the HUD"
```

---

### Task 5: Verify on screen and record the result

Unit tests cover the clock and the fan-out; nothing covers whether the game still feels right. This is the task that finds what the tests cannot.

**Files:**
- Modify: `docs/engine-roadmap.md` — the "Worth doing before gameplay" item 1

- [ ] **Step 1: Run the Sandbox and screenshot it**

Launch the Sandbox and capture the window. Grab the **GLFW30** window class, not `MainWindowHandle`. Close via `WM_CLOSE` — killing the process loses the logs. Mouse input can be driven from a script; keyboard cannot.

- [ ] **Step 2: Read the HUD against these expectations**

- `STEPS` reads `1` at a healthy frame rate. A steady `5` means the cap is saturated and the simulation is running slow — report it rather than adjusting `MaxTicksPerFrame` silently.
- `FPS` is unchanged from before the change.
- `POS` changes smoothly while walking, with no stutter or snapping.

- [ ] **Step 3: Check the two behaviours the tests cannot reach**

- Jump, and while airborne drag the window by its title bar, hold a moment, release. Expected: no snap to the ground on release. This is the latent bug the accumulator cap fixes.
- Press F9 to reload. Expected: the player stays where they were working, with no smear of the camera across the map. A smear means a teleport site was missed in Task 3.

- [ ] **Step 4: Confirm the two expected differences, and that they are only these**

Jump height shifts slightly — Euler-integrated gravity is rate-dependent, so a fixed 60 Hz tick gives a marginally different apex than the old frame-rate-dependent one. Interpolation renders up to one step (16.67 ms) in the past. Both are expected. Anything else is a regression.

- [ ] **Step 5: Update the roadmap**

In `docs/engine-roadmap.md`, mark item 1 of "Worth doing before gameplay" done, in the style the arc items above it use: strike the heading, add **DONE 2026-08-22**, and state what shipped — `FrameClock` at 60 Hz with a five-step cap, `OnUpdate` split into `OnFixedUpdate`/`OnFrameUpdate`, camera interpolation in the Sandbox, and a `STEPS` HUD readout. Note that binding input to numbered ticks is still open and belongs with the `BlockEdit` item.

- [ ] **Step 6: Commit and push**

```bash
git add docs/engine-roadmap.md
git commit -m "Record the fixed timestep as shipped"
git push
```
