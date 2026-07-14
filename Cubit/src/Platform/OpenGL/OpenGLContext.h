#pragma once

#include "Cubit/Renderer/GraphicsContext.h"

struct GLFWwindow;

class OpenGLContext final : public GraphicsContext
{
public:
    //Stores the GLFW window that owns this OpenGL context.
    explicit OpenGLContext(void* nativeWindow);

    //Makes the OpenGL context current and initializes GLAD.
    void Init() override;

    //Swaps the GLFW window's OpenGL buffers.
    void SwapBuffers() override;

private:
    GLFWwindow* m_Window;
};
