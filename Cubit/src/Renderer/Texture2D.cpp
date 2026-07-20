#include "cub.h"

#include "Cubit/Renderer/Texture2D.h"

#include "Core/CoreLogger.h"

#include <glad/glad.h>

Texture2D::Texture2D(std::uint32_t width, std::uint32_t height, const void* pixels)
    : m_Width(width), m_Height(height)
{
    CB_CORE_ASSERT(width > 0 && height > 0, "Texture dimensions must be positive");

    glGenTextures(1, &m_RendererId);
    glBindTexture(GL_TEXTURE_2D, m_RendererId);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    //Rows are packed with no padding, which the default alignment of four does
    //not assume.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        static_cast<GLsizei>(width),
        static_cast<GLsizei>(height),
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        pixels);

    glBindTexture(GL_TEXTURE_2D, 0);
}

Texture2D::~Texture2D()
{
    if (m_RendererId != 0)
        glDeleteTextures(1, &m_RendererId);
}

Texture2D::Texture2D(Texture2D&& other) noexcept
    : m_RendererId(other.m_RendererId),
      m_Width(other.m_Width),
      m_Height(other.m_Height)
{
    //The moved-from texture must forget the id, or both destructors would try
    //to delete the same OpenGL texture.
    other.m_RendererId = 0;
    other.m_Width = 0;
    other.m_Height = 0;
}

Texture2D& Texture2D::operator=(Texture2D&& other) noexcept
{
    if (this == &other)
        return *this;

    if (m_RendererId != 0)
        glDeleteTextures(1, &m_RendererId);

    m_RendererId = other.m_RendererId;
    m_Width = other.m_Width;
    m_Height = other.m_Height;
    other.m_RendererId = 0;
    other.m_Width = 0;
    other.m_Height = 0;

    return *this;
}

void Texture2D::Bind(std::uint32_t slot) const
{
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, m_RendererId);
}

void Texture2D::Unbind(std::uint32_t slot) const
{
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, 0);
}
