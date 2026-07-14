#include "cub.h"

#include "Cubit/Renderer/VertexArray.h"
#include "Cubit/Renderer/VertexBuffer.h"

#include <glad/glad.h>
#include <stdexcept>

namespace
{
    //Converts a Cubit shader type to its OpenGL scalar type.
    GLenum ShaderDataTypeToOpenGLType(ShaderDataType type)
    {
        switch (type)
        {
            case ShaderDataType::Float:
            case ShaderDataType::Float2:
            case ShaderDataType::Float3:
            case ShaderDataType::Float4:
                return GL_FLOAT;
            case ShaderDataType::Int:
            case ShaderDataType::Int2:
            case ShaderDataType::Int3:
            case ShaderDataType::Int4:
                return GL_INT;
            default:
                throw std::invalid_argument("Unsupported shader data type");
        }
    }
}

VertexArray::VertexArray()
{
    glGenVertexArrays(1, &m_RendererId);
}

VertexArray::~VertexArray()
{
    if (m_RendererId != 0)
        glDeleteVertexArrays(1, &m_RendererId);
}

VertexArray::VertexArray(VertexArray&& other) noexcept
    : m_RendererId(other.m_RendererId)
{
    other.m_RendererId = 0;
}

VertexArray& VertexArray::operator=(VertexArray&& other) noexcept
{
    if (this == &other)
        return *this;

    if (m_RendererId != 0)
        glDeleteVertexArrays(1, &m_RendererId);

    m_RendererId = other.m_RendererId;
    other.m_RendererId = 0;
    return *this;
}

void VertexArray::AddBuffer(const VertexBuffer& vertexBuffer, const BufferLayout& layout)
{
    Bind();
    vertexBuffer.Bind();

    const auto& elements = layout.GetElements();
    for (std::uint32_t index = 0; index < elements.size(); ++index)
    {
        const BufferElement& element = elements[index];
        glEnableVertexAttribArray(index);
        glVertexAttribPointer(
            index,
            static_cast<GLint>(element.GetComponentCount()),
            ShaderDataTypeToOpenGLType(element.Type),
            element.Normalized ? GL_TRUE : GL_FALSE,
            static_cast<GLsizei>(layout.GetStride()),
            reinterpret_cast<const void*>(static_cast<std::uintptr_t>(element.Offset)));
    }
}

void VertexArray::Bind() const
{
    glBindVertexArray(m_RendererId);
}

void VertexArray::Unbind() const
{
    glBindVertexArray(0);
}
