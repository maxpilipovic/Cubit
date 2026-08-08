#include "cub.h"

#include "Cubit/Voxel/World.h"

#include "Core/CoreLogger.h"

#include <stdexcept>

World::World(int chunksX, int chunksY, int chunksZ)
    : m_ChunksX(chunksX), m_ChunksY(chunksY), m_ChunksZ(chunksZ),
      m_Palette(DefaultPalette())
{
    //The member-init list bypasses SetPalette, so the derived table starts empty.
    SetPalette(m_Palette);

    if (chunksX <= 0 || chunksY <= 0 || chunksZ <= 0)
        throw std::invalid_argument("World size must be at least one chunk on each axis");

    m_Chunks.resize(
        static_cast<std::size_t>(chunksX) *
        static_cast<std::size_t>(chunksY) *
        static_cast<std::size_t>(chunksZ));

    //A freshly loaded world has no GPU meshes yet, so every chunk needs building.
    for (int z = 0; z < chunksZ; ++z)
        for (int y = 0; y < chunksY; ++y)
            for (int x = 0; x < chunksX; ++x)
                m_DirtyChunks.insert(glm::ivec3(x, y, z));
}

BlockId World::GetBlock(int x, int y, int z) const
{
    if (!IsInBounds(x, y, z))
        return 0;

    const Chunk& chunk = GetChunk(
        x / Chunk::Width,
        y / Chunk::Height,
        z / Chunk::Depth);

    return chunk.GetBlock(
        x % Chunk::Width,
        y % Chunk::Height,
        z % Chunk::Depth);
}

void World::SetBlock(int x, int y, int z, BlockId block)
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

    MarkChunkDirtyAt(x, y, z);
}

std::uint8_t World::GetSkyLight(int x, int y, int z) const
{
    if (!IsInBounds(x, y, z))
        return 15; // Outside the world is open sky.

    const Chunk& chunk = GetChunk(
        x / Chunk::Width,
        y / Chunk::Height,
        z / Chunk::Depth);

    return chunk.GetSkyLight(
        x % Chunk::Width,
        y % Chunk::Height,
        z % Chunk::Depth);
}

void World::SetSkyLight(int x, int y, int z, std::uint8_t level)
{
    if (!IsInBounds(x, y, z))
        throw std::out_of_range("World block coordinates are out of bounds");

    Chunk& chunk = m_Chunks[GetChunkIndex(
        x / Chunk::Width,
        y / Chunk::Height,
        z / Chunk::Depth)];

    chunk.SetSkyLight(
        x % Chunk::Width,
        y % Chunk::Height,
        z % Chunk::Depth,
        level);
}

void World::MarkChunkDirtyAt(int x, int y, int z)
{
    const int chunkX = x / Chunk::Width;
    const int chunkY = y / Chunk::Height;
    const int chunkZ = z / Chunk::Depth;

    const int localX = x % Chunk::Width;
    const int localY = y % Chunk::Height;
    const int localZ = z % Chunk::Depth;

    //A chunk's mesh samples the cells diagonally around each face corner, so a
    //block on a chunk edge or corner affects the diagonal neighbours too, not
    //just the ones sharing a face. Step to a neighbour along an axis only when
    //the position lies on that face, so an interior edit only marks its own
    //chunk.
    for (int dz = -1; dz <= 1; ++dz)
    {
        if ((dz < 0 && localZ != 0) || (dz > 0 && localZ != Chunk::Depth - 1))
            continue;

        for (int dy = -1; dy <= 1; ++dy)
        {
            if ((dy < 0 && localY != 0) || (dy > 0 && localY != Chunk::Height - 1))
                continue;

            for (int dx = -1; dx <= 1; ++dx)
            {
                if ((dx < 0 && localX != 0) || (dx > 0 && localX != Chunk::Width - 1))
                    continue;

                if (IsChunkInBounds(chunkX + dx, chunkY + dy, chunkZ + dz))
                    m_DirtyChunks.insert(
                        glm::ivec3(chunkX + dx, chunkY + dy, chunkZ + dz));
            }
        }
    }
}

bool World::IsBlockPresent(int x, int y, int z) const
{
    return IsPresent(GetBlock(x, y, z));
}

void World::SetPalette(const Palette& palette)
{
    m_Palette = palette;

    for (std::size_t i = 0; i < m_Opaque.size(); ++i)
        m_Opaque[i] = IsOpaqueColor(m_Palette[i]);
}

bool World::IsBlockOpaque(int x, int y, int z) const
{
    if (!IsInBounds(x, y, z))
        return false;

    return IsIdOpaque(GetBlock(x, y, z));
}

bool World::IsBlockSolid(int x, int y, int z) const
{
    return IsIdSolid(GetBlock(x, y, z));
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
