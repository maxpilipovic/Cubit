#include <doctest.h>

#include "Cubit/Renderer/DebugFont.h"

//The harness has no suite of its own, so its one testable rule - that its
//readout is spelled in letters the font has - is checked from here.
#include "HudLayer.h"

#include <cstdint>
#include <set>
#include <string>
#include <string_view>

TEST_CASE("Every character in the order string has a glyph")
{
    //A mismatch between Order and Glyphs draws the WRONG letter with no error
    //at all, which is the failure mode the font's own tests exist to end.
    CHECK(DebugFont::Order.size() == DebugFont::GlyphCount);
}

TEST_CASE("Every glyph is five by seven and no two are the same")
{
    //The count above catches a missing or extra glyph, not a duplicated one:
    //paste a glyph twice and drop another, and the count still matches while
    //one letter silently draws as its neighbour. Every glyph here is meant to
    //be distinct - a blank space is the only empty one - so a repeat is always
    //a mistake.
    std::set<std::string> seen;

    for (std::uint32_t glyph = 0; glyph < DebugFont::GlyphCount; ++glyph)
    {
        CAPTURE(glyph);

        std::string pixels;
        for (std::uint32_t row = 0; row < DebugFont::GlyphHeight; ++row)
        {
            const std::string_view line = DebugFont::Glyphs[glyph][row];

            CAPTURE(row);
            REQUIRE(line.size() == DebugFont::GlyphWidth);
            CHECK(line.find_first_not_of(".#") == std::string_view::npos);

            pixels += line;
        }

        CHECK(seen.insert(pixels).second);
    }
}

TEST_CASE("Every label the harness HUD draws is one the debug font can draw")
{
    //An unsupported character falls back to the blank glyph, so a HUD label
    //with a letter missing renders as a gap rather than failing - which is why
    //two stages of HUD were limited to words the old font could spell.
    //
    //Only the harness's words are checked here. The game's are its own, and
    //GameHudTests checks them against this same font.
    for (const std::string_view label : HudLayer::Labels)
    {
        CAPTURE(label);

        for (const char character : label)
        {
            if (character == ' ')
                continue;

            CAPTURE(character);
            CHECK(DebugFont::IndexOf(character) != DebugFont::IndexOf(' '));
        }
    }
}
