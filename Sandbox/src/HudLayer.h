#pragma once

#include "Cubit/Cubit.h"

#include <glm/gtc/matrix_transform.hpp>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

//Draws screen-space overlay art on top of the rendered scene.
class HudLayer final : public Layer
{
public:
    //Builds the quad, shader, and crosshair texture used by the overlay.
    HudLayer(std::uint32_t width, std::uint32_t height)
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

            void main()
            {
                color = texture(u_Texture, v_TexCoord) * u_Tint;
            }
        )";
        m_Shader = std::make_unique<Shader>(vertexSource, fragmentSource);

        m_Crosshair = CreateCrosshairTexture();
    }

    //Draws the overlay with depth testing off so it sits above the scene.
    void OnRender() override
    {
        Renderer::SetDepthTest(false);
        Renderer::BeginScene(m_Camera);

        m_Shader->SetInt("u_Texture", 0);
        m_Shader->SetFloat4("u_Tint", glm::vec4(1.0f));
        m_Crosshair->Bind(0);

        const float size = static_cast<float>(CrosshairPixels);
        const glm::vec3 corner(
            std::floor(static_cast<float>(m_Width) * 0.5f - size * 0.5f),
            std::floor(static_cast<float>(m_Height) * 0.5f - size * 0.5f),
            0.0f);

        glm::mat4 transform = glm::translate(glm::mat4(1.0f), corner);
        transform = glm::scale(transform, glm::vec3(size, size, 1.0f));

        Renderer::Submit(*m_VertexArray, *m_IndexBuffer, *m_Shader, transform);

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
            const bool inGap =
                i + armGap >= centre && i <= centre + armGap;
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

    std::unique_ptr<VertexArray> m_VertexArray;
    std::unique_ptr<VertexBuffer> m_VertexBuffer;
    std::unique_ptr<IndexBuffer> m_IndexBuffer;
    std::unique_ptr<Shader> m_Shader;
    std::unique_ptr<Texture2D> m_Crosshair;
    OrthographicCamera m_Camera;
    std::uint32_t m_Width = 0;
    std::uint32_t m_Height = 0;
};
