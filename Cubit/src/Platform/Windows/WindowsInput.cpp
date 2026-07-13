#include "cub.h"

#include "Cubit/Input.h"
#include "Cubit/Window.h"

#include <GLFW/glfw3.h>

namespace
{
    Window* s_InputWindow = nullptr;

    //Returns the GLFW window currently used for input polling.
    GLFWwindow* GetGLFWWindow()
    {
        if (s_InputWindow == nullptr)
            return nullptr;

        return static_cast<GLFWwindow*>(s_InputWindow->GetNativeWindow());
    }
}

bool Input::IsKeyPressed(KeyCode key)
{
    GLFWwindow* window = GetGLFWWindow();
    if (window == nullptr)
        return false;

    const int state = glfwGetKey(window, static_cast<int>(key));
    return state == GLFW_PRESS || state == GLFW_REPEAT;
}

bool Input::IsMouseButtonPressed(MouseCode button)
{
    GLFWwindow* window = GetGLFWWindow();
    if (window == nullptr)
        return false;

    return glfwGetMouseButton(window, static_cast<int>(button)) == GLFW_PRESS;
}

MousePosition Input::GetMousePosition()
{
    GLFWwindow* window = GetGLFWWindow();
    if (window == nullptr)
        return {};

    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(window, &x, &y);
    return { static_cast<float>(x), static_cast<float>(y) };
}

float Input::GetMouseX()
{
    return GetMousePosition().X;
}

float Input::GetMouseY()
{
    return GetMousePosition().Y;
}

void Input::SetWindow(Window* window)
{
    s_InputWindow = window;
}
