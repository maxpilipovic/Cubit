#include "cub.h"

#include "Cubit/Renderer/Renderer.h"
#include "Cubit/Renderer/Camera.h"
#include "Cubit/Renderer/IndexBuffer.h"
#include "Cubit/Renderer/Shader.h"
#include "Cubit/Renderer/VertexArray.h"

#include <glad/glad.h>

namespace
{
    glm::mat4 s_ViewProjection{ 1.0f };
}

void Renderer::Init()
{
    glEnable(GL_DEPTH_TEST);

    //Discards inward-facing triangles; meshes must wind faces counter-clockwise.
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
}

void Renderer::SetViewport(
    std::int32_t x,
    std::int32_t y,
    std::uint32_t width,
    std::uint32_t height)
{
    glViewport(
        x,
        y,
        static_cast<GLsizei>(width),
        static_cast<GLsizei>(height));
}

void Renderer::SetClearColor(float red, float green, float blue, float alpha)
{
    glClearColor(red, green, blue, alpha);
}

void Renderer::Clear()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::BeginScene(const Camera& camera)
{
    s_ViewProjection = camera.GetViewProjectionMatrix();
}

void Renderer::EndScene()
{
}

void Renderer::Submit(
    const VertexArray& vertexArray,
    const IndexBuffer& indexBuffer,
    const Shader& shader,
    const glm::mat4& transform)
{
    shader.SetMat4("u_ViewProjection", s_ViewProjection);
    shader.SetMat4("u_Transform", transform);
    Draw(vertexArray, indexBuffer, shader);
}

void Renderer::Draw(
    const VertexArray& vertexArray,
    const IndexBuffer& indexBuffer,
    const Shader& shader)
{
    shader.Bind();
    vertexArray.Bind();
    indexBuffer.Bind();
    glDrawElements(
        GL_TRIANGLES,
        static_cast<GLsizei>(indexBuffer.GetCount()),
        GL_UNSIGNED_INT,
        nullptr);
}
