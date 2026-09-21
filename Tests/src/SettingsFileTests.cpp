#include <doctest.h>

#include "Cubit/SettingsFile.h"

#include <filesystem>
#include <fstream>

TEST_CASE("A setting is read with the whitespace around it trimmed")
{
    const SettingsFile file = SettingsFile::Parse("   mouse_sensitivity   =   0.5   \n");

    CHECK(file.GetString("mouse_sensitivity") == "0.5");

    const std::optional<float> value = file.GetFloat("mouse_sensitivity");
    REQUIRE(value.has_value());
    CHECK(*value == doctest::Approx(0.5f));
    CHECK(file.Problems().empty());
}

TEST_CASE("Comments are ignored, on their own line and after a value")
{
    const SettingsFile file = SettingsFile::Parse(
        "# a whole-line comment\n"
        "field_of_view = 90 # wide\n");

    CHECK(file.GetInt("field_of_view") == 90);
    CHECK(file.Keys().size() == 1);
    CHECK(file.Problems().empty());
}

TEST_CASE("Blank lines are ignored")
{
    const SettingsFile file = SettingsFile::Parse("\n\n   \nx = 1\n\n");

    REQUIRE(file.Keys().size() == 1);
    CHECK(file.Keys()[0] == "x");
    CHECK(file.Problems().empty());
}

TEST_CASE("A repeated key takes its last value")
{
    //A player who appends a line to the end of the file expects that line to
    //be the one that counts.
    const SettingsFile file = SettingsFile::Parse("x = 1\nx = 2\n");

    CHECK(file.GetInt("x") == 2);
    CHECK(file.Keys().size() == 1);
}

TEST_CASE("A line without = is a problem on its own line, and the rest still loads")
{
    const SettingsFile file = SettingsFile::Parse("a = 1\nnonsense\nb = 2\n");

    CHECK(file.GetInt("a") == 1);
    CHECK(file.GetInt("b") == 2);
    REQUIRE(file.Problems().size() == 1);
    CHECK(file.Problems()[0].Line == 2);
}

TEST_CASE("A line with nothing before = is a problem")
{
    const SettingsFile file = SettingsFile::Parse(" = 5\n");

    CHECK(file.Keys().empty());
    REQUIRE(file.Problems().size() == 1);
    CHECK(file.Problems()[0].Line == 1);
}

TEST_CASE("Windows line endings read the same as Unix ones")
{
    //What Notepad writes. A value that kept its \r would not parse as a number.
    const SettingsFile file = SettingsFile::Parse("x = 1\r\ny = 2\r\n");

    CHECK(file.GetInt("x") == 1);
    CHECK(file.GetInt("y") == 2);
    CHECK(file.GetString("y") == "2");
    CHECK(file.Problems().empty());
}

TEST_CASE("A number with anything after it is not a number")
{
    //12abc is not 12: reading the part that parses would hide the typo.
    const SettingsFile file = SettingsFile::Parse("x = 12abc\n");

    CHECK_FALSE(file.GetInt("x").has_value());
    CHECK_FALSE(file.GetFloat("x").has_value());
    CHECK(file.GetString("x") == "12abc");
}

TEST_CASE("A whole number reads as a float")
{
    const SettingsFile file = SettingsFile::Parse("field_of_view = 60\n");

    const std::optional<float> value = file.GetFloat("field_of_view");
    REQUIRE(value.has_value());
    CHECK(*value == doctest::Approx(60.0f));
}

TEST_CASE("A missing key reads as empty, not as zero")
{
    const SettingsFile file = SettingsFile::Parse("");

    CHECK_FALSE(file.GetInt("x").has_value());
    CHECK_FALSE(file.GetFloat("x").has_value());
    CHECK_FALSE(file.GetString("x").has_value());
}

TEST_CASE("Loading a file that does not exist is empty rather than an error")
{
    CHECK_FALSE(SettingsFile::Load("this/path/does/not/exist/settings.cfg").has_value());
}

TEST_CASE("A file on disk loads the same as its text")
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "cubit-settings-file-test.cfg";
    {
        std::ofstream out(path, std::ios::binary);
        out << "x = 3\n";
    }

    const std::optional<SettingsFile> file = SettingsFile::Load(path.string());
    std::filesystem::remove(path);

    REQUIRE(file.has_value());
    CHECK(file->GetInt("x") == 3);
}
