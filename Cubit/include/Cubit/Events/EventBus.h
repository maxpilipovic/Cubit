#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

class EventBus;

//Keeps one subscription alive.
//
//Destroying it unsubscribes, which is the whole point: a subscriber's callback
//almost always captures `this`, so the callback must not outlive it. Held as a
//member, a subscription ends exactly when its owner does, and there is nothing
//for the owner to remember to call.
//
//Move-only, because two copies would each unsubscribe. A default-constructed one
//holds nothing, so it can be a member assigned to later.
class Subscription
{
public:
    Subscription() = default;
    Subscription(EventBus* bus, std::type_index type, std::uint64_t id)
        : m_Bus(bus), m_Type(type), m_Id(id)
    {
    }

    ~Subscription() { Reset(); }

    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;

    Subscription(Subscription&& other) noexcept
        : m_Bus(other.m_Bus), m_Type(other.m_Type), m_Id(other.m_Id)
    {
        other.m_Bus = nullptr;
    }

    Subscription& operator=(Subscription&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_Bus = other.m_Bus;
            m_Type = other.m_Type;
            m_Id = other.m_Id;
            other.m_Bus = nullptr;
        }

        return *this;
    }

    //Ends the subscription now, rather than when this is destroyed.
    void Reset();

    //Whether this still holds a subscription.
    bool Active() const { return m_Bus != nullptr; }

private:
    EventBus* m_Bus = nullptr;
    std::type_index m_Type = std::type_index(typeid(void));
    std::uint64_t m_Id = 0;
};

//Typed gameplay notifications, so one part of the game can say what happened
//without knowing who cares.
class EventBus
{
public:
    //Runs `callback` whenever this event type is published, until the returned
    //subscription is destroyed. Keep it: dropping it on the floor unsubscribes
    //immediately, which is why it must not be discarded silently.
    template<typename EventT>
    [[nodiscard]] Subscription Subscribe(std::function<void(const EventT&)> callback)
    {
        const std::type_index type(typeid(EventT));
        const std::uint64_t id = ++m_NextId;

        m_Callbacks[type].push_back(Entry{
            id,
            [callback = std::move(callback)](const void* event)
            {
                callback(*static_cast<const EventT*>(event));
            },
            true });

        return Subscription(this, type, id);
    }

    //Immediately calls every callback subscribed to this event type.
    //
    //A callback may subscribe and unsubscribe, its own included, which is what
    //the two rules here are for. One unsubscribed during a publish does not run
    //in it, and one subscribed during a publish first runs in the next: the loop
    //covers the entries that existed when it started, and skips any since marked
    //dead. Nothing is erased while a publish is running, so no callback is
    //destroyed while it is on the stack.
    template<typename EventT>
    void Publish(const EventT& event)
    {
        const std::type_index type(typeid(EventT));

        const auto found = m_Callbacks.find(type);
        if (found == m_Callbacks.end())
            return;

        const std::size_t count = found->second.size();

        ++m_Publishing;
        for (std::size_t i = 0; i < count; ++i)
        {
            //Found again each time: subscribing during a publish can grow the
            //vector, or rehash the map, and either invalidates a reference kept
            //across a callback. Indices stay right because entries are only ever
            //appended and are never erased while publishing.
            const Entry& entry = m_Callbacks.find(type)->second[i];
            if (entry.Alive)
                entry.Callback(&event);
        }
        --m_Publishing;

        if (m_Publishing == 0)
            Compact();
    }

    //How many callbacks are subscribed to this event type. For tests.
    template<typename EventT>
    std::size_t SubscriberCount() const
    {
        const auto found = m_Callbacks.find(std::type_index(typeid(EventT)));
        if (found == m_Callbacks.end())
            return 0;

        std::size_t alive = 0;
        for (const Entry& entry : found->second)
            alive += entry.Alive ? 1 : 0;

        return alive;
    }

private:
    friend class Subscription;

    using Callback = std::function<void(const void*)>;

    struct Entry
    {
        std::uint64_t Id = 0;
        Callback Callback;

        //Cleared by Remove. A dead entry is skipped and swept up by Compact,
        //never erased mid-publish.
        bool Alive = true;
    };

    //Ends one subscription. Marks it dead, and erases it unless a publish is
    //running, which Compact finishes afterwards.
    void Remove(std::type_index type, std::uint64_t id)
    {
        const auto found = m_Callbacks.find(type);
        if (found == m_Callbacks.end())
            return;

        for (Entry& entry : found->second)
        {
            if (entry.Id != id)
                continue;

            entry.Alive = false;
            break;
        }

        if (m_Publishing == 0)
            Compact();
    }

    //Drops every dead entry. Only called with no publish in flight.
    void Compact()
    {
        for (auto& [type, entries] : m_Callbacks)
        {
            (void)type;
            std::erase_if(entries, [](const Entry& entry) { return !entry.Alive; });
        }
    }

    std::unordered_map<std::type_index, std::vector<Entry>> m_Callbacks;

    //Publishes in flight, counted because a callback may publish.
    int m_Publishing = 0;

    std::uint64_t m_NextId = 0;
};

inline void Subscription::Reset()
{
    if (m_Bus == nullptr)
        return;

    m_Bus->Remove(m_Type, m_Id);
    m_Bus = nullptr;
}
