#include <doctest.h>

#include "Cubit/FrameClock.h"
#include "Cubit/Layer/LayerStack.h"

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

    //Two steps and one frame update reach both, not one each or two frames.
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
