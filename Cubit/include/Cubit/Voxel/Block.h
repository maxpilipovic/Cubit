#pragma once

#include <glm/glm.hpp>
#include <array>
#include <cstdint>

//A block is a palette index, not a material. 0 is empty (air). Colour lives in
//the world's palette, loaded from the map file, so there is nothing to texture.
using BlockId = std::uint8_t;

//256 colours addressed directly by BlockId. Index 0 (air) is never rendered.
using Palette = std::array<glm::vec3, 256>;

//Reports whether a block occupies space and should generate visible faces.
constexpr bool IsSolid(BlockId block)
{
    return block != 0;
}

//The palette a world uses before a map overrides it. Reproduces the colours the
//engine shipped with, at the indices the old named blocks used, so test terrain
//reads the same after the refactor.
inline Palette DefaultPalette()
{
    Palette palette;
    const glm::vec3 solid(0.78f, 0.85f, 1.00f);
    palette.fill(solid);
    palette[0] = glm::vec3(0.0f);                 // Air, never rendered
    palette[1] = solid;                           // Solid
    palette[2] = glm::vec3(0.85f, 0.25f, 0.25f);  // Red
    palette[3] = glm::vec3(0.90f, 0.55f, 0.20f);  // Orange
    palette[4] = glm::vec3(0.92f, 0.85f, 0.30f);  // Yellow
    palette[5] = glm::vec3(0.35f, 0.75f, 0.35f);  // Green
    palette[6] = glm::vec3(0.30f, 0.50f, 0.90f);  // Blue
    palette[7] = glm::vec3(0.62f, 0.38f, 0.80f);  // Purple
    palette[8] = glm::vec3(0.95f, 0.95f, 0.95f);  // White
    palette[9] = glm::vec3(0.55f, 0.57f, 0.60f);  // Grey
    return palette;
}
