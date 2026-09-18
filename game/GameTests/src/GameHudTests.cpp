#include <doctest.h>

#include "Cubit/Renderer/DebugFont.h"

#include "GameHudLayer.h"

#include <string_view>

TEST_CASE("Every label the game HUD draws is one the debug font can draw")
{
    //An unsupported character renders as a blank glyph rather than an error, so
    //a label that drifts out of the font hides the value it was added to show.
    //This case lives with the game because the words are the game's: HEALTH and
    //KILLED mean nothing to the engine, and the engine's suite should not have
    //to change when the game renames a line.
    for (const std::string_view label : GameHudLayer::Labels)
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
