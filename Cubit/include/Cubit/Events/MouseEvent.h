#pragma once

#include "Cubit/Events/Event.h"
#include "Cubit/MouseCodes.h"

class CB_API MouseMovedEvent final : public Event
{
public:
    //Stores the current logical mouse coordinates.
    MouseMovedEvent(double x, double y)
        : m_X(x), m_Y(y)
    {
    }

    //Returns the current horizontal mouse coordinate.
    double GetX() const { return m_X; }

    //Returns the current vertical mouse coordinate.
    double GetY() const { return m_Y; }

    //Returns the static runtime type for mouse-moved events.
    static constexpr EventType GetStaticType() { return EventType::MouseMoved; }

    //Returns the runtime type for this mouse-moved event.
    EventType GetEventType() const override { return GetStaticType(); }

    //Returns the diagnostic name for this mouse-moved event.
    std::string_view GetName() const override { return "MouseMovedEvent"; }

    //Returns the input and mouse categories for this event.
    EventCategory GetCategoryFlags() const override
    {
        return EventCategory::Input | EventCategory::Mouse;
    }

private:
    double m_X;
    double m_Y;
};

class CB_API MouseScrolledEvent final : public Event
{
public:
    //Stores the horizontal and vertical scroll offsets.
    MouseScrolledEvent(double xOffset, double yOffset)
        : m_XOffset(xOffset), m_YOffset(yOffset)
    {
    }

    //Returns the horizontal scroll offset.
    double GetXOffset() const { return m_XOffset; }

    //Returns the vertical scroll offset.
    double GetYOffset() const { return m_YOffset; }

    //Returns the static runtime type for mouse-scrolled events.
    static constexpr EventType GetStaticType() { return EventType::MouseScrolled; }

    //Returns the runtime type for this mouse-scrolled event.
    EventType GetEventType() const override { return GetStaticType(); }

    //Returns the diagnostic name for this mouse-scrolled event.
    std::string_view GetName() const override { return "MouseScrolledEvent"; }

    //Returns the input and mouse categories for this event.
    EventCategory GetCategoryFlags() const override
    {
        return EventCategory::Input | EventCategory::Mouse;
    }

private:
    double m_XOffset;
    double m_YOffset;
};

class CB_API MouseButtonEvent : public Event
{
public:
    //Returns the engine-owned mouse button code.
    MouseCode GetMouseButton() const { return m_Button; }

    //Returns the input, mouse, and mouse-button categories for this event.
    EventCategory GetCategoryFlags() const override
    {
        return EventCategory::Input | EventCategory::Mouse | EventCategory::MouseButton;
    }

protected:
    //Stores the button associated with a mouse-button event.
    explicit MouseButtonEvent(MouseCode button)
        : m_Button(button)
    {
    }

private:
    MouseCode m_Button;
};

class CB_API MouseButtonPressedEvent final : public MouseButtonEvent
{
public:
    //Stores the mouse button that was pressed.
    explicit MouseButtonPressedEvent(MouseCode button)
        : MouseButtonEvent(button)
    {
    }

    //Returns the static runtime type for mouse-button-pressed events.
    static constexpr EventType GetStaticType() { return EventType::MouseButtonPressed; }

    //Returns the runtime type for this mouse-button-pressed event.
    EventType GetEventType() const override { return GetStaticType(); }

    //Returns the diagnostic name for this mouse-button-pressed event.
    std::string_view GetName() const override { return "MouseButtonPressedEvent"; }
};

class CB_API MouseButtonReleasedEvent final : public MouseButtonEvent
{
public:
    //Stores the mouse button that was released.
    explicit MouseButtonReleasedEvent(MouseCode button)
        : MouseButtonEvent(button)
    {
    }

    //Returns the static runtime type for mouse-button-released events.
    static constexpr EventType GetStaticType() { return EventType::MouseButtonReleased; }

    //Returns the runtime type for this mouse-button-released event.
    EventType GetEventType() const override { return GetStaticType(); }

    //Returns the diagnostic name for this mouse-button-released event.
    std::string_view GetName() const override { return "MouseButtonReleasedEvent"; }
};
