#pragma once

#include "Cubit/Core.h"

#include <cstdint>

class CB_API Renderer
{
public:
    //Prevents instances of the static renderer utility.
    Renderer() = delete;

    //Initializes the fixed OpenGL state used by the renderer.
    static void Init();

    //Sets the pixel region that receives rendered output.
    static void SetViewport(
        std::int32_t x,
        std::int32_t y,
        std::uint32_t width,
        std::uint32_t height);

    //Sets the color written when the color buffer is cleared.
    static void SetClearColor(float red, float green, float blue, float alpha);

    //Clears the color and depth buffers for a new frame.
    static void Clear();
};
