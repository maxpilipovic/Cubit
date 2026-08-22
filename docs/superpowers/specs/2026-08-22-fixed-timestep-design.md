# Fixed Timestep and Render Interpolation — Design

**Date:** 2026-08-22
**Status:** Approved (pending spec review)

## Goal

Give the simulation its own clock, running at a fixed rate independent of the frame
rate, and let rendering interpolate between the last two simulated states.

`Application::Run` currently measures wall-clock time between frames and hands that
raw delta to every `Layer::OnUpdate`. Simulation therefore advances by whatever the
last frame happened to cost.

## Why now

Not because the loop is wrong for a single-player sandbox — it is fine. Two reasons,
one of which has a deadline attached.

**Client prediction requires the client and the server to run the same simulation.**
A predicting client moves the player immediately, sends the input, and later rewinds
to the server's authoritative state and replays every unacknowledged input. That
replay converges only if simulating a given input produces the same result on both
machines. With a variable delta it cannot: the client steps 13.7 ms, then 11.2 ms,
then 19.4 ms, while the server steps 16.67 ms. Same key, same code, different answer,
every frame. The correction never settles, and the symptom is permanent low-grade
rubber-banding whose cause is that the two machines are running different
simulations. The fix is that an input belongs to a *numbered tick*: input 4,271 is
applied to step 4,271 on both machines, and both step by exactly the same amount.
This is a property of how the loop is shaped and cannot be added later as a wrapper.

**The cost only rises.** Exactly one thing consumes `Timestep` for simulation today —
the player physics in `SandboxLayer::OnUpdate`. Once there are weapons with fire
rates, projectiles, reload timers, and animation states, all written against a
variable delta because that is what the interface offered, conversion means touching
every one of them and re-tuning their constants.

A secondary benefit worth naming: determinism makes a physics bug reproducible
instead of frame-timing-dependent.

**The delta clamp is a smaller, separate matter.** The roadmap says a window drag
blocks `glfwPollEvents`, so the next delta is the whole drag and gravity integrates
across all of it. Reading the code, that is mostly latent: standing on terrain,
`MoveBox` hits the ground immediately, sets `BlockedY`, and zeroes the velocity, so
nothing is visible. It bites only when the player is airborne at the moment the title
bar is grabbed. The accumulator cap below subsumes it at no extra cost, so it is
fixed here rather than separately — but it is not the reason for this work.

## Context

Live `Layer::OnUpdate` overrides, in full:

- `SandboxLayer::OnUpdate` (`Sandbox/src/Sandbox.cpp:169`) — player physics. Wants a
  fixed tick.
- `HudLayer::OnUpdate` (`Sandbox/src/HudLayer.h:104`) — smooths a frame-rate readout.
  Wants the real frame delta; handed a fixed step it would report a constant 60.

That is the whole surface. `PerspectiveCameraController::OnUpdate` and
`OrthographicCameraController::OnUpdate` are **not** `Layer` overrides — they are
standalone classes with a method of the same name — so they and their tests are
untouched. Mouse look already runs in `OnEvent`, not per update, so view rotation is
independent of this change.

`VoxelCollision`'s `MaxStep = 0.25` already prevents tunnelling on a large move. It
is not affected and is not a substitute.

## The clock

A new value type, `FrameClock` (`Cubit/include/Cubit/FrameClock.h`), converts one
wall-clock frame delta into a number of fixed steps plus an interpolation alpha.

```cpp
class CB_API FrameClock
{
public:
    static constexpr double FixedStepSeconds = 1.0 / 60.0;
    static constexpr int    MaxTicksPerFrame = 5;

    //Absorbs one frame's wall-clock delta. Returns how many fixed steps to run.
    int Advance(double frameSeconds);

    //Fraction of the way from the last completed step to the next, in [0,1).
    float Alpha() const;

    //The duration every fixed step advances the simulation by.
    static constexpr Timestep Step() { return Timestep(FixedStepSeconds); }

private:
    double m_Accumulator = 0.0;
};
```

`Advance` adds the delta to the accumulator, returns how many whole steps it holds
capped at `MaxTicksPerFrame`, subtracts those, and — if the cap was reached — drops
whatever remains. `Alpha` is `m_Accumulator / FixedStepSeconds`.

**One clock per `Application`, not one per layer.** Layer count does not enter the
arithmetic; the number of steps owed is a property of elapsed time alone. The
instance is a local in `Run`, alongside `lastFrameTime` — nothing outside the loop
reads it, so it does not belong in `ApplicationData`.

**Why it is a class rather than locals in `Run`.** `Application::Run` needs a window
and a GL context, so nothing inside it can be unit tested. The accumulator arithmetic
— the fencepost on `>=`, the remainder carried between frames, dropping the surplus
at the cap, alpha staying inside `[0,1)` — is exactly the fiddly logic that deserves
tests. Extracting it gets it under test the same way `Cubit/src/Voxel/` is testable
because it includes no GL headers.

**Surplus time is discarded, not repaid.** Keeping the leftover accumulator would
make the simulation wall-clock accurate at the price of draining a three-second stall
in slow motion over the following second — a longer, stranger hitch than the one
being fixed. Dropping it means the simulation quietly skips that wall-clock time,
which is the desired behaviour for a window drag, an F9 reload, and the first frame
after a 17 s Debug load alike. One mechanism covers all three, so there is no
`ResetFrameClock` for long operations to remember to call.

**`MaxTicksPerFrame = 5` is the single knob.** An earlier draft also clamped the
frame delta to a separate `MaxFrameSeconds`. That constant can never fire first — the
tick cap already bounds a frame to 5 × 16.67 ms ≈ 83 ms — so it was two mechanisms
doing one job and is not in this design.

