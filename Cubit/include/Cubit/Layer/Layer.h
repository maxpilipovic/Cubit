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

    //Advances the layer's simulation by one fixed step. Called zero or more
    //times per frame, always with the same duration, so simulation does not
    //depend on how long a frame took to draw.
    virtual void OnFixedUpdate(Timestep step) { (void)step; }

    //Updates the layer once per frame with the real elapsed time. For work that
    //is about the frame itself rather than about the world.
    virtual void OnFrameUpdate(Timestep delta) { (void)delta; }

    //Renders the layer once per frame after updates finish. `alpha` is how far
    //the frame falls between the last completed fixed step and the next, for
    //interpolating a rendered position.
    virtual void OnRender(float alpha) { (void)alpha; }

    //Allows the layer to inspect or consume a routed platform event.
    virtual void OnEvent(Event& event) { (void)event; }
};
