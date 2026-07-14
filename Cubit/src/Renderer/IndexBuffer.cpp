#include "cub.h"

#include "Cubit/Renderer/IndexBuffer.h"

#include <glad/glad.h>

IndexBuffer::IndexBuffer(const std::uint32_t* indices, std::uint32_t count)
    : m_Count(count)
{
    glGenBuffers(1, &m_RendererId);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererId);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        count * sizeof(std::uint32_t),
        indices,
        GL_STATIC_DRAW);
}

IndexBuffer::~IndexBuffer()
{
    if (m_RendererId != 0)
        glDeleteBuffers(1, &m_RendererId);
}

IndexBuffer::IndexBuffer(IndexBuffer&& other) noexcept
    : m_RendererId(other.m_RendererId), m_Count(other.m_Count)
{
    other.m_RendererId = 0;
    other.m_Count = 0;
}

IndexBuffer& IndexBuffer::operator=(IndexBuffer&& other) noexcept
{
    if (this == &other)
        return *this;

    if (m_RendererId != 0)
        glDeleteBuffers(1, &m_RendererId);

    m_RendererId = other.m_RendererId;
    m_Count = other.m_Count;
    other.m_RendererId = 0;
    other.m_Count = 0;
    return *this;
}

void IndexBuffer::Bind() const
{
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererId);
}

void IndexBuffer::Unbind() const
{
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}
