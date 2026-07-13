#pragma once

#include "Cubit/Core.h"
#include "Cubit/Timestep.h"

struct ApplicationData;

class CB_API Application
{
public:
    Application();
    virtual ~Application();

    void Run();

protected:
    virtual void OnUpdate(Timestep timestep);

private:
    ApplicationData* m_Data;
};
