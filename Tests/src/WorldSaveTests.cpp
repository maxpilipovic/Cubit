#include <doctest.h>

#include "Cubit/Voxel/VoxLoader.h"
#include "Cubit/Voxel/VoxWriter.h"
#include "Cubit/Voxel/World.h"

#include <cstdint>
#include <filesystem>
#include <stdexcept>

namespace
{
    //The first position where two worlds disagree, or (-1, -1, -1) when they
    //agree everywhere. Returning the position rather than a bool means a failure
    //message names the block that broke, and it keeps the comparison to a single
    //assertion instead of tens of thousands.
    glm::ivec3 FirstBlockMismatch(const World& left, const World& right)
    {
        for (int z = 0; z < left.GetDepth(); ++z)
            for (int y = 0; y < left.GetHeight(); ++y)
                for (int x = 0; x < left.GetWidth(); ++x)
                    if (left.GetBlock(x, y, z) != right.GetBlock(x, y, z))
                        return glm::ivec3(x, y, z);

        return glm::ivec3(-1);
    }

    //The same comparison between two models.
    glm::ivec3 FirstVoxelMismatch(const VoxModel& left, const VoxModel& right)
    {
        for (int z = 0; z < left.Size.z; ++z)
            for (int y = 0; y < left.Size.y; ++y)
                for (int x = 0; x < left.Size.x; ++x)
                    if (left.At(x, y, z) != right.At(x, y, z))
                        return glm::ivec3(x, y, z);

        return glm::ivec3(-1);
    }

    //Builds a VoxModel directly (no parsing), matching BuildWorldTests.cpp's helper.
    VoxModel MakeModel(int sx, int sy, int sz)
    {
        VoxModel model;
        model.Size = glm::ivec3(sx, sy, sz);
        model.Voxels.assign(
            static_cast<std::size_t>(sx) * sy * sz, 0);
        model.Colors = DefaultPalette();
        return model;
    }

    void Set(VoxModel& model, int x, int y, int z, std::uint8_t id)
    {
        model.Voxels[static_cast<std::size_t>(x) +
            static_cast<std::size_t>(model.Size.x) *
            (static_cast<std::size_t>(y) +
             static_cast<std::size_t>(model.Size.y) * static_cast<std::size_t>(z))] = id;
    }
}

TEST_CASE("ToVoxModel reports the world's padded block size")
{
    // A World is always whole chunks, so the saved size is the padded size, not
    // whatever size the map originally came from.
    const World world(2, 1, 3); // chunks -> 32 x 16 x 48 blocks

    CHECK(ToVoxModel(world).Size == glm::ivec3(32, 16, 48));
}

TEST_CASE("ToVoxModel places blocks at matching positions")
{
    World world(2, 1, 3);
    world.SetBlock(0, 0, 0, BlockId{1});
    world.SetBlock(17, 5, 40, BlockId{9});
    world.SetBlock(31, 15, 47, BlockId{4}); // the far corner

    const VoxModel model = ToVoxModel(world);

    CHECK(model.At(0, 0, 0) == 1);
    CHECK(model.At(17, 5, 40) == 9);
    CHECK(model.At(31, 15, 47) == 4);
}

TEST_CASE("ToVoxModel leaves untouched positions as air")
{
    World world(1, 1, 1);
    world.SetBlock(8, 8, 8, BlockId{1});

    const VoxModel model = ToVoxModel(world);

    CHECK(model.At(8, 8, 8) == 1);
    CHECK(model.At(7, 8, 8) == 0);
    CHECK(model.At(0, 0, 0) == 0);
}

TEST_CASE("ToVoxModel carries the world's palette")
{
    World world(1, 1, 1);
    Palette palette = DefaultPalette();
    palette[4] = glm::vec3(0.2f, 0.4f, 0.6f);
    world.SetPalette(palette);

    CHECK(ToVoxModel(world).Colors[4] == glm::vec3(0.2f, 0.4f, 0.6f));
}