## The loop

```cpp
constexpr Timestep step = FrameClock::Step();

const int ticks = clock.Advance(frameDuration.count());
for (int i = 0; i < ticks; ++i)
{
    m_Data->Layers.OnFixedUpdate(step);
    OnFixedUpdate(step);
}

m_Data->Layers.OnFrameUpdate(timestep);
OnFrameUpdate(timestep);

Renderer::SetClearColor(...);
Renderer::Clear();
m_Data->Layers.OnRender(clock.Alpha());
OnRender(clock.Alpha());
m_Data->WindowInstance->SwapBuffers();
```

**The tick loop is outer and the layer loop inner.** Every layer advances one step,
then every layer advances the next. Inverted — one layer running all its steps before
the next layer runs any — a layer reading another's state would read it from up to
five steps in the future.

`OnFrameUpdate` runs after the ticks so per-frame work sees this frame's results.

## Interface

`Layer` loses `OnUpdate` and gains:

```cpp
virtual void OnFixedUpdate(Timestep step)  {}
virtual void OnFrameUpdate(Timestep delta) {}
virtual void OnRender(float alpha)         {}
```

`Application`'s protected virtuals and `LayerStack` split the same way.

**`OnUpdate` is removed rather than redefined.** Every existing override becomes a
compile error that forces an explicit choice between the two clocks. That is the
point: the failure this work exists to prevent is gameplay silently written against a
variable delta, and either alternative — redefining `OnUpdate` to mean the tick, or
keeping it per-frame and adding an opt-in `OnFixedUpdate` — leaves a familiar name
that compiles fine while meaning the wrong thing. Redefining it would also leave
`HudLayer` compiling and silently reporting a hardcoded 60 fps.

## Interpolation

Interpolation state belongs to whoever owns the entity. The engine passes alpha and
knows nothing about what is being interpolated; a general mechanism would require the
engine to know what a position is.

`SandboxLayer` keeps `m_PreviousPlayerPosition`, snapshots `m_PlayerPosition` into it
at the top of `OnFixedUpdate`, and computes the eye in `OnRender(alpha)`:

```cpp
const glm::vec3 eye = glm::mix(m_PreviousPlayerPosition, m_PlayerPosition, alpha);
m_CameraController.SetPosition(eye + WorldOffset + glm::vec3(0.0f, EyeOffset, 0.0f));
```

`UpdateCameraPosition` moves out of the update path into the render path.

**Teleports must not interpolate.** The player is moved discontinuously in three
places — the `FallResetHeight` respawn, `LoadWorld`'s spawn resolve, and
`LiftPlayerClearOfTerrain` after an F9 reload. Lerping across any of them smears the
camera across the map for a frame. All three route through one helper that writes
both positions, making the lerp a no-op:

```cpp
//Moves the player without interpolating through the intervening space.
void TeleportPlayer(const glm::vec3& position)
{
    m_PlayerPosition = position;
    m_PreviousPlayerPosition = position;
}
```

**`m_EyeInFluid` stays on the tick**, computed from the simulated position rather
than the interpolated eye. A one-tick lag on the underwater fog and HUD wash is
invisible, and this keeps the render path free of world queries.

**`HudLayer`'s frame-rate smoothing moves to `OnFrameUpdate`,** logic unchanged. It
is the one consumer that genuinely wants the real delta.

## Instrumentation

`HudState` gains a ticks-per-frame field, written by `SandboxLayer` and drawn in the
existing readout.

This is not decoration. Below roughly 12 fps the cap stops the loop running every
step it owes and the simulation runs in slow motion — correct behaviour, invisible
without a readout, and indistinguishable from a physics bug when it happens. Given
that Debug on the 512 map is the normal way this project is run, that case is
plausible rather than theoretical.

## Testing

New `Tests/src/FrameClockTests.cpp`. Adding a test file requires re-running
`premake5 vs2026` directly — **not** `GenerateProjects.bat`, which deletes `bin/` and
ends in a blocking `pause`.

- Zero delta yields no steps and alpha 0.
- Exactly one step yields one step and alpha 0.
- Half a step yields no steps and alpha 0.5.
- One and a half steps yields one step and alpha 0.5.
- Several short frames accumulate: the step lands on the frame that crosses the
  boundary, not before.
- A three-second stall yields exactly `MaxTicksPerFrame` steps, and alpha is 0
  afterwards — the remainder was dropped.
- The frame after a stall behaves normally, carrying no debt.
- One thousand frames of an awkward 7.3 ms delta produce a step count within one of
  elapsed ÷ step, proving no drift.

`SandboxLayer` and `HudLayer` remain untested by the suite, as they are today.

## Verification on screen

Per the usual method: build, run the Sandbox, screenshot, close via `WM_CLOSE`.

- Walking and jumping feel unchanged, and the HUD frame rate is unchanged.
- Ticks per frame reads 1 at a healthy frame rate.
- Drag the window while airborne and release: no snap to the ground.
- F9 reload leaves the player where the reload put them, with no smear.

**Two changes are expected and are not bugs.** Jump height shifts slightly: Euler-
integrated gravity is rate-dependent, so ticking at a fixed 60 Hz rather than at
whatever the frame rate was gives a marginally different apex. That difference is the
determinism this work is for. And interpolation renders up to one step in the past,
which is up to 16.67 ms of added input latency.

## Out of scope

- Extracting the character controller out of `Sandbox.cpp`, and any entity concept.
  Both are on the roadmap; neither is needed to shape the loop.
- Networking itself. This lands a precondition, not a down payment.
- Threading. The loop stays single-threaded.
- Binding input to numbered ticks for later replay. That needs the `BlockEdit`-style
  input value type, which is its own roadmap item.
