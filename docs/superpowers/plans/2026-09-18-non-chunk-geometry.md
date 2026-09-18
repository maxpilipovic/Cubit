# B3: Drawing Geometry That Is Not a Chunk — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The engine gains a way to draw arbitrary voxel geometry at a transform, and the
game's remote players stop being wireframe boxes and become models that turn as they turn.

**Architecture:** A `ModelMesher` turns a loaded `.vox` into the same `MeshGeometry` the
chunk mesher already produces, a `Mesh` holds that geometry on the GPU, and `WorldScene`
draws it with the voxel shader it already owns — which already takes a per-draw
`u_Transform`, so no new shader is needed. The face table and the shading maths are lifted
out of `ChunkMesher` into an engine-internal header first, so both meshers share one
definition of what a voxel face looks like.

**Tech Stack:** C++20, premake5 (vs2026 action), OpenGL 3.3/4.3 via GLAD and GLFW, glm,
doctest.

**Spec:** [`docs/superpowers/specs/2026-09-18-non-chunk-geometry-design.md`](../specs/2026-09-18-non-chunk-geometry-design.md)

## Global Constraints

- Build: `MSBuild.exe C:\dev\Cubit\Cubit.slnx /p:Configuration=Debug /p:Platform=x64`
  (find MSBuild with `vswhere.exe -latest -find MSBuild\**\Bin\MSBuild.exe`). The build runs
  both test executables as post-build steps, so a failing test fails the build.
- Regenerate projects after adding, moving or removing any source file or premake file:
  `C:\dev\premake\premake5 vs2026` from the repo root. Never run `GenerateProjects.bat` — it
  deletes `bin/`.
- Every task ends green in Debug **and** Release, then commits and pushes to `master`
  (standing authorization). No Claude co-author trailers or attribution in commits.
- Comments explain why, in the voice of the surrounding code: `//` comments, four-space
  indent, `m_` members, PascalCase functions.
- The engine must not include a header from `game/` or `Sandbox/`.
- Existing behaviour does not change. Task 1 is a pure refactor and `ChunkMesherTests` is
  the proof; every later task adds rather than alters.

## One correction to the spec

The spec says the three additions all live under `Cubit/Renderer`. Two of them do — `Mesh`
and `WorldScene::DrawMesh` — but `ModelMesher` belongs beside `ChunkMesher` under
`Cubit/Voxel`, because it is voxel meshing and holds no GL. The spec's architecture is
unchanged; only the directory is.

## File structure

| File | Responsibility |
|---|---|
| `Cubit/src/Voxel/VoxelFaces.h` | Engine-internal: the six face definitions, the per-face shades, the AO shade table, and the one function that turns a corner's occlusion and light into a vertex colour. Not public — it is how the two meshers agree, not an API. |
| `Cubit/include/Cubit/Voxel/ModelMesher.h` | `ModelMesher::Build(const VoxModel&) -> MeshGeometry`. |
| `Cubit/src/Voxel/ModelMesher.cpp` | Its implementation. |
| `Cubit/include/Cubit/Renderer/Mesh.h` | `Mesh`: GPU buffers for one `MeshGeometry`, drawn at a transform. |
| `Cubit/src/Renderer/Mesh.cpp` | Its implementation. |
| `Cubit/include/Cubit/Renderer/WorldScene.h` | Gains `DrawMesh`. |
| `Cubit/src/Renderer/WorldScene.cpp` | Gains `u_Brightness` in the shader and the `DrawMesh` body. |
| `Tests/src/ModelMesherTests.cpp` | The engine suite's cover for the new mesher. |
| `game/assets/models/generate_player.ps1` | Writes the placeholder `player.vox`, following `game/assets/maps/generate_starter.ps1`. |
| `game/assets/models/player.vox` | The committed placeholder model. |
| `game/GameApp/src/GameApp.cpp` | Loads and meshes the model once; draws remote players with it. |

---

### Task 1: One definition of a voxel face

A pure refactor, done first so the new mesher has something to share rather than something
to copy. `ChunkMesher.cpp` currently holds the face table, the six per-face shades, and the
shading maths in its anonymous namespace. A second mesher that duplicated them would drift:
different winding, different shades, a different diagonal split, and two meshes that do not
look like they belong in the same world.

**Files:**
- Create: `Cubit/src/Voxel/VoxelFaces.h`
- Modify: `Cubit/src/Voxel/ChunkMesher.cpp` (delete what moved, include the new header)

