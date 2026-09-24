#include <doctest.h>

#include "Cubit/Renderer/FontAtlas.h"

#include <filesystem>

namespace
{
    //The game's own assets, by absolute path. See CB_GAME_ASSETS in
    //game/premake5.lua for why this is not relative to the working directory.
    std::filesystem::path FontPath(const char* name)
    {
        return std::filesystem::path(CB_GAME_ASSETS) / "fonts" / name;
    }
}

TEST_CASE("The font the game ships bakes, and covers everything the HUD can print")
{
    const std::filesystem::path path = FontPath("CascadiaMono.ttf");
    REQUIRE(std::filesystem::exists(path));

    const FontAtlas atlas = FontAtlas::FromFile(path.string(), 32.0f);

    for (char character = FontAtlas::FirstCharacter;
         character <= FontAtlas::LastCharacter; ++character)
    {
        CAPTURE(static_cast<int>(character));
        CHECK(atlas.GlyphFor(character).Advance > 0.0f);
    }
}

TEST_CASE("The font's license ships beside it")
{
    //SIL Open Font License 1.1 permits bundling the font only while its license
    //travels with it, so a build that lost this file would not be shippable.
    REQUIRE(std::filesystem::exists(FontPath("OFL.txt")));
    CHECK(std::filesystem::file_size(FontPath("OFL.txt")) > 0);
}
