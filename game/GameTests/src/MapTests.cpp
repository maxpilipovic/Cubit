#include <doctest.h>

#include "Cubit/Voxel/TerrainGen.h"
#include "Cubit/Voxel/VoxLoader.h"

#include <filesystem>

//The game's maps, checked from the game's side. The engine's suite does not
//read them: they are the game's content, and the engine has to be testable
//without the game beside it.

namespace
{
    //A map the game ships, by absolute path. See CB_GAME_ASSETS in
    //game/premake5.lua for why this is not relative to the working directory.
    std::filesystem::path MapPath(const char* name)
    {
        return std::filesystem::path(CB_GAME_ASSETS) / "maps" / name;
    }
}

TEST_CASE("The committed 256 battlefield loads at the expected size")
{
    const std::filesystem::path path = MapPath("battlefield.vox");
    REQUIRE(std::filesystem::exists(path));

    const VoxModel model = VoxLoader::LoadFile(path.string());
    CHECK(model.Size == glm::ivec3(256, 64, 256));
}

TEST_CASE("The shipped 512 battlefield is exactly what TerrainGen generates")
{
    //What lets the engine's suite, and the harness, generate this map instead
    //of reading the game's copy of it: MapGen writes TerrainGen's output at
    //this size and nothing else. Only the game can check that, because only
    //the game may read its own map. If this fails, the engine's tests are
    //measuring a world the game no longer ships.
    const std::filesystem::path path = MapPath("battlefield512.vox");
    REQUIRE(std::filesystem::exists(path));

    TerrainConfig config;
    config.Size = glm::ivec3(512, 64, 512);
    const VoxModel expected = TerrainGen::Generate(config);
    const VoxModel actual = VoxLoader::LoadFile(path.string());

    REQUIRE(actual.Size == expected.Size);

    //The position of the first disagreement, not a bool: 16.7M cells is far
    //too many to assert one at a time, and a difference is only diagnosable if
    //the failure says where it is.
    glm::ivec3 firstBad(-1);
    for (int z = 0; z < expected.Size.z && firstBad.x < 0; ++z)
        for (int y = 0; y < expected.Size.y && firstBad.x < 0; ++y)
            for (int x = 0; x < expected.Size.x; ++x)
                if (actual.At(x, y, z) != expected.At(x, y, z))
                {
                    firstBad = glm::ivec3(x, y, z);
                    break;
                }

    CAPTURE(firstBad.x);
    CAPTURE(firstBad.y);
    CAPTURE(firstBad.z);
    CHECK(firstBad == glm::ivec3(-1));
}
