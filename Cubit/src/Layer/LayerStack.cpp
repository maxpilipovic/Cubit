#include "cub.h"

#include "Cubit/Layer/LayerStack.h"

#include <cstddef>

struct LayerStackData
{
    std::vector<std::unique_ptr<Layer>> Layers;
    std::size_t LayerInsertIndex = 0;
};

LayerStack::LayerStack()
    : m_Data(new LayerStackData())
{
}

LayerStack::~LayerStack()
{
    for (auto iterator = m_Data->Layers.rbegin(); iterator != m_Data->Layers.rend(); ++iterator)
        (*iterator)->OnDetach();

    delete m_Data;
}

void LayerStack::PushLayer(std::unique_ptr<Layer> layer)
{
    Layer* layerPointer = layer.get();
    m_Data->Layers.emplace(
        m_Data->Layers.begin() + static_cast<std::ptrdiff_t>(m_Data->LayerInsertIndex),
        std::move(layer));
    ++m_Data->LayerInsertIndex;
    layerPointer->OnAttach();
}

void LayerStack::PushOverlay(std::unique_ptr<Layer> overlay)
{
    Layer* overlayPointer = overlay.get();
    m_Data->Layers.emplace_back(std::move(overlay));
    overlayPointer->OnAttach();
}

void LayerStack::OnFixedUpdate(Timestep step)
{
    for (const auto& layer : m_Data->Layers)
        layer->OnFixedUpdate(step);
}

void LayerStack::OnFrameUpdate(Timestep delta)
{
    for (const auto& layer : m_Data->Layers)
        layer->OnFrameUpdate(delta);
}

void LayerStack::OnRender(float alpha)
{
    for (const auto& layer : m_Data->Layers)
        layer->OnRender(alpha);
}

void LayerStack::OnEvent(Event& event)
{
    for (auto iterator = m_Data->Layers.rbegin(); iterator != m_Data->Layers.rend(); ++iterator)
    {
        (*iterator)->OnEvent(event);

        if (event.Handled)
            break;
    }
}
