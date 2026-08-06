#include "cub.h"

#include "Cubit/Renderer/WorldRenderer.h"

#include "Cubit/Renderer/Renderer.h"
#include "Cubit/Renderer/Shader.h"
#include "Cubit/Renderer/Frustum.h"
#include "Cubit/Voxel/Chunk.h"
#include "Cubit/Voxel/ChunkMesher.h"

#include <glm/gtc/matrix_transform.hpp>

#include <chrono>

void WorldRenderer::Update(World& world)
{
    // Absorb this frame's changes into the pending set, then let the world forget
    // them: tracking changes is the world's job, scheduling remeshes is ours.
    for (const glm::ivec3& coord : world.DirtyChunks())
        m_Pending.insert(coord);
    world.ClearDirty();

    // Mesh until this frame's slice is spent, removing each chunk as it is
    // handled. The budget is checked after building rather than before, so one
    // chunk is always built however long it takes and the set always drains.
    const auto start = std::chrono::steady_clock::now();

    for (auto it = m_Pending.begin(); it != m_Pending.end(); )
    {
        const glm::ivec3 coord = *it;
        const ChunkMeshData mesh =
            ChunkMesher::Build(world, coord.x, coord.y, coord.z);

        if (mesh.Opaque.Indices.empty())
        {
            //A chunk that meshes to nothing keeps no buffers; drop any it had.
            m_Meshes.erase(coord);
        }
        else
        {
            ChunkMesh gpu;
            gpu.Array = std::make_unique<VertexArray>();
            gpu.Buffer = std::make_unique<VertexBuffer>(
                mesh.Opaque.Vertices.data(),
                static_cast<std::uint32_t>(mesh.Opaque.Vertices.size() * sizeof(VoxelVertex)));
            gpu.Array->AddBuffer(
                *gpu.Buffer,
                BufferLayout{ ShaderDataType::Float3, ShaderDataType::Float3 });
            gpu.Indices = std::make_unique<IndexBuffer>(
                mesh.Opaque.Indices.data(),
                static_cast<std::uint32_t>(mesh.Opaque.Indices.size()));
            gpu.FaceCount = gpu.Indices->GetCount() / 6;

            m_Meshes[coord] = std::move(gpu);
        }

        it = m_Pending.erase(it);

        const std::chrono::duration<double, std::milli> spent =
            std::chrono::steady_clock::now() - start;

        if (spent.count() >= MeshBudgetMilliseconds)
            break;
    }
}

void WorldRenderer::Render(const Shader& shader, const glm::mat4& viewProjection,
    const glm::vec3& worldOffset)
{
    const Frustum frustum(viewProjection);
    m_LastDrawnChunks = 0;

    for (const auto& entry : m_Meshes)
    {
        const glm::ivec3& coord = entry.first;
        const ChunkMesh& mesh = entry.second;

        const glm::vec3 origin =
            glm::vec3(World::GetChunkOrigin(coord.x, coord.y, coord.z));
        const glm::vec3 min = worldOffset + origin;
        const glm::vec3 max = min + glm::vec3(
            Chunk::Width, Chunk::Height, Chunk::Depth);

        if (!frustum.IntersectsAABB(min, max))
            continue; // outside the view

        const glm::mat4 transform = glm::translate(glm::mat4(1.0f), min);
        Renderer::Submit(*mesh.Array, *mesh.Indices, shader, transform);
        ++m_LastDrawnChunks;
    }
}

std::uint32_t WorldRenderer::TotalFaceCount() const
{
    std::uint32_t total = 0;
    for (const auto& entry : m_Meshes)
        total += entry.second.FaceCount;

    return total;
}
