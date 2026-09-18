# B3: Drawing Geometry That Is Not a Chunk — Design

**Status:** approved 2026-09-18, ready for an implementation plan.

**Roadmap item:** B3 in [`docs/engine-roadmap.md`](../../engine-roadmap.md). There is no
`Mesh` type and no model loading; remote players are `DebugDraw` wireframe boxes. Player
models, a held tool and team colours all wait on this.

## Goal

The engine gains a way to draw arbitrary voxel geometry at a transform, and the game's
remote players stop being wireframe boxes and become models that turn as they turn.

## Decisions

Four questions were settled before any design, and each closes off a direction that would
have been expensive to back out of later.

**Models are voxels, loaded from `.vox`.** Cubit already parses MagicaVoxel and meshes
voxels with ambient occlusion; the maps are authored the same way. A triangle-mesh format
would have opened a second art pipeline, a new dependency and a lighting model that could
not match the world. Rejected.

**The arc ends at the mechanism plus remote players.** The engine gets the mechanism; the
game's proof is remote players drawn as models. A first-person held tool needs a second
camera path and a depth range that does not clip through walls — a distinct rendering
problem, and its own item. Team colours likewise.

**A model is one rigid mesh, yaw only.** Named parts with their own transforms would make
aim readable at a glance, but there is no animation system to drive them, so a rig would
sit inert. Splitting a model into parts later is additive.

**A model samples the world's light once per draw.** Constant brightness reads as a
rendering bug the first time someone stands in a tunnel. Per-vertex world light would mean
re-shading and re-uploading every model every frame it moves. One sample, one uniform.

## What already exists

Worth stating, because it is most of the work.

- `ChunkMesher` emits `MeshGeometry`: a vector of `VoxelVertex { position, colour }` and
  indices. Colour is baked with ambient occlusion and sky light.
- The voxel shader in `WorldScene.cpp` already takes a per-draw `u_Transform`, so drawing
  geometry at a position and rotation needs **no new shader** — the same vertex format and
  the same distance fog apply.
- `VoxLoader` returns a `VoxModel`: a dense voxel grid plus a palette. That is exactly what
  a small model is.
- `Heading.h` fixes the facing convention: yaw 0 faces +x, and yaw grows toward +z, pinned
  against the camera by `HeadingTests`.

## Architecture

Three additions, all under `Cubit/Renderer`.

### `Mesh`

A GPU object built from a `MeshGeometry`: vertex array, vertex buffer, index buffer and an
index count. Owns its buffers and is non-copyable. This is the roadmap's "way to draw
anything that is not a chunk" — model meshes and anything later all become one of these.

### `ModelMesher`

`static MeshGeometry Build(const VoxModel&)`. A sibling of `ChunkMesher`, not a caller of
it: no `World`, no chunk grid, no neighbour lookups across boundaries. It emits only faces
exposed to air and takes ambient occlusion from the model's own voxels.

Where `ChunkMesher` multiplies a vertex colour by sampled sky light, `ModelMesher`
multiplies by a single constant — the model is meshed as if fully lit, and how bright it
actually is comes from the per-draw brightness below. An object carries its own shading
rather than being lit as terrain.

It returns a plain `MeshGeometry`, not a `ChunkMeshData`: models are opaque. A model with
transparent voxels would need the same back-to-front sorting a chunk gets, and nothing in
the game has one.

The alternative considered was building a tiny `World` from the model with the existing
`BuildWorld`, propagating sky light and calling `ChunkMesher::Build` per chunk. It is
almost no new code, but it allocates a full chunk grid, palette and dirty-tracking for a
player-sized model, and it lights models by terrain rules — a player's underside goes dark
because the sky cannot see it, which is right for ground and wrong for a person.

### `WorldScene::DrawMesh(const Mesh&, const glm::mat4& transform, float brightness)`

Models are drawn by the scene that already owns the voxel shader, the fog and the camera.
The shader gains one uniform, `u_Brightness`, which chunks pass as 1.0.

A separate `ModelRenderer` with its own shader would keep `WorldScene` strictly about the
world, at the cost of two near-identical shaders that drift apart. `WorldScene` exists
precisely because a voxel vertex format and its fog are the engine's business rather than
an app's, and models are the same geometry in the same space with the same fog. Its role
widens from "the meshed world" to "the voxel scene: the world and the things standing in
it".

## The game side

- **The asset** is `game/assets/models/player.vox`, loaded once at `GameApp` startup with
  `VoxLoader`, meshed once into a `Mesh`, and drawn per remote player. One model, one
  upload, one draw call each.
- **Scale** comes from the model rather than from a constant:
  `scale = 2 × HalfExtents.y ÷ model height in voxels`, where the player box is 1.8 blocks
  tall. An artist can rebuild the model at any resolution and it still fits; no voxel count
  is hardcoded.
- **Placement and facing:** centred horizontally on the interpolated pose, feet at the box
  bottom, rotated about Y by the pose's yaw. The model is authored facing +x, so `Heading.h`'s
  convention carries over untouched.
- **Brightness:** `World::GetSkyLight` at the cell the player's centre occupies, divided by
  the maximum sky-light level, with a floor so a model in a sealed tunnel is dim rather
  than black. One float per draw, and the same floor the chunk shading already applies.
- **The local player is still not drawn.** First person; the only visible change is that
  remote players stop being wireframes.
- **A missing model file throws**, exactly as a missing map does. It is an asset the game
  ships, and a silent fallback to wireframes would hide a broken build — the failure mode
  that cost an agent a whole task on 2026-08-10.

## Testing

`ModelMesher` is pure logic and goes in the engine suite beside `ChunkMesherTests`:

- a single voxel emits six faces;
- a fully enclosed voxel emits none;
- a 2x1x1 pair emits ten faces, not twelve, because the shared face is interior;
- an empty model meshes to nothing rather than crashing;
- ambient occlusion darkens an inside corner.

`Mesh` and the draw call are GPU work with no unit test available. They are verified by
running two connected clients and seeing each draw the other as a model that turns as they
turn — the verification route recorded in
`.claude/projects/C--dev-Cubit/memory/scripted-input-does-not-reach-gl-window.md`, which is
hands-on rather than scripted, because synthetic input does not reach the window.

## Out of scope

- **Team colours**, named in the roadmap item. A palette swap per team is a natural
  follow-on and a separate decision.
- **A first-person held tool**, which needs its own camera path and depth range.
- **Animation of any kind.** A model is rigid.
- **A general model format.** `.vox` is the pipeline.
