#include "cub.h"

#include "Cubit/Renderer/WorldRenderer.h"

#include "Cubit/Renderer/Renderer.h"
#include "Cubit/Renderer/Shader.h"
#include "Cubit/Renderer/Frustum.h"
#include "Cubit/Voxel/Chunk.h"
#include "Cubit/Voxel/ChunkMesher.h"
#include "Cubit/Profiler.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <vector>

namespace
{
    //Uploads one geometry set, or leaves it empty when there is nothing to draw.
    void UploadGeometry(WorldRenderer::GpuGeometry& gpu, const MeshGeometry& source)
    {
        if (source.Indices.empty())
            return;

        gpu.Array = std::make_unique<VertexArray>();
        gpu.Buffer = std::make_unique<VertexBuffer>(
            source.Vertices.data(),
            static_cast<std::uint32_t>(source.Vertices.size() * sizeof(VoxelVertex)));
        gpu.Array->AddBuffer(
            *gpu.Buffer,
            BufferLayout{ ShaderDataType::Float3, ShaderDataType::Float4 });
        gpu.Indices = std::make_unique<IndexBuffer>(
            source.Indices.data(),
            static_cast<std::uint32_t>(source.Indices.size()));
        gpu.FaceCount = gpu.Indices->GetCount() / 6;
    }
}

void WorldRenderer::Update(World& world)
{
    CB_PROFILE_SCOPE("WorldRenderer::Update");

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

        if (mesh.Opaque.Indices.empty() && mesh.Transparent.Indices.empty())
        {
            //A chunk that meshes to nothing keeps no buffers; drop any it had.
            m_Meshes.erase(coord);
        }
        else
        {
            ChunkMesh gpu;
            UploadGeometry(gpu.Opaque, mesh.Opaque);
            UploadGeometry(gpu.Transparent, mesh.Transparent);

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
    const glm::vec3& worldOffset, const glm::vec3& cameraPosition)
{
    const Frustum frustum(viewProjection);
    m_LastDrawnChunks = 0;

    //Transparent chunks are collected rather than drawn as they are found: they
    //have to go out far to near, and the mesh map is ordered by coordinate.
    struct TransparentDraw
    {
        float DistanceSquared;
        const GpuGeometry* Geometry;
        glm::vec3 Origin;
    };
    std::vector<TransparentDraw> transparent;

    for (const auto& entry : m_Meshes)
    {
        const glm::ivec3& coord = entry.first;
        const ChunkMesh& mesh = entry.second;

        const glm::vec3 origin =
            glm::vec3(World::GetChunkOrigin(coord.x, coord.y, coord.z));
        const glm::vec3 min = worldOffset + origin;
        const glm::vec3 extent(Chunk::Width, Chunk::Height, Chunk::Depth);

        if (!frustum.IntersectsAABB(min, min + extent))
            continue; // outside the view

        if (mesh.Opaque.Indices != nullptr)
        {
            const glm::mat4 transform = glm::translate(glm::mat4(1.0f), min);
            Renderer::Submit(*mesh.Opaque.Array, *mesh.Opaque.Indices,
                shader, transform);
            ++m_LastDrawnChunks;
        }

        if (mesh.Transparent.Indices != nullptr)
        {
            const glm::vec3 centre = min + extent * 0.5f;
            const glm::vec3 toCamera = centre - cameraPosition;

            transparent.push_back({ glm::dot(toCamera, toCamera),
                &mesh.Transparent, min });
        }
    }

    if (transparent.empty())
        return;

    // Farthest first: a near surface has to blend over what is already behind
    // it, so what is behind has to be on screen first.
    std::sort(transparent.begin(), transparent.end(),
        [](const TransparentDraw& a, const TransparentDraw& b)
        {
            return a.DistanceSquared > b.DistanceSquared;
        });

    // Depth testing stays on so terrain in front still hides water, but writing
    // is off so two water surfaces do not reject each other.
    Renderer::SetDepthWrite(false);

    for (const TransparentDraw& draw : transparent)
    {
        const glm::mat4 transform = glm::translate(glm::mat4(1.0f), draw.Origin);
        Renderer::Submit(*draw.Geometry->Array, *draw.Geometry->Indices,
            shader, transform);
        ++m_LastDrawnChunks;
    }

    Renderer::SetDepthWrite(true);
}

std::uint32_t WorldRenderer::TotalFaceCount() const
{
    std::uint32_t total = 0;
    for (const auto& entry : m_Meshes)
        total += entry.second.Opaque.FaceCount + entry.second.Transparent.FaceCount;

    return total;
}
