#include "cub.h"

#include "Cubit/Voxel/ChunkMesher.h"

#include "Cubit/Voxel/Chunk.h"
#include "Cubit/Voxel/SkyLight.h"
#include "Cubit/Voxel/World.h"

#include "Core/CoreLogger.h"

#include <array>

namespace
{
    //A chunk plus the one-block shell around it, copied out of the world once
    //per mesh. Occlusion and light sampling reach exactly one cell past a block
    //on each axis, so one block of shell is enough to mesh without touching the
    //world again — which matters because resolving a world position to a chunk
    //and an offset, tens of thousands of times a chunk, was most of what
    //meshing cost.
    //
    //Reads outside the world go through World on the way in, so the shell keeps
    //its answers: air, and open sky.
    class Neighbourhood
    {
    public:
        static constexpr int Shell = 1;
        static constexpr int SpanX = Chunk::Width + 2 * Shell;
        static constexpr int SpanY = Chunk::Height + 2 * Shell;
        static constexpr int SpanZ = Chunk::Depth + 2 * Shell;
        static constexpr int Count = SpanX * SpanY * SpanZ;

        Neighbourhood(const World& world, const glm::ivec3& chunkOrigin)
        {
            //The middle of the neighbourhood is exactly one chunk, which can be
            //read by local coordinates; only the shell has to go through the
            //world and pay for the conversion.
            const Chunk& chunk = world.GetChunk(
                chunkOrigin.x / Chunk::Width,
                chunkOrigin.y / Chunk::Height,
                chunkOrigin.z / Chunk::Depth);

            const glm::ivec3 base = chunkOrigin - glm::ivec3(Shell);
            int index = 0;

            for (int z = 0; z < SpanZ; ++z)
            {
                const bool insideZ = z >= Shell && z < Shell + Chunk::Depth;

                for (int y = 0; y < SpanY; ++y)
                {
                    const bool insideY =
                        insideZ && y >= Shell && y < Shell + Chunk::Height;

                    for (int x = 0; x < SpanX; ++x, ++index)
                    {
                        if (insideY && x >= Shell && x < Shell + Chunk::Width)
                        {
                            m_Blocks[index] = chunk.GetBlock(
                                x - Shell, y - Shell, z - Shell);
                            m_Light[index] = chunk.GetSkyLight(
                                x - Shell, y - Shell, z - Shell);
                            continue;
                        }

                        const glm::ivec3 cell = base + glm::ivec3(x, y, z);
                        m_Blocks[index] =
                            world.GetBlock(cell.x, cell.y, cell.z);
                        m_Light[index] =
                            world.GetSkyLight(cell.x, cell.y, cell.z);
                    }
                }
            }
        }

        //The flat index of a block at chunk-local coordinates.
        static int At(int localX, int localY, int localZ)
        {
            return (localX + Shell) +
                SpanX * ((localY + Shell) + SpanY * (localZ + Shell));
        }

        //The flat offset matching a step of v. Linear in v, so scaling a face's
        //tangent axis scales its offset the same way — which is what lets a
        //corner be addressed by adding two integers.
        static int Step(const glm::ivec3& v)
        {
            return v.x + SpanX * v.y + SpanX * SpanY * v.z;
        }

        BlockId Block(int cell) const { return m_Blocks[cell]; }
        bool IsSolid(int cell) const { return ::IsSolid(m_Blocks[cell]); }
        int Light(int cell) const { return m_Light[cell]; }

    private:
        //Raw arrays rather than std::array: meshing subscripts these tens of
        //thousands of times per chunk, and a debug build checks every
        //std::array subscript. Every index comes from At plus a Step, so it
        //stays inside the shell by construction.
        BlockId m_Blocks[Count];
        std::uint8_t m_Light[Count];
    };

    //Reads straight from the world, for the callers that hand one over rather
    //than meshing a whole chunk.
    struct WorldCells
    {
        const World& Cells;

        bool IsSolid(const glm::ivec3& cell) const
        {
            return Cells.IsBlockSolid(cell.x, cell.y, cell.z);
        }

        int Light(const glm::ivec3& cell) const
        {
            return Cells.GetSkyLight(cell.x, cell.y, cell.z);
        }
    };

