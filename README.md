<p align="center">
  <img src="images/CubitLogo.png" alt="Cubit logo" width="360">
</p>

# Cubit

A C++ voxel engine, built alongside a sandbox application that exercises each system as
it lands. The target is an Ace of Spades style multiplayer FPS: destructible terrain,
building, shooting, and team-based matches.

The engine builds as a DLL (`Cubit`). `Sandbox` is the executable that drives it.

Full scope and feature spec lives in `Documentation/Cubit.pdf`.

## What works

You can walk around a voxel world, dig into it, and build on it.

- GLFW-backed window, routed input, layers and overlays, and a typed gameplay event bus
- OpenGL renderer: vertex and index buffers, shaders, textures with alpha blending,
  orthographic and perspective cameras
- Voxel chunks with face-culled meshing, and a `World` grid that meshes chunks against
  their neighbours
- Raycasting, box collision, and a walking player with gravity and jumping
- Terrain editing with coloured blocks, remeshed on each edit
- Debug HUD with a crosshair, player position, and frame rate
- Logging and debug-only assertions

The sandbox currently renders a single chunk of test terrain. That terrain comes from a
sine wave in `Sandbox.cpp` — it exists to give the mesher something to chew on, not as a
world generator.

**Controls:** `W`/`A`/`S`/`D` to move, `Space` to jump, mouse to look. Left click breaks
a block, right click places one, and `1`–`8` pick the colour.

## Building

Windows, Visual Studio 2026 or compatible, Premake 5 on `PATH`, C++20.

```bat
git submodule update --init --recursive
GenerateProjects.bat
```

Open `Cubit.sln`, select `Debug` and `x64`, build, then run `Sandbox`. `Cubit.dll` is
copied next to the executable as a post-build step.

## Tests

`Tests` is a doctest suite covering the parts that can be checked without a GPU or a
window — chunk storage, meshing, raycasting, and collision. It runs automatically after
building, so a failing test breaks the build.

Rendering, windowing, and input are not unit tested. Those are checked by running the
sandbox and looking at the result.

## Layout

```text
Cubit/      Engine, built as a DLL
  include/  Public headers
  src/      Implementation; engine-only code under Core/
Sandbox/    Executable that drives the engine
Tests/      doctest suite
vendor/     GLFW, GLAD, GLM, doctest
```

Public headers live under `Cubit/include/Cubit` and are exported with `CB_API`. The
sandbox only includes that directory, so it gets the `CB_*` logging and assert macros
but not the engine-internal `CB_CORE_*` ones.

`bin/`, `bin-int/`, and the Visual Studio project files are generated.

## What's next

Near term:

- Render the full chunk grid rather than a single chunk
- Move raycasting and collision onto world coordinates, which also allows building
  against the edge of a chunk

After that:

- Load maps from MagicaVoxel `.vox` files instead of generating terrain
- Weapons and shooting
- Health, death, and respawn
- Client/server networking, including replicating terrain edits
- Match state: teams, scoring, objectives

The rule the project follows is to build only what the game needs, and to prove each
system in the sandbox before the game layer depends on it.
