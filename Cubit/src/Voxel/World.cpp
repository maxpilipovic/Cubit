#include "cub.h"

#include "Cubit/Voxel/World.h"

#include "Core/CoreLogger.h"

#include <stdexcept>

World::World(int chunksX, int chunksY, int chunksZ)
    : m_ChunksX(chunksX), m_ChunksY(chunksY), m_ChunksZ(chunksZ)
{
    if (chunksX <= 0 || chunksY <= 0 || chunksZ <= 0)
        throw std::invalid_argument("World size must be at least one chunk on each axis");

    m_Chunks.resize(
        static_cast<std::size_t>(chunksX) *
        static_cast<std::size_t>(chunksY) *
        static_cast<std::size_t>(chunksZ));
}

BlockType World::GetBlock(int x, int y, int z) const
{
    if (!IsInBounds(x, y, z))
        return BlockType::Air;

    const Chunk& chunk = GetChunk(
        x / Chunk::Width,
        y / Chunk::Height,
        z / Chunk::Depth);

    return chunk.GetBlock(
        x % Chunk::Width,
        y % Chunk::Height,
        z % Chunk::Depth);
}

void World::SetBlock(int x, int y, int z, BlockType block)
{
    if (!IsInBounds(x, y, z))
        throw std::out_of_range("World block coordinates are out of bounds");

    Chunk& chunk = m_Chunks[GetChunkIndex(
        x / Chunk::Width,
        y / Chunk::Height,
        z / Chunk::Depth)];

    chunk.SetBlock(
        x % Chunk::Width,
        y % Chunk::Height,
        z % Chunk::Depth,
        block);
}

bool World::IsBlockSolid(int x, int y, int z) const
{
    return IsSolid(GetBlock(x, y, z));
}

bool World::IsInBounds(int x, int y, int z) const
{
    //Checked before dividing, so the conversion to chunk coordinates never sees
    //a negative value and never has to worry about truncation.
    return x >= 0 && x < GetWidth() &&
           y >= 0 && y < GetHeight() &&
           z >= 0 && z < GetDepth();
}

const Chunk& World::GetChunk(int chunkX, int chunkY, int chunkZ) const
{
    if (!IsChunkInBounds(chunkX, chunkY, chunkZ))
        throw std::out_of_range("Chunk coordinates are out of bounds");

    return m_Chunks[GetChunkIndex(chunkX, chunkY, chunkZ)];
}

bool World::IsChunkInBounds(int chunkX, int chunkY, int chunkZ) const
{
    return chunkX >= 0 && chunkX < m_ChunksX &&
           chunkY >= 0 && chunkY < m_ChunksY &&
           chunkZ >= 0 && chunkZ < m_ChunksZ;
}

glm::ivec3 World::GetChunkOrigin(int chunkX, int chunkY, int chunkZ)
{
    return glm::ivec3(
        chunkX * Chunk::Width,
        chunkY * Chunk::Height,
        chunkZ * Chunk::Depth);
}

std::size_t World::GetChunkIndex(int chunkX, int chunkY, int chunkZ) const
{
    CB_CORE_ASSERT(
        IsChunkInBounds(chunkX, chunkY, chunkZ),
        "Chunk index is out of bounds");

    return static_cast<std::size_t>(
        chunkX + m_ChunksX * (chunkY + m_ChunksY * chunkZ));
}
