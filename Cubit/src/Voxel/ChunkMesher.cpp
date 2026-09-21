#include "cub.h"

#include "Cubit/Voxel/ChunkMesher.h"

#include "Cubit/Voxel/Chunk.h"
#include "Cubit/Voxel/SkyLight.h"
#include "Cubit/Voxel/World.h"
#include "Cubit/Profiler.h"

#include "Voxel/Neighbourhood.h"
#include "Voxel/VoxelFaces.h"

#include <array>

namespace
{
    //Reads straight from the world, for the callers that hand one over rather
    //than meshing a whole chunk.
    struct WorldCells
    {
        const World& Cells;

        bool IsOpaque(const glm::ivec3& cell) const
        {
            return Cells.IsBlockOpaque(cell.x, cell.y, cell.z);
        }

        int Light(const glm::ivec3& cell) const
        {
            return Cells.GetSkyLight(cell.x, cell.y, cell.z);
        }
    };

    //The light sitting at one corner: the mean of the open cells touching it.
    template <typename Cells, typename Cell, typename Side>
    float CornerLight(const Cells& cells, const Cell& openCell,
        const Side& sideA, const Side& sideB)
    {
        const Cell corners[4] =
        {
            openCell,
            openCell + sideA,
            openCell + sideB,
            openCell + sideA + sideB,
        };

        int total = 0;
        int counted = 0;

        for (const Cell& cell : corners)
        {
            if (cells.IsOpaque(cell))
                continue;

            total += cells.Light(cell);
            ++counted;
        }

        // A corner boxed in on every side has nowhere for light to sit; it is
        // not sampled by any visible face, but guard the division anyway.
        if (counted == 0)
            return 0.0f;

        return static_cast<float>(total) /
            (static_cast<float>(counted) * SkyLight::Max);
    }

    //Emits one face: four vertices shaded by their own corner occlusion, then
    //the two triangles joining them.
    //A face's normal and tangent axes as flat neighbourhood offsets, worked out
    //once per mesh instead of per block.
    struct FaceSteps
    {
        int Normal;
        int U;
        int V;
    };


    void AddFace(
        MeshGeometry& mesh,
        const Neighbourhood& cells,
        int blockCell,
        const glm::vec3& blockOrigin,
        const VoxelFaces::Face& face,
        const FaceSteps& steps,
        const glm::vec4& blockColor)
    {
        const int openCell = blockCell + steps.Normal;

        int ao[4];
        float light[4];
        for (int i = 0; i < 4; ++i)
        {
            const int sideA = steps.U * face.CornerU[i];
            const int sideB = steps.V * face.CornerV[i];

            ao[i] = VoxelFaces::CornerAo(cells, openCell, sideA, sideB);
            light[i] = CornerLight(cells, openCell, sideA, sideB);
        }

        for (int i = 0; i < 4; ++i)
        {
            mesh.Vertices.push_back(
                { blockOrigin + face.Corner[i],
                  VoxelFaces::ShadeVertex(blockColor, face.Shade, ao[i], light[i]) });
        }

        VoxelFaces::AddFaceIndices(mesh, ao);
    }

    //Emits the faces of one block that are exposed to air. Neighbours are looked
    //up in world coordinates so blocks in the next chunk are visible, while the
    //vertices use chunk-local coordinates.
    void AddExposedFaces(
        MeshGeometry& mesh,
        const Neighbourhood& cells,
        const Palette& palette,
        const FaceSteps (&steps)[6],
        int blockCell,
        const glm::vec3& blockOrigin)
    {
        const glm::vec4 color = palette[cells.Block(blockCell)];

        const BlockId self = cells.Block(blockCell);

        for (int f = 0; f < 6; ++f)
        {
            const int neighbourCell = blockCell + steps[f].Normal;

            // A face is worth drawing when what is beyond it does not hide it,
            // and is not more of the same block: two water cells meet at a face
            // that would only blend against itself.
            if (cells.IsOpaque(neighbourCell) ||
                cells.Block(neighbourCell) == self)
                continue;

            AddFace(mesh, cells, blockCell, blockOrigin,
                VoxelFaces::All[f], steps[f], color);
        }
    }
}

ChunkMeshData ChunkMesher::Build(const World& world, int chunkX, int chunkY, int chunkZ)
{
    CB_PROFILE_SCOPE("ChunkMesher::Build");

    ChunkMeshData mesh;
    const glm::ivec3 origin = World::GetChunkOrigin(chunkX, chunkY, chunkZ);

    const Neighbourhood cells(world, origin);
    const Palette& palette = world.GetPalette();

    FaceSteps steps[6];
    for (int f = 0; f < 6; ++f)
        steps[f] = {
            Neighbourhood::Step(VoxelFaces::All[f].Normal),
            Neighbourhood::Step(VoxelFaces::All[f].U),
            Neighbourhood::Step(VoxelFaces::All[f].V) };

    for (int z = 0; z < Chunk::Depth; ++z)
    {
        for (int y = 0; y < Chunk::Height; ++y)
        {
            for (int x = 0; x < Chunk::Width; ++x)
            {
                const int cell = Neighbourhood::At(x, y, z);
                if (!cells.IsPresent(cell))
                    continue;

                // A block's own opacity decides which pass draws it; the faces
                // of one block never span both.
                MeshGeometry& target = cells.IsOpaque(cell)
                    ? mesh.Opaque
                    : mesh.Transparent;

                // Vertices are chunk-local, so the loop counters are already
                // the block's origin.
                AddExposedFaces(target, cells, palette, steps, cell,
                    glm::vec3(x, y, z));
            }
        }
    }

    return mesh;
}

int ChunkMesher::CornerAoLevel(
    const World& world,
    const glm::ivec3& openCell,
    const glm::ivec3& sideA,
    const glm::ivec3& sideB)
{
    return VoxelFaces::CornerAo(WorldCells{ world }, openCell, sideA, sideB);
}

float ChunkMesher::CornerLightShade(
    const World& world,
    const glm::ivec3& openCell,
    const glm::ivec3& sideA,
    const glm::ivec3& sideB)
{
    return CornerLight(WorldCells{ world }, openCell, sideA, sideB);
}
