#include <doctest.h>

#include "Cubit/CursorCapture.h"

TEST_CASE("The game starts with the cursor")
{
    CursorCapture cursor;
    CHECK(cursor.Captured());
}

TEST_CASE("Escape gives the cursor back, and a click takes it again without acting")
{
    //A3 on the pre-game punch list. The Sandbox took the cursor once and never
    //returned it, so clicks meant for another window landed in the game as edits.
    CursorCapture cursor;

    CHECK(cursor.Release());
    CHECK_FALSE(cursor.Captured());

    //Already released: nothing changes.
    CHECK_FALSE(cursor.Release());

    //The click that takes the cursor back is spent doing that. Acting on it too
    //would break or place a block, or fire, at wherever the view happened to
    //point when the player clicked to come back.
    CHECK_FALSE(cursor.OnClick());
    CHECK(cursor.Captured());

    //The next one is the game's.
    CHECK(cursor.OnClick());
}

TEST_CASE("A click while the game has the cursor is the game's")
{
    CursorCapture cursor;
    CHECK(cursor.OnClick());
    CHECK(cursor.OnClick());
    CHECK(cursor.Captured());
}

TEST_CASE("Losing focus gives the cursor back, and coming back needs a click")
{
    //Alt+Tab away and back. Focus returning is not the player asking for the
    //game: the click that brought the window forward is swallowed like any other
    //recapturing click.
    CursorCapture cursor;

    CHECK(cursor.OnFocusLost());
    CHECK_FALSE(cursor.Captured());

    CHECK_FALSE(cursor.OnClick());
    CHECK(cursor.Captured());
}
