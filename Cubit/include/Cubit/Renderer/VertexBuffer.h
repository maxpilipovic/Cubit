#pragma once

#include "Cubit/Core.h"

#include <cstdint>

class CB_API VertexBuffer
{
public:
    //Creates GPU storage containing raw vertex data.
    VertexBuffer(const void* data, std::uint32_t size);

    //Releases the owned OpenGL vertex buffer.
    ~VertexBuffer();

    //Prevents copying ownership of an OpenGL buffer.
    VertexBuffer(const VertexBuffer&) = delete;

    //Prevents assigning duplicate ownership of an OpenGL buffer.
    VertexBuffer& operator=(const VertexBuffer&) = delete;

    //Transfers ownership of an OpenGL vertex buffer.
    VertexBuffer(VertexBuffer&& other) noexcept;

    //Releases the current buffer before taking another buffer.
    VertexBuffer& operator=(VertexBuffer&& other) noexcept;

    //Replaces vertex data beginning at the start of the buffer.
    void SetData(const void* data, std::uint32_t size) const;

    //Binds this vertex buffer for OpenGL array operations.
    void Bind() const;

    //Removes the current OpenGL array-buffer binding.
    void Unbind() const;

private:
    std::uint32_t m_RendererId = 0;
};
