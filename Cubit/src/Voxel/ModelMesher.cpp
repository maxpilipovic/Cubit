#include "cub.h"

#include "Cubit/Voxel/ModelMesher.h"

#include "Cubit/Voxel/Block.h"
#include "Cubit/Voxel/VoxLoader.h"

#include "Core/CoreLogger.h"
#include "Voxel/VoxelFaces.h"

namespace
{
    //Whether a voxel is there to occlude a neighbour or be drawn itself.
    //Outside the model's bounds is absent rather than an error, which is what
    //makes the model's outer shell come out as faces instead of a bounds
    //check the caller has to do first.
    bool Present(const VoxModel& model, int x, int y, int z)
    {
        if (x < 0 || y < 0 || z < 0 ||
            x >= model.Size.x || y >= model.Size.y || z >= model.Size.z)
            return false;

        return IsPresent(model.At(x, y, z));
    }

    //Mirrors ChunkMesher's CornerAoLevel, but against a model's own voxels
    //rather than a world: two walls meeting at a right angle seal the corner,
    //so what sits diagonally behind them cannot lighten it.
    int CornerAo(const VoxModel& model, const glm::ivec3& openCell,
        const glm::ivec3& sideA, const glm::ivec3& sideB)
    {
        const glm::ivec3 cellA = openCell + sideA;
        const glm::ivec3 cellB = openCell + sideB;

        const bool presentA = Present(model, cellA.x, cellA.y, cellA.z);
        const bool presentB = Present(model, cellB.x, cellB.y, cellB.z);

        if (presentA && presentB)
            return 0;

        const glm::ivec3 cellCorner = openCell + sideA + sideB;
        const bool presentCorner =
            Present(model, cellCorner.x, cellCorner.y, cellCorner.z);

        return 3
            - static_cast<int>(presentA)
            - static_cast<int>(presentB)
            - static_cast<int>(presentCorner);
    }

    //Adds two triangles referencing the four vertices most recently appended,
    //split along whichever diagonal is darker so the shading gradient stays
    //smooth instead of seaming across the quad.
    void AddFaceIndices(MeshGeometry& mesh, bool flip)
    {
        CB_CORE_ASSERT(
            mesh.Vertices.size() >= 4,
            "A face must append its four vertices before its indices");

        const std::uint32_t firstVertex =
            static_cast<std::uint32_t>(mesh.Vertices.size()) - 4;

        if (flip)
        {
            mesh.Indices.push_back(firstVertex + 1);
            mesh.Indices.push_back(firstVertex + 2);
            mesh.Indices.push_back(firstVertex + 3);
            mesh.Indices.push_back(firstVertex + 3);
            mesh.Indices.push_back(firstVertex + 0);
            mesh.Indices.push_back(firstVertex + 1);
        }
        else
        {
            mesh.Indices.push_back(firstVertex + 0);
            mesh.Indices.push_back(firstVertex + 1);
            mesh.Indices.push_back(firstVertex + 2);
            mesh.Indices.push_back(firstVertex + 2);
            mesh.Indices.push_back(firstVertex + 3);
            mesh.Indices.push_back(firstVertex + 0);
        }
    }

    //Emits one face of one voxel: four vertices shaded by their own corner
    //occlusion, then the two triangles joining them.
    void AddFace(
        MeshGeometry& mesh,
        const VoxModel& model,
        const glm::ivec3& cell,
        const VoxelFaces::Face& face,
        const glm::vec4& color)
    {
        const glm::ivec3 openCell = cell + face.Normal;

        int ao[4];
        for (int i = 0; i < 4; ++i)
        {
            const glm::ivec3 sideA = face.U * face.CornerU[i];
            const glm::ivec3 sideB = face.V * face.CornerV[i];

            ao[i] = CornerAo(model, openCell, sideA, sideB);
        }

        for (int i = 0; i < 4; ++i)
        {
            //Light is 1.0 because a model is meshed as if fully lit: how bright it
            //actually is depends on where it is standing, which is a per-draw
            //value the scene supplies. At 1.0 the shading floor has no effect,
            //which is the intent - a model is never dimmed at mesh time.
            mesh.Vertices.push_back(
                { glm::vec3(cell) + face.Corner[i],
                  VoxelFaces::ShadeVertex(color, face.Shade, ao[i], 1.0f) });
        }

        AddFaceIndices(mesh, ao[0] + ao[2] > ao[1] + ao[3]);
    }
}

MeshGeometry ModelMesher::Build(const VoxModel& model)
{
    MeshGeometry mesh;

    for (int z = 0; z < model.Size.z; ++z)
    {
        for (int y = 0; y < model.Size.y; ++y)
        {
            for (int x = 0; x < model.Size.x; ++x)
            {
                if (!Present(model, x, y, z))
                    continue;

                const glm::ivec3 cell(x, y, z);
                const glm::vec4 color = model.Colors[model.At(x, y, z)];

                for (const VoxelFaces::Face& face : VoxelFaces::All)
                {
                    const glm::ivec3 neighbour = cell + face.Normal;
                    if (Present(model, neighbour.x, neighbour.y, neighbour.z))
                        continue;

                    AddFace(mesh, model, cell, face, color);
                }
            }
        }
    }

    return mesh;
}
