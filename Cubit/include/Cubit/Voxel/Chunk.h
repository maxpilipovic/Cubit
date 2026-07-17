#pragma once

#include "Cubit/Core.h"
#include "Cubit/Voxel/Block.h"

#include <array>
#include <cstddef>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

class CB_API Chunk
{
public:
    static constexpr int Width = 16;
    static constexpr int Height = 16;
    static constexpr int Depth = 16;
    static constexpr std::size_t BlockCount = Width * Height * Depth;

    //Creates an empty chunk containing only air blocks.
    Chunk();

    //Returns a block, treating positions outside this chunk as air.
    BlockType GetBlock(int x, int y, int z) const;

    //Changes a block; throws when the position is outside this chunk.
    void SetBlock(int x, int y, int z, BlockType block);

    //Reports whether the block at this position occupies space.
    bool IsBlockSolid(int x, int y, int z) const;

    //Reports whether local coordinates are inside the chunk.
    static bool IsInBounds(int x, int y, int z);

private:
    //Converts three-dimensional local coordinates into array storage.
    static std::size_t GetIndex(int x, int y, int z);

    std::array<BlockType, BlockCount> m_Blocks;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
