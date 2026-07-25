#pragma once

#include "Cubit/Core.h"

#include <glm/glm.hpp>
#include <array>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

//A camera view frustum as six planes, for culling. Built from a view-projection
//matrix; tests axis-aligned boxes for visibility.
class CB_API Frustum
{
public:
    //Extracts the six clip planes from a view-projection matrix.
    explicit Frustum(const glm::mat4& viewProjection);

    //True if the axis-aligned box is at least partially inside the frustum.
    bool IntersectsAABB(const glm::vec3& min, const glm::vec3& max) const;

private:
    //Each plane is (a, b, c, d); a point is inside when a*x+b*y+c*z+d >= 0.
    std::array<glm::vec4, 6> m_Planes;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
