#pragma once

#include "Cubit/Core.h"

#include <glm/glm.hpp>
#include <cstdint>

class IndexBuffer;
class Camera;
class Shader;
class VertexArray;

class CB_API Renderer
{
public:
    //Prevents instances of the static renderer utility.
    Renderer() = delete;

    //Initializes the fixed OpenGL state used by the renderer.
    static void Init();

    //Enables or disables depth testing. Screen-space overlays draw with it off
    //so they are not hidden by scene geometry.
    static void SetDepthTest(bool enabled);

    //Sets the pixel region that receives rendered output.
    static void SetViewport(
        std::int32_t x,
        std::int32_t y,
        std::uint32_t width,
        std::uint32_t height);

    //Sets the color written when the color buffer is cleared.
    static void SetClearColor(float red, float green, float blue, float alpha);

    //Clears the color and depth buffers for a new frame.
    static void Clear();

    //Begins a scene using the supplied camera for subsequent submissions.
    static void BeginScene(const Camera& camera);

    //Finishes the current scene.
    static void EndScene();

    //Draws one transformed object using the current scene camera.
    static void Submit(
        const VertexArray& vertexArray,
        const IndexBuffer& indexBuffer,
        const Shader& shader,
        const glm::mat4& transform = glm::mat4(1.0f));

    //Draws indexed triangles using the supplied GPU resources.
    static void Draw(
        const VertexArray& vertexArray,
        const IndexBuffer& indexBuffer,
        const Shader& shader);
};
