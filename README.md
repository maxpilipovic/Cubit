<p align="center">
  <img src="images/CubitLogo.png" alt="Cubit logo" width="360">
</p>

# Cubit

A C++ voxel engine, built alongside a sandbox application that exercises each system as
it lands. The target is an Ace of Spades style multiplayer FPS: destructible terrain,
building, shooting, and team-based matches.

The engine builds as a DLL (`Cubit`) and knows no game rules: health, damage, shot
range, fire rate and dig reach are a `MatchRules` value the game supplies. `Sandbox` is
the harness that exercises the engine — a map, a free camera, editing, save and reload,
and the timing readouts. The game itself lives under `game/`: `GameApp` is the player's
executable, `Server` is a headless authoritative match server, `MapGen` writes map
files, and `GameTests` is the game's own suite. One directory, so the game can become
its own repository with a single `git subtree split -P game`.

Full scope and feature spec lives in `Documentation/Cubit.pdf`.

## What works

You load a map, walk around it under gravity, and dig into it or build on it, lit by
sky light and ambient occlusion — alone, or in a match with other players over the
network, where you can shoot each other. Water is see-through and you swim through it
rather than walking on it, with the screen washing blue and hazing out while you are
under. Swimming alone will not get you up the banks, though — there is no step-up
assist, so climbing out means digging or building a way up, same as anywhere else on
the map. The current map is a 512x64x512 battlefield.

**Platform and core loop**

- GLFW-backed window on an OpenGL core context with vsync: 3.3 in Release, 4.3 in Debug,
  where the extra version is what makes the driver's debug message callback available
- A fixed 60 Hz simulation step (`FrameClock`) separate from the frame rate, with
  layers split into `OnFixedUpdate`, `OnFrameUpdate` and an interpolated `OnRender`
- Typed platform events (window, key, mouse) dispatched through a layer stack, overlays
  first, where a layer can be removed — including by itself, mid-event, which is what a
  menu closing itself does
- A separate `EventBus` for typed gameplay notifications, so layers do not need to know
  about each other. Subscribing hands back a subscription that unsubscribes when it is
  destroyed, so a listener's callback cannot outlive the listener
- Polled input, cursor capture, and debug-only assertions
- Blocks with nothing holding them up fall, and disappear: after a change empties cells,
  whatever they were touching is checked for a path of solid blocks down to the map's
  bottom layer, and anything without one is cleared. Terrain is anchored through the
  ground, so digging a hillside never brings the world down
- Logging with a wall-clock time on every line, flushed per line, and copied to
  `logs/<program>-<date>-<time>-<pid>.log`
- A crash handler: an uncaught exception or a native fault is logged with a symbolised
  stack and leaves a minidump in `crashes/`
- `CB_PROFILE_SCOPE` times named scopes into a Chrome trace, compiled into Debug
  and Release and out of Dist

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
- `DebugDraw`: world-space lines and wireframe boxes callable from anywhere, used to
  outline the block under the crosshair and to draw other players

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
  is grounded, plus solid and fluid overlap queries). A block is *present* if it is
  there at all, *solid* if it stops you, and *fluid* if it is present but does not —
  so water is swum through, aimed through, and cannot be dug or placed
- `BlockEdit`: one block change as a value. Applying it relights and returns its
  inverse, which is what undo and edit rollback are built on
- `CharacterController`: the player's movement as a pure step over state, an input and
  the world — gravity, jumping, swimming and collision — so prediction and replay are
  just calling it again
- `FindSpawn` resolves a map column into a standable, dry spawn position

**Networking**

- `MatchState` holds the world, the roster and the tick, and both ends step it
- A `Transport` interface over ENet, plus a loopback and a `SimulatedTransport` that adds
  deterministic latency, jitter and loss — so the netcode is tested with a server and
  clients in one process, identically every run
- `MatchServer`, the only authority: it applies each client's inputs one per tick,
  validates and applies edits, resolves shots, and sends a snapshot to every client
