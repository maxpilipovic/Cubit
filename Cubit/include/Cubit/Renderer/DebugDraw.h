#pragma once

#include "Cubit/Core.h"

#include <glm/glm.hpp>

class Camera;

//Draws line geometry for one frame, callable from anywhere in the engine.
//
//Immediate mode: a shape is submitted on every frame it should be visible, and
//Flush both draws and discards the frame's batch. There are no handles and
//nothing to remove.
//
//Static rather than an instance because the point is to look at engine
//internals, and code like SpawnFinder or VoxelCollision holds no renderer
//reference to plumb one through. This header pulls in no OpenGL, so such code
//may call it without acquiring a rendering dependency — but a build that never
//calls Flush accumulates without bound, which is what Clear is for.
//
//Main thread only, and one GL context only: the batch is an unsynchronised
//vector, and the GPU resources are process-wide statics with no notion of which
//context created them. Neither is a limit anything hits today, and both are
//cheap to state now rather than to discover later.
class CB_API DebugDraw
{
public:
    //Prevents instances of the static debug drawer.
    DebugDraw() = delete;

    //Queues one line segment for this frame.
    static void Line(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color);

    //Queues a wireframe axis-aligned box for this frame.
    static void Box(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color);

    //Draws everything queued this frame and then discards it.
    //
    //Takes a camera rather than reusing the renderer's current scene: overlays
    //render last and leave an orthographic matrix behind, so an automatic
    //end-of-frame flush would draw the world's debug lines in screen space.
    static void Flush(const Camera& camera, const glm::mat4& transform = glm::mat4(1.0f));

    //Discards everything queued without drawing it.
    static void Clear();

    //Releases the GPU resources. Must run before the OpenGL context is
    //destroyed, which is why the Application calls it rather than leaving it to
    //static destruction at DLL unload.
    static void Shutdown();
};
