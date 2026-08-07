<p align="center">
  <img src="images/CubitLogo.png" alt="Cubit logo" width="360">
</p>

# Cubit

A C++ voxel engine, built alongside a sandbox application that exercises each system as
it lands. The target is an Ace of Spades style multiplayer FPS: destructible terrain,
building, shooting, and team-based matches.

The engine builds as a DLL (`Cubit`). `Sandbox` is the executable that drives it, and
`MapGen` is an offline tool that writes map files.

Full scope and feature spec lives in `Documentation/Cubit.pdf`.

## What works

You load a map, walk around it under gravity, and dig into it or build on it, lit by
sky light and ambient occlusion. The current map is a 256x64x256 battlefield.

**Platform and core loop**

- GLFW-backed window on an OpenGL 3.3 core context, with vsync
- Typed platform events (window, key, mouse) dispatched through a layer stack, overlays
  first
- A separate `EventBus` for typed gameplay notifications, so layers do not need to know
  about each other
- Polled input, cursor capture, frame timesteps, flushed logging, and debug-only
  assertions

**Rendering**

- Vertex arrays, vertex and index buffers with a described layout, shaders with uniform
  setters, and unfiltered RGBA textures
- Backface culling, a depth-test toggle for screen-space overlays, and alpha blending
- Orthographic and perspective cameras, each with a controller
- `Frustum`: six clip planes pulled from a view-projection matrix, tested against
  axis-aligned boxes
- `WorldRenderer`: one GPU mesh per chunk, rebuilt only for chunks the world reports
  dirty, meshed against a 4 ms per-frame time slice so a burst of remeshing spreads over
  frames instead of stalling one, and drawn only when the chunk's box is inside the
  frustum
- Two-pass drawing: opaque geometry first, then transparent geometry sorted back
  to front with depth writes off, so water blends over the riverbed beneath it

**Voxel world**

- 16x16x16 chunks storing one palette index and one sky-light level per block
- `World`: a fixed grid of chunks addressed in world coordinates, with a palette and
  dirty-chunk tracking that includes the diagonal neighbours a mesh's corner sampling
  reads
- `ChunkMesher`: face-culled and neighbour-aware, so no faces are emitted at a chunk
  seam. It copies the chunk plus its one-block shell into a flat 18³ array once per
  mesh and samples that by flat index, rather than resolving every read through the
  world
- Per-vertex ambient occlusion, plus sky light averaged over the open cells at each
  face corner, so shading graduates smoothly across a surface. Each quad is split along
  its darker diagonal to keep that gradient seam-free
- `SkyLight`: a flood that falls from the open sky, spreads through air, and dims with
  distance — except at full strength, where it falls for free. An edit relights only
  what it disturbed, working outward until the light stops moving, and marks just the
  chunks whose light actually changed
- `VoxelRaycast` (grid traversal, reporting the entry face) and `VoxelCollision` (a
  stepped, per-axis box move that reports which axes were blocked and whether the box
  is grounded)

**Content pipeline**

- `VoxLoader` parses MagicaVoxel `.vox` into Cubit's Y-up space and `BuildWorld` sizes
  a world to hold it, palette included
- `VoxWriter` is the exact inverse, so a model round-trips through the loader
- `ToVoxModel` and `VoxWriter::WriteFile` save an edited world back out, so a map
  can be fixed by playing it — the sandbox binds this to `F5` and `F9`, though
  promoting a save over the shipped map still takes a manual copy and rebuild
- `TerrainGen` generates a symmetric Ace-of-Spades-style map: noise hills, mountain
  flanks with snow caps, a central river with sand banks, scattered forests, and two
  mirrored team-coloured forts
- `MapGen` is the offline tool that runs the generator and writes `battlefield.vox`

**Sandbox**

- Loads `assets/maps/battlefield.vox` — 1024 chunks, of which around 600 hold geometry
- A player box that falls, lands, jumps, slides along walls, and respawns after falling
  off the map
- Breaking and placing blocks along the view ray, within reach, relit on each edit
- A debug HUD: crosshair, position, grounded flag, total meshed faces, drawn and total
  chunks, chunks still pending a remesh, and a smoothed frame rate — drawn with a 5x7
  bitmap font defined in code

