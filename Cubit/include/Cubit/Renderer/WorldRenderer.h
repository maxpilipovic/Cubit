#pragma once

#include "Cubit/Core.h"
#include "Cubit/Renderer/VertexArray.h"
#include "Cubit/Renderer/VertexBuffer.h"
#include "Cubit/Renderer/IndexBuffer.h"
#include "Cubit/Voxel/World.h"

#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <set>

class Shader;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

//Holds one GPU mesh per chunk and keeps them in step with the world. Only the
//chunks the world reports dirty are rebuilt, so an edit does not remesh the
//whole world.
class CB_API WorldRenderer
{
public:
    WorldRenderer() = default;

    //Owns GPU buffers through move-only chunk meshes, so it cannot be copied.
    //Declaring this also stops the DLL export from instantiating the map's copy
    //constructor, which a move-only value type has no way to satisfy.
    WorldRenderer(const WorldRenderer&) = delete;
    WorldRenderer& operator=(const WorldRenderer&) = delete;
    WorldRenderer(WorldRenderer&&) noexcept = default;
    WorldRenderer& operator=(WorldRenderer&&) noexcept = default;

    //Rebuilds the GPU mesh of every dirty chunk, then clears the world's dirty
    //set. Call once per frame before Render. Needs a current GL context.
    void Update(World& world);

    //Draws every non-empty chunk mesh inside the camera frustum, each translated
    //to its chunk origin plus worldOffset. Opaque geometry is drawn first, then
    //transparent geometry back to front with depth writes off, so it blends
    //against what is behind it. cameraPosition must be in the same space as the
    //chunk transforms — that is, worldOffset already applied.
    void Render(const Shader& shader, const glm::mat4& viewProjection,
        const glm::vec3& worldOffset, const glm::vec3& cameraPosition);

    //One draw's GPU buffers plus its face count.
    struct GpuGeometry
    {
        std::unique_ptr<VertexArray> Array;
        std::unique_ptr<VertexBuffer> Buffer;
        std::unique_ptr<IndexBuffer> Indices;
        std::uint32_t FaceCount = 0;
    };

    //A chunk's geometry, split by pass. Either half may be empty.
    struct ChunkMesh
    {
        GpuGeometry Opaque;
        GpuGeometry Transparent;
    };

    //Chunks actually submitted in the last Render (after frustum culling).
    std::size_t DrawnChunkCount() const { return m_LastDrawnChunks; }

    //Total exposed faces across all chunk meshes, for the HUD.
    std::uint32_t TotalFaceCount() const;

    //Cached chunk meshes ready to draw.
    std::size_t TotalChunkCount() const { return m_Meshes.size(); }

    //Chunks still waiting to be (re)meshed.
    std::size_t PendingCount() const { return m_Pending.size(); }

private:
    //How long one Update may spend rebuilding chunk meshes, so a burst of them
    //spreads over frames instead of stalling one.
    //
    //A count cannot do this job. Chunks differ several-fold in how much
    //geometry they carry, and the same count costs very different amounts in a
    //debug and an optimised build, so any count is either a stall in one of
    //them or needless dawdling in the other. A slice of the frame holds either
    //way.
    //
    //Update always builds at least one chunk, so however small this is, the
    //pending set still drains.
    static constexpr double MeshBudgetMilliseconds = 4.0;

    std::map<glm::ivec3, ChunkMesh, IVec3Less> m_Meshes;
    std::set<glm::ivec3, IVec3Less> m_Pending;
    std::size_t m_LastDrawnChunks = 0;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
