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

    //A font's table directory: the 12-byte offset table, then one 16-byte record
    //per table.
    constexpr std::size_t OffsetTableSize = 12;
    constexpr std::size_t TableRecordSize = 16;

    //Everything in a font file is big-endian whatever the host is, so these read
    //byte by byte rather than copying into an integer.
    std::uint16_t ReadBigEndian16(std::span<const std::uint8_t> bytes, std::size_t at)
    {
        return static_cast<std::uint16_t>(
            (static_cast<std::uint32_t>(bytes[at]) << 8) |
            static_cast<std::uint32_t>(bytes[at + 1]));
    }

    std::uint64_t ReadBigEndian32(std::span<const std::uint8_t> bytes, std::size_t at)
    {
        return (static_cast<std::uint64_t>(bytes[at]) << 24) |
            (static_cast<std::uint64_t>(bytes[at + 1]) << 16) |
            (static_cast<std::uint64_t>(bytes[at + 2]) << 8) |
            static_cast<std::uint64_t>(bytes[at + 3]);
    }

    //stb_truetype does no bounds checking of any kind - its own header says so -
    //so a file cut short by an interrupted copy reads past the end of the buffer
    //instead of failing, and the tag at the front is still perfectly valid. The
    //font's own table directory is the only thing that says how far the bytes are
    //supposed to reach, so walk it here, while there is still a buffer size to
    //compare it against.
    void RequireWholeFont(std::span<const std::uint8_t> ttf, std::size_t offset)
    {
        const std::uint64_t size = ttf.size();

        if (offset + OffsetTableSize > size)
            throw std::runtime_error("font: truncated before the table directory");

        const std::size_t tableCount = ReadBigEndian16(ttf, offset + 4);
        const std::uint64_t directoryEnd = static_cast<std::uint64_t>(offset) +
            OffsetTableSize + static_cast<std::uint64_t>(tableCount) * TableRecordSize;

        if (directoryEnd > size)
            throw std::runtime_error("font: truncated inside the table directory");

        for (std::size_t i = 0; i < tableCount; ++i)
        {
            const std::size_t record =
                offset + OffsetTableSize + i * TableRecordSize;

            const std::uint64_t tableOffset = ReadBigEndian32(ttf, record + 8);
            const std::uint64_t tableLength = ReadBigEndian32(ttf, record + 12);

            if (tableOffset + tableLength > size)
                throw std::runtime_error("font: truncated - a table runs past the end of the file");
        }
    }
}

FontAtlas FontAtlas::FromTrueType(std::span<const std::uint8_t> ttf, float pixelHeight)
{
    if (ttf.empty())
        throw std::runtime_error("font: no bytes to bake");

    //The tag check below reads four bytes and the directory walk reads twelve,
    //neither of which stb will bounds check for us.
    if (ttf.size() < OffsetTableSize)
        throw std::runtime_error("font: truncated before the table directory");

    stbtt_fontinfo info;
    const int offset = stbtt_GetFontOffsetForIndex(ttf.data(), 0);
    if (offset < 0)
        throw std::runtime_error("font: not a TrueType font");

    RequireWholeFont(ttf, static_cast<std::size_t>(offset));

    if (!stbtt_InitFont(&info, ttf.data(), offset))
        throw std::runtime_error("font: not a TrueType font");

    FontAtlas atlas;
    atlas.m_Width = AtlasSize;
    atlas.m_Height = AtlasSize;

    std::vector<std::uint8_t> baked(
        static_cast<std::size_t>(AtlasSize) * AtlasSize, 0);
    std::vector<stbtt_bakedchar> characters(CharacterCount);

    //Positive is how many rows were used. Anything else means the glyphs did not
    //fit: stb returns the negated index of the one that overflowed, which is
    //plain zero when that is the very first glyph - so this is `<= 0` and not
    //`< 0`, and tidying it to the latter would let a failed bake through.
    const int result = stbtt_BakeFontBitmap(
        ttf.data(), offset, pixelHeight, baked.data(), AtlasSize, AtlasSize,
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
    int index = static_cast<int>(character) - FirstCharacter;

    //Substitute the index rather than calling back into GlyphFor('?'): a
    //recursive call would assume '?' is in range, which only holds once
    //m_Glyphs is fully baked. Looking the fallback up by index instead keeps
    //that assumption out of this function altogether.
    if (index < 0 || index >= static_cast<int>(m_Glyphs.size()))
        index = '?' - FirstCharacter;

    return m_Glyphs[static_cast<std::size_t>(index)];
}

float FontAtlas::Measure(std::string_view text) const
{
    float width = 0.0f;
    for (const char character : text)
        width += GlyphFor(character).Advance;

    return width;
}

std::vector<std::uint8_t> FontAtlas::RgbaPixels() const
{
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(m_Pixels.size()) * 4);

    for (std::size_t i = 0; i < m_Pixels.size(); ++i)
    {
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = m_Pixels[i];
    }

    return rgba;
}
