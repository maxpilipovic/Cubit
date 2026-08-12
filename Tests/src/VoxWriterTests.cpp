#include <doctest.h>

#include "Cubit/Voxel/VoxWriter.h"
#include "Cubit/Voxel/VoxLoader.h"

#include <cstdint>
#include <cstring>
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
    m.Colors[7] = glm::vec4(60 / 255.0f, 120 / 255.0f, 200 / 255.0f, 1.0f);
    m.Colors[12] = glm::vec4(190 / 255.0f, 45 / 255.0f, 45 / 255.0f, 1.0f);
    SetId(m, 0, 0, 0, 7);

    const VoxModel r = VoxLoader::Parse(VoxWriter::Write(m));

    CHECK(r.Colors[7].x == doctest::Approx(m.Colors[7].x).epsilon(1.0 / 255));
    CHECK(r.Colors[7].y == doctest::Approx(m.Colors[7].y).epsilon(1.0 / 255));
    CHECK(r.Colors[12].z == doctest::Approx(m.Colors[12].z).epsilon(1.0 / 255));
}

TEST_CASE("VoxWriter round-trips palette alpha")
{
    // Alpha is how a block declares itself transparent, so it has to survive a
    // save and load like any other channel.
    VoxModel m = MakeModel(1, 1, 1);
    m.Colors[7] = glm::vec4(0.2f, 0.4f, 0.8f, 0.55f);
    SetId(m, 0, 0, 0, 7);

    const VoxModel r = VoxLoader::Parse(VoxWriter::Write(m));

    CHECK(r.Colors[7].a == doctest::Approx(0.55f).epsilon(1.0 / 255));
}

TEST_CASE("An unwritten palette entry loads back fully opaque")
{
    VoxModel m = MakeModel(1, 1, 1);
    m.Colors[3] = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
    SetId(m, 0, 0, 0, 3);

    const VoxModel r = VoxLoader::Parse(VoxWriter::Write(m));

    CHECK(r.Colors[3].a == doctest::Approx(1.0f).epsilon(1.0 / 255));
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

//Counts SIZE chunks, which is one per model written.
static int CountModels(const std::vector<std::uint8_t>& bytes)
{
    int count = 0;
    for (std::size_t i = 0; i + 4 <= bytes.size(); ++i)
        if (std::memcmp(bytes.data() + i, "SIZE", 4) == 0)
            ++count;
    return count;
}

TEST_CASE("A model wider than 256 is written as several tiles")
{
    VoxModel m = MakeModel(300, 2, 2);
    SetId(m, 0, 0, 0, 1);
    SetId(m, 299, 1, 1, 2);

    const std::vector<std::uint8_t> bytes = VoxWriter::Write(m);

    CHECK(CountModels(bytes) == 2); // 256 + 44
}

TEST_CASE("A tiled model round-trips through the loader")
{
    VoxModel m = MakeModel(300, 2, 300);
    SetId(m, 0, 0, 0, 1);
    SetId(m, 299, 1, 299, 2);
    SetId(m, 255, 0, 256, 3);   // straddles the tile boundary on both axes
    SetId(m, 256, 0, 255, 4);

    const VoxModel r = VoxLoader::Parse(VoxWriter::Write(m));

    CHECK(r.Size == m.Size);
    for (int z = 0; z < m.Size.z; ++z)
        for (int y = 0; y < m.Size.y; ++y)
            for (int x = 0; x < m.Size.x; ++x)
                REQUIRE(r.At(x, y, z) == m.At(x, y, z));
}

TEST_CASE("All-air tiles are skipped rather than written empty")
{
    // 300 wide is two tiles, but only the first holds anything.
    VoxModel m = MakeModel(300, 2, 2);
    SetId(m, 0, 0, 0, 1);

    const std::vector<std::uint8_t> bytes = VoxWriter::Write(m);

    CHECK(CountModels(bytes) == 1);
    CHECK(VoxLoader::Parse(bytes).At(0, 0, 0) == 1);
}

TEST_CASE("A model that fits in one tile is written exactly as before")
{
    // Pins the single-model path: its bytes are what starter.vox and the
    // existing suite are built on, so tiling must not touch them.
    VoxModel m = MakeModel(4, 4, 4);
    SetId(m, 1, 2, 3, 7);

    const std::vector<std::uint8_t> bytes = VoxWriter::Write(m);

    CHECK(CountModels(bytes) == 1);
    // No scene graph at all: a single model needs none, and adding one would
    // change every file Cubit has already written.
    bool hasGraph = false;
    for (std::size_t i = 0; i + 4 <= bytes.size(); ++i)
        if (std::memcmp(bytes.data() + i, "nTRN", 4) == 0)
            hasGraph = true;
    CHECK_FALSE(hasGraph);
}
