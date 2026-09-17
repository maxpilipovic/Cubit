#include <doctest.h>

#include "Cubit/Events/EventBus.h"
#include "Cubit/FrameClock.h"
#include "Cubit/Layer/LayerStack.h"

#include <functional>
#include <memory>

namespace
{
    //Counts what the stack forwarded, so a fan-out can be asserted without a
    //window or a GL context.
    class RecordingLayer final : public Layer
    {
    public:
        int FixedUpdates = 0;
        int FrameUpdates = 0;
        float LastAlpha = -1.0f;

        void OnFixedUpdate(Timestep step) override
        {
            (void)step;
            ++FixedUpdates;
        }

        void OnFrameUpdate(Timestep delta) override
        {
            (void)delta;
            ++FrameUpdates;
        }

        void OnRender(float alpha) override { LastAlpha = alpha; }
    };

    //What a layer was told, kept OUTSIDE the layer. Remove destroys the layer,
    //so counters held inside one cannot be read afterwards: doing that was a
    //use-after-free that made three of the tests below fail before they were
    //written this way.
    struct Calls
    {
        int Attaches = 0;
        int Detaches = 0;
        int FixedUpdates = 0;
        int FrameUpdates = 0;
        int Renders = 0;
        int Events = 0;
    };

    //Records into a Calls that outlives it, and can act on the first call of a
    //kind - which is what a layer removing itself mid-pass needs.
    class ScriptedLayer final : public Layer
    {
    public:
        explicit ScriptedLayer(Calls& calls)
            : m_Calls(calls)
        {
        }

        std::function<void()> OnFirstFixedUpdate;
        std::function<void()> OnFirstEvent;

        void OnAttach() override { ++m_Calls.Attaches; }
        void OnDetach() override { ++m_Calls.Detaches; }

        void OnFixedUpdate(Timestep) override
        {
            ++m_Calls.FixedUpdates;
            if (m_Calls.FixedUpdates == 1 && OnFirstFixedUpdate)
                OnFirstFixedUpdate();
        }

        void OnFrameUpdate(Timestep) override { ++m_Calls.FrameUpdates; }
        void OnRender(float) override { ++m_Calls.Renders; }

        void OnEvent(Event&) override
        {
            ++m_Calls.Events;
            if (m_Calls.Events == 1 && OnFirstEvent)
                OnFirstEvent();
        }

    private:
        Calls& m_Calls;
    };

    //An event to route, with nothing in it: the stack reads only its Handled
    //flag as it walks the layers.
    class TestEvent final : public Event
    {
    public:
        static constexpr EventType GetStaticType() { return EventType::WindowMoved; }
        EventType GetEventType() const override { return GetStaticType(); }
        std::string_view GetName() const override { return "TestEvent"; }
        EventCategory GetCategoryFlags() const override { return EventCategory::Application; }
    };

    ScriptedLayer* PushScripted(LayerStack& stack, Calls& calls, bool overlay = false)
    {
        auto layer = std::make_unique<ScriptedLayer>(calls);
        ScriptedLayer* instance = layer.get();

        if (overlay)
            stack.PushOverlay(std::move(layer));
        else
            stack.PushLayer(std::move(layer));

        return instance;
    }
}

TEST_CASE("Every layer receives every fixed step and one frame update")
{
    LayerStack stack;

    auto layer = std::make_unique<RecordingLayer>();
    auto overlay = std::make_unique<RecordingLayer>();
    RecordingLayer* layerPointer = layer.get();
    RecordingLayer* overlayPointer = overlay.get();

    stack.PushLayer(std::move(layer));
    stack.PushOverlay(std::move(overlay));

    stack.OnFixedUpdate(FrameClock::Step());
    stack.OnFixedUpdate(FrameClock::Step());
    stack.OnFrameUpdate(Timestep(0.033));

    // Two steps and one frame update reach both, not one each or two frames.
    CHECK(layerPointer->FixedUpdates == 2);
    CHECK(overlayPointer->FixedUpdates == 2);
    CHECK(layerPointer->FrameUpdates == 1);
    CHECK(overlayPointer->FrameUpdates == 1);
}

