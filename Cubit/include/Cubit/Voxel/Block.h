#pragma once

#include <glm/glm.hpp>
#include <array>
#include <cstdint>

//A block is a palette index, not a material. 0 is empty (air). Colour lives in
//the world's palette, loaded from the map file, so there is nothing to texture.
using BlockId = std::uint8_t;

//256 colours addressed directly by BlockId. Index 0 (air) is never rendered.
//The alpha channel is what makes a block transparent: a block is opaque when its
//alpha is 1.0, and anything less lets the blocks behind it show through.
using Palette = std::array<glm::vec4, 256>;

//Reports whether a palette colour hides what is behind it. Air is alpha 0, so it
//falls out of this rule rather than needing a special case.
constexpr bool IsOpaqueColor(const glm::vec4& color)
{
    return color.a >= 1.0f;
}

//Reports whether a block occupies a cell at all, as opposed to being air.
//Palette-blind on purpose: whether that block stops light or stops movement
//are questions only the world can answer, because only the world holds the
//palette those answers come from.
constexpr bool IsPresent(BlockId block)
{
    return block != 0;
}

//The palette a world uses before a map overrides it. Reproduces the colours the
//engine shipped with, at the indices the old named blocks used, so test terrain
//reads the same after the refactor. Everything but air is fully opaque.
inline Palette DefaultPalette()
{
    Palette palette;
    const glm::vec4 solid(0.78f, 0.85f, 1.00f, 1.0f);
    palette.fill(solid);
    palette[0] = glm::vec4(0.0f);                       // Air, never rendered
    palette[1] = solid;                                 // Solid
    palette[2] = glm::vec4(0.85f, 0.25f, 0.25f, 1.0f);  // Red
    palette[3] = glm::vec4(0.90f, 0.55f, 0.20f, 1.0f);  // Orange
    palette[4] = glm::vec4(0.92f, 0.85f, 0.30f, 1.0f);  // Yellow
    palette[5] = glm::vec4(0.35f, 0.75f, 0.35f, 1.0f);  // Green
    palette[6] = glm::vec4(0.30f, 0.50f, 0.90f, 1.0f);  // Blue
    palette[7] = glm::vec4(0.62f, 0.38f, 0.80f, 1.0f);  // Purple
    palette[8] = glm::vec4(0.95f, 0.95f, 0.95f, 1.0f);  // White
    palette[9] = glm::vec4(0.55f, 0.57f, 0.60f, 1.0f);  // Grey
    return palette;
}
