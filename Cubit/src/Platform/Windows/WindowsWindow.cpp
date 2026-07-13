#include "cub.h"

#include "Platform/Windows/WindowsWindow.h"

#include "Core/CoreLogger.h"

#include <GLFW/glfw3.h>
#include <stdexcept>

namespace
{
    std::uint32_t s_WindowCount = 0;

    void GLFWErrorCallback(int error, const char* description)
    {
        CB_CORE_ERROR(std::string("GLFW error ") + std::to_string(error) + ": " + description);
    }
}

WindowsWindow::WindowsWindow(const WindowProperties& properties)
    : m_Properties(properties)
{
    if (s_WindowCount == 0)
    {
        glfwSetErrorCallback(GLFWErrorCallback);

        if (glfwInit() != GLFW_TRUE)
            throw std::runtime_error("Failed to initialize GLFW");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    m_Window = glfwCreateWindow(
        static_cast<int>(m_Properties.Width),
        static_cast<int>(m_Properties.Height),
        m_Properties.Title.c_str(),
        nullptr,
        nullptr);

    if (m_Window == nullptr)
    {
        if (s_WindowCount == 0)
            glfwTerminate();

        throw std::runtime_error("Failed to create GLFW window");
    }

    ++s_WindowCount;
    CB_CORE_INFO("Created window: " + m_Properties.Title);
}

WindowsWindow::~WindowsWindow()
{
    glfwDestroyWindow(m_Window);
    --s_WindowCount;

    if (s_WindowCount == 0)
        glfwTerminate();
}

void WindowsWindow::OnUpdate()
{
    glfwPollEvents();
}

bool WindowsWindow::ShouldClose() const
{
    return glfwWindowShouldClose(m_Window) == GLFW_TRUE;
}
