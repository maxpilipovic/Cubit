#include "cub.h"

#include "Cubit/Voxel/ChunkMesher.h"

#include "Cubit/Voxel/Chunk.h"

#include "Core/CoreLogger.h"

namespace
{
    //Adds two triangles referencing the four vertices most recently appended.
    void AddFaceIndices(ChunkMeshData& mesh)
    {
        CB_CORE_ASSERT(
            mesh.Vertices.size() >= 4,
            "A face must append its four vertices before its indices");

        const std::uint32_t firstVertex =
            static_cast<std::uint32_t>(mesh.Vertices.size()) - 4;

        mesh.Indices.push_back(firstVertex + 0);
        mesh.Indices.push_back(firstVertex + 1);
        mesh.Indices.push_back(firstVertex + 2);
        mesh.Indices.push_back(firstVertex + 2);
        mesh.Indices.push_back(firstVertex + 3);
        mesh.Indices.push_back(firstVertex + 0);
    }

    //Per-face brightness, so a solid-coloured block still reads as a cube.
    //Roughly the shading a single overhead light would give.
    constexpr float TopShade = 1.00f;
    constexpr float RightShade = 0.92f;
    constexpr float FrontShade = 0.86f;
    constexpr float LeftShade = 0.80f;
    constexpr float BackShade = 0.72f;
    constexpr float BottomShade = 0.60f;

    void AddFrontFace(ChunkMeshData& mesh, float x, float y, float z, const glm::vec3& blockColor)
    {
        const glm::vec3 color = blockColor * FrontShade;
        mesh.Vertices.push_back({ { x, y, z + 1.0f }, color });
        mesh.Vertices.push_back({ { x + 1.0f, y, z + 1.0f }, color });
        mesh.Vertices.push_back({ { x + 1.0f, y + 1.0f, z + 1.0f }, color });
        mesh.Vertices.push_back({ { x, y + 1.0f, z + 1.0f }, color });
        AddFaceIndices(mesh);
    }

    void AddBackFace(ChunkMeshData& mesh, float x, float y, float z, const glm::vec3& blockColor)
    {
        const glm::vec3 color = blockColor * BackShade;
        mesh.Vertices.push_back({ { x + 1.0f, y, z }, color });
        mesh.Vertices.push_back({ { x, y, z }, color });
        mesh.Vertices.push_back({ { x, y + 1.0f, z }, color });
        mesh.Vertices.push_back({ { x + 1.0f, y + 1.0f, z }, color });
        AddFaceIndices(mesh);
    }

    void AddRightFace(ChunkMeshData& mesh, float x, float y, float z, const glm::vec3& blockColor)
    {
        const glm::vec3 color = blockColor * RightShade;
        mesh.Vertices.push_back({ { x + 1.0f, y, z + 1.0f }, color });
        mesh.Vertices.push_back({ { x + 1.0f, y, z }, color });
        mesh.Vertices.push_back({ { x + 1.0f, y + 1.0f, z }, color });
        mesh.Vertices.push_back({ { x + 1.0f, y + 1.0f, z + 1.0f }, color });
        AddFaceIndices(mesh);
    }

    void AddLeftFace(ChunkMeshData& mesh, float x, float y, float z, const glm::vec3& blockColor)
    {
        const glm::vec3 color = blockColor * LeftShade;
        mesh.Vertices.push_back({ { x, y, z }, color });
        mesh.Vertices.push_back({ { x, y, z + 1.0f }, color });
        mesh.Vertices.push_back({ { x, y + 1.0f, z + 1.0f }, color });
        mesh.Vertices.push_back({ { x, y + 1.0f, z }, color });
        AddFaceIndices(mesh);
    }

    void AddTopFace(ChunkMeshData& mesh, float x, float y, float z, const glm::vec3& blockColor)
    {
        const glm::vec3 color = blockColor * TopShade;
        mesh.Vertices.push_back({ { x, y + 1.0f, z + 1.0f }, color });
        mesh.Vertices.push_back({ { x + 1.0f, y + 1.0f, z + 1.0f }, color });
        mesh.Vertices.push_back({ { x + 1.0f, y + 1.0f, z }, color });
        mesh.Vertices.push_back({ { x, y + 1.0f, z }, color });
        AddFaceIndices(mesh);
    }

    void AddBottomFace(ChunkMeshData& mesh, float x, float y, float z, const glm::vec3& blockColor)
    {
        const glm::vec3 color = blockColor * BottomShade;
        mesh.Vertices.push_back({ { x, y, z }, color });
        mesh.Vertices.push_back({ { x + 1.0f, y, z }, color });
        mesh.Vertices.push_back({ { x + 1.0f, y, z + 1.0f }, color });
        mesh.Vertices.push_back({ { x, y, z + 1.0f }, color });
        AddFaceIndices(mesh);
    }

    void AddExposedFaces(ChunkMeshData& mesh, const Chunk& chunk, int x, int y, int z)
    {
        const float blockX = static_cast<float>(x);
        const float blockY = static_cast<float>(y);
        const float blockZ = static_cast<float>(z);
        const glm::vec3 color = GetBlockColor(chunk.GetBlock(x, y, z));

        if (!chunk.IsBlockSolid(x, y, z + 1))
            AddFrontFace(mesh, blockX, blockY, blockZ, color);
        if (!chunk.IsBlockSolid(x, y, z - 1))
            AddBackFace(mesh, blockX, blockY, blockZ, color);
        if (!chunk.IsBlockSolid(x + 1, y, z))
            AddRightFace(mesh, blockX, blockY, blockZ, color);
        if (!chunk.IsBlockSolid(x - 1, y, z))
            AddLeftFace(mesh, blockX, blockY, blockZ, color);
        if (!chunk.IsBlockSolid(x, y + 1, z))
            AddTopFace(mesh, blockX, blockY, blockZ, color);
        if (!chunk.IsBlockSolid(x, y - 1, z))
            AddBottomFace(mesh, blockX, blockY, blockZ, color);
    }
}

ChunkMeshData ChunkMesher::Build(const Chunk& chunk)
{
    ChunkMeshData mesh;

    for (int z = 0; z < Chunk::Depth; ++z)
    {
        for (int y = 0; y < Chunk::Height; ++y)
        {
            for (int x = 0; x < Chunk::Width; ++x)
            {
                if (chunk.IsBlockSolid(x, y, z))
                    AddExposedFaces(mesh, chunk, x, y, z);
            }
        }
    }

    return mesh;
}
