#include "cub.h"

#include "Cubit/Renderer/DebugDraw.h"

#include "Cubit/Renderer/Camera.h"
#include "Cubit/Renderer/DebugLineBatch.h"
#include "Cubit/Renderer/Shader.h"
#include "Cubit/Renderer/VertexArray.h"
#include "Cubit/Renderer/VertexBuffer.h"

#include <glad/glad.h>

#include <memory>

namespace
{
    DebugLineBatch s_Batch;

    std::unique_ptr<Shader> s_Shader;
    std::unique_ptr<VertexArray> s_VertexArray;
    std::unique_ptr<VertexBuffer> s_VertexBuffer;
    std::uint32_t s_Capacity = 0;

    constexpr std::string_view VertexSource = R"(
        #version 330 core
        layout(location = 0) in vec3 a_Position;
        layout(location = 1) in vec4 a_Color;

        uniform mat4 u_ViewProjection;
        uniform mat4 u_Transform;

        out vec4 v_Color;

        void main()
        {
            v_Color = a_Color;
            gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 1.0);
        }
    )";

    constexpr std::string_view FragmentSource = R"(
        #version 330 core
        layout(location = 0) out vec4 color;

        in vec4 v_Color;

        void main()
        {
            color = v_Color;
        }
    )";

    //Creates the GPU resources on first use and grows the vertex buffer when a
    //frame needs more room than any previous frame did. Lazy because a debug
    //drawer nothing draws through should cost no GPU memory, and because this
    //runs long after the context exists.
    void EnsureCapacity(std::uint32_t vertexCount)
    {
        if (s_Shader == nullptr)
            s_Shader = std::make_unique<Shader>(VertexSource, FragmentSource);

        if (s_VertexBuffer != nullptr && vertexCount <= s_Capacity)
            return;

        s_VertexBuffer = std::make_unique<VertexBuffer>(
            static_cast<std::uint32_t>(vertexCount * sizeof(DebugVertex)));

        // The array records the buffer it was configured against, so it is
        // rebuilt alongside it rather than left pointing at the freed one.
        s_VertexArray = std::make_unique<VertexArray>();
        s_VertexArray->AddBuffer(
            *s_VertexBuffer,
            BufferLayout{
                { ShaderDataType::Float3 },
                { ShaderDataType::Float4 }
            });

        s_Capacity = vertexCount;
    }
}

void DebugDraw::Line(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color)
{
    s_Batch.AddLine(from, to, color);
}

void DebugDraw::Box(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color)
{
    s_Batch.AddBox(min, max, color);
}

void DebugDraw::Flush(const Camera& camera, const glm::mat4& transform)
{
    const std::vector<DebugVertex>& vertices = s_Batch.Vertices();

    // The common case once the engine is littered with DebugDraw calls is a
    // frame drawing none of them. That must cost a branch, not a bind.
    if (vertices.empty())
        return;

    EnsureCapacity(static_cast<std::uint32_t>(vertices.size()));

    s_VertexBuffer->SetData(
        vertices.data(),
        static_cast<std::uint32_t>(vertices.size() * sizeof(DebugVertex)));

    s_Shader->Bind();
    s_Shader->SetMat4("u_ViewProjection", camera.GetViewProjectionMatrix());
    s_Shader->SetMat4("u_Transform", transform);

    s_VertexArray->Bind();
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(vertices.size()));

    s_Batch.Clear();
}

void DebugDraw::Clear()
{
    s_Batch.Clear();
}

void DebugDraw::Shutdown()
{
    s_VertexArray.reset();
    s_VertexBuffer.reset();
    s_Shader.reset();
    s_Capacity = 0;
    s_Batch.Clear();
}