- `MatchClient` predicts its own movement and its own edits, reconciles against each
  snapshot by replaying what the server has not yet acknowledged, and draws other
  players interpolated between snapshots
- Hitscan shooting with lag compensation: the server rewinds everyone to the instant the
  shooter saw. Three hits kill, and death respawns instantly
- A joining client receives every cell that differs from the map, and a server can be
  stopped cleanly
- A client whose inputs have piled up on the server — a lag spike, a fast clock —
  catches up by skipping inputs, so input delay does not grow over a match

**Content pipeline**

- `VoxLoader` parses MagicaVoxel `.vox` into Cubit's Y-up space and `BuildWorld` sizes
  a world to hold it, palette included. A map larger than 256 on an axis is stitched
  together from several models
- `VoxWriter` is the exact inverse, so a model round-trips through the loader
- `ToVoxModel` and `VoxWriter::WriteFile` save an edited world back out, so a map
  can be fixed by playing it — the sandbox binds this to `F5` and `F9`, though
  promoting a save over the shipped map still takes a manual copy and rebuild
- `TerrainGen` generates a symmetric Ace-of-Spades-style map: noise hills, mountain
  flanks with snow caps, a central river with sand banks, scattered forests, and two
  mirrored team-coloured forts
- `MapGen` is the offline tool that runs the generator and writes a `.vox`

**Sandbox**

- Loads `assets/maps/battlefield512.vox` — 4,096 chunks, of which 2,408 hold geometry
- A player that falls, lands, jumps, swims, slides along walls, and respawns after
  falling off the map
- Breaking and placing blocks along the view ray, within reach, relit on each edit, with
  an undo stack
- A debug HUD: crosshair, position, grounded and in-water flags, meshed faces, drawn and
  total chunks, chunks pending a remesh, physics steps per frame, undo depth and frame
  rate; connected, it adds the player count, round-trip time, health and hit markers.
  It is drawn with a bitmap font defined in code

**Controls:** `W`/`A`/`S`/`D` to move, `Space` to jump, mouse to look. Left click breaks
a block, right click places one, `1`–`8` pick the colour, and middle click fires. `U`
undoes the last block edit, along with anything that fell because of it. `B`
(single-player) blows a radius-3 ball out of the terrain where you aim, as one batch that
`U` undoes whole. `F5` saves the edited world, `F9` restores it — a
checkpoint pair for authoring a map by playing it.

## Building

Windows, Visual Studio 2026 or compatible, Premake 5 on `PATH`, C++20.

```bat
git submodule update --init --recursive
GenerateProjects.bat
```

Open the generated solution (`Cubit.slnx`), select `Debug` and `x64`, build, then run
either application: `Sandbox` for the engine harness, `GameApp` for the game.
`Cubit.dll` and the `assets` directory are copied next to each executable as post-build
steps, so a running app resolves `assets/...` the way a shipped build would.

Each project describes itself in a `premake5.lua` beside its own sources, and the root
file is the workspace and a list of `include` lines. A project's paths are relative to
its own build file, because premake resolves a script's paths from that script's
directory.

Premake expands its file lists when it generates the projects, so a new source file
needs the projects regenerated. `GenerateProjects.bat` deletes `bin/` and `bin-int/`
first; `premake5 vs2026` on its own regenerates without the clean rebuild.

To regenerate the map, build and run `MapGen` with the size and output path you want
it written to, then rebuild the app you want it copied next to.
The shipped map is 512x64x512, which needs an explicit `--size` since `MapGen`
defaults to 256x64x256:

```bat
MapGen.exe --size 512 64 512 <repo>gamessetsmapsattlefield512.vox
```

## Playing a match

Start the server, then connect clients:

```bat
Server.exe
GameApp.exe --connect 127.0.0.1
```

