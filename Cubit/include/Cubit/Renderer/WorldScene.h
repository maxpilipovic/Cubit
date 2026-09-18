#pragma once

#include "Cubit/Core.h"
#include "Cubit/Renderer/PerspectiveCamera.h"
#include "Cubit/Renderer/Shader.h"
#include "Cubit/Renderer/WorldRenderer.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>

class World;
class Mesh;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

//The voxel scene: the world and the things standing in it. Drawn with the
//same distance fog - the chunk renderer, the shader that colours its
//vertices, and the calls that put chunks and standalone meshes on screen.
//
//Here rather than in an app because both the harness and a game draw the same
//world the same way, and the shader is the engine's own - a voxel chunk's
//vertex format and its fog are not an app's business. It is also a step towards
//shaders being assets rather than string literals (performance.md's asset-layer
//item): when that lands, only this file changes.
class CB_API WorldScene
{
public:
    WorldScene();
    ~WorldScene();

    WorldScene(const WorldScene&) = delete;
    WorldScene& operator=(const WorldScene&) = delete;

    //Meshes whatever the world reports dirty, inside the renderer's per-frame
    //time slice. Call once a frame before Render.
    void Update(World& world);

    //Draws every chunk inside the camera's frustum, opaque first and then
    //transparent geometry back to front.
    //
    //`worldOffset` is where the world's origin sits in view space; the camera
    //position is read in that same space, which is what lets the shader take
    //the distance to a fragment by subtraction. `fogDensity` of zero is no fog
    //at all, which is how a dry camera draws.
    void Render(const PerspectiveCamera& camera, const glm::vec3& worldOffset,
        const glm::vec3& fogColor, float fogDensity);

    //Draws one standalone mesh, such as a player model or a held tool.
    //`transform` carries the mesh's world offset the same way the chunk
    //draw's does; `brightness` is how lit the thing is where it stands - the
    //scene does not work that out, because the scene does not know what a
    //model is. Uses the camera set by the last Render, so call it there; in
    //debug builds this is enforced by an assert, not only by this comment.
    void DrawMesh(const Mesh& mesh, const glm::mat4& transform, float brightness);

    //What the last Render drew, for a readout.
    std::uint32_t TotalFaceCount() const { return m_Renderer.TotalFaceCount(); }
    std::size_t DrawnChunkCount() const { return m_Renderer.DrawnChunkCount(); }
    std::size_t TotalChunkCount() const { return m_Renderer.TotalChunkCount(); }
    std::size_t PendingCount() const { return m_Renderer.PendingCount(); }

private:
    WorldRenderer m_Renderer;
    std::unique_ptr<Shader> m_Shader;

    //Set once Render has run, so DrawMesh's assert can tell a real Renderer
    //view-projection from one that was never set. See DrawMesh's definition
    //for why this matters.
    bool m_HasRendered = false;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
