#pragma once

#include "Cubit/Core.h"
#include "Cubit/Renderer/Font.h"
#include "Cubit/Renderer/IndexBuffer.h"
#include "Cubit/Renderer/OrthographicCamera.h"
#include "Cubit/Renderer/Shader.h"
#include "Cubit/Renderer/Texture2D.h"
#include "Cubit/Renderer/VertexArray.h"
#include "Cubit/Renderer/VertexBuffer.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

//Screen-space drawing for an on-screen readout: text in the 5x7 debug font or in
//a real font the caller supplies, a crosshair, a full-screen wash, and any
//textured quad.
//
//The plumbing an on-screen readout needs - a pixel-space camera, a unit quad, a
//shader that samples one glyph out of an atlas, and the debug font's own atlas -
//rather than the readout. What to say belongs to whoever is drawing: an app's
//own layer owns one of these and draws its own lines through it, bringing its
//own Font if it wants one. Two apps wanting a readout is exactly why this is
//here and not in either of them.
//
//Needs a live GL context, so one of these is built from a layer, never before
//the window exists.
class CB_API ScreenOverlay
{
public:
    //Pixel size of the surface being drawn on - the framebuffer, not the window,
    //because that is what the viewport is sized in.
    ScreenOverlay(std::uint32_t width, std::uint32_t height);
    ~ScreenOverlay();

    ScreenOverlay(const ScreenOverlay&) = delete;
    ScreenOverlay& operator=(const ScreenOverlay&) = delete;

    //Twice the font's own size, which is what makes a 5x7 glyph readable.
    static constexpr float DefaultTextScale = 2.0f;

    //Distance from the edge to the first line, in pixels.
    static constexpr float Margin = 12.0f;

    //Opens a pixel-space scene with depth testing off, so the overlay sits above
    //everything drawn before it. Pair with End.
    void Begin() const;
    void End() const;

    //Draws one glyph per character, left to right, with `x` and `y` the bottom
    //left of the first glyph. A space advances without drawing, and a character
    //the font lacks draws as a blank rather than failing - which is why
    //DebugFontTests checks every label an app draws.
    void DrawText(std::string_view text, float x, float y,
        float scale = DefaultTextScale) const;

    //Draws a line of text in a real font. `x, y` is the pen on the BASELINE, in
    //the overlay's y-up pixel space - deliberately not the same as the debug
    //font's DrawText above, whose y is the bottom of a fixed glyph cell. A real
    //font has descenders, so a baseline is the only origin that makes sense. The
    //default scale differs too - a real font is baked at the size it wants,
    //where the 5x7 one needs doubling - so adding a font argument to an existing
    //call changes both where the text sits and how big it is.
    void DrawText(const Font& font, std::string_view text, float x, float y,
        float scale = 1.0f, const glm::vec4& colour = glm::vec4(1.0f)) const;

    //Width of that text if it were drawn, for centring and right-aligning.
    static float MeasureText(const Font& font, std::string_view text,
        float scale = 1.0f);

    //Submits one textured quad in pixel space.
    void DrawQuad(const Texture2D& texture, float x, float y, float width, float height,
        const glm::vec2& uvOffset = glm::vec2(0.0f),
        const glm::vec2& uvScale = glm::vec2(1.0f),
        const glm::vec4& tint = glm::vec4(1.0f)) const;

    //A small plus in the middle of the screen, with a gap so the block under it
    //stays visible.
    void DrawCrosshair() const;

    //Covers the whole surface, for an underwater wash or a fade.
    void FillScreen(const glm::vec4& colour) const;

    //Keeps the projection matched to a resized framebuffer. Zero in either
    //dimension is ignored: a minimised window reports one.
    void Resize(std::uint32_t width, std::uint32_t height);

    std::uint32_t Width() const { return m_Width; }
    std::uint32_t Height() const { return m_Height; }

    //Baseline-to-baseline distance for stacked lines of text.
    static float LineHeight(float scale = DefaultTextScale);

    //Where the top line of a debug-font readout sits. Only the harness uses it
    //now that the game stacks its lines from its own font's line height.
    float TopLine(float scale = DefaultTextScale) const;

    //One decimal place, without pulling in iostreams. Here because every
    //readout that shows a position needs it.
    static std::string FormatOneDecimal(float value);

private:
    std::unique_ptr<VertexArray> m_VertexArray;
    std::unique_ptr<VertexBuffer> m_VertexBuffer;
    std::unique_ptr<IndexBuffer> m_IndexBuffer;
    std::unique_ptr<Shader> m_Shader;
    std::unique_ptr<Texture2D> m_Crosshair;
    std::unique_ptr<Texture2D> m_Font;
    std::unique_ptr<Texture2D> m_White;
    OrthographicCamera m_Camera;
    std::uint32_t m_Width = 0;
    std::uint32_t m_Height = 0;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
