#include <doctest.h>

#include "GameSettings.h"

#include <string>
#include <vector>

using namespace CubitGame;

namespace
{
    //The defaults with text applied on top, as if text were settings.cfg.
    GameSettings Applied(const char* text, std::vector<std::string>& warnings)
    {
        GameSettings settings;
        Apply(SettingsFile::Parse(text), settings, warnings);
        return settings;
    }

    bool AnyMentions(const std::vector<std::string>& warnings, const std::string& word)
    {
        for (const std::string& warning : warnings)
            if (warning.find(word) != std::string::npos)
                return true;

        return false;
    }
}

TEST_CASE("An empty settings file leaves every default in place")
{
    std::vector<std::string> warnings;

    CHECK(Applied("", warnings) == GameSettings{});
    CHECK(warnings.empty());
}

TEST_CASE("Each setting in the file replaces its default")
{
    std::vector<std::string> warnings;
    const GameSettings settings = Applied(
        "mouse_sensitivity = 0.5\n"
        "field_of_view = 90\n"
        "window_width = 1600\n"
        "window_height = 900\n",
        warnings);

    CHECK(settings.MouseSensitivity == doctest::Approx(0.5f));
    CHECK(settings.FieldOfView == doctest::Approx(90.0f));
    CHECK(settings.WindowWidth == 1600);
    CHECK(settings.WindowHeight == 900);
    CHECK(warnings.empty());
}

TEST_CASE("A value that is not a number keeps the default and says so")
{
    std::vector<std::string> warnings;
    const GameSettings settings = Applied("field_of_view = wide\n", warnings);

    CHECK(settings.FieldOfView == doctest::Approx(60.0f));
    REQUIRE(warnings.size() == 1);
    CHECK(AnyMentions(warnings, "field_of_view"));
}

TEST_CASE("A value outside its range is clamped to the nearer end and says so")
{
    //500 plainly means "as wide as allowed", so it becomes 120 rather than
    //being refused and leaving the player at 60 wondering why.
    std::vector<std::string> warnings;
    const GameSettings settings = Applied(
        "field_of_view = 500\n"
        "window_width = 100\n",
        warnings);

    CHECK(settings.FieldOfView == doctest::Approx(120.0f));
    CHECK(settings.WindowWidth == 640);
    CHECK(warnings.size() == 2);
    CHECK(AnyMentions(warnings, "field_of_view"));
    CHECK(AnyMentions(warnings, "window_width"));
}

TEST_CASE("A field of view of nan keeps the default and says so")
{
    //from_chars accepts "nan", and clamp returns it unchanged because every
    //comparison against a NaN is false. Left through, it would be a NaN
    //projection - a black screen, saved in the file, with no way back from
    //inside the game.
    std::vector<std::string> warnings;
    const GameSettings settings = Applied("field_of_view = nan\n", warnings);

    CHECK(settings.FieldOfView == doctest::Approx(60.0f));
    REQUIRE(warnings.size() == 1);
    CHECK(AnyMentions(warnings, "field_of_view"));
    CHECK(AnyMentions(warnings, "is not a number"));
}

TEST_CASE("A mouse sensitivity of nan keeps the default and says so")
{
    //The same hole on the other float: a NaN sensitivity is a NaN yaw on the
    //first mouse move.
    std::vector<std::string> warnings;
    const GameSettings settings = Applied("mouse_sensitivity = -nan\n", warnings);

    CHECK(settings.MouseSensitivity == doctest::Approx(0.12f));
    REQUIRE(warnings.size() == 1);
    CHECK(AnyMentions(warnings, "mouse_sensitivity"));
    CHECK(AnyMentions(warnings, "is not a number"));
}

