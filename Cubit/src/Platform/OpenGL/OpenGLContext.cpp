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
        (void)userParameter;

        const std::string text =
            "OpenGL: " + std::string(message, static_cast<std::size_t>(length));

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
    // pixels, which is the most expensive way to find out. The entry point is
    // probed because GLAD resolves it only through its 4.3 core path — it was
    // generated without the GL_KHR_debug extension — and only Debug builds ask
    // for a 4.3 context. So this branch always succeeds in Debug and always
    // fails in Release; how much KHR_debug the driver supports never enters
    // into it.
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
        CB_CORE_WARN("OpenGL debug output unavailable: needs the 4.3 context only Debug builds request");
    }
}

void OpenGLContext::SwapBuffers()
{
    glfwSwapBuffers(m_Window);
}
