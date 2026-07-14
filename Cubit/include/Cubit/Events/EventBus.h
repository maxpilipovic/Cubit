#pragma once

#include <functional>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

class EventBus
{
public:
    //Saves a callback that will run whenever this event type is published.
    template<typename EventT>
    void Subscribe(std::function<void(const EventT&)> callback)
    {
        m_Callbacks[std::type_index(typeid(EventT))].push_back(
            [callback = std::move(callback)](const void* event)
            {
                callback(*static_cast<const EventT*>(event));
            });
    }

    //Immediately calls every callback subscribed to this event type.
    template<typename EventT>
    void Publish(const EventT& event)
    {
        const auto callbacks = m_Callbacks.find(std::type_index(typeid(EventT)));
        if (callbacks == m_Callbacks.end())
            return;

        const std::vector<Callback> callbacksToRun = callbacks->second;
        for (const Callback& callback : callbacksToRun)
            callback(&event);
    }

private:
    using Callback = std::function<void(const void*)>;

    std::unordered_map<std::type_index, std::vector<Callback>> m_Callbacks;
};