TEST_CASE("The interpolation alpha reaches every layer")
{
    LayerStack stack;

    auto layer = std::make_unique<RecordingLayer>();
    RecordingLayer* layerPointer = layer.get();
    stack.PushLayer(std::move(layer));

    stack.OnRender(0.25f);

    CHECK(layerPointer->LastAlpha == doctest::Approx(0.25f));
}

TEST_CASE("A removed layer is detached once and hears nothing more")
{
    LayerStack stack;

    Calls keptCalls;
    Calls goingCalls;
    PushScripted(stack, keptCalls);
    ScriptedLayer* going = PushScripted(stack, goingCalls);

    REQUIRE(keptCalls.Attaches == 1);
    REQUIRE(goingCalls.Attaches == 1);
    CHECK(stack.Count() == 2);

    CHECK(stack.Remove(going));
    CHECK(goingCalls.Detaches == 1);
    CHECK(stack.Count() == 1);

    stack.OnFixedUpdate(FrameClock::Step());
    stack.OnFrameUpdate(Timestep(0.016));
    stack.OnRender(0.5f);

    TestEvent event;
    stack.OnEvent(event);

    CHECK(keptCalls.FixedUpdates == 1);
    CHECK(keptCalls.FrameUpdates == 1);
    CHECK(keptCalls.Renders == 1);
    CHECK(keptCalls.Events == 1);

    //Nothing reached the removed layer, and it was detached exactly once.
    CHECK(goingCalls.FixedUpdates == 0);
    CHECK(goingCalls.Events == 0);
    CHECK(goingCalls.Detaches == 1);
}

TEST_CASE("Removing a layer the stack does not hold answers false")
{
    LayerStack stack;

    Calls calls;
    ScriptedLayer* layer = PushScripted(stack, calls);

    CHECK(stack.Remove(layer));

    //Twice over is the case that matters: the pointer is stale now.
    CHECK_FALSE(stack.Remove(layer));
    CHECK_FALSE(stack.Remove(nullptr));

    Calls strangerCalls;
    ScriptedLayer stranger(strangerCalls);
    CHECK_FALSE(stack.Remove(&stranger));
    CHECK(strangerCalls.Detaches == 0);
}

TEST_CASE("A layer that removes itself while handling an event stops there and is detached after")
{
    //The menu case: a click on "resume" takes the menu away. The layer must not
    //be destroyed with its own handler still on the stack.
    LayerStack stack;

    Calls belowCalls;
    Calls menuCalls;
    PushScripted(stack, belowCalls);
    ScriptedLayer* menu = PushScripted(stack, menuCalls, true);

    menu->OnFirstEvent = [&stack, menu] { stack.Remove(menu); };

    TestEvent first;
    stack.OnEvent(first);

    //It handled that event, the layer below still got it, and it is gone now.
    CHECK(menuCalls.Events == 1);
    CHECK(belowCalls.Events == 1);
    CHECK(menuCalls.Detaches == 1);
    CHECK(stack.Count() == 1);

    TestEvent second;
    stack.OnEvent(second);

    CHECK(belowCalls.Events == 2);
    CHECK(menuCalls.Events == 1);
}

TEST_CASE("A layer that removes itself during a fixed update is detached when the pass ends")
{
    LayerStack stack;

    Calls firstCalls;
    Calls secondCalls;
    ScriptedLayer* first = PushScripted(stack, firstCalls);
    PushScripted(stack, secondCalls);

    first->OnFirstFixedUpdate = [&stack, first] { stack.Remove(first); };

    stack.OnFixedUpdate(FrameClock::Step());

    //Removed from inside its own update: the layer after it still stepped.
    CHECK(firstCalls.FixedUpdates == 1);
    CHECK(secondCalls.FixedUpdates == 1);
    CHECK(firstCalls.Detaches == 1);

    stack.OnFixedUpdate(FrameClock::Step());
    CHECK(secondCalls.FixedUpdates == 2);
    CHECK(firstCalls.FixedUpdates == 1);
}

TEST_CASE("A layer removed during a pass hears nothing else in that pass")
{
    LayerStack stack;

    Calls doomedCalls;
    Calls removerCalls;
    ScriptedLayer* doomed = PushScripted(stack, doomedCalls);
    ScriptedLayer* remover = PushScripted(stack, removerCalls, true);

    //Events go newest first, so the overlay runs before the layer below it is
    //reached: without the skip, a layer already removed would still be called.
    remover->OnFirstEvent = [&stack, doomed] { stack.Remove(doomed); };

    TestEvent event;
    stack.OnEvent(event);

    CHECK(removerCalls.Events == 1);
    CHECK(doomedCalls.Events == 0);
    CHECK(doomedCalls.Detaches == 1);
}

