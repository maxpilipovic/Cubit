#pragma once

#include "Cubit/Renderer/FontAtlas.h"
#include "Cubit/Renderer/Texture2D.h"

#include <memory>
#include <string_view>

//A baked font on the GPU: a FontAtlas plus the texture it was uploaded into.
//
//Needs a live GL context, so one of these is built from a layer and never
//before the window exists - the same rule as Mesh, and the reason neither has
//a unit test.
class CB_API Font
{
public:
    //Uploads the atlas's coverage as an RGBA texture, white where the glyph is.
    explicit Font(const FontAtlas& atlas);

    //Keeps the atlas rather than copying its measurements out, so a glyph is
    //described in exactly one place.
    const FontAtlas& Atlas() const { return m_Atlas; }
    const Texture2D& Texture() const { return *m_Texture; }

    const FontAtlas::Glyph& GlyphFor(char character) const
    {
        return m_Atlas.GlyphFor(character);
    }

    float LineHeight() const { return m_Atlas.LineHeight(); }
    float Measure(std::string_view text) const { return m_Atlas.Measure(text); }

private:
    FontAtlas m_Atlas;
    std::unique_ptr<Texture2D> m_Texture;
};