TEST_CASE("A world survives a round trip through BuildWorld")
{
    // The property the whole feature exists to provide. It fails loudly if
    // either half of the conversion drifts.
    World world(2, 1, 2);
    Palette palette = DefaultPalette();
    palette[6] = glm::vec3(0.1f, 0.2f, 0.3f);
    world.SetPalette(palette);

    world.SetBlock(0, 0, 0, BlockId{1});
    world.SetBlock(5, 9, 17, BlockId{6});
    world.SetBlock(31, 15, 31, BlockId{3});

    const World restored = BuildWorld(ToVoxModel(world));

    REQUIRE(restored.GetWidth() == world.GetWidth());
    REQUIRE(restored.GetHeight() == world.GetHeight());
    REQUIRE(restored.GetDepth() == world.GetDepth());
    CHECK(FirstBlockMismatch(world, restored) == glm::ivec3(-1));
    CHECK(restored.GetBlockColor(BlockId{6}) == world.GetBlockColor(BlockId{6}));
}

TEST_CASE("A world survives a round trip through .vox bytes")
{
    // Goes through Write and Parse as well, so the axis swap and the palette
    // off-by-one are exercised, not just the conversion.
    World world(1, 1, 2);
    world.SetBlock(3, 4, 20, BlockId{7});
    world.SetBlock(15, 15, 31, BlockId{2});

    const VoxModel model = ToVoxModel(world);
    const VoxModel restored = VoxLoader::Parse(VoxWriter::Write(model));

    REQUIRE(restored.Size == model.Size);
    CHECK(FirstVoxelMismatch(model, restored) == glm::ivec3(-1));
}

TEST_CASE("A world too wide for a single .vox model is rejected")
{
    // 17 chunks on x is 272 blocks. Write stores a coordinate in one byte, so
    // without this guard the file would wrap round and load back as garbage.
    const World world(17, 1, 1);

    CHECK_THROWS_AS(ToVoxModel(world), std::runtime_error);
}

TEST_CASE("A world exactly 256 blocks on an axis is accepted")
{
    // 256 is the largest legal size, not the first illegal one: coordinates run
    // 0 to 255, which is exactly a byte. Round-trip an actual voxel at x=255
    // through Write/Parse, since that's the exact coordinate the single-byte
    // encoding could wrap on.
    World world(16, 1, 1);
    world.SetBlock(255, 15, 15, BlockId{3});

    const VoxModel model = ToVoxModel(world);
    CHECK(model.Size.x == 256);

    const VoxModel restored = VoxLoader::Parse(VoxWriter::Write(model));

    CHECK(restored.Size.x == 256);
    CHECK(restored.At(255, 15, 15) == 3);
}

TEST_CASE("A world too deep for a single .vox model is rejected")
{
    // 17 chunks on z is 272 blocks. The guard tests above only vary x, so this
    // pins that every axis is checked, not just x three times over, and that
    // the failing axis is named correctly in the message.
    const World world(1, 1, 17);

    CHECK_THROWS_WITH_AS(ToVoxModel(world),
        "vox: world is too large for a single .vox model (z = 272)",
        std::runtime_error);
}

TEST_CASE("WriteFile round-trips a world through disk")
{
    World world(1, 1, 1);
    world.SetBlock(2, 3, 4, BlockId{5});

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "cubit_worldsave_test.vox";

    VoxWriter::WriteFile(ToVoxModel(world), path.string());
    const VoxModel restored = VoxLoader::LoadFile(path.string());
    std::filesystem::remove(path);

    CHECK(restored.Size == glm::ivec3(16, 16, 16));
    CHECK(restored.At(2, 3, 4) == 5);
}

TEST_CASE("A model shorter than one chunk on y is padded on the way back out")
{
    // The headline behaviour documented on ToVoxModel: a 16x6x16 map comes
    // back as 16x16x16, with the added layers as air.
    VoxModel model = MakeModel(16, 6, 16);
    Set(model, 1, 2, 3, 6);
    Set(model, 15, 5, 0, 4);

    const VoxModel restored = ToVoxModel(BuildWorld(model));

    REQUIRE(restored.Size == glm::ivec3(16, 16, 16));
    CHECK(restored.At(1, 2, 3) == 6);
    CHECK(restored.At(15, 5, 0) == 4);

    for (int z = 0; z < restored.Size.z; ++z)
        for (int y = 6; y < restored.Size.y; ++y)
            for (int x = 0; x < restored.Size.x; ++x)
                CHECK(restored.At(x, y, z) == 0);
}

TEST_CASE("WriteFile throws when the path cannot be opened")
{
    const World world(1, 1, 1);
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        "cubit_no_such_directory" / "out.vox";

    CHECK_THROWS_AS(
        VoxWriter::WriteFile(ToVoxModel(world), path.string()),
        std::runtime_error);
}
