#include "cub.h"

#include "Cubit/Renderer/GraphicsContext.h"
#include "Platform/OpenGL/OpenGLContext.h"

std::unique_ptr<GraphicsContext> GraphicsContext::Create(void* nativeWindow)
{
    return std::make_unique<OpenGLContext>(nativeWindow);
}
