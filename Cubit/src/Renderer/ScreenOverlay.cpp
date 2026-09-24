#include "cub.h"

#include "Cubit/Renderer/ScreenOverlay.h"

#include "Cubit/Renderer/DebugFont.h"
#include "Cubit/Renderer/Renderer.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstddef>

namespace
{
    constexpr std::uint32_t CrosshairPixels = 24;
    constexpr std::uint32_t CrosshairTextureSize = 12;

    //Writes one opaque white pixel into an RGBA buffer.
    void SetPixel(std::vector<std::uint8_t>& pixels, std::uint32_t size,
        std::uint32_t x, std::uint32_t y)
    {
        if (x >= size || y >= size)
            return;

        const std::size_t index = (static_cast<std::size_t>(y) * size + x) * 4;
        pixels[index + 0] = 255;
        pixels[index + 1] = 255;
        pixels[index + 2] = 255;
        pixels[index + 3] = 255;
    }

    //A small plus sign with a transparent background.
    std::unique_ptr<Texture2D> CreateCrosshairTexture()
    {
        constexpr std::uint32_t size = CrosshairTextureSize;
        constexpr std::uint32_t centre = size / 2;
        constexpr std::uint32_t armGap = 2;

        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size) * size * 4, 0);

        for (std::uint32_t i = 0; i < size; ++i)
        {
            //Leave a gap in the middle so the aimed-at block stays visible.
            const bool inGap = i + armGap >= centre && i <= centre + armGap;
            if (inGap)
                continue;

            SetPixel(pixels, size, i, centre);
            SetPixel(pixels, size, i, centre - 1);
            SetPixel(pixels, size, centre, i);
            SetPixel(pixels, size, centre - 1, i);
        }

        return std::make_unique<Texture2D>(size, size, pixels.data());
    }

    //A single white pixel. The overlay shader always samples a texture, so a
    //flat tinted fill needs something neutral to multiply against.
    std::unique_ptr<Texture2D> CreateWhiteTexture()
    {
        const std::uint8_t pixel[4] = { 255, 255, 255, 255 };
        return std::make_unique<Texture2D>(1, 1, pixel);
    }
}

ScreenOverlay::ScreenOverlay(std::uint32_t width, std::uint32_t height)
    : m_Camera(0.0f, static_cast<float>(width), 0.0f, static_cast<float>(height)),
      m_Width(width),
      m_Height(height)
{
    //A unit quad in the positive corner, positioned and sized by a transform.
    //Wound counter-clockwise so backface culling keeps it.
    const float vertices[] =
    {
        // Position          UV
        0.0f, 0.0f, 0.0f,    0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,    1.0f, 0.0f,
        1.0f, 1.0f, 0.0f,    1.0f, 1.0f,
        0.0f, 1.0f, 0.0f,    0.0f, 1.0f
    };
    const std::uint32_t indices[] = { 0, 1, 2, 2, 3, 0 };

    m_VertexArray = std::make_unique<VertexArray>();
    m_VertexBuffer = std::make_unique<VertexBuffer>(
        vertices,
        static_cast<std::uint32_t>(sizeof(vertices)));
    m_VertexArray->AddBuffer(
        *m_VertexBuffer,
        BufferLayout{ ShaderDataType::Float3, ShaderDataType::Float2 });
    m_IndexBuffer = std::make_unique<IndexBuffer>(indices, 6);

    constexpr std::string_view vertexSource = R"(
        #version 330 core
        layout(location = 0) in vec3 a_Position;
        layout(location = 1) in vec2 a_TexCoord;
        uniform mat4 u_ViewProjection;
        uniform mat4 u_Transform;
        out vec2 v_TexCoord;

        void main()
        {
            v_TexCoord = a_TexCoord;
            gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 1.0);
        }
    )";
    constexpr std::string_view fragmentSource = R"(
        #version 330 core
        layout(location = 0) out vec4 color;
        in vec2 v_TexCoord;
        uniform sampler2D u_Texture;
        uniform vec4 u_Tint;
        uniform vec2 u_UvOffset;
        uniform vec2 u_UvScale;

        void main()
        {
            color = texture(u_Texture, u_UvOffset + v_TexCoord * u_UvScale) * u_Tint;
        }
    )";
    m_Shader = std::make_unique<Shader>(vertexSource, fragmentSource);

    m_Crosshair = CreateCrosshairTexture();
    m_Font = DebugFont::CreateTexture();
    m_White = CreateWhiteTexture();
}

ScreenOverlay::~ScreenOverlay() = default;

void ScreenOverlay::Begin() const
{
    Renderer::SetDepthTest(false);
    Renderer::BeginScene(m_Camera);
}

void ScreenOverlay::End() const
{
    Renderer::EndScene();
    Renderer::SetDepthTest(true);
}

