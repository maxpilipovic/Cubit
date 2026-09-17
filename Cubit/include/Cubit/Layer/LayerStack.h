#pragma once

#include "Cubit/Core.h"
#include "Cubit/Layer/Layer.h"

#include <cstddef>
#include <memory>

struct LayerStackData;

class CB_API LayerStack
{
public:
    //Creates an empty owned layer stack.
    LayerStack();

    //Detaches and releases all owned layers.
    ~LayerStack();

    //Prevents copying a stack that owns layer instances.
    LayerStack(const LayerStack&) = delete;

    //Prevents assigning a stack that owns layer instances.
    LayerStack& operator=(const LayerStack&) = delete;

    //Adds an owned gameplay or engine layer below overlays, and hands back a
    //pointer to it for a later Remove. The stack keeps ownership; the pointer is
    //only valid until that layer is removed.
    //
    //Pushed during a pass over the layers - from an event handler or an update -
    //the layer joins once that pass finishes, and its OnAttach runs then. So a
    //menu opened from a click first updates on the next frame.
    Layer* PushLayer(std::unique_ptr<Layer> layer);

    //Adds an owned overlay above all regular layers. Deferred during a pass,
    //like PushLayer.
    Layer* PushOverlay(std::unique_ptr<Layer> overlay);

    //Detaches and destroys a layer this stack owns. False if it holds no such
    //layer, which includes one already removed.
    //
    //A layer may remove itself from inside its own handler. It stops receiving
    //anything immediately, and is destroyed once the pass that was running
    //finishes - never while a call into it is on the stack.
    bool Remove(Layer* layer);

    //How many layers the stack holds, counting ones waiting to join and not
    //ones waiting to go. For tests.
    std::size_t Count() const;

    //Advances regular layers and overlays by one fixed step, in forward order.
    void OnFixedUpdate(Timestep step);

    //Updates regular layers and overlays once for the frame, in forward order.
    void OnFrameUpdate(Timestep delta);

    //Renders regular layers and overlays in forward order.
    void OnRender(float alpha);

    //Routes an event from the newest overlay toward the base layers.
    void OnEvent(Event& event);

private:
    LayerStackData* m_Data;
};
