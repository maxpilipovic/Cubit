#include "cub.h"

#include "Cubit/Application.h"
#include "Cubit/Logger.h"
#include "Cubit/Window.h"
#include "Core/CoreLogger.h"

#include <chrono>

struct ApplicationData
{
    std::unique_ptr<Window> WindowInstance;
};

Application::Application()
    : m_Data(nullptr)
{
    CoreLogger::Init();
    Logger::Init();

    try
    {
        m_Data = new ApplicationData{ Window::Create() };
        CB_CORE_INFO("Application created");
    }
    catch (...)
    {
        Logger::Shutdown();
        CoreLogger::Shutdown();
        throw;
    }
}

Application::~Application()
{
    delete m_Data;
    CB_CORE_INFO("Application destroyed");

    Logger::Shutdown();
    CoreLogger::Shutdown();
}

void Application::Run()
{
    CB_CORE_INFO("Engine running");

    using Clock = std::chrono::steady_clock;
    auto lastFrameTime = Clock::now();

    while (!m_Data->WindowInstance->ShouldClose())
    {
        const auto currentFrameTime = Clock::now();
        const std::chrono::duration<double> frameDuration = currentFrameTime - lastFrameTime;
        lastFrameTime = currentFrameTime;

        OnUpdate(Timestep(frameDuration.count()));
        m_Data->WindowInstance->OnUpdate();
    }
}

void Application::OnUpdate(Timestep timestep)
{
    (void)timestep;
}
