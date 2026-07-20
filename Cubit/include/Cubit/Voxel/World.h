#pragma once

#include "Cubit/Core.h"
#include "Cubit/Voxel/Block.h"
#include "Cubit/Voxel/Chunk.h"

#include <glm/glm.hpp>
#include <cstddef>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

//A fixed grid of chunks addressed in world block coordinates. The size is known
//when the world is created and never changes, because maps are loaded whole
//rather than generated as the player moves.
class CB_API World
{
public:
    //Creates a world of the given size in chunks, containing only air.
    World(int chunksX, int chunksY, int chunksZ);

    //Returns a block, treating positions outside the world as air.
    BlockType GetBlock(int x, int y, int z) const;

    //Changes a block; throws when the position is outside the world.
    void SetBlock(int x, int y, int z, BlockType block);

    //Reports whether the block at this position occupies space.
    bool IsBlockSolid(int x, int y, int z) const;

    //Reports whether world block coordinates are inside the world.
    bool IsInBounds(int x, int y, int z) const;

    //Returns a chunk by its grid position; throws when outside the grid.
    const Chunk& GetChunk(int chunkX, int chunkY, int chunkZ) const;

    //Reports whether chunk grid coordinates are inside the world.
    bool IsChunkInBounds(int chunkX, int chunkY, int chunkZ) const;

    //Returns the world block coordinates of a chunk's minimum corner.
    static glm::ivec3 GetChunkOrigin(int chunkX, int chunkY, int chunkZ);

    int GetChunksX() const { return m_ChunksX; }
    int GetChunksY() const { return m_ChunksY; }
    int GetChunksZ() const { return m_ChunksZ; }

    int GetWidth() const { return m_ChunksX * Chunk::Width; }
    int GetHeight() const { return m_ChunksY * Chunk::Height; }
    int GetDepth() const { return m_ChunksZ * Chunk::Depth; }

private:
    //Returns the chunk holding a world position, which must be in bounds.
    std::size_t GetChunkIndex(int chunkX, int chunkY, int chunkZ) const;

    int m_ChunksX = 0;
    int m_ChunksY = 0;
    int m_ChunksZ = 0;
    std::vector<Chunk> m_Chunks;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
