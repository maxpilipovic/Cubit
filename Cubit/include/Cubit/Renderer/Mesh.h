#pragma once

#include "Cubit/Core.h"
#include "Cubit/Renderer/VertexArray.h"
#include "Cubit/Renderer/VertexBuffer.h"
#include "Cubit/Renderer/IndexBuffer.h"

#include <cstdint>
#include <memory>

struct MeshGeometry;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

//A standalone GPU mesh: geometry that is not a chunk, uploaded once and drawn
//at whatever transform its owner picks. The same vertex format as a chunk's
//mesh, because it is built by the same mesher - a held tool or a player model
//is voxel geometry too, just not one that lives at a chunk origin.
class CB_API Mesh
{
public:
    //Uploads geometry to the GPU. Empty geometry uploads nothing, so an empty
    //mesh is always safe to hold and draw - never a null buffer waiting to be
    //dereferenced.
    explicit Mesh(const MeshGeometry& geometry);
    ~Mesh();

    //Owns GPU buffers, so it cannot be copied.
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    //Transfers ownership of the GPU buffers.
    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;

    //True when there is no geometry to draw.
    bool Empty() const { return m_Indices == nullptr; }

    //Indices in the mesh, zero for an empty one.
    std::uint32_t IndexCount() const;

    //For the scene to draw this mesh; only meaningful when Empty() is false.
    const VertexArray& Array() const { return *m_Array; }
    const IndexBuffer& Indices() const { return *m_Indices; }

private:
    std::unique_ptr<VertexArray> m_Array;
    std::unique_ptr<VertexBuffer> m_Buffer;
    std::unique_ptr<IndexBuffer> m_Indices;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
