#include "cub.h"

#include "Platform/Windows/WindowsWindow.h"

#include "Cubit/Events/ApplicationEvent.h"
#include "Cubit/Events/KeyEvent.h"
#include "Cubit/Events/MouseEvent.h"
#include "Core/CoreLogger.h"

#include <GLFW/glfw3.h>
#include <stdexcept>

namespace
{
    std::uint32_t s_WindowCount = 0;

    //Sends GLFW failures through the engine logging channel.
    void GLFWErrorCallback(int error, const char* description)
    {
        CB_CORE_ERROR(std::string("GLFW error ") + std::to_string(error) + ": " + description);
    }
}

WindowsWindow::WindowsWindow(const WindowProperties& properties)
    : m_Data{
        properties.Title,
        properties.Width,
        properties.Height,
        properties.Width,
        properties.Height,
        {} }
{
    if (s_WindowCount == 0)
    {
        glfwSetErrorCallback(GLFWErrorCallback);

        if (glfwInit() != GLFW_TRUE)
            throw std::runtime_error("Failed to initialize GLFW");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    m_Window = glfwCreateWindow(
        static_cast<int>(m_Data.Width),
        static_cast<int>(m_Data.Height),
        m_Data.Title.c_str(),
        nullptr,
        nullptr);

    if (m_Window == nullptr)
    {
        if (s_WindowCount == 0)
            glfwTerminate();

        throw std::runtime_error("Failed to create GLFW window");
    }

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(m_Window, &framebufferWidth, &framebufferHeight);
    m_Data.FramebufferWidth = static_cast<std::uint32_t>(framebufferWidth);
    m_Data.FramebufferHeight = static_cast<std::uint32_t>(framebufferHeight);

    glfwSetWindowUserPointer(m_Window, &m_Data);

    //Translates a native close request into an immediate Cubit event.
    glfwSetWindowCloseCallback(
        m_Window,
        [](GLFWwindow* window)
        {
            auto& data = *static_cast<WindowsWindowData*>(glfwGetWindowUserPointer(window));
            WindowCloseEvent event;
            if (data.EventCallback)
                data.EventCallback(event);
        });

    //Updates logical dimensions before routing a window-resize event.
    glfwSetWindowSizeCallback(
        m_Window,
        [](GLFWwindow* window, int width, int height)
        {
            auto& data = *static_cast<WindowsWindowData*>(glfwGetWindowUserPointer(window));
            data.Width = static_cast<std::uint32_t>(width);
            data.Height = static_cast<std::uint32_t>(height);
            WindowResizeEvent event(data.Width, data.Height);
            if (data.EventCallback)
                data.EventCallback(event);
        });

    //Updates pixel dimensions before routing a framebuffer-resize event.
    glfwSetFramebufferSizeCallback(
        m_Window,
        [](GLFWwindow* window, int width, int height)
        {
            auto& data = *static_cast<WindowsWindowData*>(glfwGetWindowUserPointer(window));
            data.FramebufferWidth = static_cast<std::uint32_t>(width);
            data.FramebufferHeight = static_cast<std::uint32_t>(height);
            FramebufferResizeEvent event(data.FramebufferWidth, data.FramebufferHeight);
            if (data.EventCallback)
                data.EventCallback(event);
        });

    //Routes focus gain and loss as distinct event types.
    glfwSetWindowFocusCallback(
        m_Window,
        [](GLFWwindow* window, int focused)
        {
            auto& data = *static_cast<WindowsWindowData*>(glfwGetWindowUserPointer(window));
            if (!data.EventCallback)
                return;

            if (focused == GLFW_TRUE)
            {
                WindowFocusEvent event;
                data.EventCallback(event);
            }
            else
            {
                WindowLostFocusEvent event;
                data.EventCallback(event);
            }
        });

    //Routes logical desktop movement through the window event path.
    glfwSetWindowPosCallback(
        m_Window,
        [](GLFWwindow* window, int x, int y)
        {
            auto& data = *static_cast<WindowsWindowData*>(glfwGetWindowUserPointer(window));
            WindowMovedEvent event(x, y);
            if (data.EventCallback)
                data.EventCallback(event);
        });

    //Separates physical key presses, repeats, and releases.
    glfwSetKeyCallback(
        m_Window,
        [](GLFWwindow* window, int key, int scanCode, int action, int modifiers)
        {
            (void)scanCode;
            (void)modifiers;
            auto& data = *static_cast<WindowsWindowData*>(glfwGetWindowUserPointer(window));
            if (!data.EventCallback)
                return;

            const KeyCode keyCode = static_cast<KeyCode>(key);
            if (action == GLFW_PRESS || action == GLFW_REPEAT)
            {
                KeyPressedEvent event(keyCode, action == GLFW_REPEAT);
                data.EventCallback(event);
            }
            else if (action == GLFW_RELEASE)
            {
                KeyReleasedEvent event(keyCode);
                data.EventCallback(event);
            }
        });

    //Routes Unicode text independently from physical key activity.
    glfwSetCharCallback(
        m_Window,
        [](GLFWwindow* window, unsigned int codePoint)
        {
            auto& data = *static_cast<WindowsWindowData*>(glfwGetWindowUserPointer(window));
            KeyTypedEvent event(static_cast<char32_t>(codePoint));
            if (data.EventCallback)
                data.EventCallback(event);
        });

    //Routes logical cursor movement without altering polling state.
    glfwSetCursorPosCallback(
        m_Window,
        [](GLFWwindow* window, double x, double y)
        {
            auto& data = *static_cast<WindowsWindowData*>(glfwGetWindowUserPointer(window));
            MouseMovedEvent event(x, y);
            if (data.EventCallback)
                data.EventCallback(event);
        });

    //Routes horizontal and vertical scroll deltas.
    glfwSetScrollCallback(
        m_Window,
        [](GLFWwindow* window, double xOffset, double yOffset)
        {
            auto& data = *static_cast<WindowsWindowData*>(glfwGetWindowUserPointer(window));
            MouseScrolledEvent event(xOffset, yOffset);
            if (data.EventCallback)
                data.EventCallback(event);
        });

    //Separates mouse-button presses from releases.
    glfwSetMouseButtonCallback(
        m_Window,
        [](GLFWwindow* window, int button, int action, int modifiers)
        {
            (void)modifiers;
            auto& data = *static_cast<WindowsWindowData*>(glfwGetWindowUserPointer(window));
            if (!data.EventCallback)
                return;

            const MouseCode mouseCode = static_cast<MouseCode>(button);
            if (action == GLFW_PRESS)
            {
                MouseButtonPressedEvent event(mouseCode);
                data.EventCallback(event);
            }
            else if (action == GLFW_RELEASE)
            {
                MouseButtonReleasedEvent event(mouseCode);
                data.EventCallback(event);
            }
        });

    ++s_WindowCount;
    CB_CORE_INFO("Created window: " + m_Data.Title);
}

WindowsWindow::~WindowsWindow()
{
    glfwDestroyWindow(m_Window);
    --s_WindowCount;

    if (s_WindowCount == 0)
        glfwTerminate();
}

void WindowsWindow::PollEvents()
{
    glfwPollEvents();
}

bool WindowsWindow::ShouldClose() const
{
    return glfwWindowShouldClose(m_Window) == GLFW_TRUE;
}

void WindowsWindow::SetEventCallback(EventCallback callback)
{
    m_Data.EventCallback = std::move(callback);
}
