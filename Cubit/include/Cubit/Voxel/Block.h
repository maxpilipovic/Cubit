#pragma once

#include <cstdint>

enum class BlockType : std::uint8_t
{
    Air = 0,
    Solid
};

//Reports whether a block occupies space and should generate visible faces.
constexpr bool IsSolid(BlockType block)
{
    return block != BlockType::Air;
}
