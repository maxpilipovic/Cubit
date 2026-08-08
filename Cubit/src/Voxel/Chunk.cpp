#include "cub.h"

#include "Cubit/Voxel/Chunk.h"

#include "Core/CoreLogger.h"

#include <stdexcept>

Chunk::Chunk()
{
    m_Blocks.fill(0);
    m_SkyLight.fill(0);
}

BlockId Chunk::GetBlock(int x, int y, int z) const
{
    if (!IsInBounds(x, y, z))
        return 0;

    return m_Blocks[GetIndex(x, y, z)];
}

void Chunk::SetBlock(int x, int y, int z, BlockId block)
{
    if (!IsInBounds(x, y, z))
        throw std::out_of_range("Chunk block coordinates are out of bounds");

    m_Blocks[GetIndex(x, y, z)] = block;
}

bool Chunk::IsBlockPresent(int x, int y, int z) const
{
    return IsPresent(GetBlock(x, y, z));
}

std::uint8_t Chunk::GetSkyLight(int x, int y, int z) const
{
    if (!IsInBounds(x, y, z))
        return 15; // Outside the chunk is open sky, not darkness.

    return m_SkyLight[GetIndex(x, y, z)];
}

void Chunk::SetSkyLight(int x, int y, int z, std::uint8_t level)
{
    if (!IsInBounds(x, y, z))
        throw std::out_of_range("Chunk sky light coordinates are out of bounds");

    m_SkyLight[GetIndex(x, y, z)] = level;
}

bool Chunk::IsInBounds(int x, int y, int z)
{
    return x >= 0 && x < Width &&
           y >= 0 && y < Height &&
           z >= 0 && z < Depth;
}

std::size_t Chunk::GetIndex(int x, int y, int z)
{
    //Callers are expected to have rejected or clamped out-of-range positions
    //already, so reaching here with one is a bug rather than bad input.
    CB_CORE_ASSERT(IsInBounds(x, y, z), "Chunk index is out of bounds");

    return static_cast<std::size_t>(x + Width * (y + Height * z));
}
