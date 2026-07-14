#include "cub.h"

#include "Platform/OpenGL/OpenGLContext.h"

#include "Core/CoreLogger.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <stdexcept>

OpenGLContext::OpenGLContext(void* nativeWindow)
    : m_Window(static_cast<GLFWwindow*>(nativeWindow))
{
    if (m_Window == nullptr)
        throw std::invalid_argument("OpenGLContext requires a valid GLFW window");
}

void OpenGLContext::Init()
{
    glfwMakeContextCurrent(m_Window);

    if (gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)) == 0)
        throw std::runtime_error("Failed to initialize GLAD");

    CB_CORE_INFO(std::string("OpenGL vendor: ") +
        reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
    CB_CORE_INFO(std::string("OpenGL renderer: ") +
        reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    CB_CORE_INFO(std::string("OpenGL version: ") +
        reinterpret_cast<const char*>(glGetString(GL_VERSION)));
}

void OpenGLContext::SwapBuffers()
{
    glfwSwapBuffers(m_Window);
}
