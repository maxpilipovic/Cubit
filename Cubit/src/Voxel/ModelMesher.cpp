#include "cub.h"

#include "Cubit/Voxel/ModelMesher.h"

#include "Cubit/Voxel/Block.h"
#include "Cubit/Voxel/VoxLoader.h"

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

    //What VoxelFaces::CornerAo needs from a model. A model has no notion of a
    //see-through block, so anything present occludes: presence is opacity here,
    //and that single difference is the whole reason this adapter exists rather
    //than a second copy of the occlusion rule.
    struct ModelCells
    {
        const VoxModel& Model;

        bool IsOpaque(const glm::ivec3& cell) const
        {
            return Present(Model, cell.x, cell.y, cell.z);
        }
    };

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

            ao[i] = VoxelFaces::CornerAo(ModelCells{ model }, openCell, sideA, sideB);
        }

        for (int i = 0; i < 4; ++i)
        {
            //Light is 1.0 because a model is meshed as if fully lit: how bright
            //it actually is depends on where it is standing, which is a per-draw
            //value the scene supplies.
            //
            //The shading floor still applies at 1.0 - it is a floor on the
            //finished shading, not on light. What it bakes is face shade times
            //AO compressed into [LightFloor, 1]: only a fully open top face,
            //whose shade and AO are both 1.0, comes out unchanged, while a fully
            //open front face's 0.86 comes out as 0.15 + 0.85 * 0.86 = 0.881. The
            //per-draw brightness then scales that baked result.
            mesh.Vertices.push_back(
                { glm::vec3(cell) + face.Corner[i],
                  VoxelFaces::ShadeVertex(color, face.Shade, ao[i], 1.0f) });
        }

        VoxelFaces::AddFaceIndices(mesh, ao);
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
