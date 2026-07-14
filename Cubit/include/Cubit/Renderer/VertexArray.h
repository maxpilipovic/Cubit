#pragma once

#include "Cubit/Core.h"

#include <cstdint>
#include <initializer_list>
#include <vector>

class VertexBuffer;

enum class ShaderDataType : std::uint8_t
{
    None = 0,
    Float,
    Float2,
    Float3,
    Float4,
    Int,
    Int2,
    Int3,
    Int4
};

struct BufferElement
{
    ShaderDataType Type = ShaderDataType::None;
    std::uint32_t Offset = 0;
    bool Normalized = false;

    //Creates one vertex attribute description.
    BufferElement(ShaderDataType type, bool normalized = false)
        : Type(type), Normalized(normalized)
    {
    }

    //Returns the byte size of this vertex attribute.
    std::uint32_t GetSize() const { return GetComponentCount() * sizeof(std::uint32_t); }

    //Returns the number of scalar values in this attribute.
    std::uint32_t GetComponentCount() const
    {
        switch (Type)
        {
            case ShaderDataType::Float:
            case ShaderDataType::Int:
                return 1;
            case ShaderDataType::Float2:
            case ShaderDataType::Int2:
                return 2;
            case ShaderDataType::Float3:
            case ShaderDataType::Int3:
                return 3;
            case ShaderDataType::Float4:
            case ShaderDataType::Int4:
                return 4;
            default:
                return 0;
        }
    }
};

class BufferLayout
{
public:
    //Creates an empty vertex layout.
    BufferLayout() = default;

    //Creates a vertex layout and calculates its byte offsets.
    BufferLayout(std::initializer_list<BufferElement> elements)
        : m_Elements(elements)
    {
        CalculateOffsetsAndStride();
    }

    //Returns the ordered attributes in this layout.
    const std::vector<BufferElement>& GetElements() const { return m_Elements; }

    //Returns the byte distance between consecutive vertices.
    std::uint32_t GetStride() const { return m_Stride; }

private:
    //Calculates attribute offsets and the full vertex stride.
    void CalculateOffsetsAndStride()
    {
        std::uint32_t offset = 0;
        m_Stride = 0;

        for (BufferElement& element : m_Elements)
        {
            element.Offset = offset;
            offset += element.GetSize();
            m_Stride += element.GetSize();
        }
    }

    std::vector<BufferElement> m_Elements;
    std::uint32_t m_Stride = 0;
};

class CB_API VertexArray
{
public:
    //Creates an OpenGL vertex-array object.
    VertexArray();

    //Releases the owned OpenGL vertex-array object.
    ~VertexArray();

    //Prevents copying ownership of an OpenGL vertex array.
    VertexArray(const VertexArray&) = delete;

    //Prevents assigning duplicate ownership of an OpenGL vertex array.
    VertexArray& operator=(const VertexArray&) = delete;

    //Transfers ownership of an OpenGL vertex array.
    VertexArray(VertexArray&& other) noexcept;

    //Releases the current array before taking another array.
    VertexArray& operator=(VertexArray&& other) noexcept;

    //Connects a vertex buffer to this array using the supplied layout.
    void AddBuffer(const VertexBuffer& vertexBuffer, const BufferLayout& layout);

    //Binds this vertex array for rendering or configuration.
    void Bind() const;

    //Removes the current OpenGL vertex-array binding.
    void Unbind() const;

private:
    std::uint32_t m_RendererId = 0;
};
