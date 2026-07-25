#include "cub.h"

#include "Cubit/Renderer/WorldRenderer.h"

#include "Cubit/Renderer/Renderer.h"
#include "Cubit/Renderer/Shader.h"
#include "Cubit/Voxel/ChunkMesher.h"

#include <glm/gtc/matrix_transform.hpp>

void WorldRenderer::Update(World& world)
{
    // Absorb this frame's changes into the pending set, then let the world forget
    // them: tracking changes is the world's job, scheduling remeshes is ours.
    for (const glm::ivec3& coord : world.DirtyChunks())
        m_Pending.insert(coord);
    world.ClearDirty();

    // Mesh up to the per-frame budget, removing each chunk as it is handled.
    std::size_t built = 0;
    for (auto it = m_Pending.begin();
         it != m_Pending.end() && built < MeshBudgetPerFrame; )
    {
        const glm::ivec3 coord = *it;
        const ChunkMeshData mesh =
            ChunkMesher::Build(world, coord.x, coord.y, coord.z);

        if (mesh.Indices.empty())
        {
            //A chunk that meshes to nothing keeps no buffers; drop any it had.
            m_Meshes.erase(coord);
        }
        else
        {
            ChunkMesh gpu;
            gpu.Array = std::make_unique<VertexArray>();
            gpu.Buffer = std::make_unique<VertexBuffer>(
                mesh.Vertices.data(),
                static_cast<std::uint32_t>(mesh.Vertices.size() * sizeof(VoxelVertex)));
            gpu.Array->AddBuffer(
                *gpu.Buffer,
                BufferLayout{ ShaderDataType::Float3, ShaderDataType::Float3 });
            gpu.Indices = std::make_unique<IndexBuffer>(
                mesh.Indices.data(),
                static_cast<std::uint32_t>(mesh.Indices.size()));
            gpu.FaceCount = gpu.Indices->GetCount() / 6;

            m_Meshes[coord] = std::move(gpu);
        }

        it = m_Pending.erase(it);
        ++built;
    }
}

void WorldRenderer::Render(const Shader& shader, const glm::vec3& worldOffset) const
{
    for (const auto& entry : m_Meshes)
    {
        const glm::ivec3& coord = entry.first;
        const ChunkMesh& mesh = entry.second;

        const glm::ivec3 origin =
            World::GetChunkOrigin(coord.x, coord.y, coord.z);
        const glm::mat4 transform = glm::translate(
            glm::mat4(1.0f),
            worldOffset + glm::vec3(origin));

        Renderer::Submit(*mesh.Array, *mesh.Indices, shader, transform);
    }
}

std::uint32_t WorldRenderer::TotalFaceCount() const
{
    std::uint32_t total = 0;
    for (const auto& entry : m_Meshes)
        total += entry.second.FaceCount;

    return total;
}
