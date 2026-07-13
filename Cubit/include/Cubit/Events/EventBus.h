#pragma once

#include "Cubit/Core.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <typeindex>
#include <utility>

struct EventBusState;
struct EventSubscriptionData;

class EventBus;

class CB_API EventSubscription
{
public:
    //Creates an empty subscription token.
    EventSubscription();

    //Disconnects the subscription when its token leaves scope.
    ~EventSubscription();

    //Prevents two tokens from owning the same subscription.
    EventSubscription(const EventSubscription&) = delete;

    //Prevents assigning duplicate ownership of a subscription.
    EventSubscription& operator=(const EventSubscription&) = delete;

    //Transfers a subscription token without disconnecting it.
    EventSubscription(EventSubscription&& other) noexcept;

    //Disconnects the current subscription before taking another token.
    EventSubscription& operator=(EventSubscription&& other) noexcept;

    //Disconnects the listener before the token leaves scope.
    void Disconnect();

    //Reports whether this token still represents a live subscription.
    bool IsConnected() const;

private:
    friend class EventBus;

    //Creates a token for an internal typed channel subscription.
    explicit EventSubscription(EventSubscriptionData* data);

    EventSubscriptionData* m_Data;
};

struct EventBusData;

class CB_API EventBus
{
public:
    //Creates an empty typed gameplay event bus.
    EventBus();

    //Invalidates subscriptions and releases queued gameplay events.
    ~EventBus();

    //Prevents copying a bus with live subscriptions and owned queues.
    EventBus(const EventBus&) = delete;

    //Prevents assigning a bus with live subscriptions and owned queues.
    EventBus& operator=(const EventBus&) = delete;

    //Subscribes a callable with unspecified ordering and next-publish mutation visibility.
    template<typename EventT, typename Handler>
    EventSubscription Subscribe(Handler&& handler)
    {
        std::function<void(const EventT&)> typedHandler(std::forward<Handler>(handler));
        return SubscribeInternal(
            std::type_index(typeid(EventT)),
            [callback = std::move(typedHandler)](const void* event)
            {
                callback(*static_cast<const EventT*>(event));
            });
    }

    //Synchronously notifies the starting subscriber set while honoring removals before invocation.
    template<typename EventT>
    void Publish(const EventT& event)
    {
        PublishInternal(std::type_index(typeid(EventT)), &event);
    }

    //Stores an owning gameplay event for the next deterministic dispatch cycle.
    template<typename EventT>
    void Enqueue(EventT event)
    {
        EnqueueInternal(
            std::type_index(typeid(EventT)),
            std::make_shared<EventT>(std::move(event)));
    }

    //Stores an owning worker result without invoking callbacks on the worker thread.
    template<typename EventT>
    void EnqueueFromWorker(EventT event)
    {
        EnqueueFromWorkerInternal(
            std::type_index(typeid(EventT)),
            std::make_shared<EventT>(std::move(event)));
    }

    //Delivers one deferred buffer while newly enqueued events wait for the next call.
    void DispatchQueued();

private:
    using ErasedCallback = std::function<void(const void*)>;
    using ErasedEvent = std::shared_ptr<const void>;

    //Adds a type-erased callback to a typed subscription channel.
    EventSubscription SubscribeInternal(std::type_index type, ErasedCallback callback);

    //Publishes a borrowed event to the matching typed channel.
    void PublishInternal(std::type_index type, const void* event);

    //Adds an owning event to the main-thread next queue.
    void EnqueueInternal(std::type_index type, ErasedEvent event);

    //Adds an owning event to the synchronized worker inbox.
    void EnqueueFromWorkerInternal(std::type_index type, ErasedEvent event);

    EventBusData* m_Data;
};
