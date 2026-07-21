#pragma once

#include "Cubit/Core.h"
#include "Cubit/Voxel/Block.h"
#include "Cubit/Voxel/Chunk.h"

#include <glm/glm.hpp>
#include <cstddef>
#include <set>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

//Orders chunk coordinates so they can be stored in a set or map. Compares x,
//then y, then z; the values are only ever equal for the same chunk.
struct IVec3Less
{
    bool operator()(const glm::ivec3& a, const glm::ivec3& b) const
    {
        if (a.x != b.x) return a.x < b.x;
        if (a.y != b.y) return a.y < b.y;
        return a.z < b.z;
    }
};

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

    //Returns the chunks whose meshes are out of date because a block in them,
    //or in a chunk sharing a face with them, changed.
    const std::set<glm::ivec3, IVec3Less>& DirtyChunks() const { return m_DirtyChunks; }

    //Forgets every dirty chunk. The renderer calls this once it has remeshed
    //them.
    void ClearDirty() { m_DirtyChunks.clear(); }

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

    //Marks the chunk holding a world position dirty, plus any chunk sharing a
    //face the position lies on, when that neighbour is inside the world.
    void MarkChunkDirtyForEdit(int x, int y, int z);

    int m_ChunksX = 0;
    int m_ChunksY = 0;
    int m_ChunksZ = 0;
    std::vector<Chunk> m_Chunks;
    std::set<glm::ivec3, IVec3Less> m_DirtyChunks;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
