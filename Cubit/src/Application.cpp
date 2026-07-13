#include "cub.h"

#include "Cubit/Application.h"
#include "Cubit/Layer/LayerStack.h"
#include "Cubit/Logger.h"
#include "Cubit/Window.h"
#include "Core/CoreLogger.h"

#include <chrono>

struct ApplicationData
{
    std::unique_ptr<Window> WindowInstance;
    LayerStack Layers;
    bool Running = true;
};

Application::Application()
    : m_Data(nullptr)
{
    CoreLogger::Init();
    Logger::Init();

    try
    {
        m_Data = new ApplicationData{ Window::Create() };
        m_Data->WindowInstance->SetEventCallback(
            [this](Event& event)
            {
                OnEvent(event);
            });
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

    while (m_Data->Running && !m_Data->WindowInstance->ShouldClose())
    {
        const auto currentFrameTime = Clock::now();
        const std::chrono::duration<double> frameDuration = currentFrameTime - lastFrameTime;
        lastFrameTime = currentFrameTime;

        const Timestep timestep(frameDuration.count());

        m_Data->Layers.OnUpdate(timestep);
        OnUpdate(timestep);
        m_Data->WindowInstance->OnUpdate();
    }
}

void Application::OnEvent(Event& event)
{
    EventDispatcher dispatcher(event);
    dispatcher.Dispatch<WindowCloseEvent>(
        [this](WindowCloseEvent& closeEvent)
        {
            return OnWindowClose(closeEvent);
        });
    dispatcher.Dispatch<WindowResizeEvent>(
        [this](WindowResizeEvent& resizeEvent)
        {
            return OnWindowResize(resizeEvent);
        });

    if (!event.Handled)
        m_Data->Layers.OnEvent(event);
}

void Application::PushLayer(std::unique_ptr<Layer> layer)
{
    m_Data->Layers.PushLayer(std::move(layer));
}

void Application::PushOverlay(std::unique_ptr<Layer> overlay)
{
    m_Data->Layers.PushOverlay(std::move(overlay));
}

void Application::OnUpdate(Timestep timestep)
{
    (void)timestep;
}

bool Application::OnWindowClose(WindowCloseEvent& event)
{
    (void)event;
    m_Data->Running = false;
    return true;
}

bool Application::OnWindowResize(WindowResizeEvent& event)
{
    (void)event;
    return false;
}
