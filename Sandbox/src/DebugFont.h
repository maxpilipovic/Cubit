#pragma once

#include "Cubit/Cubit.h"

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

//A minimal 5x7 bitmap font covering only the characters the debug readout
//needs. Defined in code so the sandbox has no asset to load; a real font
//replaces this once there is an asset pipeline.
namespace DebugFont
{
    constexpr std::uint32_t GlyphWidth = 5;
    constexpr std::uint32_t GlyphHeight = 7;

    //One pixel of padding keeps neighbouring glyphs from bleeding into each
    //other when sampled.
    constexpr std::uint32_t CellWidth = GlyphWidth + 1;
    constexpr std::uint32_t CellHeight = GlyphHeight + 1;

    //Characters in the same order as the glyph table below.
    constexpr std::string_view Order = "0123456789-.: ACDEFGNOPS";

    //Each glyph is seven rows of five columns, top row first.
    constexpr const char* Glyphs[][GlyphHeight] =
    {
        { ".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###." }, // 0
        { "..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###." }, // 1
        { ".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####" }, // 2
        { "#####", "...#.", "..#..", "...#.", "....#", "#...#", ".###." }, // 3
        { "...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#." }, // 4
        { "#####", "#....", "####.", "....#", "....#", "#...#", ".###." }, // 5
        { "..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###." }, // 6
        { "#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..." }, // 7
        { ".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###." }, // 8
        { ".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.." }, // 9
        { ".....", ".....", ".....", "#####", ".....", ".....", "....." }, // -
        { ".....", ".....", ".....", ".....", ".....", ".##..", ".##.." }, // .
        { ".....", ".##..", ".##..", ".....", ".##..", ".##..", "....." }, // :
        { ".....", ".....", ".....", ".....", ".....", ".....", "....." }, // space
        { ".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#" }, // A
        { ".###.", "#...#", "#....", "#....", "#....", "#...#", ".###." }, // C
        { "####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####." }, // D
        { "#####", "#....", "#....", "####.", "#....", "#....", "#####" }, // E
        { "#####", "#....", "#....", "####.", "#....", "#....", "#...." }, // F
        { ".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".###." }, // G
        { "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#", "#...#" }, // N
        { ".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###." }, // O
        { "####.", "#...#", "#...#", "####.", "#....", "#....", "#...." }, // P
        { ".####", "#....", "#....", ".###.", "....#", "....#", "####." }  // S
    };

    constexpr std::uint32_t GlyphCount =
        static_cast<std::uint32_t>(sizeof(Glyphs) / sizeof(Glyphs[0]));

    //Returns the glyph slot for a character, falling back to the blank glyph.
    inline std::uint32_t IndexOf(char character)
    {
        const std::size_t found = Order.find(character);
        if (found == std::string_view::npos)
            return static_cast<std::uint32_t>(Order.find(' '));

        return static_cast<std::uint32_t>(found);
    }

    //Builds a single-row atlas holding every glyph.
    inline std::unique_ptr<Texture2D> CreateTexture()
    {
        const std::uint32_t width = GlyphCount * CellWidth;
        const std::uint32_t height = CellHeight;

        std::vector<std::uint8_t> pixels(
            static_cast<std::size_t>(width) * height * 4, 0);

        for (std::uint32_t glyph = 0; glyph < GlyphCount; ++glyph)
        {
            for (std::uint32_t row = 0; row < GlyphHeight; ++row)
            {
                const std::string_view line = Glyphs[glyph][row];

                for (std::uint32_t column = 0; column < GlyphWidth; ++column)
                {
                    if (line[column] != '#')
                        continue;

                    //Texture row zero is the bottom of the quad, so the top row
                    //of a glyph is written to the highest pixel row.
                    const std::uint32_t x = glyph * CellWidth + column;
                    const std::uint32_t y = GlyphHeight - 1 - row;

                    const std::size_t index =
                        (static_cast<std::size_t>(y) * width + x) * 4;
                    pixels[index + 0] = 255;
                    pixels[index + 1] = 255;
                    pixels[index + 2] = 255;
                    pixels[index + 3] = 255;
                }
            }
        }

        return std::make_unique<Texture2D>(width, height, pixels.data());
    }
}
