#pragma once

#include "Cubit/Core.h"
#include "Cubit/Voxel/Block.h"

#include <array>
#include <cstddef>
#include <cstdint>

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
    BlockId GetBlock(int x, int y, int z) const;

    //Changes a block; throws when the position is outside this chunk.
    void SetBlock(int x, int y, int z, BlockId block);

    //Reports whether a block occupies this position at all. A chunk holds no
    //palette, so presence is the only one of the three block properties it can
    //answer; solidity and opacity live on World.
    bool IsBlockPresent(int x, int y, int z) const;

    //Returns a cell's sky light, treating positions outside this chunk as open
    //sky. The mesher samples corners that straddle a chunk edge, so an
    //out-of-range read must not read as darkness.
    std::uint8_t GetSkyLight(int x, int y, int z) const;

    //Sets a cell's sky light; throws when the position is outside this chunk.
    void SetSkyLight(int x, int y, int z, std::uint8_t level);

    //Reports whether local coordinates are inside the chunk.
    static bool IsInBounds(int x, int y, int z);

    //Direct access to this chunk's storage, for passes that copy a whole chunk
    //in or out in one go. Cells are addressed x + Width * (y + Height * z), so
    //x runs contiguously, y strides by Width and z by Width * Height.
    //
    //Wide on purpose, and narrow in use: a pass that reads or writes all 4096
    //cells through GetBlock/SetSkyLight pays a bounds check and an index
    //computation per cell, and in a debug build a function call too, none of
    //which it needs - it already knows every cell is in range. Sky light
    //propagation copies the world out and the finished light back in exactly
    //once each, and that is what these are for. Anything touching cells one at
    //a time belongs on the checked accessors above.
    const BlockId* BlockData() const { return m_Blocks.data(); }
    std::uint8_t* SkyLightData() { return m_SkyLight.data(); }

private:
    //Converts three-dimensional local coordinates into array storage.
    static std::size_t GetIndex(int x, int y, int z);

    std::array<BlockId, BlockCount> m_Blocks;
    std::array<std::uint8_t, BlockCount> m_SkyLight;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
