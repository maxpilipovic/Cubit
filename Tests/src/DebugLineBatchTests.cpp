#include <doctest.h>

#include "Cubit/Renderer/DebugLineBatch.h"

#include <array>
#include <cstddef>

namespace
{
    const glm::vec4 Red{ 1.0f, 0.0f, 0.0f, 1.0f };

    // Exactly representable in binary floating point, so the corner comparisons
    // below can use == without tolerance.
    const glm::vec3 BoxMin{ 1.0f, 2.0f, 3.0f };
    const glm::vec3 BoxMax{ 4.0f, 6.0f, 9.0f };
}

TEST_CASE("A new batch holds nothing")
{
    const DebugLineBatch batch;

    CHECK(batch.Vertices().empty());
}

TEST_CASE("A line appends its two endpoints")
{
    DebugLineBatch batch;
    batch.AddLine(glm::vec3(1.0f, 2.0f, 3.0f), glm::vec3(4.0f, 5.0f, 6.0f), Red);

    REQUIRE(batch.Vertices().size() == 2);
    CHECK(batch.Vertices()[0].Position == glm::vec3(1.0f, 2.0f, 3.0f));
    CHECK(batch.Vertices()[1].Position == glm::vec3(4.0f, 5.0f, 6.0f));
    CHECK(batch.Vertices()[0].Color == Red);
    CHECK(batch.Vertices()[1].Color == Red);
}

TEST_CASE("A box appends twelve edges")
{
    DebugLineBatch batch;
    batch.AddBox(BoxMin, BoxMax, Red);

    CHECK(batch.Vertices().size() == 24);
}

TEST_CASE("Every box vertex sits on a corner")
{
    DebugLineBatch batch;
    batch.AddBox(BoxMin, BoxMax, Red);

    REQUIRE(batch.Vertices().size() == 24);

    for (const DebugVertex& vertex : batch.Vertices())
    {
        CHECK((vertex.Position.x == BoxMin.x || vertex.Position.x == BoxMax.x));
        CHECK((vertex.Position.y == BoxMin.y || vertex.Position.y == BoxMax.y));
        CHECK((vertex.Position.z == BoxMin.z || vertex.Position.z == BoxMax.z));
    }
}

TEST_CASE("Every box corner carries three edges")
{
    // The check that can actually fail. A transposed, duplicated or omitted edge
    // still yields 24 vertices and still lands every vertex on a corner, but it
    // breaks this distribution: each of the eight corners has exactly three
    // edges meeting at it.
    DebugLineBatch batch;
    batch.AddBox(BoxMin, BoxMax, Red);

    std::array<int, 8> counts{};
    for (const DebugVertex& vertex : batch.Vertices())
    {
        const std::size_t corner =
            (vertex.Position.x == BoxMax.x ? 1u : 0u) |
            (vertex.Position.y == BoxMax.y ? 2u : 0u) |
            (vertex.Position.z == BoxMax.z ? 4u : 0u);
        ++counts[corner];
    }

    for (int count : counts)
        CHECK(count == 3);
}

TEST_CASE("A degenerate box is drawn rather than rejected")
{
    DebugLineBatch batch;
    batch.AddBox(BoxMin, BoxMin, Red);

    REQUIRE(batch.Vertices().size() == 24);
    for (const DebugVertex& vertex : batch.Vertices())
        CHECK(vertex.Position == BoxMin);
}

TEST_CASE("An inverted box is drawn as given rather than corrected")
{
    // This documents intent rather than verifying it. Swapping min and max maps
    // corner index i to ~i & 7, a bijection, so a normalizing AddBox and this
    // one emit the same 24 vertices in a different order — no implementation
    // built from per-axis corner combinations could fail this.
    DebugLineBatch batch;
    batch.AddBox(BoxMax, BoxMin, Red);

    REQUIRE(batch.Vertices().size() == 24);

    bool sawInvertedCorner = false;
    for (const DebugVertex& vertex : batch.Vertices())
        if (vertex.Position == BoxMax)
            sawInvertedCorner = true;

    CHECK(sawInvertedCorner);
}

TEST_CASE("Shapes accumulate rather than replacing one another")
{
    DebugLineBatch batch;
    batch.AddLine(BoxMin, BoxMax, Red);
    batch.AddBox(BoxMin, BoxMax, Red);

    CHECK(batch.Vertices().size() == 26);
}

TEST_CASE("Clear empties the batch")
{
    DebugLineBatch batch;
    batch.AddBox(BoxMin, BoxMax, Red);
    batch.Clear();

    CHECK(batch.Vertices().empty());
}
