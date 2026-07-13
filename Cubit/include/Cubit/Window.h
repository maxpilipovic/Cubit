#pragma once

#include "Cubit/Core.h"

#include <cstdint>
#include <memory>
#include <string>

struct WindowProperties
{
    std::string Title = "Cubit";
    std::uint32_t Width = 1280;
    std::uint32_t Height = 720;
};

class CB_API Window
{
public:
    //Releases resources owned by a platform window.
    virtual ~Window() = default;

    //Processes pending native window messages for the frame.
    virtual void OnUpdate() = 0;

    //Reports whether the user has requested that the window close.
    virtual bool ShouldClose() const = 0;

    //Returns the current window width in screen coordinates.
    virtual std::uint32_t GetWidth() const = 0;

    //Returns the current window height in screen coordinates.
    virtual std::uint32_t GetHeight() const = 0;

    //Returns the platform-specific native window handle.
    virtual void* GetNativeWindow() const = 0;

    //Creates a window for the active desktop platform.
    static std::unique_ptr<Window> Create(
        const WindowProperties& properties = WindowProperties());
};