**Interfaces:**
- Consumes: nothing.
- Produces, in `namespace VoxelFaces`:
  - `struct Face { glm::ivec3 Normal; glm::vec3 Corner[4]; glm::ivec3 U; glm::ivec3 V; int CornerU[4]; int CornerV[4]; float Shade; };`
  - `constexpr Face All[6]` — the six faces, moved verbatim.
  - `constexpr float AoShade[4]` — moved from `ChunkMesher::AoShade`, which keeps its
    public name by referring to this.
  - `glm::vec4 ShadeVertex(const glm::vec4& blockColor, float faceShade, int ao, float light)`
    — the colour maths lifted out of `AddFace`, floor included.

- [ ] **Step 1: Read the code being moved**

Read `Cubit/src/Voxel/ChunkMesher.cpp` lines 120-260. The pieces are: `TopShade` through
`BottomShade` (126-131), `struct FaceGeometry` (135-148), `constexpr FaceGeometry Faces[6]`
(150-198), and the shading block inside `AddFace` (233-247). Note that `ChunkMesher::AoShade`
and `ChunkMesher::LightFloor` are public constants in the header and are named by
`ChunkMesherTests` — they keep their names and values.

- [ ] **Step 2: Create the shared header**

Create `Cubit/src/Voxel/VoxelFaces.h` with a `#pragma once`, `#include <glm/glm.hpp>`,
`#include "Cubit/Voxel/ChunkMesher.h"`, and a `namespace VoxelFaces`. Move `FaceGeometry`
into it renamed `Face`, move the six shade constants and the `Faces[6]` table in verbatim as
`All[6]`, and add:

```cpp
    //One vertex's colour: the block's colour dimmed by which way the face
    //points, how enclosed this corner is, and how much light reaches it.
    //
    //AoShade and LightFloor stay on ChunkMesher, which is public and which
    //ChunkMesherTests names directly. This header reads them rather than
    //holding a second copy - two tables that must agree are a drift waiting to
    //happen, and the whole point of this file is that there is one of each.
    //
    //Shading scales the colour channels only. Alpha is the block's opacity and
    //has nothing to do with how lit the face is.
    inline glm::vec4 ShadeVertex(const glm::vec4& blockColor, float faceShade, int ao, float light)
    {
        const float lit = faceShade * ChunkMesher::AoShade[ao] * light;
        const glm::vec3 shaded = glm::vec3(blockColor)
            * (ChunkMesher::LightFloor + (1.0f - ChunkMesher::LightFloor) * lit);
        return glm::vec4(shaded, blockColor.a);
    }
```

An internal header including a public one is the allowed direction. The reverse — anything
under `include/` including `VoxelFaces.h` — would export an engine-internal file and must
not happen.

- [ ] **Step 3: Point ChunkMesher at it**

In `ChunkMesher.cpp`: include `"Voxel/VoxelFaces.h"`, delete the moved constants, struct and
table, and replace the shading block in `AddFace` with

```cpp
            mesh.Vertices.push_back(
                { blockOrigin + face.Corner[i],
                  VoxelFaces::ShadeVertex(blockColor, face.Shade, ao[i], light[i]) });
```

`ChunkMesher.h` does not change: `AoShade` and `LightFloor` stay exactly where they are,
with the names and values `ChunkMesherTests` already uses. Only the face table and the
shading maths move.

- [ ] **Step 4: Build and run the suite**

Run: `C:\dev\premake\premake5 vs2026`, then the Debug build.
Expected: green, and `ChunkMesherTests` unchanged — this task adds no test because it adds
no behaviour. The existing mesher tests are the proof that nothing moved wrong.

- [ ] **Step 5: Prove the refactor is load-bearing**

Change `VoxelFaces::TopShade` from `1.00f` to `0.50f`, rebuild, and expect
`ChunkMesherTests` to fail — which shows the chunk mesher really is reading the moved table
rather than a leftover copy the compiler kept alive. Restore, rebuild, green.

- [ ] **Step 6: Run Release and commit**

```bash
git add -A
git commit -m "Give the voxel meshers one definition of a face"
git push origin master
```

---

### Task 2: ModelMesher

**Files:**
- Create: `Cubit/include/Cubit/Voxel/ModelMesher.h`, `Cubit/src/Voxel/ModelMesher.cpp`
- Test: `Tests/src/ModelMesherTests.cpp`

