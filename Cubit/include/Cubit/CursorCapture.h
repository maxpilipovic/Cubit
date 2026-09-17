#pragma once

//Whether the game has the mouse, and what each event that can change that does.
//
//Only the rules: the caller applies the result with Input::SetCursorCaptured.
//Header-only and window-free so the rules are tested rather than eyeballed -
//the Sandbox cannot be driven from the keyboard by a script, so Escape in
//particular would otherwise only ever be checked by hand.
class CursorCapture
{
public:
    //The game starts with the mouse, as it always has.
    bool Captured() const { return m_Captured; }

    //Escape. True if the cursor was captured and is now released.
    bool Release()
    {
        const bool changed = m_Captured;
        m_Captured = false;
        return changed;
    }

    //The window lost focus - Alt+Tab, or a click on another window. True if the
    //cursor was captured and is now released.
    bool OnFocusLost() { return Release(); }

    //A mouse button went down in the window. True when the click is the game's
    //to act on. False when it was spent taking the cursor back: acting on it too
    //would edit or fire at wherever the view pointed when the player clicked to
    //return, which is the bug this class exists for.
    bool OnClick()
    {
        if (m_Captured)
            return true;

        m_Captured = true;
        return false;
    }

private:
    bool m_Captured = true;
};
