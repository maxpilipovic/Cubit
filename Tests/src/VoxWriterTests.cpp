#include <doctest.h>

#include "Cubit/Voxel/VoxWriter.h"
#include "Cubit/Voxel/VoxLoader.h"

#include <cstdint>
#include <vector>

namespace
{
    void SetId(VoxModel& m, int x, int y, int z, std::uint8_t id)
    {
        m.Voxels[static_cast<std::size_t>(x) +
            static_cast<std::size_t>(m.Size.x) *
            (static_cast<std::size_t>(y) +
             static_cast<std::size_t>(m.Size.y) * static_cast<std::size_t>(z))] = id;
    }

    VoxModel MakeModel(int sx, int sy, int sz)
    {
        VoxModel m;
        m.Size = glm::ivec3(sx, sy, sz);
        m.Voxels.assign(static_cast<std::size_t>(sx) * sy * sz, 0);
        m.Colors = DefaultPalette();
        return m;
    }
}

TEST_CASE("VoxWriter round-trips size and voxels through the loader")
{
    VoxModel m = MakeModel(3, 4, 5);
    SetId(m, 1, 2, 3, 7);
    SetId(m, 0, 0, 0, 1);
    SetId(m, 2, 3, 4, 12);

    const VoxModel r = VoxLoader::Parse(VoxWriter::Write(m));

    CHECK(r.Size == m.Size);
    for (int z = 0; z < m.Size.z; ++z)
        for (int y = 0; y < m.Size.y; ++y)
            for (int x = 0; x < m.Size.x; ++x)
                CHECK(r.At(x, y, z) == m.At(x, y, z));
}

TEST_CASE("VoxWriter round-trips custom palette colours (byte precision)")
{
    VoxModel m = MakeModel(1, 1, 1);
    m.Colors[7] = glm::vec3(60 / 255.0f, 120 / 255.0f, 200 / 255.0f);
    m.Colors[12] = glm::vec3(190 / 255.0f, 45 / 255.0f, 45 / 255.0f);
    SetId(m, 0, 0, 0, 7);

    const VoxModel r = VoxLoader::Parse(VoxWriter::Write(m));

    CHECK(r.Colors[7].x == doctest::Approx(m.Colors[7].x).epsilon(1.0 / 255));
    CHECK(r.Colors[7].y == doctest::Approx(m.Colors[7].y).epsilon(1.0 / 255));
    CHECK(r.Colors[12].z == doctest::Approx(m.Colors[12].z).epsilon(1.0 / 255));
}

TEST_CASE("VoxWriter emits an all-air model with no voxels")
{
    VoxModel m = MakeModel(2, 2, 2); // all air

    const VoxModel r = VoxLoader::Parse(VoxWriter::Write(m));

    CHECK(r.Size == glm::ivec3(2, 2, 2));
    for (int z = 0; z < 2; ++z)
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x)
                CHECK(r.At(x, y, z) == 0);
}