**Interfaces:**
- Consumes: `VoxelFaces::All`, `VoxelFaces::ShadeVertex` from Task 1; `VoxModel` and
  `Palette` from `Cubit/Voxel/VoxLoader.h` and `Cubit/Voxel/Block.h`; `MeshGeometry` and
  `VoxelVertex` from `Cubit/Voxel/ChunkMesher.h`.
- Produces: `MeshGeometry ModelMesher::Build(const VoxModel& model)`.

**Behaviour, stated exactly:**
- A voxel is present when its palette index is non-zero (`IsPresent` in `Block.h`).
- A face is emitted when the neighbour across it is absent. **Outside the model's bounds
  counts as absent**, so the model's outer shell is drawn.
- Vertices are in model voxel units with the model's minimum corner at the origin. The
  caller scales and positions.
- Ambient occlusion is computed from the model's own voxels, by the same corner rule the
  chunk mesher uses.
- Light is `1.0` for every corner: a model is meshed as if fully lit, and how bright it
  actually is comes from the per-draw brightness in Task 3. Passing `1.0` through
  `ShadeVertex` leaves `LightFloor + (1 - LightFloor) * 1.0 = 1.0`, so the floor has no
  effect here — which is correct, and worth a comment saying so.
- The result is a plain `MeshGeometry`, not a `ChunkMeshData`: models are opaque.

- [ ] **Step 1: Write the failing tests**

Create `Tests/src/ModelMesherTests.cpp`:

```cpp
#include <doctest.h>

#include "Cubit/Voxel/ModelMesher.h"
#include "Cubit/Voxel/VoxLoader.h"

#include <cstdint>

namespace
{
    //A model of the given size with every voxel empty, and a palette whose
    //index 1 is opaque white so shading is the only thing changing a colour.
    VoxModel EmptyModel(int sizeX, int sizeY, int sizeZ)
    {
        VoxModel model;
        model.Size = { sizeX, sizeY, sizeZ };
        model.Voxels.assign(
            static_cast<std::size_t>(sizeX) * sizeY * sizeZ, std::uint8_t{ 0 });
        model.Colors[1] = glm::vec4(1.0f);
        return model;
    }

    void Set(VoxModel& model, int x, int y, int z, std::uint8_t index)
    {
        model.Voxels[static_cast<std::size_t>(x) +
            static_cast<std::size_t>(model.Size.x) *
            (static_cast<std::size_t>(y) +
             static_cast<std::size_t>(model.Size.y) * static_cast<std::size_t>(z))] = index;
    }

    //Six indices per face, so the face count is what a reader actually means.
    std::size_t FaceCount(const MeshGeometry& mesh)
    {
        return mesh.Indices.size() / 6;
    }
}

TEST_CASE("A single voxel is meshed as a whole cube")
{
    //Nothing neighbours it and the model's edge counts as open, so every one
    //of its six faces is exposed.
    VoxModel model = EmptyModel(1, 1, 1);
    Set(model, 0, 0, 0, 1);

    const MeshGeometry mesh = ModelMesher::Build(model);

    CHECK(FaceCount(mesh) == 6);
    CHECK(mesh.Vertices.size() == 24);
}

TEST_CASE("A voxel enclosed on all sides emits nothing")
{
    //The centre of a 3x3x3 block of solid voxels: every neighbour is present,
    //so no face of it is exposed. This is the rule that stops a model's
    //interior being meshed, which on a solid model is most of it.
    VoxModel model = EmptyModel(3, 3, 3);
    for (int x = 0; x < 3; ++x)
        for (int y = 0; y < 3; ++y)
            for (int z = 0; z < 3; ++z)
                Set(model, x, y, z, 1);

    const MeshGeometry mesh = ModelMesher::Build(model);

    //The shell is 26 voxels; only their outward faces are drawn. A 3x3x3 cube
    //has six 3x3 sides.
    CHECK(FaceCount(mesh) == 6 * 9);
}

TEST_CASE("Two touching voxels do not mesh the face between them")
{
    //Twelve faces if the shared face were emitted twice, ten when it is not -
    //which is the whole point of meshing only what is exposed.
    VoxModel model = EmptyModel(2, 1, 1);
    Set(model, 0, 0, 0, 1);
    Set(model, 1, 0, 0, 1);

    const MeshGeometry mesh = ModelMesher::Build(model);

    CHECK(FaceCount(mesh) == 10);
}

TEST_CASE("An empty model meshes to nothing rather than failing")
{
    //A model with no voxels is a valid file, and a mesher that cannot survive
    //one turns a content mistake into a crash.
    const MeshGeometry mesh = ModelMesher::Build(EmptyModel(4, 4, 4));

    CHECK(mesh.Vertices.empty());
    CHECK(mesh.Indices.empty());
}

TEST_CASE("An inside corner is darker than an open face")
{
    //Ambient occlusion is what stops a model reading as a flat silhouette. An
    //L of three voxels has one corner enclosed by its two neighbours; the
    //vertex there must come out darker than the same model's open corners.
    VoxModel model = EmptyModel(2, 2, 1);
    Set(model, 0, 0, 0, 1);
    Set(model, 1, 0, 0, 1);
    Set(model, 0, 1, 0, 1);

    const MeshGeometry mesh = ModelMesher::Build(model);

    float darkest = 2.0f;
    float brightest = -1.0f;
    for (const VoxelVertex& vertex : mesh.Vertices)
    {
        darkest = std::min(darkest, vertex.Color.r);
        brightest = std::max(brightest, vertex.Color.r);
    }

    CHECK(darkest < brightest);
}
```

