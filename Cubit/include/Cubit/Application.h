#pragma once

#include "Cubit/Core.h"
#include "Cubit/Timestep.h"

struct ApplicationData;

class CB_API Application
{
public:
    //Creates the engine application and its core systems.
    Application();

    //Releases all engine systems owned by the application.
    virtual ~Application();

    //Runs frame updates until the application window closes.
    void Run();

protected:
    //Updates client behavior for one frame.
    virtual void OnUpdate(Timestep timestep);

private:
    ApplicationData* m_Data;
};
