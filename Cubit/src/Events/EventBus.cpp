#include "cub.h"

#include "Cubit/Events/EventBus.h"

#include <algorithm>
#include <iterator>
#include <mutex>
#include <typeindex>

namespace
{
    struct EventBusSubscriber
    {
        std::uint64_t Id;
        std::function<void(const void*)> Callback;
        bool Active = true;
    };

    struct QueuedGameplayEvent
    {
        std::type_index Type;
        std::shared_ptr<const void> Payload;
    };
}

struct EventBusState
{
    std::unordered_map<std::type_index, std::vector<EventBusSubscriber>> Subscribers;
    std::vector<QueuedGameplayEvent> CurrentQueue;
    std::vector<QueuedGameplayEvent> NextQueue;
    std::vector<QueuedGameplayEvent> WorkerInbox;
    std::mutex WorkerMutex;
    std::uint64_t NextSubscriptionId = 1;
    std::size_t DispatchDepth = 0;
    bool Alive = true;

    //Removes a subscription immediately or marks it inactive during publishing.
    void Disconnect(std::type_index type, std::uint64_t id)
    {
        auto channel = Subscribers.find(type);
        if (channel == Subscribers.end())
            return;

        auto subscriber = std::find_if(
            channel->second.begin(),
            channel->second.end(),
            [id](const EventBusSubscriber& candidate)
            {
                return candidate.Id == id;
            });

        if (subscriber == channel->second.end())
            return;

        if (DispatchDepth > 0)
            subscriber->Active = false;
        else
            channel->second.erase(subscriber);
    }

    //Reports whether a subscription remains active on its typed channel.
    bool IsConnected(std::type_index type, std::uint64_t id) const
    {
        auto channel = Subscribers.find(type);
        if (channel == Subscribers.end())
            return false;

        return std::any_of(
            channel->second.begin(),
            channel->second.end(),
            [id](const EventBusSubscriber& subscriber)
            {
                return subscriber.Id == id && subscriber.Active;
            });
    }

    //Erases subscriptions that were disconnected during callback execution.
    void RemoveInactiveSubscribers()
    {
        for (auto channel = Subscribers.begin(); channel != Subscribers.end();)
        {
            std::erase_if(
                channel->second,
                [](const EventBusSubscriber& subscriber)
                {
                    return !subscriber.Active;
                });

            if (channel->second.empty())
                channel = Subscribers.erase(channel);
            else
                ++channel;
        }
    }
};

struct EventSubscriptionData
{
    std::weak_ptr<EventBusState> State;
    std::type_index Type = std::type_index(typeid(void));
    std::uint64_t Id = 0;
};

struct EventBusData
{
    std::shared_ptr<EventBusState> State = std::make_shared<EventBusState>();
};

EventSubscription::EventSubscription()
    : m_Data(nullptr)
{
}

EventSubscription::EventSubscription(EventSubscriptionData* data)
    : m_Data(data)
{
}

EventSubscription::~EventSubscription()
{
    Disconnect();
    delete m_Data;
}

EventSubscription::EventSubscription(EventSubscription&& other) noexcept
    : m_Data(std::exchange(other.m_Data, nullptr))
{
}

EventSubscription& EventSubscription::operator=(EventSubscription&& other) noexcept
{
    if (this == &other)
        return *this;

    Disconnect();
    delete m_Data;
    m_Data = std::exchange(other.m_Data, nullptr);
    return *this;
}

void EventSubscription::Disconnect()
{
    if (m_Data == nullptr || m_Data->Id == 0)
        return;

    if (const std::shared_ptr<EventBusState> state = m_Data->State.lock())
        state->Disconnect(m_Data->Type, m_Data->Id);

    m_Data->Id = 0;
}

bool EventSubscription::IsConnected() const
{
    if (m_Data == nullptr || m_Data->Id == 0)
        return false;

    const std::shared_ptr<EventBusState> state = m_Data->State.lock();
    return state != nullptr && state->Alive && state->IsConnected(m_Data->Type, m_Data->Id);
}

EventBus::EventBus()
    : m_Data(new EventBusData())
{
}

EventBus::~EventBus()
{
    m_Data->State->Alive = false;
    m_Data->State->Subscribers.clear();
    delete m_Data;
}

EventSubscription EventBus::SubscribeInternal(std::type_index type, ErasedCallback callback)
{
    std::shared_ptr<EventBusState> state = m_Data->State;
    const std::uint64_t id = state->NextSubscriptionId++;
    state->Subscribers[type].push_back({ id, std::move(callback), true });
    return EventSubscription(new EventSubscriptionData{ state, type, id });
}

void EventBus::PublishInternal(std::type_index type, const void* event)
{
    std::shared_ptr<EventBusState> state = m_Data->State;
    auto channel = state->Subscribers.find(type);
    if (channel == state->Subscribers.end())
        return;

    std::vector<std::uint64_t> subscriberIds;
    subscriberIds.reserve(channel->second.size());
    for (const EventBusSubscriber& subscriber : channel->second)
    {
        if (subscriber.Active)
            subscriberIds.push_back(subscriber.Id);
    }

    ++state->DispatchDepth;
    try
    {
        for (const std::uint64_t id : subscriberIds)
        {
            channel = state->Subscribers.find(type);
            if (channel == state->Subscribers.end())
                break;

            auto subscriber = std::find_if(
                channel->second.begin(),
                channel->second.end(),
                [id](const EventBusSubscriber& candidate)
                {
                    return candidate.Id == id && candidate.Active;
                });

            if (subscriber == channel->second.end())
                continue;

            const ErasedCallback callback = subscriber->Callback;
            callback(event);
        }
    }
    catch (...)
    {
        --state->DispatchDepth;
        if (state->DispatchDepth == 0)
            state->RemoveInactiveSubscribers();
        throw;
    }
    --state->DispatchDepth;

    if (state->DispatchDepth == 0)
        state->RemoveInactiveSubscribers();
}

void EventBus::EnqueueInternal(std::type_index type, ErasedEvent event)
{
    m_Data->State->NextQueue.push_back({ type, std::move(event) });
}

void EventBus::EnqueueFromWorkerInternal(std::type_index type, ErasedEvent event)
{
    std::shared_ptr<EventBusState> state = m_Data->State;
    std::scoped_lock lock(state->WorkerMutex);
    state->WorkerInbox.push_back({ type, std::move(event) });
}

void EventBus::DispatchQueued()
{
    std::shared_ptr<EventBusState> state = m_Data->State;
    {
        std::scoped_lock lock(state->WorkerMutex);
        state->NextQueue.insert(
            state->NextQueue.end(),
            std::make_move_iterator(state->WorkerInbox.begin()),
            std::make_move_iterator(state->WorkerInbox.end()));
        state->WorkerInbox.clear();
    }

    state->CurrentQueue.clear();
    state->CurrentQueue.swap(state->NextQueue);

    for (const QueuedGameplayEvent& event : state->CurrentQueue)
        PublishInternal(event.Type, event.Payload.get());

    state->CurrentQueue.clear();
}
