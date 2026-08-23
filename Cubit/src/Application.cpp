#include "cub.h"

#include "Cubit/Application.h"
#include "Cubit/FrameClock.h"
#include "Cubit/Input.h"
#include "Cubit/Layer/LayerStack.h"
#include "Cubit/Logger.h"
#include "Cubit/Renderer/DebugDraw.h"
#include "Cubit/Renderer/Renderer.h"
#include "Cubit/Window.h"
#include "Core/CoreLogger.h"

#include <chrono>

struct ApplicationData
{
    std::unique_ptr<Window> WindowInstance;
    EventBus GameplayEvents;
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
        Input::SetWindow(m_Data->WindowInstance.get());
        m_Data->WindowInstance->SetEventCallback(
            [this](Event& event)
            {
                OnEvent(event);
            });
        Renderer::Init();
        Renderer::SetViewport(
            0,
            0,
            m_Data->WindowInstance->GetFramebufferWidth(),
            m_Data->WindowInstance->GetFramebufferHeight());
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
    // Before delete m_Data, which destroys the window and with it the GL
    // context these resources belong to.
    DebugDraw::Shutdown();

    Input::SetWindow(nullptr);
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
    FrameClock frameClock;

    while (m_Data->Running)
    {
        m_Data->WindowInstance->PollEvents();

        if (!m_Data->Running || m_Data->WindowInstance->ShouldClose())
            break;

        const auto currentFrameTime = Clock::now();
        const std::chrono::duration<double> frameDuration = currentFrameTime - lastFrameTime;
        lastFrameTime = currentFrameTime;

        const Timestep timestep(frameDuration.count());

        // Every layer takes one step before any layer takes the next. Stepping a
        // layer to completion first would let a layer read another's state from
        // up to MaxTicksPerFrame steps in the future.
        constexpr Timestep step = FrameClock::Step();
        const int ticks = frameClock.Advance(frameDuration.count());
        for (int tick = 0; tick < ticks; ++tick)
        {
            m_Data->Layers.OnFixedUpdate(step);
            OnFixedUpdate(step);
        }

        // After the steps, so per-frame work observes their result.
        m_Data->Layers.OnFrameUpdate(timestep);
        OnFrameUpdate(timestep);

        const float alpha = frameClock.Alpha();

        Renderer::SetClearColor(0.08f, 0.10f, 0.15f, 1.0f);
        Renderer::Clear();
        m_Data->Layers.OnRender(alpha);
        OnRender(alpha);
        m_Data->WindowInstance->SwapBuffers();
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
    dispatcher.Dispatch<FramebufferResizeEvent>(
        [this](FramebufferResizeEvent& resizeEvent)
        {
            return OnFramebufferResize(resizeEvent);
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

EventBus& Application::GetEventBus()
{
    return m_Data->GameplayEvents;
}

Window& Application::GetWindow()
{
    return *m_Data->WindowInstance;
}

void Application::OnFixedUpdate(Timestep step)
{
    (void)step;
}

void Application::OnFrameUpdate(Timestep delta)
{
    (void)delta;
}

void Application::OnRender(float alpha)
{
    (void)alpha;
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

bool Application::OnFramebufferResize(FramebufferResizeEvent& event)
{
    Renderer::SetViewport(0, 0, event.GetWidth(), event.GetHeight());
    return false;
}
