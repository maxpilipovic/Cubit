#pragma once

#include "Cubit/Core.h"
#include "Cubit/Events/ApplicationEvent.h"
#include "Cubit/Events/EventBus.h"
#include "Cubit/Layer/Layer.h"
#include "Cubit/Timestep.h"

#include <memory>

struct ApplicationData;
class Window;

class CB_API Application
{
public:
    //Creates the engine application and its core systems.
    Application();

    //Releases all engine systems owned by the application.
    virtual ~Application();

    //Runs frame updates until the application window closes.
    void Run();

    //Handles application events before routing unhandled events through layers.
    void OnEvent(Event& event);

    //Transfers ownership of a regular layer to the application.
    void PushLayer(std::unique_ptr<Layer> layer);

    //Transfers ownership of an overlay to the application.
    void PushOverlay(std::unique_ptr<Layer> overlay);

    //Returns the gameplay notification bus owned by this application.
    EventBus& GetEventBus();

    //Returns the window owned by this application, for callers that need its
    //size or native handle.
    Window& GetWindow();

protected:
    //Updates client behavior for one frame.
    virtual void OnUpdate(Timestep timestep);

    //Renders client behavior for one frame.
    virtual void OnRender();

private:
    //Stops the application after a routed window-close request.
    bool OnWindowClose(WindowCloseEvent& event);

    //Allows application resize bookkeeping before layers receive the event.
    bool OnWindowResize(WindowResizeEvent& event);

    //Updates the renderer viewport when framebuffer pixels change.
    bool OnFramebufferResize(FramebufferResizeEvent& event);

    ApplicationData* m_Data;
};
