#pragma once

#include "Cubit/Core.h"

#include <glm/glm.hpp>

#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

//One endpoint of a debug line segment.
struct DebugVertex
{
    glm::vec3 Position{ 0.0f };
    glm::vec4 Color{ 1.0f };
};

//Accumulates debug line geometry on the CPU.
//
//Holds no GPU resources and includes no OpenGL headers, so simulation code can
//build debug geometry without acquiring a rendering dependency.
class CB_API DebugLineBatch
{
public:
    //Appends one line segment as two vertices.
    void AddLine(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color);

    //Appends a wireframe axis-aligned box as its twelve edges.
    //
    //Bounds are used as given: a min greater than a max draws the inverted box
    //rather than being silently corrected, because a debug tool that repairs its
    //own input hides the bug being looked for.
    void AddBox(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color);

    //Discards everything accumulated so far.
    void Clear();

    //The accumulated vertices, in pairs, each pair one line segment.
    const std::vector<DebugVertex>& Vertices() const { return m_Vertices; }

private:
    std::vector<DebugVertex> m_Vertices;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
