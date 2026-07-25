#include "cub.h"

#include "Cubit/Renderer/Frustum.h"

#include <cmath>

namespace
{
    //Scales a plane so its normal is unit length (keeps distances meaningful).
    glm::vec4 NormalizePlane(const glm::vec4& p)
    {
        const float length = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        return (length > 0.0f) ? p / length : p;
    }
}

Frustum::Frustum(const glm::mat4& m)
{
    // glm is column-major (m[col][row]); pull the four rows of the matrix.
    const glm::vec4 row0(m[0][0], m[1][0], m[2][0], m[3][0]);
    const glm::vec4 row1(m[0][1], m[1][1], m[2][1], m[3][1]);
    const glm::vec4 row2(m[0][2], m[1][2], m[2][2], m[3][2]);
    const glm::vec4 row3(m[0][3], m[1][3], m[2][3], m[3][3]);

    m_Planes[0] = NormalizePlane(row3 + row0); // left
    m_Planes[1] = NormalizePlane(row3 - row0); // right
    m_Planes[2] = NormalizePlane(row3 + row1); // bottom
    m_Planes[3] = NormalizePlane(row3 - row1); // top
    m_Planes[4] = NormalizePlane(row3 + row2); // near
    m_Planes[5] = NormalizePlane(row3 - row2); // far
}

bool Frustum::IntersectsAABB(const glm::vec3& min, const glm::vec3& max) const
{
    for (const glm::vec4& plane : m_Planes)
    {
        // Positive vertex: the box corner furthest along the plane normal. If even
        // that corner is behind the plane, the whole box is outside it.
        const glm::vec3 positive(
            plane.x >= 0.0f ? max.x : min.x,
            plane.y >= 0.0f ? max.y : min.y,
            plane.z >= 0.0f ? max.z : min.z);

        if (plane.x * positive.x + plane.y * positive.y +
            plane.z * positive.z + plane.w < 0.0f)
            return false;
    }
    return true;
}
