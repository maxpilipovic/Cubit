<p align="center">
  <img src="images/CubitLogo.png" alt="Cubit logo" width="360">
</p>

# Cubit

Cubit is a custom C++ engine project with a sandbox application used to test engine features as they are built. The long-term design target is an Ace of Spades style multiplayer voxel FPS: destructible block terrain, building, shooting, team-based match rules, and networked multiplayer.

The repository is intentionally split into an engine DLL and a small sandbox executable. The engine lives in `Cubit`, while `Sandbox` is the current executable used to load and exercise engine code.

## Current Status

Cubit is in the foundation stage. The current implementation is minimal:

- `Cubit` builds as a shared library.
- `Sandbox` builds as a console application.
- `Sandbox` links against `Cubit`.
- The engine exposes a basic `Application` class.
- The sandbox creates an `Application` instance and runs it.
- Premake generates the Visual Studio solution and project files.

The current runtime behavior is simple console output:

```text
Application created
Engine running
Sandbox
```

Most systems described in the design document are planned scope, not implemented scope yet.

## Project Goal

The design target is a playable prototype first, then a vertical slice. The intended game loop is:

- Move around a voxel map.
- Shoot other players.
- Dig or destroy terrain.
- Place blocks to build cover or routes.
- Respawn and continue in a team-based match.
- Synchronize players, combat, and terrain edits over the network.

The guiding rule from the project documentation is: build only what the game needs. Each engine subsystem should be testable in the sandbox before it is relied on by the game layer.

## Repository Layout

```text
Cubit/
+-- Cubit/                  Engine project
|   +-- src/
|       +-- Application.cpp
|       +-- Application.h
|       +-- Cubit.h
+-- Sandbox/                Test executable / startup project
|   +-- src/
|       +-- Sandbox.cpp
+-- Documentation/
|   +-- Cubit.pdf           Project scope and feature specification
+-- images/
|   +-- CubitLogo.png
+-- GenerateProjects.bat    Regenerates Visual Studio files with Premake
+-- premake5.lua            Build configuration
+-- README.md
```

Generated folders such as `bin/`, `bin-int/`, and Visual Studio project files are build artifacts produced by Premake and MSBuild.

## Build Requirements

- Windows
- Visual Studio 2026 or a compatible Visual Studio C++ toolchain
- Premake 5 available on `PATH`
- C++20 compiler support

## Generating Projects

Run:

```bat
GenerateProjects.bat
```

This removes old build output and solution files, then runs:

```bat
premake5 vs2026
```

After generation, open:

```text
Cubit.sln
```

The solution start project is `Sandbox`.

## Building and Running

1. Generate the project files.
2. Open `Cubit.sln` in Visual Studio.
3. Select the `Debug` configuration and `x64` platform.
4. Build the solution.
5. Run `Sandbox`.

The expected output binary layout is:

```text
bin/
+-- Debug-windows-x86_64/
    +-- Cubit/
    |   +-- Cubit.dll
    |   +-- Cubit.lib
    +-- Sandbox/
        +-- Sandbox.exe
        +-- Cubit.dll
```

`Cubit.dll` is copied into the `Sandbox` output directory after `Sandbox` builds so the executable can load the engine DLL at runtime.

## Architecture

### Cubit

`Cubit` is the engine library. It currently builds as a Windows DLL and exports engine symbols through `CB_API`:

```cpp
#ifdef CB_PLATFORM_WINDOWS
    #ifdef CB_BUILD_DLL
        #define CB_API __declspec(dllexport)
    #else
        #define CB_API __declspec(dllimport)
    #endif
#else
    #define CB_API
#endif
```

The first exported engine type is `Application`, which is responsible for startup and the main run path.

### Sandbox

`Sandbox` is the development executable. Its job is to test engine systems without requiring a full game layer. As the engine grows, new systems should be proven here first: windowing, input, rendering, voxel chunks, collision, debug drawing, networking diagnostics, and gameplay experiments.

### Premake

`premake5.lua` defines:

- Workspace: `Cubit`
- Architecture: `x64`
- Configurations: `Debug`, `Release`, `Dist`
- Engine project: `Cubit`, built as `SharedLib`
- Sandbox project: `Sandbox`, built as `ConsoleApp`
- Startup project: `Sandbox`

## Planned Systems

The project documentation organizes future work into scope bands:

- `FND`: Foundation systems needed before real gameplay can scale.
- `PRT`: Prototype systems needed for the first playable loop.
- `VSL`: Vertical slice systems needed for a representative match.
- `POL`: Polish and quality improvements.

Major planned areas include:

- Logging and assertions
- Window abstraction
- Input handling
- Time step and main loop
- File/config loading
- Debug overlay
- Graphics context and rendering abstractions
- Shader and buffer systems
- Camera and transforms
- Voxel chunk storage
- Chunk mesh generation
- Terrain editing
- Collision against voxel terrain
- Character controller
- Weapons and tools
- Health, damage, death, and respawn
- Client/server networking
- Terrain edit replication
- Match state, teams, scoring, and objectives
- HUD, menus, settings, audio, and feedback

## Recommended Development Order

The safest order is:

1. Add logging and assertions.
2. Create a real window.
3. Add input and time-step handling.
4. Establish the engine main loop.
5. Add a debug overlay.
6. Initialize the renderer and draw a triangle or cube.
7. Build basic mesh submission.
8. Add voxel chunk data.
9. Render chunk meshes.
10. Add block raycasts.
11. Implement block removal and placement.
12. Rebuild dirty chunk meshes.
13. Add terrain collision.
14. Build the player controller.
15. Add weapons, damage, and respawn.
16. Only then start networking.

Networking should come after the offline gameplay loop works. Multiplayer terrain editing, combat authority, prediction, and state synchronization are difficult enough on their own; they should not be mixed with unfinished rendering, collision, and gameplay foundations.

## Learning Scope

This is a good project for learning engine development if it is treated as a long-term learning project with strict milestones. It is not a small beginner project.

The full documented goal is large because it combines several hard topics:

- C++ engine architecture
- Platform/window/input abstraction
- Real-time rendering
- Voxel world storage and meshing
- Collision and character movement
- Game tools and debugging UI
- Multiplayer networking
- Server authority and replication
- Game rules and UX polish

Trying to build all of that at once would be too large. The project becomes manageable only if the first milestone is much smaller: a windowed sandbox that renders editable voxel chunks in single-player. That milestone alone is enough to teach real engine development fundamentals without burying the work under networking and game polish.

Recommended first playable target:

- Open a window.
- Move a debug camera.
- Render a small voxel chunk world.
- Select blocks with a raycast.
- Remove and place blocks.
- Rebuild chunk meshes correctly.
- Show frame time and chunk debug data.

Once that works cleanly, add collision and a simple player controller. After that, add weapons and health. Networking should wait until the single-player prototype is reliable.

## Definition of Done for the First Prototype

A realistic first prototype is done when:

- `Sandbox` opens a window.
- The engine has a stable update/render loop.
- A voxel chunk world renders correctly.
- Hidden voxel faces are culled.
- The camera or player can target blocks.
- Blocks can be removed and placed.
- Edited chunks rebuild without corrupting neighbors.
- Debug information is visible at runtime.
- The code remains understandable and testable.

That is a strong learning milestone and a good base for the larger game.

## Notes for Contributors

- Keep engine systems small and testable in `Sandbox`.
- Prefer simple implementations first, then optimize after profiling.
- Avoid adding networking before the local gameplay loop is working.
- Document subsystem assumptions when they are introduced.
- Keep generated files and build output separate from source changes.
