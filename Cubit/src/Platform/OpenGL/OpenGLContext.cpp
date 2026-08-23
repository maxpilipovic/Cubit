#include "cub.h"

#include "Platform/OpenGL/OpenGLContext.h"

#include "Core/CoreLogger.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <stdexcept>

namespace
{
    //Routes a driver message into the engine log, mapping GL severity onto the
    //logger's levels.
    void APIENTRY GLDebugCallback(
        GLenum source,
        GLenum type,
        GLuint id,
        GLenum severity,
        GLsizei length,
        const GLchar* message,
        const void* userParameter)
    {
        (void)source;
        (void)type;
        (void)id;
        (void)length;
        (void)userParameter;

        const std::string text = std::string("OpenGL: ") + message;

        if (severity == GL_DEBUG_SEVERITY_HIGH)
        {
            CB_CORE_ERROR(text);
            return;
        }

        CB_CORE_WARN(text);
    }
}

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

    // Without this a GL error produces no output at all and surfaces as wrong
    // pixels, which is the most expensive way to find out. GL_DEBUG_OUTPUT is
    // core only in 4.3 and this context is 3.3, so the entry point is probed
    // rather than assumed; nearly every driver offers it through GL_KHR_debug.
    if (glDebugMessageCallback != nullptr)
    {
        glEnable(GL_DEBUG_OUTPUT);

        // Synchronous is the point of the feature, not a detail: the callback
        // then runs inside the offending GL call, so the stack above it names
        // the culprit. Asynchronously the message can arrive arbitrarily later,
        // on any thread, which makes it nearly useless.
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(GLDebugCallback, nullptr);

        // Drivers emit allocation chatter at notification level constantly, and
        // a log nobody can read is a log nobody reads.
        glDebugMessageControl(
            GL_DONT_CARE,
            GL_DONT_CARE,
            GL_DEBUG_SEVERITY_NOTIFICATION,
            0,
            nullptr,
            GL_FALSE);

        CB_CORE_INFO("OpenGL debug output enabled");
    }
    else
    {
        CB_CORE_WARN("OpenGL debug output unavailable: no KHR_debug entry point");
    }
}

void OpenGLContext::SwapBuffers()
{
    glfwSwapBuffers(m_Window);
}
