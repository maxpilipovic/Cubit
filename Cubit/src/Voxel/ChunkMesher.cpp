#include "cub.h"

#include "Cubit/Voxel/ChunkMesher.h"

#include "Cubit/Voxel/Chunk.h"
#include "Cubit/Voxel/SkyLight.h"
#include "Cubit/Voxel/World.h"

#include "Core/CoreLogger.h"

namespace
{
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
    void AddFace(
        ChunkMeshData& mesh,
        const World& world,
        const glm::ivec3& worldPosition,
        const glm::vec3& blockOrigin,
        const FaceGeometry& face,
        const glm::vec3& blockColor)
    {
        const glm::ivec3 airCell = worldPosition + face.Normal;

        int ao[4];
        float light[4];
        for (int i = 0; i < 4; ++i)
        {
            const glm::ivec3 sideA = face.U * face.CornerU[i];
            const glm::ivec3 sideB = face.V * face.CornerV[i];

            ao[i] = ChunkMesher::CornerAoLevel(world, airCell, sideA, sideB);
            light[i] =
                ChunkMesher::CornerLightShade(world, airCell, sideA, sideB);
        }

        for (int i = 0; i < 4; ++i)
        {
            const glm::vec3 color = blockColor
                * face.Shade
                * ChunkMesher::AoShade[ao[i]]
                * light[i];

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
        const World& world,
        const glm::ivec3& worldPosition,
        const glm::ivec3& localPosition)
    {
        const glm::vec3 blockOrigin(localPosition);
        const glm::vec3 color = world.GetBlockColor(
            world.GetBlock(worldPosition.x, worldPosition.y, worldPosition.z));

        for (const FaceGeometry& face : Faces)
        {
            const glm::ivec3 neighbour = worldPosition + face.Normal;

            if (!world.IsBlockSolid(neighbour.x, neighbour.y, neighbour.z))
                AddFace(mesh, world, worldPosition, blockOrigin, face, color);
        }
    }
}

ChunkMeshData ChunkMesher::Build(const World& world, int chunkX, int chunkY, int chunkZ)
{
    ChunkMeshData mesh;
    const glm::ivec3 origin = World::GetChunkOrigin(chunkX, chunkY, chunkZ);

    for (int z = 0; z < Chunk::Depth; ++z)
    {
        for (int y = 0; y < Chunk::Height; ++y)
        {
            for (int x = 0; x < Chunk::Width; ++x)
            {
                const glm::ivec3 local(x, y, z);
                const glm::ivec3 position = origin + local;

                if (world.IsBlockSolid(position.x, position.y, position.z))
                    AddExposedFaces(mesh, world, position, local);
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
    const glm::ivec3 a = airCell + sideA;
    const glm::ivec3 b = airCell + sideB;

    const bool solidA = world.IsBlockSolid(a.x, a.y, a.z);
    const bool solidB = world.IsBlockSolid(b.x, b.y, b.z);

    // Two walls meeting at a right angle seal the corner completely, so what
    // sits diagonally behind them cannot lighten it.
    if (solidA && solidB)
        return 0;

    const glm::ivec3 c = airCell + sideA + sideB;
    const bool solidCorner = world.IsBlockSolid(c.x, c.y, c.z);

    return 3
        - static_cast<int>(solidA)
        - static_cast<int>(solidB)
        - static_cast<int>(solidCorner);
}

float ChunkMesher::CornerLightShade(
    const World& world,
    const glm::ivec3& airCell,
    const glm::ivec3& sideA,
    const glm::ivec3& sideB)
{
    const glm::ivec3 cells[4] =
    {
        airCell,
        airCell + sideA,
        airCell + sideB,
        airCell + sideA + sideB,
    };

    int total = 0;
    int counted = 0;

    for (const glm::ivec3& cell : cells)
    {
        if (world.IsBlockSolid(cell.x, cell.y, cell.z))
            continue;

        total += world.GetSkyLight(cell.x, cell.y, cell.z);
        ++counted;
    }

    // A corner boxed in on every side has nowhere for light to sit; it is not
    // sampled by any visible face, but guard the division anyway.
    if (counted == 0)
        return LightFloor;

    const float average =
        static_cast<float>(total) / (static_cast<float>(counted) * SkyLight::Max);

    return LightFloor + (1.0f - LightFloor) * average;
}
