#pragma once

#include "Cubit/Window.h"

struct GLFWwindow;

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

    //Processes pending GLFW events for the frame.
    void OnUpdate() override;

    //Reports whether GLFW has received a close request.
    bool ShouldClose() const override;

    //Stores the callback that receives translated GLFW events.
    void SetEventCallback(EventCallback callback) override;

    //Returns the cached window width.
    std::uint32_t GetWidth() const override { return m_Properties.Width; }

    //Returns the cached window height.
    std::uint32_t GetHeight() const override { return m_Properties.Height; }

    //Returns the underlying GLFW window pointer.
    void* GetNativeWindow() const override { return m_Window; }

private:
    WindowProperties m_Properties;
    EventCallback m_EventCallback;
    GLFWwindow* m_Window = nullptr;
};