- [ ] **Step 2: Run it and watch it fail**

Run: the Debug build.
Expected: it does not compile — `Cubit/Voxel/ModelMesher.h` does not exist. That is the
failure this step wants; the next two steps make it a passing compile.

- [ ] **Step 3: Write the header**

Create `Cubit/include/Cubit/Voxel/ModelMesher.h`:

```cpp
#pragma once

#include "Cubit/Core.h"
#include "Cubit/Voxel/ChunkMesher.h"

struct VoxModel;

//Meshes a standalone voxel model - a player, a tool, a pickup - into geometry
//the renderer can draw anywhere.
//
//A sibling of ChunkMesher rather than a caller of it: a model has no World, no
//chunk grid and no neighbours across a boundary, and it is lit as an object
//rather than as terrain. What the two share is the face table and the shading,
//which live in VoxelFaces.h so they cannot drift apart.
//
//Vertices come out in model voxel units with the model's minimum corner at the
//origin. Scaling a model to the size its owner should be, and putting it where
//that owner stands, are the caller's business.
class CB_API ModelMesher
{
public:
    ModelMesher() = delete;

    //Builds the model's exposed faces. A voxel outside the model counts as
    //absent, so the outer shell is drawn.
    static MeshGeometry Build(const VoxModel& model);
};
```

- [ ] **Step 4: Write the implementation**

Create `Cubit/src/Voxel/ModelMesher.cpp`. Structure it as: a local `Present(model, x, y, z)`
returning false outside bounds and for palette index 0; a local `CornerAo` mirroring
`ChunkMesher::CornerAoLevel` but reading `Present`; then a triple loop over the model's
voxels emitting, for each present voxel and each of `VoxelFaces::All`, a face when the
neighbour across its normal is absent. Colour each vertex with

```cpp
        //Light is 1.0 because a model is meshed as if fully lit: how bright it
        //actually is depends on where it is standing, which is a per-draw
        //value the scene supplies. At 1.0 the shading floor has no effect,
        //which is the intent - a model is never dimmed at mesh time.
        VoxelFaces::ShadeVertex(color, face.Shade, ao[i], 1.0f)
```

and split the quad along its darker diagonal exactly as `ChunkMesher::AddFaceIndices` does —
`ao[0] + ao[2] > ao[1] + ao[3]`. Keep the winding identical to the chunk mesher's or the
model will be backface-culled inside out.

- [ ] **Step 5: Add the files and run the tests**

Run: `C:\dev\premake\premake5 vs2026`, then the Debug build.
Expected: all five new cases pass, and the engine suite's count rises by five.

- [ ] **Step 6: Prove the tests can fail**

Change `Present` to return `true` outside the model's bounds, rebuild, and expect the single
voxel case to drop to zero faces. Restore, rebuild, green.

- [ ] **Step 7: Run Release and commit**

```bash
git add -A
git commit -m "Mesh a standalone voxel model"
git push origin master
```

---

### Task 3: A mesh the scene can draw

