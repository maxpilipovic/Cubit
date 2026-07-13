#pragma once

#include "Cubit/Core.h"
#include "Cubit/Layer/Layer.h"

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

    //Adds an owned gameplay or engine layer below overlays.
    void PushLayer(std::unique_ptr<Layer> layer);

    //Adds an owned overlay above all regular layers.
    void PushOverlay(std::unique_ptr<Layer> overlay);

    //Updates regular layers and overlays in forward order.
    void OnUpdate(Timestep timestep);

    //Routes an event from the newest overlay toward the base layers.
    void OnEvent(Event& event);

private:
    LayerStackData* m_Data;
};
