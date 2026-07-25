#include <doctest.h>

#include "Cubit/Renderer/Frustum.h"

#include <glm/gtc/matrix_transform.hpp>

namespace
{
    //A camera at the origin looking down -z, standard perspective.
    glm::mat4 TestViewProjection()
    {
        const glm::mat4 proj =
            glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
        const glm::mat4 view = glm::lookAt(
            glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        return proj * view;
    }
}

TEST_CASE("A box in front of the camera is inside the frustum")
{
    const Frustum f(TestViewProjection());
    CHECK(f.IntersectsAABB(glm::vec3(-1, -1, -6), glm::vec3(1, 1, -4)));
}

TEST_CASE("A box behind the camera is culled")
{
    const Frustum f(TestViewProjection());
    CHECK_FALSE(f.IntersectsAABB(glm::vec3(-1, -1, 4), glm::vec3(1, 1, 6)));
}

TEST_CASE("A box far to the side is culled")
{
    const Frustum f(TestViewProjection());
    CHECK_FALSE(f.IntersectsAABB(glm::vec3(999, -1, -6), glm::vec3(1001, 1, -4)));
}

TEST_CASE("A box straddling the near plane still intersects")
{
    const Frustum f(TestViewProjection());
    CHECK(f.IntersectsAABB(glm::vec3(-1, -1, -1.0f), glm::vec3(1, 1, -0.05f)));
}
