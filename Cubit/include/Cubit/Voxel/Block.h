#pragma once

#include <glm/glm.hpp>
#include <cstdint>

//A block is a colour, not a material. Voxel map formats such as .vox and .vxl
//store colour per voxel, and team-coloured terrain is the point of the game, so
//there is nothing here to texture. These named entries stand in for what will
//become a data-driven palette once maps are loaded from a file.
enum class BlockType : std::uint8_t
{
    Air = 0,
    Solid,
    Red,
    Orange,
    Yellow,
    Green,
    Blue,
    Purple,
    White,
    Grey
};

//Reports whether a block occupies space and should generate visible faces.
constexpr bool IsSolid(BlockType block)
{
    return block != BlockType::Air;
}

//Returns the base colour of a block, before face shading is applied.
constexpr glm::vec3 GetBlockColor(BlockType block)
{
    switch (block)
    {
        case BlockType::Red:
            return glm::vec3(0.85f, 0.25f, 0.25f);
        case BlockType::Orange:
            return glm::vec3(0.90f, 0.55f, 0.20f);
        case BlockType::Yellow:
            return glm::vec3(0.92f, 0.85f, 0.30f);
        case BlockType::Green:
            return glm::vec3(0.35f, 0.75f, 0.35f);
        case BlockType::Blue:
            return glm::vec3(0.30f, 0.50f, 0.90f);
        case BlockType::Purple:
            return glm::vec3(0.62f, 0.38f, 0.80f);
        case BlockType::White:
            return glm::vec3(0.95f, 0.95f, 0.95f);
        case BlockType::Grey:
            return glm::vec3(0.55f, 0.57f, 0.60f);
        case BlockType::Solid:
        default:
            return glm::vec3(0.78f, 0.85f, 1.00f);
    }
}
