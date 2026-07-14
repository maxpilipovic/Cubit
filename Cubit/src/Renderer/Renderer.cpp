#include "cub.h"

#include "Cubit/Renderer/Renderer.h"
#include "Cubit/Renderer/IndexBuffer.h"
#include "Cubit/Renderer/Shader.h"
#include "Cubit/Renderer/VertexArray.h"

#include <glad/glad.h>

void Renderer::Init()
{
    glEnable(GL_DEPTH_TEST);
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