**Controls:** `W`/`A`/`S`/`D` to move, `Space` to jump, mouse to look. Left click breaks
a block, right click places one, and `1`–`8` pick the colour. `F5` saves the edited
world, `F9` restores it — a checkpoint pair for authoring a map by playing it.

## Building

Windows, Visual Studio 2026 or compatible, Premake 5 on `PATH`, C++20.

```bat
git submodule update --init --recursive
GenerateProjects.bat
```

Open the generated solution (`Cubit.slnx`), select `Debug` and `x64`, build, then run
`Sandbox`. `Cubit.dll` and the `assets` directory are copied next to the executable as
post-build steps, so the running app resolves `assets/...` the way a shipped build
would.

To regenerate the map, build and run `MapGen` with the output path you want it written
to, then rebuild `Sandbox` so the new file is copied next to the executable:

```bat
MapGen.exe <repo>\Sandbox\assets\maps\battlefield.vox
```

## Tests

`Tests` is a doctest suite — around 140 cases — covering the parts that can be checked
without a GPU or a window: chunk and world storage, meshing and its face counts,
ambient occlusion and light sampling, sky-light propagation, raycasting, collision,
frustum culling, `.vox` loading and writing, and the generated terrain's invariants. It
runs automatically after building, so a failing test breaks the build.

Rendering, windowing, and input are not unit tested. Those are checked by running the
sandbox and looking at the result.

## Layout

```text
Cubit/         Engine, built as a DLL
  include/     Public headers
  src/         Implementation; engine-only code under Core/
Sandbox/       Executable that drives the engine
  assets/maps/ The .vox maps it loads
MapGen/        Offline map generator
Tests/         doctest suite
docs/          Roadmap, performance notes, designs and plans
Documentation/ Scope spec and per-commit design notes
vendor/        GLFW, GLAD, GLM, doctest
```

Public headers live under `Cubit/include/Cubit` and are exported with `CB_API`. The
sandbox only includes that directory, so it gets the `CB_*` logging and assert macros
but not the engine-internal `CB_CORE_*` ones.

`bin/`, `bin-int/`, and the Visual Studio project files are generated.

## Performance

The engine is fast enough to build on, and the work to get there is written up rather
than guessed at. [`docs/performance.md`](docs/performance.md) catalogs each known
problem, where it lives, and what it cost; the block-edit investigation under
`docs/superpowers/investigations/` records how the causes were found, including three
optimisations that measured slower and were reverted.

Greedy meshing is the fourth. It was built in full, measured, and reverted: it cut
geometry 20.7% but doubled meshing time in both Debug and Release, and draw calls are
one per chunk either way. P3 in the performance notes has the numbers and the reason —
per-vertex ambient occlusion and greedy merging turn out to be close to mutually
exclusive on lit outdoor terrain.

Where an edit stands today, on the 256x64x256 map in a debug build: relighting a broken
surface block takes 0.03 ms, down from 173 ms, and remeshing the four chunks it touches
about 7 ms, down from 26 ms and now spread across frames by the mesh budget. Still open
at load: the initial sky-light flood over the whole world.

## What's next

Finishing the engine first, in this order — see
[`docs/engine-roadmap.md`](docs/engine-roadmap.md):

- **Multi-model stitching**, for maps beyond the 256-per-axis limit of a single `.vox`
- **Threaded meshing** — the per-frame budget hides load cost but does not remove it

Smaller gaps: reusing a chunk's GPU buffers instead of reallocating them per remesh,
batching chunk draws, and a yaw/pitch setter on the camera controller so a spawn can
choose its facing.

Deliberately out of scope: per-block textures (blocks are palette colours by design),
LOD and streaming (maps are a fixed known size), and audio (that belongs with gameplay).

After the engine, the game:

- Weapons and shooting
- Health, death, and respawn
- Client/server networking, including replicating terrain edits
- Match state: teams, scoring, objectives

The rule the project follows is to build only what the game needs, and to prove each
system in the sandbox before the game layer depends on it.