GPU work, so there is no unit test to write: `VertexArray`, `VertexBuffer` and `IndexBuffer`
have none either, for the same reason — constructing one needs a live GL context, which the
test executable has no window for. This task is verified by the build and then by Task 5's
run. Say so in the commit rather than pretending otherwise.

**Files:**
- Create: `Cubit/include/Cubit/Renderer/Mesh.h`, `Cubit/src/Renderer/Mesh.cpp`
- Modify: `Cubit/include/Cubit/Renderer/WorldScene.h`, `Cubit/src/Renderer/WorldScene.cpp`

**Interfaces:**
- Consumes: `MeshGeometry` from Task 2's mesher or the chunk mesher.
- Produces:
  - `Mesh(const MeshGeometry& geometry)`, `bool Mesh::Empty() const`,
    `std::uint32_t Mesh::IndexCount() const`, and internal access for the scene to draw it.
  - `void WorldScene::DrawMesh(const Mesh& mesh, const glm::mat4& transform, float brightness)`.

- [ ] **Step 1: Write the mesh**

`Mesh.h` declares a class holding `std::unique_ptr<VertexArray>`, `std::unique_ptr<VertexBuffer>`
and `std::unique_ptr<IndexBuffer>`, non-copyable and movable, constructed from a
`MeshGeometry`. `Mesh.cpp` uploads exactly as `UploadGeometry` in
`Cubit/src/Renderer/WorldRenderer.cpp:21-38` does — the same
`BufferLayout{ ShaderDataType::Float3, ShaderDataType::Float4 }`, because it is the same
vertex format. An empty geometry produces an empty mesh that draws nothing rather than a
null pointer waiting to be dereferenced.

- [ ] **Step 2: Add the brightness uniform**

In `WorldScene.cpp`, add `uniform float u_Brightness;` to the fragment shader and multiply
the colour by it:

```glsl
            color = vec4(mix(v_Color.rgb * u_Brightness, u_FogColor, f), v_Color.a);
```

Brightness applies before the fog, not after: fog is the colour of the air between the
camera and the surface, and a dark model in fog should fade to the same fog colour a bright
one does. In `WorldScene::Render`, set `u_Brightness` to `1.0f` before drawing chunks, so
the world is unchanged.

- [ ] **Step 3: Add DrawMesh**

```cpp
void WorldScene::DrawMesh(const Mesh& mesh, const glm::mat4& transform, float brightness)
{
    if (mesh.Empty())
        return;

    m_Shader->Bind();
    m_Shader->SetFloat("u_Brightness", brightness);
    Renderer::Submit(mesh.Array(), mesh.Indices(), *m_Shader, transform);
}
```

Declare it in `WorldScene.h` with a comment saying the transform carries the world offset
the same way the chunk draw's does, and that brightness is how lit the thing is where it
stands — the scene does not work that out, because the scene does not know what a model is.
Widen the class comment from "the meshed world" to the voxel scene: the world and the things
standing in it.

- [ ] **Step 4: Build both configurations**

Run: `C:\dev\premake\premake5 vs2026`, the Debug build, then Release.
Expected: green, and the world looks exactly as it did — `u_Brightness` is 1.0 everywhere
until Task 5 passes something else.

- [ ] **Step 5: Check the world really is unchanged**