Both read the map from `assets/` in the working directory, which the build copies next
to each executable, so run each from its own output directory. A client checks the
map's hash against the server's and refuses to join on a mismatch. The server listens on port 27015 and takes a map path as an argument; `--port` changes
the port on either side. `--latency <ms>` (round trip) and `--loss <percent>` add a
simulated bad network to either end, which is how to see prediction and lag
compensation working on one machine. `--duration <seconds>` stops the server by itself,
for scripts; otherwise `Ctrl+C` stops it and tells every client. With no arguments,
`GameApp` is the single-player game, with no socket anywhere.

## Tests

There are two suites, divided by what a failing test would point at. `Tests` is the
engine's — 574 cases — covering everything that can be checked
without a GPU or a window: chunk and world storage, meshing and its face counts,
ambient occlusion and light sampling, sky-light propagation, raycasting, collision,
character movement, frustum culling, `.vox` loading and writing, the generated terrain's
invariants, the wire protocol, and the netcode end to end under simulated latency and
loss — prediction, corrections, predicted edits, lag compensation and input delay. The
crash handler is tested by running the test executable itself as a child process that
crashes on purpose. `GameTests` is the game's — 3 cases — covering the numbers the game
states for itself and the labels its HUD draws. Both suites run automatically after
building, so a failing test breaks the build.

Rendering, windowing, and input are not unit tested. Those are checked by running an
application and looking at the result.

## Layout

```text
Cubit/           Engine, built as a DLL
  include/       Public headers
  src/           Implementation; engine-only code under Core/
Sandbox/         The engine's harness
Tests/           The engine's doctest suite
game/            The game, laid out to become its own repository
  Game/src/      Static library: rules, options, the player layer, the HUD
  GameApp/src/   The player's executable
  Server/src/    Headless match server
  MapGen/src/    Offline map generator
  GameTests/src/ The game's doctest suite
  assets/maps/   The .vox maps
docs/            Roadmap, performance notes, designs and plans
Documentation/   Scope spec and per-commit design notes
vendor/          GLFW, GLAD, GLM, ENet, doctest
```

Public headers live under `Cubit/include/Cubit` and are exported with `CB_API`. Both
applications only include that directory, so they get the `CB_*` logging and assert
macros but not the engine-internal `CB_CORE_*` ones.

The engine names nothing under `Sandbox/` or `game/`. Two threads still cross the other
way: the harness copies the game's `assets/` next to its executable, and six engine test
files open maps from it — so the maps would have to be sorted out before a repository
split actually happens. That is recorded as its own roadmap item rather than hidden.

`bin/`, `bin-int/`, and the Visual Studio project files are generated.

## Performance

The engine is fast enough to build on, and the work to get there is written up rather
than guessed at. [`docs/performance.md`](docs/performance.md) catalogs each known
problem, where it lives, and what it cost; the investigations under
`docs/superpowers/investigations/` record how the causes were found, including
optimisations that measured slower and were reverted.

Greedy meshing is one of them. It was built in full, measured, and reverted: it cut
geometry 20.7% but doubled meshing time in both Debug and Release, and draw calls are
one per chunk either way. P3 in the performance notes has the numbers and the reason —
per-vertex ambient occlusion and greedy merging turn out to be close to mutually
exclusive on lit outdoor terrain.

Loading the 512-wide map takes 1.42 s in a debug build, down from 33.1 s, with no single
phase dominating any more. The largest remaining cost is meshing the whole map, about
5 s of debug work spread across frames by the mesh budget, so the world visibly builds
itself around you rather than stalling.

## What's next

Before gameplay, every gap from an engine audit is being closed, in
[`docs/engine-roadmap.md`](docs/engine-roadmap.md) under "Before the game — engine punch
list": bugs and robustness first, then missing systems the game will need — multi-block
edits, a way to draw things that are not chunks, text and UI, configuration, audio, and
a separate game target — then parked items to do or drop on purpose, such as threaded
meshing.

Deliberately out of scope: per-block textures (blocks are palette colours by design)
and LOD and streaming (maps are a fixed known size).

After the engine, the game: teams, match state, an objective and a scoreboard; tool
slots for digging, building and shooting; and ammunition.

The rule the project follows is to build only what the game needs, and to prove each
system in the sandbox before the game layer depends on it.
