#pragma once

#include "Cubit/Core.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

//A TrueType font baked into one bitmap, with the measurements needed to draw
//from it. Holds no GL object, so it can be built and tested without a window -
//the same split as MeshGeometry against Mesh.
class CB_API FontAtlas
{
public:
    //Where one character sits in the atlas and how it sits on the line.
    //
    //Measured in the overlay's own axes: y points UP, and Bearing.y is the
    //distance from the baseline up to the glyph's BOTTOM edge, so a descender
    //like g is negative. stb_truetype counts the other way, and converting it
    //once here is what keeps that out of every caller.
    struct Glyph
    {
        glm::vec2 Uv0{ 0.0f };
        glm::vec2 Uv1{ 0.0f };
        glm::vec2 Size{ 0.0f };
        glm::vec2 Bearing{ 0.0f };
        float Advance = 0.0f;
    };

    //Printable ASCII, and nothing else: the game writes English and a bake of
    //every codepoint would cost an atlas far larger than a HUD needs.
    static constexpr char FirstCharacter = ' ';
    static constexpr char LastCharacter = '~';
    static constexpr int CharacterCount = LastCharacter - FirstCharacter + 1;

    //Bakes the font at a pixel height. Throws std::runtime_error when the bytes
    //are not a font, or when the glyphs do not fit the atlas - a truncated bake
    //would surface as missing letters much later and far from the cause.
    static FontAtlas FromTrueType(std::span<const std::uint8_t> ttf, float pixelHeight);

    //Reads a .ttf and bakes it, mirroring VoxLoader::LoadFile. Throws when the
    //file cannot be read.
    static FontAtlas FromFile(const std::string& path, float pixelHeight);

    //The glyph for a character, or the one for '?' when it is outside the baked
    //range. Never a blank: silently drawing nothing is the failure this whole
    //type exists to replace.
    const Glyph& GlyphFor(char character) const;

    //Baseline to baseline, including the font's own line gap.
    float LineHeight() const { return m_LineHeight; }

    //Width of the text in pixels at the baked height, which is what centring
    //and right-aligning are arithmetic on.
    float Measure(std::string_view text) const;

    //Tightly packed 8-bit coverage. Row zero is the BOTTOM row, matching the
    //engine's texture convention - see DebugFont::CreateTexture.
    const std::vector<std::uint8_t>& Pixels() const { return m_Pixels; }
    std::uint32_t Width() const { return m_Width; }
    std::uint32_t Height() const { return m_Height; }

private:
    std::vector<std::uint8_t> m_Pixels;
    std::uint32_t m_Width = 0;
    std::uint32_t m_Height = 0;
    float m_LineHeight = 0.0f;
    std::vector<Glyph> m_Glyphs;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
