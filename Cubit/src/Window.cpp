#include "cub.h"

#include "Cubit/Window.h"
#include "Platform/Windows/WindowsWindow.h"

std::unique_ptr<Window> Window::Create(const WindowProperties& properties)
{
    return std::make_unique<WindowsWindow>(properties);
}
