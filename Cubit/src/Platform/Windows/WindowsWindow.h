#pragma once

#include "Cubit/Window.h"

struct GLFWwindow;

class WindowsWindow final : public Window
{
public:
    explicit WindowsWindow(const WindowProperties& properties);
    ~WindowsWindow() override;

    WindowsWindow(const WindowsWindow&) = delete;
    WindowsWindow& operator=(const WindowsWindow&) = delete;

    void OnUpdate() override;

    [[nodiscard]] std::uint32_t GetWidth() const override { return m_Properties.Width; }
    [[nodiscard]] std::uint32_t GetHeight() const override { return m_Properties.Height; }
    [[nodiscard]] void* GetNativeWindow() const override { return m_Window; }

private:
    WindowProperties m_Properties;
    GLFWwindow* m_Window = nullptr;
};
