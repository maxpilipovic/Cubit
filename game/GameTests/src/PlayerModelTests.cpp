#include <doctest.h>

#include "Cubit/Voxel/VoxLoader.h"

#include <filesystem>

TEST_CASE("The placeholder player model parses and has the generated dimensions")
{
    //GameTests.exe is launched by its own postbuildcommands step with the
    //project directory (game/GameTests) as the working directory, but the
    //suite is also run by hand from the repo root, so try both rather than
    //silently skipping when neither is found - a skipped check looks green
    //while proving nothing.
    std::filesystem::path path;
    for (const char* candidate : {
            "../assets/models/player.vox",
            "game/assets/models/player.vox" })
        if (std::filesystem::exists(candidate))
        {
            path = candidate;
            break;
        }

    REQUIRE_FALSE(path.empty());

    const VoxModel model = VoxLoader::LoadFile(path.string());

    //generate_player.ps1 writes vox SIZE (4, 6, 18); VoxLoader converts vox
    //(x, y, z) to Cubit (x, z, y), so the loaded model is 4 deep, 18 tall and
    //6 wide in Cubit space.
    CHECK(model.Size.x == 4);
    CHECK(model.Size.y == 18);
    CHECK(model.Size.z == 6);
}
