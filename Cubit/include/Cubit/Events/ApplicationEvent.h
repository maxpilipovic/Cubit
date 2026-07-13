#pragma once

#include "Cubit/Events/Event.h"

#include <cstdint>

class CB_API WindowCloseEvent final : public Event
{
public:
    //Returns the static runtime type for window-close events.
    static constexpr EventType GetStaticType() { return EventType::WindowClose; }

    //Returns the runtime type for this window-close event.
    EventType GetEventType() const override { return GetStaticType(); }

    //Returns the diagnostic name for this window-close event.
    std::string_view GetName() const override { return "WindowCloseEvent"; }

    //Returns the application category for this event.
    EventCategory GetCategoryFlags() const override { return EventCategory::Application; }
};

class CB_API WindowResizeEvent final : public Event
{
public:
    //Stores the new logical window dimensions.
    WindowResizeEvent(std::uint32_t width, std::uint32_t height)
        : m_Width(width), m_Height(height)
    {
    }

    //Returns the new logical window width.
    std::uint32_t GetWidth() const { return m_Width; }

    //Returns the new logical window height.
    std::uint32_t GetHeight() const { return m_Height; }

    //Returns the static runtime type for window-resize events.
    static constexpr EventType GetStaticType() { return EventType::WindowResize; }

    //Returns the runtime type for this window-resize event.
    EventType GetEventType() const override { return GetStaticType(); }

    //Returns the diagnostic name for this window-resize event.
    std::string_view GetName() const override { return "WindowResizeEvent"; }

    //Returns the application category for this event.
    EventCategory GetCategoryFlags() const override { return EventCategory::Application; }

private:
    std::uint32_t m_Width;
    std::uint32_t m_Height;
};

class CB_API FramebufferResizeEvent final : public Event
{
public:
    //Stores the new framebuffer dimensions in pixels.
    FramebufferResizeEvent(std::uint32_t width, std::uint32_t height)
        : m_Width(width), m_Height(height)
    {
    }

    //Returns the new framebuffer width in pixels.
    std::uint32_t GetWidth() const { return m_Width; }

    //Returns the new framebuffer height in pixels.
    std::uint32_t GetHeight() const { return m_Height; }

    //Returns the static runtime type for framebuffer-resize events.
    static constexpr EventType GetStaticType() { return EventType::FramebufferResize; }

    //Returns the runtime type for this framebuffer-resize event.
    EventType GetEventType() const override { return GetStaticType(); }

    //Returns the diagnostic name for this framebuffer-resize event.
    std::string_view GetName() const override { return "FramebufferResizeEvent"; }

    //Returns the application category for this event.
    EventCategory GetCategoryFlags() const override { return EventCategory::Application; }

private:
    std::uint32_t m_Width;
    std::uint32_t m_Height;
};

class CB_API WindowFocusEvent final : public Event
{
public:
    //Returns the static runtime type for window-focus events.
    static constexpr EventType GetStaticType() { return EventType::WindowFocus; }

    //Returns the runtime type for this window-focus event.
    EventType GetEventType() const override { return GetStaticType(); }

    //Returns the diagnostic name for this window-focus event.
    std::string_view GetName() const override { return "WindowFocusEvent"; }

    //Returns the application category for this event.
    EventCategory GetCategoryFlags() const override { return EventCategory::Application; }
};

class CB_API WindowLostFocusEvent final : public Event
{
public:
    //Returns the static runtime type for window-lost-focus events.
    static constexpr EventType GetStaticType() { return EventType::WindowLostFocus; }

    //Returns the runtime type for this window-lost-focus event.
    EventType GetEventType() const override { return GetStaticType(); }

    //Returns the diagnostic name for this window-lost-focus event.
    std::string_view GetName() const override { return "WindowLostFocusEvent"; }

    //Returns the application category for this event.
    EventCategory GetCategoryFlags() const override { return EventCategory::Application; }
};

class CB_API WindowMovedEvent final : public Event
{
public:
    //Stores the new logical window position.
    WindowMovedEvent(std::int32_t x, std::int32_t y)
        : m_X(x), m_Y(y)
    {
    }

    //Returns the new horizontal window position.
    std::int32_t GetX() const { return m_X; }

    //Returns the new vertical window position.
    std::int32_t GetY() const { return m_Y; }

    //Returns the static runtime type for window-moved events.
    static constexpr EventType GetStaticType() { return EventType::WindowMoved; }

    //Returns the runtime type for this window-moved event.
    EventType GetEventType() const override { return GetStaticType(); }

    //Returns the diagnostic name for this window-moved event.
    std::string_view GetName() const override { return "WindowMovedEvent"; }

    //Returns the application category for this event.
    EventCategory GetCategoryFlags() const override { return EventCategory::Application; }

private:
    std::int32_t m_X;
    std::int32_t m_Y;
};
