#pragma once

#include "Cubit/Core.h"
#include "Cubit/KeyCodes.h"
#include "Cubit/MouseCodes.h"

class Window;

struct MousePosition
{
    float X = 0.0f;
    float Y = 0.0f;
};

class CB_API Input
{
public:
    //Reports whether a physical keyboard key is currently held.
    static bool IsKeyPressed(KeyCode key);

    //Reports whether a mouse button is currently held.
    static bool IsMouseButtonPressed(MouseCode button);

    //Returns the current logical mouse coordinates.
    static MousePosition GetMousePosition();

    //Returns the current horizontal logical mouse coordinate.
    static float GetMouseX();

    //Returns the current vertical logical mouse coordinate.
    static float GetMouseY();

private:
    friend class Application;

    //Connects polling to the application-owned window without exposing GLFW.
    static void SetWindow(Window* window);
};
