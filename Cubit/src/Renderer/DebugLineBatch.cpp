#include "cub.h"

#include "Cubit/Renderer/DebugLineBatch.h"

void DebugLineBatch::AddLine(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color)
{
    m_Vertices.push_back(DebugVertex{ from, color });
    m_Vertices.push_back(DebugVertex{ to, color });
}

void DebugLineBatch::AddBox(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color)
{
    // Corners are indexed so bit 0 is x, bit 1 is y and bit 2 is z. Two corners
    // share an edge exactly when their indices differ in a single bit, which is
    // what the edge table below encodes — and why every corner ends up with
    // three edges.
    const glm::vec3 corners[8] = {
        { min.x, min.y, min.z },
        { max.x, min.y, min.z },
        { min.x, max.y, min.z },
        { max.x, max.y, min.z },
        { min.x, min.y, max.z },
        { max.x, min.y, max.z },
        { min.x, max.y, max.z },
        { max.x, max.y, max.z },
    };

    static constexpr int Edges[12][2] = {
        { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 },
        { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 },
        { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
    };

    for (const auto& edge : Edges)
        AddLine(corners[edge[0]], corners[edge[1]], color);
}

void DebugLineBatch::Clear()
{
    m_Vertices.clear();
}
