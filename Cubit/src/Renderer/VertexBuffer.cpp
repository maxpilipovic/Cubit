#include "cub.h"

#include "Cubit/Renderer/VertexBuffer.h"

#include <glad/glad.h>

VertexBuffer::VertexBuffer(const void* data, std::uint32_t size)
{
    glGenBuffers(1, &m_RendererId);
    glBindBuffer(GL_ARRAY_BUFFER, m_RendererId);
    glBufferData(GL_ARRAY_BUFFER, size, data, GL_STATIC_DRAW);
}

VertexBuffer::~VertexBuffer()
{
    if (m_RendererId != 0)
        glDeleteBuffers(1, &m_RendererId);
}

VertexBuffer::VertexBuffer(VertexBuffer&& other) noexcept
    : m_RendererId(other.m_RendererId)
{
    other.m_RendererId = 0;
}

VertexBuffer& VertexBuffer::operator=(VertexBuffer&& other) noexcept
{
    if (this == &other)
        return *this;

    if (m_RendererId != 0)
        glDeleteBuffers(1, &m_RendererId);

    m_RendererId = other.m_RendererId;
    other.m_RendererId = 0;
    return *this;
}

void VertexBuffer::SetData(const void* data, std::uint32_t size) const
{
    Bind();
    glBufferSubData(GL_ARRAY_BUFFER, 0, size, data);
}

void VertexBuffer::Bind() const
{
    glBindBuffer(GL_ARRAY_BUFFER, m_RendererId);
}

void VertexBuffer::Unbind() const
{
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}
