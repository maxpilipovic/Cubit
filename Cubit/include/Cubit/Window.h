#pragma once

#include "Cubit/Core.h"
#include "Cubit/Events/Event.h"

#include <cstdint>
#include <functional>
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
    using EventCallback = std::function<void(Event&)>;

    //Releases resources owned by a platform window.
    virtual ~Window() = default;

    //Processes pending native window messages immediately.
    virtual void PollEvents() = 0;

    //Presents the completed graphics back buffer.
    virtual void SwapBuffers() = 0;

    //Reports whether the user has requested that the window close.
    virtual bool ShouldClose() const = 0;

    //Registers the immediate callback used to route platform events.
    virtual void SetEventCallback(EventCallback callback) = 0;

    //Enables or disables synchronization with the display refresh rate.
    virtual void SetVSync(bool enabled) = 0;

    //Reports whether display synchronization is enabled.
    virtual bool IsVSync() const = 0;

    //Returns the current window width in screen coordinates.
    virtual std::uint32_t GetWidth() const = 0;

    //Returns the current window height in screen coordinates.
    virtual std::uint32_t GetHeight() const = 0;

    //Returns the current framebuffer width in pixels.
    virtual std::uint32_t GetFramebufferWidth() const = 0;

    //Returns the current framebuffer height in pixels.
    virtual std::uint32_t GetFramebufferHeight() const = 0;

    //Returns the platform-specific native window handle.
    virtual void* GetNativeWindow() const = 0;

    //Creates a window for the active desktop platform.
    static std::unique_ptr<Window> Create(
        const WindowProperties& properties = WindowProperties());
};