void ScreenOverlay::DrawText(std::string_view text, float x, float y, float scale) const
{
    const float glyphWidth = DebugFont::GlyphWidth * scale;
    const float glyphHeight = DebugFont::GlyphHeight * scale;
    const float advance = DebugFont::CellWidth * scale;
    const float atlasWidth =
        static_cast<float>(DebugFont::GlyphCount * DebugFont::CellWidth);

    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == ' ')
            continue;

        const std::uint32_t glyph = DebugFont::IndexOf(text[i]);

        //Sample only the glyph's cell, excluding its padding column and row.
        const glm::vec2 uvOffset(
            static_cast<float>(glyph * DebugFont::CellWidth) / atlasWidth,
            0.0f);
        const glm::vec2 uvScale(
            static_cast<float>(DebugFont::GlyphWidth) / atlasWidth,
            static_cast<float>(DebugFont::GlyphHeight) / DebugFont::CellHeight);

        DrawQuad(
            *m_Font,
            x + static_cast<float>(i) * advance,
            y,
            glyphWidth,
            glyphHeight,
            uvOffset,
            uvScale);
    }
}

void ScreenOverlay::DrawText(const Font& font, std::string_view text, float x, float y,
    float scale, const glm::vec4& colour) const
{
    float pen = x;

    for (const char character : text)
    {
        const FontAtlas::Glyph& glyph = font.GlyphFor(character);

        //A space has an advance and no pixels. Skipping it saves a draw call
        //and, more importantly, saves sampling a zero-area region of the atlas.
        if (glyph.Size.x > 0.0f && glyph.Size.y > 0.0f)
        {
            //Snap the quad's corner to a whole pixel, exactly as
            //stbtt_GetBakedQuad does. The atlas is sampled with GL_NEAREST, so a
            //glyph landing on a fractional pixel drops a different set of texel
            //columns from its neighbour - an advance rarely divides evenly, so
            //every glyph on a line would otherwise sit at its own sub-pixel
            //phase. Rounding costs nothing and makes a line of text render the
            //same way twice.
            DrawQuad(
                font.Texture(),
                std::floor(pen + glyph.Bearing.x * scale + 0.5f),
                std::floor(y + glyph.Bearing.y * scale + 0.5f),
                glyph.Size.x * scale,
                glyph.Size.y * scale,
                glyph.Uv0,
                glyph.Uv1 - glyph.Uv0,
                colour);
        }

        pen += glyph.Advance * scale;
    }
}

float ScreenOverlay::MeasureText(const Font& font, std::string_view text, float scale)
{
    return font.Measure(text) * scale;
}

void ScreenOverlay::DrawQuad(const Texture2D& texture, float x, float y,
    float width, float height, const glm::vec2& uvOffset, const glm::vec2& uvScale,
    const glm::vec4& tint) const
{
    m_Shader->SetInt("u_Texture", 0);
    m_Shader->SetFloat4("u_Tint", tint);
    m_Shader->SetFloat2("u_UvOffset", uvOffset);
    m_Shader->SetFloat2("u_UvScale", uvScale);
    texture.Bind(0);

    glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(x, y, 0.0f));
    transform = glm::scale(transform, glm::vec3(width, height, 1.0f));

    Renderer::Submit(*m_VertexArray, *m_IndexBuffer, *m_Shader, transform);
}

void ScreenOverlay::DrawCrosshair() const
{
    const float size = static_cast<float>(CrosshairPixels);
    DrawQuad(
        *m_Crosshair,
        std::floor(static_cast<float>(m_Width) * 0.5f - size * 0.5f),
        std::floor(static_cast<float>(m_Height) * 0.5f - size * 0.5f),
        size,
        size);
}

void ScreenOverlay::FillScreen(const glm::vec4& colour) const
{
    DrawQuad(
        *m_White,
        0.0f,
        0.0f,
        static_cast<float>(m_Width),
        static_cast<float>(m_Height),
        glm::vec2(0.0f),
        glm::vec2(1.0f),
        colour);
}

void ScreenOverlay::Resize(std::uint32_t width, std::uint32_t height)
{
    if (width == 0 || height == 0)
        return;

    m_Width = width;
    m_Height = height;
    m_Camera.SetProjection(
        0.0f,
        static_cast<float>(m_Width),
        0.0f,
        static_cast<float>(m_Height));
}

float ScreenOverlay::LineHeight(float scale)
{
    //Three pixels of leading, which is what the readout has always used.
    return (DebugFont::GlyphHeight + 3) * scale;
}

float ScreenOverlay::TopLine(float scale) const
{
    return static_cast<float>(m_Height) - Margin - DebugFont::GlyphHeight * scale;
}

std::string ScreenOverlay::FormatOneDecimal(float value)
{
    const bool negative = value < 0.0f;
    const long scaled = std::lround(std::abs(value) * 10.0f);

    return (negative ? "-" : "") +
        std::to_string(scaled / 10) + "." + std::to_string(scaled % 10);
}
