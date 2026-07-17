#pragma once

#include "Cubit/Core.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

class Chunk;

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

    //Builds one mesh containing only faces exposed to air.
    static ChunkMeshData Build(const Chunk& chunk);
};