Run the Sandbox from `bin\Debug-windows-x86_64\Sandbox` and screenshot it (see
`.claude/projects/C--dev-Cubit/memory/screenshot-cubit-gl-window.md`: grab the `GLFW30`
window, not `MainWindowHandle`; allow ~30 s for the mesher; close with `WM_CLOSE`).
Expected: the battlefield as before. A shader edit that dimmed or brightened the world would
show here and nowhere else.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "Let the voxel scene draw a mesh at a transform"
git push origin master
```

---

### Task 4: A placeholder player model

The game has no `player.vox` and nobody has authored one. A generated placeholder keeps this
arc self-contained and gives the mesher something fixed to chew on; a hand-made model is art,
and can replace it later without touching code.

**Files:**
- Create: `game/assets/models/generate_player.ps1`, `game/assets/models/player.vox`

**Interfaces:**
- Consumes: nothing.
- Produces: `game/assets/models/player.vox`, a model whose height in voxels Task 5 reads
  from the file rather than assuming.

- [ ] **Step 1: Write the generator**

Create `game/assets/models/generate_player.ps1`, following
`game/assets/maps/generate_starter.ps1` exactly in shape — the same `Add-Int`/`Add-Tag`
helpers, the same chunk layout, the same vox space (Z up, converted on load). Build a blocky
figure roughly 6 wide, 4 deep and 18 tall: legs, a torso, two arms and a head, in three or
four palette colours. Write it to `player.vox` beside the script.

State the facing in a comment at the top: **the model faces +x**, matching `Heading.h`'s
convention that yaw 0 faces +x and yaw grows toward +z. Put something asymmetric on the
front — a face colour, or arms forward — or nobody can tell which way it is pointing, and a
model that is silently backwards looks exactly like a correct one from behind.

- [ ] **Step 2: Generate it and check it loads**

Run the script, then confirm the engine can read what it wrote:

```bash
git add game/assets/models/player.vox game/assets/models/generate_player.ps1
```

Expected: a `.vox` a few hundred bytes long. `VoxLoaderTests` already covers loading real
files from `game/assets/maps`; this one is proved by Task 5 drawing it.

- [ ] **Step 3: Copy the models directory next to the executables**

`game/premake5.lua` copies `assets` wholesale in each app's `postbuildcommands`, so a new
directory under `game/assets` is already carried. Confirm by building and checking that
`bin\Debug-windows-x86_64\GameApp\assets\models\player.vox` exists. If the copy is
per-directory rather than wholesale, add `models` the way `maps` is added.

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "Add a placeholder player model"
git push origin master
```

---

### Task 5: Remote players are models

**Files:**
- Modify: `game/GameApp/src/GameApp.cpp` (`DrawRemotePlayers` at :526, and the layer's
  construction)

**Interfaces:**
- Consumes: `ModelMesher::Build`, `Mesh`, `WorldScene::DrawMesh`, `SkyLight::Max`,
  `World::GetSkyLight`, `HeadingForward`'s convention, `MatchClient::RemotePose`.
- Produces: nothing other tasks read.

- [ ] **Step 1: Load and mesh the model once**

In the layer that owns the scene, beside where the map is loaded, add members for the model
mesh and the model's height in voxels:

```cpp
    //Loaded once and drawn for every remote player. A missing file throws, the
    //same way a missing map does: it is an asset the game ships, and falling
    //back to wireframes would hide a broken build rather than report it.
    const VoxModel model = VoxLoader::LoadFile("assets/models/player.vox");
    m_PlayerModelHeight = static_cast<float>(model.Size.y);
    m_PlayerMesh = std::make_unique<Mesh>(ModelMesher::Build(model));
```

- [ ] **Step 2: Replace the wireframe draw**

Rewrite `DrawRemotePlayers` (`GameApp.cpp:526-544`):

```cpp
    void DrawRemotePlayers(float alpha)
    {
        if (!m_Client)
            return;

        for (const auto& [player, character] : Match_().Players())
        {
            if (player == m_LocalPlayer)
                continue;

            //From the interpolation ring rather than from the character, which
            //holds whatever the last snapshot said and steps between packets.
            const MatchClient::RemotePose pose = m_Client->PoseOf(player, alpha);
            const glm::vec3 half = character.Config().HalfExtents;

            //The model is meshed in its own voxel units, so it is scaled to the
            //height the simulation says a player is. An artist can rebuild the
            //model at any resolution and it still fits: nothing here knows how
            //many voxels tall it is except by asking the file.
            const float scale = (half.y * 2.0f) / m_PlayerModelHeight;

            //Feet at the bottom of the collision box, centred on it, turned to
            //face where the player faces. The model is authored facing +x, which
            //is what yaw 0 means everywhere else in Cubit (see Heading.h).
            glm::mat4 transform = glm::translate(glm::mat4(1.0f),
                WorldOffset + pose.Position - glm::vec3(0.0f, half.y, 0.0f));
            transform = glm::rotate(transform, glm::radians(-pose.Yaw), glm::vec3(0.0f, 1.0f, 0.0f));
            transform = glm::scale(transform, glm::vec3(scale));
            transform = glm::translate(transform,
                glm::vec3(-0.5f * m_PlayerModelWidth, 0.0f, -0.5f * m_PlayerModelDepth));

            m_Scene.DrawMesh(*m_PlayerMesh, transform, BrightnessAt(pose.Position));
        }
    }
```

