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

Mesh::Mesh(Mesh&& other) noexcept = default;

Mesh& Mesh::operator=(Mesh&& other) noexcept = default;

std::uint32_t Mesh::IndexCount() const
{
    return m_Indices != nullptr ? m_Indices->GetCount() : 0;
}
