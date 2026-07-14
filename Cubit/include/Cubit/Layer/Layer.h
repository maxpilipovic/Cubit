#pragma once

#include "Cubit/Events/Event.h"
#include "Cubit/Timestep.h"

class Layer
{
public:
    //Releases a layer through its base interface.
    virtual ~Layer() = default;

    //Notifies the layer after it joins a layer stack.
    virtual void OnAttach() {}

    //Notifies the layer before it leaves a layer stack.
    virtual void OnDetach() {}

    //Updates the layer once per frame.
    virtual void OnUpdate(Timestep timestep) { (void)timestep; }

    //Renders the layer once per frame after updates finish.
    virtual void OnRender() {}

    //Allows the layer to inspect or consume a routed platform event.
    virtual void OnEvent(Event& event) { (void)event; }
};
