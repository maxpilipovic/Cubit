#include "cub.h"

#include "Cubit/Renderer/Renderer.h"

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
