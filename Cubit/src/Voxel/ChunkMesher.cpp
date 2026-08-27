#include "cub.h"

#include "Cubit/Voxel/ChunkMesher.h"

#include "Cubit/Voxel/Chunk.h"
#include "Cubit/Voxel/SkyLight.h"
#include "Cubit/Voxel/World.h"
#include "Cubit/Profiler.h"

#include "Core/CoreLogger.h"
#include "Voxel/Neighbourhood.h"

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

    //How exposed one corner is. Written against anything that can answer
    //IsOpaque and Light, so the chunk cache and a bare world share one rule
    //rather than drifting apart as two copies. A cell is whatever that source
    //addresses cells by — a flat index for the cache, a position for the world
    //— and a side is a step in the same terms.
    template <typename Cells, typename Cell, typename Side>
    int CornerAo(const Cells& cells, const Cell& openCell,
        const Side& sideA, const Side& sideB)
    {
        const bool opaqueA = cells.IsOpaque(openCell + sideA);
        const bool opaqueB = cells.IsOpaque(openCell + sideB);

        // Two walls meeting at a right angle seal the corner completely, so what
        // sits diagonally behind them cannot lighten it.
        if (opaqueA && opaqueB)
            return 0;

        const bool opaqueCorner = cells.IsOpaque(openCell + sideA + sideB);

        return 3
            - static_cast<int>(opaqueA)
            - static_cast<int>(opaqueB)
            - static_cast<int>(opaqueCorner);
    }

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

    //Adds two triangles referencing the four vertices most recently appended.
    //A quad can be split along either diagonal; flipping picks the other one.
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

    //Per-face brightness, so a solid-coloured block still reads as a cube.
    //Roughly the shading a single overhead light would give.
    constexpr float TopShade = 1.00f;
    constexpr float RightShade = 0.92f;
    constexpr float FrontShade = 0.86f;
    constexpr float LeftShade = 0.80f;
    constexpr float BackShade = 0.72f;
    constexpr float BottomShade = 0.60f;

    //One block face, described rather than hand-written. Corner holds the four
    //vertex offsets from the block's minimum corner, in the winding order the
    //face is emitted in. U and V are the two axes spanning the face, and
    //CornerU/CornerV give each vertex's sign along them — which is what lets a
    //corner's two occluding neighbours be found without a switch per face.
    struct FaceGeometry
    {
        glm::ivec3 Normal;
        glm::vec3 Corner[4];
        glm::ivec3 U;
        glm::ivec3 V;
        int CornerU[4];
        int CornerV[4];
        float Shade;
    };

    constexpr FaceGeometry Faces[6] =
    {
        // Front (+Z)
        { {  0,  0,  1 },
          { { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 1.0f },
            { 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f, 1.0f } },
          { 1, 0, 0 }, { 0, 1, 0 },
          { -1, +1, +1, -1 }, { -1, -1, +1, +1 },
          FrontShade },

        // Back (-Z)
        { {  0,  0, -1 },
          { { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f },
            { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } },
          { 1, 0, 0 }, { 0, 1, 0 },
          { +1, -1, -1, +1 }, { -1, -1, +1, +1 },
          BackShade },

        // Right (+X)
        { {  1,  0,  0 },
          { { 1.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f },
            { 1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } },
          { 0, 0, 1 }, { 0, 1, 0 },
          { +1, -1, -1, +1 }, { -1, -1, +1, +1 },
          RightShade },

        // Left (-X)
        { { -1,  0,  0 },
          { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f },
            { 0.0f, 1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
          { 0, 0, 1 }, { 0, 1, 0 },
          { -1, +1, +1, -1 }, { -1, -1, +1, +1 },
          LeftShade },

        // Top (+Y)
        { {  0,  1,  0 },
          { { 0.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f },
            { 1.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
          { 1, 0, 0 }, { 0, 0, 1 },
          { -1, +1, +1, -1 }, { +1, +1, -1, -1 },
          TopShade },

        // Bottom (-Y)
        { {  0, -1,  0 },
          { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f },
            { 1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },
          { 1, 0, 0 }, { 0, 0, 1 },
          { -1, +1, +1, -1 }, { -1, -1, +1, +1 },
          BottomShade },
    };

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
        const FaceGeometry& face,
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

            ao[i] = CornerAo(cells, openCell, sideA, sideB);
            light[i] = CornerLight(cells, openCell, sideA, sideB);
        }

        for (int i = 0; i < 4; ++i)
        {
            const float lit = face.Shade * ChunkMesher::AoShade[ao[i]] * light[i];

            // The floor applies to the finished shading, not to light alone:
            // the three factors multiply, so flooring only the light term still
            // lets an occluded ceiling underside reach near-black.
            //
            // Shading scales the colour channels only. Alpha is the block's
            // opacity and has nothing to do with how lit the face is.
            const glm::vec3 shaded = glm::vec3(blockColor)
                * (ChunkMesher::LightFloor
                    + (1.0f - ChunkMesher::LightFloor) * lit);

            mesh.Vertices.push_back(
                { blockOrigin + face.Corner[i], glm::vec4(shaded, blockColor.a) });
        }

        // Splitting a quad along its darker diagonal keeps the shading gradient
        // smooth; splitting the other way leaves a visible seam across it.
        AddFaceIndices(mesh, ao[0] + ao[2] > ao[1] + ao[3]);
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
                Faces[f], steps[f], color);
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
            Neighbourhood::Step(Faces[f].Normal),
            Neighbourhood::Step(Faces[f].U),
            Neighbourhood::Step(Faces[f].V) };

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
    return CornerAo(WorldCells{ world }, openCell, sideA, sideB);
}

float ChunkMesher::CornerLightShade(
    const World& world,
    const glm::ivec3& openCell,
    const glm::ivec3& sideA,
    const glm::ivec3& sideB)
{
    return CornerLight(WorldCells{ world }, openCell, sideA, sideB);
}
