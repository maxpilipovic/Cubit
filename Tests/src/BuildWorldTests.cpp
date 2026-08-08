#include <doctest.h>

#include "Cubit/Voxel/VoxLoader.h"

#include <cstdint>

namespace
{
    //Builds a VoxModel directly (no parsing) for world-building tests.
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

TEST_CASE("BuildWorld sizes the world up to whole chunks")
{
    const VoxModel model = MakeModel(20, 5, 40); // 20->2, 5->1, 40->3 chunks
    const World world = BuildWorld(model);

    CHECK(world.GetChunksX() == 2);
    CHECK(world.GetChunksY() == 1);
    CHECK(world.GetChunksZ() == 3);
}

TEST_CASE("BuildWorld places solid voxels and leaves the rest air")
{
    VoxModel model = MakeModel(4, 4, 4);
    Set(model, 1, 2, 3, 6);
    const World world = BuildWorld(model);

    CHECK(world.GetBlock(1, 2, 3) == BlockId{6});
    CHECK(world.GetBlock(0, 0, 0) == BlockId{0});
    CHECK_FALSE(world.IsBlockPresent(0, 0, 0));
}

TEST_CASE("BuildWorld installs the model's palette")
{
    VoxModel model = MakeModel(1, 1, 1);
    model.Colors[4] = glm::vec4(0.2f, 0.4f, 0.6f, 1.0f);
    Set(model, 0, 0, 0, 4);
    const World world = BuildWorld(model);

    CHECK(world.GetBlockColor(BlockId{4}) == glm::vec4(0.2f, 0.4f, 0.6f, 1.0f));
}
