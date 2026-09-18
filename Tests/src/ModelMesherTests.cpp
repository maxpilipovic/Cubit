#include <doctest.h>

#include "Cubit/Voxel/ModelMesher.h"
#include "Cubit/Voxel/VoxLoader.h"

#include <algorithm>
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
