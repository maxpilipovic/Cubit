#include <doctest.h>

#include "TestMaps.h"

#include "Cubit/Renderer/FontAtlas.h"

#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

//GlyphFor's fallback looks up '?' by index without re-checking that index
//against m_Glyphs, on the assumption that an atlas which exists at all was
//built by FromTrueType and is therefore fully baked. A default-constructed
//atlas would break that assumption, so the default constructor is private -
//pin it here rather than leave it as something only the source comment says.
static_assert(!std::is_default_constructible_v<FontAtlas>,
    "an empty FontAtlas must be unreachable: GlyphFor's fallback assumes a baked atlas");

namespace
{
    //Baked once for the whole file: every case below reads the same atlas, and
    //baking a real font per case is the slowest thing in this suite.
    const FontAtlas& Baked()
    {
        static const FontAtlas atlas =
            FontAtlas::FromFile(FixturePath("CascadiaMono.ttf").string(), 32.0f);

        return atlas;
    }
}

TEST_CASE("Every printable character bakes to a glyph that advances the pen")
{
    const FontAtlas& atlas = Baked();

    for (char character = FontAtlas::FirstCharacter;
         character <= FontAtlas::LastCharacter; ++character)
    {
        CAPTURE(static_cast<int>(character));
        CHECK(atlas.GlyphFor(character).Advance > 0.0f);
    }
}

TEST_CASE("A character the font does not cover draws a question mark, not nothing")
{
    //The failure this replaces: the debug font draws an unsupported character
    //as a blank, so a readout silently loses the value it was added to show.
    const FontAtlas& atlas = Baked();

    const FontAtlas::Glyph& missing = atlas.GlyphFor('\t');
    const FontAtlas::Glyph& question = atlas.GlyphFor('?');

    CHECK(missing.Advance == doctest::Approx(question.Advance));
    CHECK(missing.Uv0.x == doctest::Approx(question.Uv0.x));
    CHECK(missing.Uv0.y == doctest::Approx(question.Uv0.y));
}

TEST_CASE("Measuring a string adds up its glyphs' advances")
{
    const FontAtlas& atlas = Baked();

    const float expected =
        atlas.GlyphFor('C').Advance +
        atlas.GlyphFor('u').Advance +
        atlas.GlyphFor('b').Advance;

    CHECK(atlas.Measure("Cub") == doctest::Approx(expected));
    CHECK(atlas.Measure("") == doctest::Approx(0.0f));
}

TEST_CASE("The atlas is a real bitmap of the size it reports")
{
    const FontAtlas& atlas = Baked();

    CHECK(atlas.Width() > 0);
    CHECK(atlas.Height() > 0);
    CHECK(atlas.Pixels().size()
        == static_cast<std::size_t>(atlas.Width()) * atlas.Height());
}

TEST_CASE("Something was actually drawn into the atlas")
{
    //A bake that produced an empty bitmap would satisfy every other case here:
    //the metrics would still be there and the sizes would still add up.
    const FontAtlas& atlas = Baked();

    std::size_t covered = 0;
    for (const std::uint8_t value : atlas.Pixels())
        if (value != 0)
            ++covered;

    CHECK(covered > 0);
}

TEST_CASE("A line of text is taller than the glyphs on it")
{
    const FontAtlas& atlas = Baked();

    CHECK(atlas.LineHeight() > atlas.GlyphFor('M').Size.y);
}

TEST_CASE("RgbaPixels expands coverage into a white texture with coverage as alpha")
{
    const FontAtlas& atlas = Baked();

    const std::vector<std::uint8_t>& coverage = atlas.Pixels();
    const std::vector<std::uint8_t> rgba = atlas.RgbaPixels();

    REQUIRE(rgba.size() == coverage.size() * 4);

    for (std::size_t i = 0; i < coverage.size(); ++i)
    {
        CHECK(rgba[i * 4 + 0] == 255);
        CHECK(rgba[i * 4 + 1] == 255);
        CHECK(rgba[i * 4 + 2] == 255);
        CHECK(rgba[i * 4 + 3] == coverage[i]);
    }
}

TEST_CASE("Baking the same font twice gives the same metrics")
{
    //Text that shifted between runs would make every screenshot comparison
    //and every layout measurement unreliable.
    const FontAtlas first =
        FontAtlas::FromFile(FixturePath("CascadiaMono.ttf").string(), 32.0f);
    const FontAtlas second =
        FontAtlas::FromFile(FixturePath("CascadiaMono.ttf").string(), 32.0f);

    CHECK(first.LineHeight() == doctest::Approx(second.LineHeight()));
    CHECK(first.Measure("Cubit") == doctest::Approx(second.Measure("Cubit")));
    CHECK(first.Pixels() == second.Pixels());
}

TEST_CASE("Bytes that are not a font are refused rather than baked")
{
    const std::vector<std::uint8_t> nonsense(256, 0x7F);

    CHECK_THROWS_AS(FontAtlas::FromTrueType(nonsense, 32.0f), std::runtime_error);
}

TEST_CASE("A font file that is not there is an error, not an empty atlas")
{
    CHECK_THROWS_AS(
        FontAtlas::FromFile("no/such/font.ttf", 32.0f), std::runtime_error);
}

TEST_CASE("A size too large for the atlas is refused rather than truncated")
{
    //Truncation would surface as missing letters long after the bake, so a
    //bake that cannot fit is an error at the point it happens.
    CHECK_THROWS_AS(
        FontAtlas::FromFile(FixturePath("CascadiaMono.ttf").string(), 512.0f),
        std::runtime_error);
}
