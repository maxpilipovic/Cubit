#pragma once

#include "Cubit/Cubit.h"

#include "DebugFont.h"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

//Values the sandbox publishes for the overlay to display.
struct HudState
{
    glm::vec3 PlayerPosition{ 0.0f };
    bool Grounded = false;
    std::uint32_t MeshFaceCount = 0;
};

//Draws screen-space overlay art on top of the rendered scene.
class HudLayer final : public Layer
{
public:
    //Builds the quad, shader, and textures used by the overlay.
    HudLayer(std::shared_ptr<const HudState> state, std::uint32_t width, std::uint32_t height)
        : m_State(std::move(state)),
          m_Camera(0.0f, static_cast<float>(width), 0.0f, static_cast<float>(height)),
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
    }

    //Tracks a smoothed frame rate for the readout.
    void OnUpdate(Timestep timestep) override
    {
        const float seconds = static_cast<float>(timestep.GetSeconds());
        if (seconds <= 0.0f)
            return;

        //Exponential smoothing, otherwise the number is unreadable.
        const float instant = 1.0f / seconds;
        m_SmoothedFps = m_SmoothedFps <= 0.0f
            ? instant
            : m_SmoothedFps * 0.9f + instant * 0.1f;
    }

    //Draws the overlay with depth testing off so it sits above the scene.
    void OnRender() override
    {
        Renderer::SetDepthTest(false);
        Renderer::BeginScene(m_Camera);

        DrawCrosshair();
        DrawReadout();

        Renderer::EndScene();
        Renderer::SetDepthTest(true);
    }

    //Keeps the overlay projection matched to the framebuffer, which is what the
    //viewport is sized in. It can differ from window coordinates on displays
    //with scaling.
    void OnEvent(Event& event) override
    {
        EventDispatcher dispatcher(event);
        dispatcher.Dispatch<FramebufferResizeEvent>(
            [this](FramebufferResizeEvent& resizeEvent)
            {
                return OnFramebufferResized(resizeEvent);
            });
    }

private:
    static constexpr std::uint32_t CrosshairPixels = 24;
    static constexpr std::uint32_t CrosshairTextureSize = 12;
    static constexpr float TextScale = 2.0f;
    static constexpr float TextMargin = 12.0f;

    //Rebuilds the pixel-space projection after the framebuffer changes size.
    bool OnFramebufferResized(FramebufferResizeEvent& event)
    {
        if (event.GetWidth() == 0 || event.GetHeight() == 0)
            return false;

        m_Width = event.GetWidth();
        m_Height = event.GetHeight();
        m_Camera.SetProjection(
            0.0f,
            static_cast<float>(m_Width),
            0.0f,
            static_cast<float>(m_Height));

        return false;
    }

    void DrawCrosshair() const
    {
        const float size = static_cast<float>(CrosshairPixels);
        DrawQuad(
            *m_Crosshair,
            std::floor(static_cast<float>(m_Width) * 0.5f - size * 0.5f),
            std::floor(static_cast<float>(m_Height) * 0.5f - size * 0.5f),
            size,
            size,
            glm::vec2(0.0f),
            glm::vec2(1.0f));
    }

    //Draws the debug lines down from the top-left corner.
    void DrawReadout() const
    {
        const float lineHeight = (DebugFont::GlyphHeight + 3) * TextScale;
        float y = static_cast<float>(m_Height) - TextMargin - DebugFont::GlyphHeight * TextScale;

        const glm::vec3& position = m_State->PlayerPosition;
        DrawText(
            "POS " + FormatOneDecimal(position.x) +
            " " + FormatOneDecimal(position.y) +
            " " + FormatOneDecimal(position.z),
            TextMargin,
            y);

        y -= lineHeight;
        DrawText(std::string("GND ") + (m_State->Grounded ? "1" : "0"), TextMargin, y);

        y -= lineHeight;
        DrawText("FACES " + std::to_string(m_State->MeshFaceCount), TextMargin, y);

        y -= lineHeight;
        DrawText("FPS " + std::to_string(static_cast<int>(m_SmoothedFps + 0.5f)), TextMargin, y);
    }

    //Draws one glyph per character, left to right.
    void DrawText(std::string_view text, float x, float y) const
    {
        const float glyphWidth = DebugFont::GlyphWidth * TextScale;
        const float glyphHeight = DebugFont::GlyphHeight * TextScale;
        const float advance = DebugFont::CellWidth * TextScale;
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

    //Submits one textured screen-space quad.
    void DrawQuad(
        const Texture2D& texture,
        float x,
        float y,
        float width,
        float height,
        const glm::vec2& uvOffset,
        const glm::vec2& uvScale) const
    {
        m_Shader->SetInt("u_Texture", 0);
        m_Shader->SetFloat4("u_Tint", glm::vec4(1.0f));
        m_Shader->SetFloat2("u_UvOffset", uvOffset);
        m_Shader->SetFloat2("u_UvScale", uvScale);
        texture.Bind(0);

        glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(x, y, 0.0f));
        transform = glm::scale(transform, glm::vec3(width, height, 1.0f));

        Renderer::Submit(*m_VertexArray, *m_IndexBuffer, *m_Shader, transform);
    }

    //Formats a value with a single decimal place, without pulling in iostreams.
    static std::string FormatOneDecimal(float value)
    {
        const bool negative = value < 0.0f;
        const long scaled = std::lround(std::abs(value) * 10.0f);

        return (negative ? "-" : "") +
            std::to_string(scaled / 10) + "." + std::to_string(scaled % 10);
    }

    //Draws a small plus sign with a transparent background.
    static std::unique_ptr<Texture2D> CreateCrosshairTexture()
    {
        constexpr std::uint32_t size = CrosshairTextureSize;
        constexpr std::uint32_t centre = size / 2;
        constexpr std::uint32_t armGap = 2;

        std::vector<std::uint8_t> pixels(
            static_cast<std::size_t>(size) * size * 4, 0);

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

    //Writes one opaque white pixel into an RGBA buffer.
    static void SetPixel(
        std::vector<std::uint8_t>& pixels,
        std::uint32_t size,
        std::uint32_t x,
        std::uint32_t y)
    {
        if (x >= size || y >= size)
            return;

        const std::size_t index = (static_cast<std::size_t>(y) * size + x) * 4;
        pixels[index + 0] = 255;
        pixels[index + 1] = 255;
        pixels[index + 2] = 255;
        pixels[index + 3] = 255;
    }

    std::shared_ptr<const HudState> m_State;
    std::unique_ptr<VertexArray> m_VertexArray;
    std::unique_ptr<VertexBuffer> m_VertexBuffer;
    std::unique_ptr<IndexBuffer> m_IndexBuffer;
    std::unique_ptr<Shader> m_Shader;
    std::unique_ptr<Texture2D> m_Crosshair;
    std::unique_ptr<Texture2D> m_Font;
    OrthographicCamera m_Camera;
    std::uint32_t m_Width = 0;
    std::uint32_t m_Height = 0;
    float m_SmoothedFps = 0.0f;
};