    //How exposed one corner is. Written against anything that can answer
    //IsSolid and Light, so the chunk cache and a bare world share one rule
    //rather than drifting apart as two copies. A cell is whatever that source
    //addresses cells by — a flat index for the cache, a position for the world
    //— and a side is a step in the same terms.
    template <typename Cells, typename Cell, typename Side>
    int CornerAo(const Cells& cells, const Cell& airCell,
        const Side& sideA, const Side& sideB)
    {
        const bool solidA = cells.IsSolid(airCell + sideA);
        const bool solidB = cells.IsSolid(airCell + sideB);

        // Two walls meeting at a right angle seal the corner completely, so what
        // sits diagonally behind them cannot lighten it.
        if (solidA && solidB)
            return 0;

        const bool solidCorner = cells.IsSolid(airCell + sideA + sideB);

        return 3
            - static_cast<int>(solidA)
            - static_cast<int>(solidB)
            - static_cast<int>(solidCorner);
    }

    //The light sitting at one corner: the mean of the open cells touching it.
    template <typename Cells, typename Cell, typename Side>
    float CornerLight(const Cells& cells, const Cell& airCell,
        const Side& sideA, const Side& sideB)
    {
        const Cell corners[4] =
        {
            airCell,
            airCell + sideA,
            airCell + sideB,
            airCell + sideA + sideB,
        };

        int total = 0;
        int counted = 0;

        for (const Cell& cell : corners)
        {
            if (cells.IsSolid(cell))
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
    void AddFaceIndices(ChunkMeshData& mesh, bool flip)
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
        ChunkMeshData& mesh,
        const Neighbourhood& cells,
        int blockCell,
        const glm::vec3& blockOrigin,
        const FaceGeometry& face,
        const FaceSteps& steps,
        const glm::vec3& blockColor)
    {
        const int airCell = blockCell + steps.Normal;

        int ao[4];
        float light[4];
        for (int i = 0; i < 4; ++i)
        {
            const int sideA = steps.U * face.CornerU[i];
            const int sideB = steps.V * face.CornerV[i];

            ao[i] = CornerAo(cells, airCell, sideA, sideB);
            light[i] = CornerLight(cells, airCell, sideA, sideB);
        }

        for (int i = 0; i < 4; ++i)
        {
            const float lit = face.Shade * ChunkMesher::AoShade[ao[i]] * light[i];

            // The floor applies to the finished shading, not to light alone:
            // the three factors multiply, so flooring only the light term still
            // lets an occluded ceiling underside reach near-black.
            const glm::vec3 color = blockColor
                * (ChunkMesher::LightFloor
                    + (1.0f - ChunkMesher::LightFloor) * lit);

            mesh.Vertices.push_back({ blockOrigin + face.Corner[i], color });
        }

        // Splitting a quad along its darker diagonal keeps the shading gradient
        // smooth; splitting the other way leaves a visible seam across it.
        AddFaceIndices(mesh, ao[0] + ao[2] > ao[1] + ao[3]);
    }

    //Emits the faces of one block that are exposed to air. Neighbours are looked
    //up in world coordinates so blocks in the next chunk are visible, while the
    //vertices use chunk-local coordinates.
    void AddExposedFaces(
        ChunkMeshData& mesh,
        const Neighbourhood& cells,
        const Palette& palette,
        const FaceSteps (&steps)[6],
        int blockCell,
        const glm::vec3& blockOrigin)
    {
        const glm::vec3 color = palette[cells.Block(blockCell)];

        for (int f = 0; f < 6; ++f)
        {
            if (!cells.IsSolid(blockCell + steps[f].Normal))
                AddFace(mesh, cells, blockCell, blockOrigin,
                    Faces[f], steps[f], color);
        }
    }
}

ChunkMeshData ChunkMesher::Build(const World& world, int chunkX, int chunkY, int chunkZ)
{
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
                if (!cells.IsSolid(cell))
                    continue;

                // Vertices are chunk-local, so the loop counters are already
                // the block's origin.
                AddExposedFaces(mesh, cells, palette, steps, cell,
                    glm::vec3(x, y, z));
            }
        }
    }

    return mesh;
}

int ChunkMesher::CornerAoLevel(
    const World& world,
    const glm::ivec3& airCell,
    const glm::ivec3& sideA,
    const glm::ivec3& sideB)
{
    return CornerAo(WorldCells{ world }, airCell, sideA, sideB);
}

float ChunkMesher::CornerLightShade(
    const World& world,
    const glm::ivec3& airCell,
    const glm::ivec3& sideA,
    const glm::ivec3& sideB)
{
    return CornerLight(WorldCells{ world }, airCell, sideA, sideB);
}
