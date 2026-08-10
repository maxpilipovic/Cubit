#pragma once

#include "Cubit/Core.h"
#include "Cubit/Voxel/Block.h"
#include "Cubit/Voxel/Chunk.h"

#include <glm/glm.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
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
    BlockId GetBlock(int x, int y, int z) const;

    //Changes a block; throws when the position is outside the world.
    void SetBlock(int x, int y, int z, BlockId block);

    //Returns the colour of a block by looking its id up in this world's palette.
    //The alpha channel carries the block's opacity.
    glm::vec4 GetBlockColor(BlockId block) const { return m_Palette[block]; }

    //The colours this world's block ids index into.
    const Palette& GetPalette() const { return m_Palette; }

    //Replaces the palette, e.g. with one loaded from a map file, and rebuilds the
    //derived opacity table.
    void SetPalette(const Palette& palette);

    //Reports whether a block id hides what is behind it.
    //
    //A table lookup rather than a comparison against the palette: the mesher
    //samples this tens of thousands of times per chunk, and resolving it per
    //sample is exactly the cost that made chunk builds slow before (see
    //docs/performance.md, P7).
    bool IsIdOpaque(BlockId block) const { return m_Opaque[block]; }

    //Reports whether the block at this position hides what is behind it.
    //Positions outside the world are not opaque, matching GetBlock reading as
    //air and GetSkyLight reading as open sky.
    bool IsBlockOpaque(int x, int y, int z) const;

    //Reports whether a block id occupies space and stops a moving box.
    //
    //Backed by the same table as IsIdOpaque: both properties are derived from
    //palette alpha, so a separate array would hold identical values on all 256
    //entries and could only drift. The names stay apart because call sites
    //should say which question they are asking — and because giving solidity
    //its own source later (glass: see-through but solid) then changes only how
    //the table is filled, not a single caller.
    bool IsIdSolid(BlockId block) const { return m_Opaque[block]; }

    //Reports whether the block at this position stops a moving box. Positions
    //outside the world are not solid, matching GetBlock reading as air.
    bool IsBlockSolid(int x, int y, int z) const;

    //Reports whether a block occupies this cell without stopping movement —
    //water, and anything else you can swim through. Air is not fluid: it is not
    //present at all, so it falls out of the rule rather than needing a case.
    //
    //Derived from the two tables above rather than stored: it is one &&, and a
    //third array would be a third thing to keep in step with a palette change.
    bool IsBlockFluid(int x, int y, int z) const;

    //Reports whether a block occupies this position at all.
    bool IsBlockPresent(int x, int y, int z) const;

    //Returns a cell's sky light, treating positions outside the world as open
    //sky so the map's edges and the space above it are lit.
    std::uint8_t GetSkyLight(int x, int y, int z) const;

    //Sets a cell's sky light; throws when the position is outside the world.
    //Does not mark anything dirty — propagation decides which chunks actually
    //changed and marks those, so clearing and re-flooding a cell to the same
    //value costs no remeshing.
    void SetSkyLight(int x, int y, int z, std::uint8_t level);

    //Marks the chunk holding this position dirty, plus any chunk sharing a face
    //the position lies on. Public because a chunk's mesh samples light from the
    //cells just across its boundary, so a light change at an edge invalidates
    //the neighbour's mesh too.
    void MarkChunkDirtyAt(int x, int y, int z);

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

    int m_ChunksX = 0;
    int m_ChunksY = 0;
    int m_ChunksZ = 0;
    std::vector<Chunk> m_Chunks;
    std::set<glm::ivec3, IVec3Less> m_DirtyChunks;
    Palette m_Palette;
    std::array<bool, 256> m_Opaque{};
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