TEST_CASE("A layer pushed during a pass joins once the pass is over")
{
    LayerStack stack;

    Calls openerCalls;
    Calls menuCalls;
    ScriptedLayer* opener = PushScripted(stack, openerCalls);

    opener->OnFirstEvent = [&stack, &menuCalls]
    {
        PushScripted(stack, menuCalls, true);

        //Not attached yet: it joins when the pass finishes.
        CHECK(menuCalls.Attaches == 0);
    };

    TestEvent event;
    stack.OnEvent(event);

    //Attached by the end of the pass, and never sent the event that made it.
    CHECK(menuCalls.Attaches == 1);
    CHECK(menuCalls.Events == 0);
    CHECK(stack.Count() == 2);

    stack.OnFixedUpdate(FrameClock::Step());
    CHECK(menuCalls.FixedUpdates == 1);
}

TEST_CASE("A layer pushed during a pass and removed before it ends never attaches")
{
    LayerStack stack;

    Calls openerCalls;
    Calls transientCalls;
    ScriptedLayer* opener = PushScripted(stack, openerCalls);

    opener->OnFirstEvent = [&stack, &transientCalls]
    {
        ScriptedLayer* transient = PushScripted(stack, transientCalls, true);
        CHECK(stack.Remove(transient));
    };

    TestEvent event;
    stack.OnEvent(event);

    CHECK(stack.Count() == 1);
    CHECK(transientCalls.Attaches == 0);
    CHECK(transientCalls.Detaches == 0);
}

TEST_CASE("Overlays stay above layers as they are added and removed")
{
    LayerStack stack;

    Calls overlayCalls;
    Calls firstCalls;
    Calls secondCalls;
    ScriptedLayer* overlay = PushScripted(stack, overlayCalls, true);
    ScriptedLayer* first = PushScripted(stack, firstCalls);
    PushScripted(stack, secondCalls);

    CHECK(stack.Remove(first));

    //The overlay still sees an event first: removing a layer must not shuffle a
    //layer above it.
    overlay->OnFirstEvent = [&secondCalls] { CHECK(secondCalls.Events == 0); };

    TestEvent event;
    stack.OnEvent(event);

    CHECK(overlayCalls.Events == 1);
    CHECK(secondCalls.Events == 1);
    CHECK(firstCalls.Events == 0);
}

namespace
{
    //A layer that listens on the gameplay bus the way a real one does: the
    //callback captures `this`, and the subscription is a member.
    class ListeningLayer final : public Layer
    {
    public:
        ListeningLayer(EventBus& bus, Calls& calls)
            : m_Calls(calls)
        {
            m_Subscription = bus.Subscribe<Tick>([this](const Tick&) { ++m_Calls.Events; });
        }

        struct Tick
        {
        };

    private:
        Calls& m_Calls;
        Subscription m_Subscription;
    };
}

TEST_CASE("A removed layer's subscription goes with it")
{
    //Why these two are one job. Before subscriptions could be ended, a layer's
    //callback outlived the layer, and the next publish called into freed memory.
    //Here the layer is destroyed while the bus is still alive and still being
    //published to.
    EventBus bus;
    LayerStack stack;

    Calls calls;
    Layer* listener = stack.PushLayer(std::make_unique<ListeningLayer>(bus, calls));

    bus.Publish(ListeningLayer::Tick{});
    CHECK(calls.Events == 1);
    CHECK(bus.SubscriberCount<ListeningLayer::Tick>() == 1);

    CHECK(stack.Remove(listener));

    bus.Publish(ListeningLayer::Tick{});

    CHECK(calls.Events == 1);
    CHECK(bus.SubscriberCount<ListeningLayer::Tick>() == 0);
}

TEST_CASE("The stack detaches what it still holds when it goes")
{
    Calls layerCalls;
    Calls overlayCalls;

    {
        LayerStack stack;
        PushScripted(stack, layerCalls);
        PushScripted(stack, overlayCalls, true);
    }

    CHECK(layerCalls.Detaches == 1);
    CHECK(overlayCalls.Detaches == 1);
}
