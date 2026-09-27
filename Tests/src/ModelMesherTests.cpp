#include <doctest.h>

#include "Cubit/Voxel/ModelMesher.h"
#include "Cubit/Voxel/VoxLoader.h"
#include "Cubit/Voxel/ChunkMesher.h"

#include <algorithm>
#include <cstdint>
#include <initializer_list>

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

TEST_CASE("Only the shell of a solid model is meshed")
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

TEST_CASE("Ambient occlusion darkens a corner its neighbours enclose")
{
    //Two models whose observed voxel has exactly the same six faces exposed:
    //the occluders are DIAGONAL to it, so they cover none of its faces and
    //change no face's shade. The only thing that can differ between the two
    //meshes is how enclosed a corner is - which makes this a test of ambient
    //occlusion rather than of the per-face shading that swamped the previous
    //version of it.
    VoxModel open = EmptyModel(3, 3, 3);
    Set(open, 1, 1, 1, 1);

    VoxModel enclosed = EmptyModel(3, 3, 3);
    Set(enclosed, 1, 1, 1, 1);

    //Both sit against the open cell above the observed voxel's top face, one
    //along each of the two axes that span it, so the corner between them is
    //occluded from both sides.
    Set(enclosed, 2, 2, 1, 1);
    Set(enclosed, 1, 2, 2, 1);

    //Shading rides in alpha; rgb is the palette colour, the same on every corner.
    float darkestOpen = 2.0f;
    for (const VoxelVertex& vertex : ModelMesher::Build(open).Vertices)
        darkestOpen = std::min(darkestOpen, vertex.Color.a);

    float darkestEnclosed = 2.0f;
    for (const VoxelVertex& vertex : ModelMesher::Build(enclosed).Vertices)
        darkestEnclosed = std::min(darkestEnclosed, vertex.Color.a);

    CHECK(darkestEnclosed < darkestOpen);
}

TEST_CASE("A model vertex carries its palette colour in rgb and its raw shading in alpha")
{
    //The scene applies the light floor AFTER multiplying by how lit the model's
    //spot is, which it can only do if the colour and the shading arrive apart.
    //A floor baked in here as well would be the second floor B3c removed.
    VoxModel model = EmptyModel(1, 1, 1);
    model.Colors[1] = glm::vec4(0.8f, 0.4f, 0.2f, 1.0f);
    Set(model, 0, 0, 0, 1);

    float brightest = 0.0f;
    float darkest = 2.0f;

    for (const VoxelVertex& vertex : ModelMesher::Build(model).Vertices)
    {
        CHECK(glm::vec3(vertex.Color) == glm::vec3(0.8f, 0.4f, 0.2f));
        brightest = std::max(brightest, vertex.Color.a);
        darkest = std::min(darkest, vertex.Color.a);
    }

    //A lone voxel's top face is open on every corner and faces the sky.
    CHECK(brightest == 1.0f);

    //Raw, not compressed into [LightFloor, 1]: the darkest face of a lone
    //voxel is its bottom, whose face shade is 0.60 with every corner open.
    //Floored, it would read 0.15 + 0.85 * 0.60 = 0.66.
    CHECK(darkest == doctest::Approx(0.60f * ChunkMesher::AoShade[3]));
}

TEST_CASE("Lit by the scene, a model is floored once, exactly as a chunk face is")
{
    //What WorldScene's shader does with a model vertex: floor the product of
    //the baked shading and the light where the model stands. Spelled out here
    //as the contract, because GLSL cannot be called from a test.
    auto lit = [](const VoxelVertex& vertex, float light)
    {
        return glm::vec3(vertex.Color) *
            (ChunkMesher::LightFloor + (1.0f - ChunkMesher::LightFloor) * vertex.Color.a * light);
    };

    VoxModel model = EmptyModel(3, 3, 3);
    Set(model, 1, 1, 1, 1);
    Set(model, 2, 2, 1, 1);
    Set(model, 1, 2, 2, 1);

    const MeshGeometry mesh = ModelMesher::Build(model);

    //In a sealed tunnel every face - open front, occluded bottom - comes out at
    //exactly the floor a chunk face beside it is guaranteed. Before B3c the two
    //floors multiplied, and these came out at 0.132 and 0.065.
    for (const VoxelVertex& vertex : mesh.Vertices)
        CHECK(lit(vertex, 0.0f).r == doctest::Approx(ChunkMesher::LightFloor));

    //In full light nothing is lost: a model vertex comes out at the same
    //value the old bake did, which is what a chunk vertex with this shading
    //and full light would be.
    for (const VoxelVertex& vertex : mesh.Vertices)
        CHECK(lit(vertex, 1.0f).r == doctest::Approx(
            ChunkMesher::LightFloor + (1.0f - ChunkMesher::LightFloor) * vertex.Color.a));

    //And light in between never drops a face below the floor.
    for (float light : { 0.1f, 0.5f, 0.9f })
        for (const VoxelVertex& vertex : mesh.Vertices)
            CHECK(lit(vertex, light).r >= ChunkMesher::LightFloor - 1e-6f);
}