TEST_CASE("A field of view of inf still clamps, unlike nan")
{
    //Pinned so a later reader does not "simplify" the NaN check to isfinite:
    //inf is ordered, so it clamps correctly, and "inf" means "as wide as
    //allowed" exactly as 500 does. Only NaN is unclampable.
    std::vector<std::string> warnings;
    const GameSettings settings = Applied("field_of_view = inf\n", warnings);

    CHECK(settings.FieldOfView == doctest::Approx(120.0f));
    REQUIRE(warnings.size() == 1);
    CHECK(AnyMentions(warnings, "field_of_view"));
}

TEST_CASE("Every setting flag lands on its own key, and passes the same checks")
{
    //The mapping lives in Game/ rather than GameApp's main so that this test
    //can exist at all. Asserted through Apply rather than against the text, so
    //what is pinned is the setting each flag produces, not a string.
    char program[] = "GameApp.exe";
    char fov[] = "--fov";
    char fovValue[] = "90";
    char sensitivity[] = "--sensitivity";
    char sensitivityValue[] = "0.5";
    char width[] = "--width";
    char widthValue[] = "1600";
    char height[] = "--height";
    char heightValue[] = "900";

    char* argv[] = { program, fov, fovValue, sensitivity, sensitivityValue,
        width, widthValue, height, heightValue };
    const int argc = static_cast<int>(sizeof(argv) / sizeof(argv[0]));

    std::vector<std::string> warnings;
    const GameSettings settings = Applied(FlagOverrides(argc, argv).c_str(), warnings);

    CHECK(settings.FieldOfView == doctest::Approx(90.0f));
    CHECK(settings.MouseSensitivity == doctest::Approx(0.5f));
    CHECK(settings.WindowWidth == 1600);
    CHECK(settings.WindowHeight == 900);
    CHECK(warnings.empty());
}

TEST_CASE("A setting flag with no value after it is ignored")
{
    //Last on the command line with nothing following. Reading its value would
    //be a read past the end of argv.
    char program[] = "GameApp.exe";
    char fov[] = "--fov";

    char* argv[] = { program, fov };

    CHECK(FlagOverrides(2, argv).empty());
}

TEST_CASE("An unknown key changes nothing and is named in a warning")
{
    std::vector<std::string> warnings;

    CHECK(Applied("fullscreen = 1\n", warnings) == GameSettings{});
    REQUIRE(warnings.size() == 1);
    CHECK(AnyMentions(warnings, "fullscreen"));
}

TEST_CASE("A line the parser could not read is reported with its line number")
{
    std::vector<std::string> warnings;
    const GameSettings settings = Applied("field_of_view = 90\nnonsense\n", warnings);

    CHECK(settings.FieldOfView == doctest::Approx(90.0f));
    REQUIRE(warnings.size() == 1);
    CHECK(AnyMentions(warnings, "line 2"));
}

TEST_CASE("The default file reads back as exactly the defaults")
{
    //What stops the file first run writes and the defaults in the code from
    //drifting apart: change one without the other and this fails.
    std::vector<std::string> warnings;

    CHECK(Applied(DefaultFileText().c_str(), warnings) == GameSettings{});
    CHECK(warnings.empty());
}

TEST_CASE("A second source applied on top of the first wins")
{
    //How flags override the file: the file is applied, then the flags as a
    //second file, so a flag passes the same checks as the line it replaces.
    std::vector<std::string> warnings;
    GameSettings settings;
    Apply(SettingsFile::Parse("field_of_view = 70\n"), settings, warnings);
    Apply(SettingsFile::Parse("field_of_view = 90\n"), settings, warnings);

    CHECK(settings.FieldOfView == doctest::Approx(90.0f));
    CHECK(warnings.empty());
}

TEST_CASE("The effective settings describe themselves in one line")
{
    const std::string line = Describe(GameSettings{});

    CHECK(line.find("0.12") != std::string::npos);
    CHECK(line.find("60") != std::string::npos);
    CHECK(line.find("1280x720") != std::string::npos);
    CHECK(line.find('\n') == std::string::npos);
}
