#include "cub.h"

#include "Cubit/Voxel/Chunk.h"

#include <stdexcept>

Chunk::Chunk()
{
    m_Blocks.fill(BlockType::Air);
}

BlockType Chunk::GetBlock(int x, int y, int z) const
{
    if (!IsInBounds(x, y, z))
        return BlockType::Air;

    return m_Blocks[GetIndex(x, y, z)];
}

void Chunk::SetBlock(int x, int y, int z, BlockType block)
{
    if (!IsInBounds(x, y, z))
        throw std::out_of_range("Chunk block coordinates are out of bounds");

    m_Blocks[GetIndex(x, y, z)] = block;
}

bool Chunk::IsBlockSolid(int x, int y, int z) const
{
    return IsSolid(GetBlock(x, y, z));
}

bool Chunk::IsInBounds(int x, int y, int z)
{
    return x >= 0 && x < Width &&
           y >= 0 && y < Height &&
           z >= 0 && z < Depth;
}

std::size_t Chunk::GetIndex(int x, int y, int z)
{
    return static_cast<std::size_t>(x + Width * (y + Height * z));
}
