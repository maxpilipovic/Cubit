#pragma once

#include "Cubit/Core.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

class World;

struct VoxelVertex
{
    glm::vec3 Position{ 0.0f };
    glm::vec3 Color{ 1.0f };
};

struct ChunkMeshData
{
    std::vector<VoxelVertex> Vertices;
    std::vector<std::uint32_t> Indices;
};

class CB_API ChunkMesher
{
public:
    ChunkMesher() = delete;

    //Builds one chunk's mesh, containing only faces exposed to air. Blocks in
    //neighbouring chunks are consulted, so a face shared with a solid block in
    //the next chunk is not emitted. Vertices are in chunk-local coordinates, so
    //the caller positions the mesh by the chunk's origin.
    static ChunkMeshData Build(const World& world, int chunkX, int chunkY, int chunkZ);
};
