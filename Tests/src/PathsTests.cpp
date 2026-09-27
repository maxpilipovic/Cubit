#include <doctest.h>

#include "Cubit/Paths.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace
{
    //Puts the working directory back however a case ends, so no later case
    //runs from somewhere it did not expect.
    struct RestoreWorkingDirectory
    {
        fs::path Saved = fs::current_path();
        ~RestoreWorkingDirectory() { fs::current_path(Saved); }
    };

    //A scratch directory of this suite's own, removed afterwards.
    struct ScratchDirectory
    {
        fs::path Path = fs::temp_directory_path() / "cubit-paths-tests";

        ScratchDirectory()
        {
            fs::remove_all(Path);
            fs::create_directories(Path);
        }

        ~ScratchDirectory() { fs::remove_all(Path); }
    };
}

TEST_CASE("The executable directory is the one this test executable is in")
{
    const fs::path directory = ExecutableDirectory();

    CHECK(directory.is_absolute());
    CHECK(fs::exists(directory / "Tests.exe"));
}

TEST_CASE("Entering the executable directory moves there and says where it came from")
{
    RestoreWorkingDirectory restore;
    ScratchDirectory scratch;
    fs::current_path(scratch.Path);

    const fs::path launchedFrom = EnterExecutableDirectory();

    CHECK(fs::equivalent(launchedFrom, scratch.Path));
    CHECK(fs::equivalent(fs::current_path(), ExecutableDirectory()));
}

TEST_CASE("A path the user typed is found where they typed it")
{
    ScratchDirectory scratch;
    std::ofstream(scratch.Path / "mine.vox") << "x";

    SUBCASE("relative, and present in the launch directory: taken from there")
    {
        const fs::path resolved = FromLaunchDirectory("mine.vox", scratch.Path);
        CHECK(resolved.is_absolute());
        CHECK(fs::equivalent(resolved, scratch.Path / "mine.vox"));
    }

    SUBCASE("relative, and absent from the launch directory: left for the executable's")
    {
        //So "--map assets/maps/battlefield256.vox" still names the shipped map
        //from wherever the game was started.
        CHECK(FromLaunchDirectory("assets/maps/x.vox", scratch.Path) == fs::path("assets/maps/x.vox"));
    }

    SUBCASE("absolute: untouched")
    {
        const fs::path absolute = scratch.Path / "elsewhere.vox";
        CHECK(FromLaunchDirectory(absolute, fs::temp_directory_path()) == absolute);
    }
}
