#include "cub.h"

#include "Cubit/Renderer/FontAtlas.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

namespace
{
    //Square, and big enough for printable ASCII at the sizes a HUD uses. A bake
    //that does not fit is refused rather than truncated, so this being too
    //small shows up as an error at load and not as missing letters on screen.
    constexpr int AtlasSize = 512;
}

FontAtlas FontAtlas::FromTrueType(std::span<const std::uint8_t> ttf, float pixelHeight)
{
    if (ttf.empty())
        throw std::runtime_error("font: no bytes to bake");

    stbtt_fontinfo info;
    const int offset = stbtt_GetFontOffsetForIndex(ttf.data(), 0);
    if (offset < 0 || !stbtt_InitFont(&info, ttf.data(), offset))
        throw std::runtime_error("font: not a TrueType font");

    FontAtlas atlas;
    atlas.m_Width = AtlasSize;
    atlas.m_Height = AtlasSize;

    std::vector<std::uint8_t> baked(
        static_cast<std::size_t>(AtlasSize) * AtlasSize, 0);
    std::vector<stbtt_bakedchar> characters(CharacterCount);

    //Negative means the glyphs did not fit; positive is how many rows were used.
    const int result = stbtt_BakeFontBitmap(
        ttf.data(), 0, pixelHeight, baked.data(), AtlasSize, AtlasSize,
        FirstCharacter, CharacterCount, characters.data());

    if (result <= 0)
        throw std::runtime_error("font: does not fit the atlas at this size");

    //stb fills its bitmap top row first; the engine's textures start at the
    //bottom row - see DebugFont::CreateTexture - so flip it once here rather
    //than flipping every glyph's coordinates at every draw.
    atlas.m_Pixels.resize(baked.size());
    for (int row = 0; row < AtlasSize; ++row)
    {
        const std::size_t source = static_cast<std::size_t>(row) * AtlasSize;
        const std::size_t destination =
            static_cast<std::size_t>(AtlasSize - 1 - row) * AtlasSize;

        std::copy_n(baked.begin() + source, AtlasSize,
            atlas.m_Pixels.begin() + destination);
    }

    const float size = static_cast<float>(AtlasSize);

    atlas.m_Glyphs.resize(CharacterCount);
    for (int i = 0; i < CharacterCount; ++i)
    {
        const stbtt_bakedchar& character = characters[i];
        Glyph& glyph = atlas.m_Glyphs[i];

        const float height =
            static_cast<float>(character.y1) - static_cast<float>(character.y0);

        //The rows moved when the bitmap was flipped, so the glyph's top edge in
        //stb's image is its bottom edge in ours.
        glyph.Uv0 = glm::vec2(
            static_cast<float>(character.x0) / size,
            (size - static_cast<float>(character.y1)) / size);
        glyph.Uv1 = glm::vec2(
            static_cast<float>(character.x1) / size,
            (size - static_cast<float>(character.y0)) / size);

        glyph.Size = glm::vec2(
            static_cast<float>(character.x1) - static_cast<float>(character.x0),
            height);

        //yoff is the distance from the baseline DOWN to the glyph's top edge,
        //so the distance from the baseline UP to its bottom edge is the
        //negation of the far edge.
        glyph.Bearing = glm::vec2(character.xoff, -(character.yoff + height));
        glyph.Advance = character.xadvance;
    }

    int ascent = 0;
    int descent = 0;
    int lineGap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);

    atlas.m_LineHeight =
        static_cast<float>(ascent - descent + lineGap) *
        stbtt_ScaleForPixelHeight(&info, pixelHeight);

    return atlas;
}

FontAtlas FontAtlas::FromFile(const std::string& path, float pixelHeight)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("font: cannot open file: " + path);

    const std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size))
        throw std::runtime_error("font: cannot read file: " + path);

    return FromTrueType(bytes, pixelHeight);
}

const FontAtlas::Glyph& FontAtlas::GlyphFor(char character) const
{
    const int index = static_cast<int>(character) - FirstCharacter;

    if (index < 0 || index >= static_cast<int>(m_Glyphs.size()))
        return GlyphFor('?');

    return m_Glyphs[static_cast<std::size_t>(index)];
}

float FontAtlas::Measure(std::string_view text) const
{
    float width = 0.0f;
    for (const char character : text)
        width += GlyphFor(character).Advance;

    return width;
}
