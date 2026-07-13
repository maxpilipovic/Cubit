#pragma once

#include "Cubit/Core.h"

#include <cstdint>
#include <functional>
#include <string_view>
#include <utility>

enum class EventType : std::uint16_t
{
    None = 0,
    WindowClose,
    WindowResize,
    FramebufferResize,
    WindowFocus,
    WindowLostFocus,
    WindowMoved,
    KeyPressed,
    KeyReleased,
    KeyTyped,
    MouseMoved,
    MouseScrolled,
    MouseButtonPressed,
    MouseButtonReleased
};

enum class EventCategory : std::uint32_t
{
    None = 0,
    Application = 1 << 0,
    Input = 1 << 1,
    Keyboard = 1 << 2,
    Mouse = 1 << 3,
    MouseButton = 1 << 4
};

//Combines two event categories into a bit mask.
constexpr EventCategory operator|(EventCategory left, EventCategory right)
{
    return static_cast<EventCategory>(
        static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}

class CB_API Event
{
public:
    //Releases a routed event through its base interface.
    virtual ~Event() = default;

    //Returns the strongly typed runtime identifier for this event.
    virtual EventType GetEventType() const = 0;

    //Returns a stable diagnostic name for this event.
    virtual std::string_view GetName() const = 0;

    //Returns the categories associated with this event.
    virtual EventCategory GetCategoryFlags() const = 0;

    //Reports whether this event belongs to the requested category.
    bool IsInCategory(EventCategory category) const
    {
        return (static_cast<std::uint32_t>(GetCategoryFlags()) &
            static_cast<std::uint32_t>(category)) != 0;
    }

    bool Handled = false;
};

class EventDispatcher
{
public:
    //Binds the dispatcher to an event for type-checked handling.
    explicit EventDispatcher(Event& event)
        : m_Event(event)
    {
    }

    //Invokes a handler when the event runtime type matches the requested type.
    template<typename EventT, typename Handler>
    bool Dispatch(Handler&& handler)
    {
        if (m_Event.GetEventType() != EventT::GetStaticType())
            return false;

        const bool handled = std::invoke(
            std::forward<Handler>(handler), static_cast<EventT&>(m_Event));
        m_Event.Handled = m_Event.Handled || handled;
        return true;
    }

private:
    Event& m_Event;
};
