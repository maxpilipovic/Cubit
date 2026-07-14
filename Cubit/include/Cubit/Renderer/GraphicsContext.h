#pragma once

#include "Cubit/Core.h"

#include <memory>

class CB_API GraphicsContext
{
public:
    //Releases a graphics context through its base interface.
    virtual ~GraphicsContext() = default;

    //Makes the context current and loads its graphics functions.
    virtual void Init() = 0;

    //Presents the completed back buffer to the window.
    virtual void SwapBuffers() = 0;

    //Creates the graphics context used by the active renderer backend.
    static std::unique_ptr<GraphicsContext> Create(void* nativeWindow);
};
