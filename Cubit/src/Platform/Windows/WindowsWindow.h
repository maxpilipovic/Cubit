#pragma once

#include "Cubit/Window.h"

class GraphicsContext;
struct GLFWwindow;

struct WindowsWindowData
{
    std::string Title;
    std::uint32_t Width;
    std::uint32_t Height;
    std::uint32_t FramebufferWidth;
    std::uint32_t FramebufferHeight;
    bool VSync;
    Window::EventCallback EventCallback;
};

class WindowsWindow final : public Window
{
public:
    //Creates a GLFW window using the requested properties.
    explicit WindowsWindow(const WindowProperties& properties);

    //Destroys the GLFW window and releases GLFW when no windows remain.
    ~WindowsWindow() override;

    //Prevents copying ownership of a native window.
    WindowsWindow(const WindowsWindow&) = delete;

    //Prevents assigning ownership of a native window.
    WindowsWindow& operator=(const WindowsWindow&) = delete;

    //Processes pending GLFW events immediately.
    void PollEvents() override;

    //Presents the OpenGL back buffer through the owned context.
    void SwapBuffers() override;

    //Reports whether GLFW has received a close request.
    bool ShouldClose() const override;

    //Stores the callback that receives translated GLFW events.
    void SetEventCallback(EventCallback callback) override;

    //Changes the GLFW swap interval for this context.
    void SetVSync(bool enabled) override;

    //Reports the cached vertical synchronization state.
    bool IsVSync() const override { return m_Data.VSync; }

    //Returns the cached window width.
    std::uint32_t GetWidth() const override { return m_Data.Width; }

    //Returns the cached window height.
    std::uint32_t GetHeight() const override { return m_Data.Height; }

    //Returns the cached framebuffer width in pixels.
    std::uint32_t GetFramebufferWidth() const override { return m_Data.FramebufferWidth; }

    //Returns the cached framebuffer height in pixels.
    std::uint32_t GetFramebufferHeight() const override { return m_Data.FramebufferHeight; }

    //Returns the underlying GLFW window pointer.
    void* GetNativeWindow() const override { return m_Window; }

private:
    WindowsWindowData m_Data;
    std::unique_ptr<GraphicsContext> m_Context;
    GLFWwindow* m_Window = nullptr;
};
