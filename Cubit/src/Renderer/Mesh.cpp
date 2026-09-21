#include "cub.h"

#include "Cubit/Renderer/Mesh.h"

#include "Cubit/Voxel/ChunkMesher.h"

Mesh::Mesh(const MeshGeometry& geometry)
{
    if (geometry.Indices.empty())
        return;

    m_Array = std::make_unique<VertexArray>();
    m_Buffer = std::make_unique<VertexBuffer>(
        geometry.Vertices.data(),
        static_cast<std::uint32_t>(geometry.Vertices.size() * sizeof(VoxelVertex)));
    m_Array->AddBuffer(
        *m_Buffer,
        BufferLayout{ ShaderDataType::Float3, ShaderDataType::Float4 });
    m_Indices = std::make_unique<IndexBuffer>(
        geometry.Indices.data(),
        static_cast<std::uint32_t>(geometry.Indices.size()));
}

Mesh::~Mesh() = default;

//Each member here is a unique_ptr, and a unique_ptr's own move already
//transfers ownership and nulls the source, so there is no raw handle left
//for this type to null by hand - unlike VertexArray or IndexBuffer, which
//own a raw m_RendererId and so write their moves themselves.
Mesh::Mesh(Mesh&& other) noexcept = default;

Mesh& Mesh::operator=(Mesh&& other) noexcept = default;
