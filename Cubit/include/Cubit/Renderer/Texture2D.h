#pragma once

#include "Cubit/Core.h"

#include <cstdint>

class CB_API Texture2D
{
public:
    //Creates a texture from tightly packed 8-bit RGBA pixels, first row first.
    //Sampling is unfiltered, which keeps crosshairs, glyphs, and block textures
    //crisp instead of blurring them.
    Texture2D(std::uint32_t width, std::uint32_t height, const void* pixels);

    //Releases the owned OpenGL texture.
    ~Texture2D();

    //Prevents copying ownership of an OpenGL texture.
    Texture2D(const Texture2D&) = delete;

    //Prevents assigning duplicate ownership of an OpenGL texture.
    Texture2D& operator=(const Texture2D&) = delete;


    //Binds this texture to a numbered sampler slot.
    void Bind(std::uint32_t slot = 0) const;

    //Removes the texture binding from a numbered sampler slot.
    void Unbind(std::uint32_t slot = 0) const;

    std::uint32_t GetWidth() const { return m_Width; }
    std::uint32_t GetHeight() const { return m_Height; }

private:
    std::uint32_t m_RendererId = 0;
    std::uint32_t m_Width = 0;
    std::uint32_t m_Height = 0;
};
