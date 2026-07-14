#pragma once

#include "Cubit/Core.h"

#include <cstdint>

class CB_API IndexBuffer
{
public:
    //Creates GPU storage containing triangle index data.
    IndexBuffer(const std::uint32_t* indices, std::uint32_t count);

    //Releases the owned OpenGL index buffer.
    ~IndexBuffer();

    //Prevents copying ownership of an OpenGL buffer.
    IndexBuffer(const IndexBuffer&) = delete;

    //Prevents assigning duplicate ownership of an OpenGL buffer.
    IndexBuffer& operator=(const IndexBuffer&) = delete;

    //Transfers ownership and index count from another buffer.
    IndexBuffer(IndexBuffer&& other) noexcept;

    //Releases the current buffer before taking another buffer.
    IndexBuffer& operator=(IndexBuffer&& other) noexcept;

    //Binds this index buffer for indexed rendering.
    void Bind() const;

    //Removes the current OpenGL index-buffer binding.
    void Unbind() const;

    //Returns the number of indices stored by this buffer.
    std::uint32_t GetCount() const { return m_Count; }

private:
    std::uint32_t m_RendererId = 0;
    std::uint32_t m_Count = 0;
};
