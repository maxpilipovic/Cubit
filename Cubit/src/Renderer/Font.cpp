#include "cub.h"

#include "Cubit/Renderer/Font.h"

#include <vector>

Font::Font(const FontAtlas& atlas)
    : m_Atlas(atlas)
{
    //The coverage-to-RGBA expansion needs no GL context, so it lives on
    //FontAtlas where it can be unit-tested; this constructor's only GL work is
    //the upload itself.
    const std::vector<std::uint8_t> rgba = m_Atlas.RgbaPixels();

    m_Texture = std::make_unique<Texture2D>(
        m_Atlas.Width(), m_Atlas.Height(), rgba.data());
}

Font::~Font() = default;

//m_Texture is a unique_ptr, and a unique_ptr's own move already transfers
//ownership and nulls the source; m_Atlas owns no GPU handle either, so there
//is no raw handle left for this type to null by hand - unlike VertexArray or
//IndexBuffer, which own a raw m_RendererId and so write their moves themselves.
Font::Font(Font&& other) noexcept = default;

Font& Font::operator=(Font&& other) noexcept = default;
