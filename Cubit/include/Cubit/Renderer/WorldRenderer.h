#pragma once

#include "Cubit/Core.h"
#include "Cubit/Renderer/VertexArray.h"
#include "Cubit/Renderer/VertexBuffer.h"
#include "Cubit/Renderer/IndexBuffer.h"
#include "Cubit/Voxel/World.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <map>
#include <memory>

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

    //Draws every non-empty chunk mesh, each translated to its chunk origin plus
    //worldOffset.
    void Render(const Shader& shader, const glm::vec3& worldOffset) const;

    //Total exposed faces across all chunk meshes, for the HUD.
    std::uint32_t TotalFaceCount() const;

private:
    //A chunk's GPU buffers plus its face count. Move-only through its unique_ptr
    //members, so it can live in a map without copying GPU resources.
    struct ChunkMesh
    {
        std::unique_ptr<VertexArray> Array;
        std::unique_ptr<VertexBuffer> Buffer;
        std::unique_ptr<IndexBuffer> Indices;
        std::uint32_t FaceCount = 0;
    };

    std::map<glm::ivec3, ChunkMesh, IVec3Less> m_Meshes;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
