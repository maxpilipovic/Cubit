#include <doctest.h>

#include "Cubit/Voxel/VoxLoader.h"

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace
{
    //Appends a little-endian int32 to a byte buffer.
    void PushInt(std::vector<std::uint8_t>& out, std::int32_t value)
    {
        for (int i = 0; i < 4; ++i)
            out.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF));
    }

    //Appends a 4-byte chunk tag.
    void PushTag(std::vector<std::uint8_t>& out, const char* tag)
    {
        out.insert(out.end(), tag, tag + 4);
    }

    struct Vox { std::uint8_t x, y, z, colorIndex; };

    //Builds a minimal single-model .vox byte buffer with the given voxels and a
    //palette whose entry for colour index i is (i, i, i) scaled to bytes.
    std::vector<std::uint8_t> MakeVoxBytes(
        std::int32_t sx, std::int32_t sy, std::int32_t sz,
        const std::vector<Vox>& voxels)
    {
        std::vector<std::uint8_t> size;
        PushTag(size, "SIZE");
        PushInt(size, 12);
        PushInt(size, 0);
        PushInt(size, sx);
        PushInt(size, sy);
        PushInt(size, sz);

        std::vector<std::uint8_t> xyzi;
        PushTag(xyzi, "XYZI");
        PushInt(xyzi, 4 + 4 * static_cast<std::int32_t>(voxels.size()));
        PushInt(xyzi, 0);
        PushInt(xyzi, static_cast<std::int32_t>(voxels.size()));
        for (const Vox& v : voxels)
        {
            xyzi.push_back(v.x);
            xyzi.push_back(v.y);
            xyzi.push_back(v.z);
            xyzi.push_back(v.colorIndex);
        }

        std::vector<std::uint8_t> rgba;
        PushTag(rgba, "RGBA");
        PushInt(rgba, 256 * 4);
        PushInt(rgba, 0);
        for (int j = 0; j < 256; ++j)
        {
            // RGBA entry j is colour index j + 1.
            const std::uint8_t c = static_cast<std::uint8_t>(j + 1);
            rgba.push_back(c);   // r
            rgba.push_back(c);   // g
            rgba.push_back(c);   // b
            rgba.push_back(255); // a
        }

        std::vector<std::uint8_t> bytes;
        PushTag(bytes, "VOX ");
        PushInt(bytes, 150);
        PushTag(bytes, "MAIN");
        PushInt(bytes, 0);
        PushInt(bytes, static_cast<std::int32_t>(size.size() + xyzi.size() + rgba.size()));
        bytes.insert(bytes.end(), size.begin(), size.end());
        bytes.insert(bytes.end(), xyzi.begin(), xyzi.end());
        bytes.insert(bytes.end(), rgba.begin(), rgba.end());
        return bytes;
    }
}

TEST_CASE("Parsing reports the model size with axes swapped to Y-up")
{
    // vox size (2, 3, 4) -> cubit (x=2, y=vox z=4, z=vox y=3)
    const auto bytes = MakeVoxBytes(2, 3, 4, {});
    const VoxModel model = VoxLoader::Parse(bytes);

    CHECK(model.Size == glm::ivec3(2, 4, 3));
}

TEST_CASE("Parsing places a voxel at the axis-swapped position")
{
    // A voxel at vox (1, 2, 3) lands at cubit (1, 3, 2).
    const auto bytes = MakeVoxBytes(4, 4, 4, { { 1, 2, 3, 7 } });
    const VoxModel model = VoxLoader::Parse(bytes);

    CHECK(model.At(1, 3, 2) == 7);
    CHECK(model.At(0, 0, 0) == 0);
}

TEST_CASE("Parsing maps colour indices through the palette off-by-one")
{
    const auto bytes = MakeVoxBytes(1, 1, 1, { { 0, 0, 0, 5 } });
    const VoxModel model = VoxLoader::Parse(bytes);

    // Colour index 5 came from RGBA entry 4, whose bytes are (5,5,5).
    const glm::vec3 expected(5.0f / 255.0f, 5.0f / 255.0f, 5.0f / 255.0f);
    CHECK(model.Colors[5].x == doctest::Approx(expected.x));
    CHECK(model.Colors[5].y == doctest::Approx(expected.y));
    CHECK(model.Colors[5].z == doctest::Approx(expected.z));
}

TEST_CASE("Parsing rejects a buffer without the VOX magic")
{
    std::vector<std::uint8_t> bytes = { 'N', 'O', 'P', 'E' };
    CHECK_THROWS_AS(VoxLoader::Parse(bytes), std::runtime_error);
}

TEST_CASE("Parsing rejects a model larger than 256 on an axis")
{
    const auto bytes = MakeVoxBytes(257, 1, 1, {});
    CHECK_THROWS_AS(VoxLoader::Parse(bytes), std::runtime_error);
}

TEST_CASE("Parsing rejects a truncated buffer")
{
    auto bytes = MakeVoxBytes(2, 2, 2, { { 0, 0, 0, 1 } });
    bytes.resize(bytes.size() - 8); // chop off part of the RGBA chunk
    CHECK_THROWS_AS(VoxLoader::Parse(bytes), std::runtime_error);
}
