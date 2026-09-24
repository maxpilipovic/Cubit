#include "cub.h"

#include "Cubit/Renderer/Font.h"

#include <vector>

Font::Font(const FontAtlas& atlas)
    : m_Atlas(atlas)
{
    //Texture2D takes RGBA, and the bake is one coverage byte per pixel. White
    //everywhere, with the coverage as alpha, so a tint at draw time decides the
    //colour and the same font serves a white readout and a red warning.
    std::vector<std::uint8_t> rgba(
        static_cast<std::size_t>(m_Atlas.Pixels().size()) * 4);

    for (std::size_t i = 0; i < m_Atlas.Pixels().size(); ++i)
    {
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = m_Atlas.Pixels()[i];
    }

    m_Texture = std::make_unique<Texture2D>(
        m_Atlas.Width(), m_Atlas.Height(), rgba.data());
}
