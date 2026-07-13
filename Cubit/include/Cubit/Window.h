#pragma once

#include "Cubit/Core.h"

#include <cstdint>
#include <memory>
#include <string>

struct WindowProperties
{
    std::string Title = "Cubit";
    std::uint32_t Width = 1280;
    std::uint32_t Height = 720;
};

class CB_API Window
{
public:
    virtual ~Window() = default;

    virtual void OnUpdate() = 0;

    [[nodiscard]] virtual bool ShouldClose() const = 0;
    [[nodiscard]] virtual std::uint32_t GetWidth() const = 0;
    [[nodiscard]] virtual std::uint32_t GetHeight() const = 0;
    [[nodiscard]] virtual void* GetNativeWindow() const = 0;

    [[nodiscard]] static std::unique_ptr<Window> Create(
        const WindowProperties& properties = WindowProperties());
};
