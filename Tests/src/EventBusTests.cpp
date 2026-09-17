#include <doctest.h>

#include "Cubit/Events/EventBus.h"

#include <string>
#include <vector>

namespace
{
    struct Ping
    {
        int Value = 0;
    };

    struct Pong
    {
        int Value = 0;
    };
}

TEST_CASE("A subscribed callback hears every publish of its own event type")
{
    EventBus bus;

    std::vector<int> heard;
    const Subscription subscription = bus.Subscribe<Ping>(
        [&heard](const Ping& ping) { heard.push_back(ping.Value); });

    bus.Publish(Ping{ 1 });
    bus.Publish(Pong{ 99 });
    bus.Publish(Ping{ 2 });

    CHECK(heard == std::vector<int>{ 1, 2 });
}

TEST_CASE("A callback stops when its subscription is destroyed, and others carry on")
{
    EventBus bus;

    int first = 0;
    int second = 0;

    const Subscription keep = bus.Subscribe<Ping>([&second](const Ping&) { ++second; });

    {
        const Subscription temporary = bus.Subscribe<Ping>([&first](const Ping&) { ++first; });
        bus.Publish(Ping{});
        CHECK(bus.SubscriberCount<Ping>() == 2);
    }

    CHECK(bus.SubscriberCount<Ping>() == 1);

    bus.Publish(Ping{});

    //The one that went out of scope heard the first publish only.
    CHECK(first == 1);
    CHECK(second == 2);
}

TEST_CASE("Dropping a subscription by hand ends it, and a moved-from one ends nothing")
{
    EventBus bus;

    int heard = 0;
    Subscription subscription = bus.Subscribe<Ping>([&heard](const Ping&) { ++heard; });
    CHECK(subscription.Active());

    Subscription moved = std::move(subscription);
    CHECK_FALSE(subscription.Active());
    CHECK(moved.Active());

    //The move must not have unsubscribed on the way.
    bus.Publish(Ping{});
    CHECK(heard == 1);

    moved.Reset();
    CHECK_FALSE(moved.Active());

    bus.Publish(Ping{});
    CHECK(heard == 1);
    CHECK(bus.SubscriberCount<Ping>() == 0);
}

TEST_CASE("A callback that unsubscribes another stops it from hearing that same publish")
{
    //The menu case: a callback tears something down, and what it tore down must
    //not be called afterwards - including later in the publish already running.
    EventBus bus;

    std::vector<std::string> heard;
    Subscription second;

    const Subscription first = bus.Subscribe<Ping>(
        [&heard, &second](const Ping&)
        {
            heard.push_back("first");
            second.Reset();
        });

    second = bus.Subscribe<Ping>(
        [&heard](const Ping&) { heard.push_back("second"); });

    const Subscription third = bus.Subscribe<Ping>(
        [&heard](const Ping&) { heard.push_back("third"); });

    bus.Publish(Ping{});

    CHECK(heard == std::vector<std::string>{ "first", "third" });

    heard.clear();
    bus.Publish(Ping{});
    CHECK(heard == std::vector<std::string>{ "first", "third" });
}

TEST_CASE("A callback that unsubscribes itself is not called again")
{
    EventBus bus;

    int heard = 0;
    Subscription own;
    own = bus.Subscribe<Ping>(
        [&heard, &own](const Ping&)
        {
            ++heard;
            own.Reset();
        });

    bus.Publish(Ping{});
    bus.Publish(Ping{});

    CHECK(heard == 1);
    CHECK(bus.SubscriberCount<Ping>() == 0);
}

TEST_CASE("A callback subscribed during a publish first hears the next one")
{
    EventBus bus;

    int late = 0;
    Subscription lateSubscription;

    const Subscription starter = bus.Subscribe<Ping>(
        [&bus, &late, &lateSubscription](const Ping&)
        {
            if (!lateSubscription.Active())
                lateSubscription = bus.Subscribe<Ping>([&late](const Ping&) { ++late; });
        });

    bus.Publish(Ping{});
    CHECK(late == 0);

    bus.Publish(Ping{});
    CHECK(late == 1);
}

TEST_CASE("A callback may publish another event, and unsubscribing still waits for both")
{
    //A nested publish must not let the outer one erase entries it is still
    //walking: the sweep happens when the last publish finishes, not the first.
    EventBus bus;

    std::vector<std::string> heard;
    Subscription pongSubscription;

    const Subscription pingSubscription = bus.Subscribe<Ping>(
        [&bus, &heard, &pongSubscription](const Ping&)
        {
            heard.push_back("ping");
            bus.Publish(Pong{});
            pongSubscription.Reset();
        });

    pongSubscription = bus.Subscribe<Pong>(
        [&heard](const Pong&) { heard.push_back("pong"); });

    const Subscription after = bus.Subscribe<Ping>(
        [&heard](const Ping&) { heard.push_back("after"); });

    bus.Publish(Ping{});

    CHECK(heard == std::vector<std::string>{ "ping", "pong", "after" });
    CHECK(bus.SubscriberCount<Pong>() == 0);
    CHECK(bus.SubscriberCount<Ping>() == 2);
}

TEST_CASE("Publishing an event nobody subscribed to does nothing")
{
    EventBus bus;

    int heard = 0;
    const Subscription subscription = bus.Subscribe<Ping>([&heard](const Ping&) { ++heard; });

    bus.Publish(Pong{});

    CHECK(heard == 0);
    CHECK(bus.SubscriberCount<Pong>() == 0);
}