Store `m_PlayerModelWidth` and `m_PlayerModelDepth` from `model.Size.x` and `model.Size.z`
alongside the height in Step 1, so the centring reads from the file too.

The rotation's sign is the one thing here that cannot be reasoned out reliably from the
convention alone — glm rotates counter-clockwise about +y while Cubit's yaw grows from +x
toward +z, which is clockwise seen from above. Try `-pose.Yaw` first, and if the model faces
backwards in Step 5's run, use `+pose.Yaw`. Whichever is right, leave a comment saying which
and why, because the next person will have the same doubt.

- [ ] **Step 3: Add the brightness helper**

```cpp
    //How lit a model standing here should be: the world's sky light where its
    //middle is, with a floor so someone in a sealed tunnel is dim rather than
    //invisible. One sample for the whole model - it is a person, not terrain,
    //and re-shading its vertices every frame it moves would cost far more than
    //this is worth.
    float BrightnessAt(const glm::vec3& position) const
    {
        const glm::ivec3 cell = glm::ivec3(glm::floor(position));
        const float light = static_cast<float>(World_().GetSkyLight(cell.x, cell.y, cell.z))
            / static_cast<float>(SkyLight::Max);

        return ChunkMesher::LightFloor + (1.0f - ChunkMesher::LightFloor) * light;
    }
```

- [ ] **Step 4: Build both configurations**

Run: the Debug build, then Release.
Expected: green. Nothing visible changes in single-player, which draws no remote players.

- [ ] **Step 5: Run two clients and look**

Start the server and two connected clients:
`bin\Debug-windows-x86_64\Server\Server.exe --duration 240`, then two
`GameApp.exe --connect 127.0.0.1`. **Synthetic input does not reach the GL window** (see
`.claude/projects/C--dev-Cubit/memory/scripted-input-does-not-reach-gl-window.md`), so this
is a hands-on run: move one client and watch the other.
Expected: each client draws the other as a model standing on the ground, at roughly a
player's height, facing the way it moves. Confirm three things specifically — it is not
sunk into the ground or floating, it turns the right way, and it dims when it walks into a
dug tunnel.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "Draw remote players as models rather than wireframes"
git push origin master
```

---

### Task 6: Document it

**Files:**
- Modify: `README.md` (the rendering list, and what the game looks like)
- Modify: `docs/engine-roadmap.md` (tick B3)

**Interfaces:**
- Consumes: everything above.
- Produces: the record of what landed and what deliberately did not.

- [ ] **Step 1: Update the README**

Add `Mesh` and `ModelMesher` to the rendering and voxel-world lists, and correct the
"What works" paragraph, which currently implies other players are debug boxes. Say that a
model is a `.vox` like a map is, meshed the same way, and lit by one sample of the world's
light where it stands.

- [ ] **Step 2: Tick B3**

Mark B3 done with how: the mechanism, the shared face table, the placeholder model, and the
two things the item named that this did **not** do — team colours and a first-person held
tool. Add them as their own items rather than leaving the tick ambiguous, in the shape B8a
and B8b already use.

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "Write down how models are drawn"
git push origin master
```

---

## Self-Review

**Spec coverage.** `Mesh` — Task 3. `ModelMesher` — Task 2. `WorldScene::DrawMesh` and
`u_Brightness` — Task 3. The asset, scale from the file, placement, facing, brightness, and
throwing on a missing file — Tasks 4 and 5. The five mesher tests — Task 2. Out-of-scope
items are recorded as roadmap items in Task 6. The one thing the spec did not anticipate is
Task 1, the shared face table; it exists because the spec's "a sibling of `ChunkMesher`, not
a caller of it" would otherwise mean copying the face table, which is the kind of
duplication that goes wrong quietly.

**Placeholders.** None: every code step carries the code, and the two judgement calls left
open — the rotation's sign, and whether the asset copy is wholesale — are written as "try
this, and here is how to tell", with the check that settles each.

**Type consistency.** `MeshGeometry`, `VoxelVertex`, `VoxModel`, `Palette`, `BlockId`,
`SkyLight::Max`, `ChunkMesher::LightFloor`, `BufferLayout{Float3, Float4}` and
`Renderer::Submit(va, ib, shader, transform)` are all named as they exist today, checked
against the headers while writing this. `m_PlayerModelHeight`, `m_PlayerModelWidth` and
`m_PlayerModelDepth` are introduced in Task 5 Step 1 and used in Step 2 under those names.
