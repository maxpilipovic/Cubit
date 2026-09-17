#include "cub.h"

#include "Cubit/Layer/LayerStack.h"

#include <algorithm>
#include <cstddef>

namespace
{
    struct LayerEntry
    {
        std::unique_ptr<Layer> Instance;

        //Set by Remove during a pass. A layer on its way out receives nothing
        //more, and is detached and destroyed when the pass ends.
        bool Removing = false;
    };

    struct PendingPush
    {
        std::unique_ptr<Layer> Instance;
        bool Overlay = false;
    };
}

struct LayerStackData
{
    std::vector<LayerEntry> Layers;

    //Where the next regular layer goes: overlays live above this.
    std::size_t LayerInsertIndex = 0;

    //Passes over the layers in flight, counted because a handler may cause
    //another - an event published from an update, say.
    int Passes = 0;

    std::vector<PendingPush> Pending;
};

namespace
{
    void Insert(LayerStackData& data, std::unique_ptr<Layer> layer, bool overlay)
    {
        Layer* instance = layer.get();

        if (overlay)
        {
            data.Layers.push_back(LayerEntry{ std::move(layer), false });
        }
        else
        {
            data.Layers.emplace(
                data.Layers.begin() + static_cast<std::ptrdiff_t>(data.LayerInsertIndex),
                LayerEntry{ std::move(layer), false });
            ++data.LayerInsertIndex;
        }

        instance->OnAttach();
    }

    //Detaches and drops every layer marked for removal, then admits everything
    //pushed while a pass was running. Only called with no pass in flight.
    void Settle(LayerStackData& data)
    {
        for (std::size_t i = data.Layers.size(); i-- > 0;)
        {
            if (!data.Layers[i].Removing)
                continue;

            data.Layers[i].Instance->OnDetach();
            data.Layers.erase(data.Layers.begin() + static_cast<std::ptrdiff_t>(i));

            if (i < data.LayerInsertIndex)
                --data.LayerInsertIndex;
        }

        //Moved out first: a pushed layer's OnAttach may push another, which would
        //otherwise grow the vector this loop is walking.
        std::vector<PendingPush> pending = std::move(data.Pending);
        data.Pending.clear();

        for (PendingPush& push : pending)
            Insert(data, std::move(push.Instance), push.Overlay);
    }

    //Counts one pass over the layers, and settles the stack when the last one
    //ends.
    class Pass
    {
    public:
        explicit Pass(LayerStackData& data)
            : m_Data(data)
        {
            ++m_Data.Passes;
        }

        ~Pass()
        {
            if (--m_Data.Passes == 0)
                Settle(m_Data);
        }

        Pass(const Pass&) = delete;
        Pass& operator=(const Pass&) = delete;

    private:
        LayerStackData& m_Data;
    };
}

LayerStack::LayerStack()
    : m_Data(new LayerStackData())
{
}

LayerStack::~LayerStack()
{
    for (auto iterator = m_Data->Layers.rbegin(); iterator != m_Data->Layers.rend(); ++iterator)
    {
        if (!iterator->Removing)
            iterator->Instance->OnDetach();
    }

    delete m_Data;
}

Layer* LayerStack::PushLayer(std::unique_ptr<Layer> layer)
{
    Layer* instance = layer.get();

    if (m_Data->Passes > 0)
    {
        m_Data->Pending.push_back(PendingPush{ std::move(layer), false });
        return instance;
    }

    Insert(*m_Data, std::move(layer), false);
    return instance;
}

Layer* LayerStack::PushOverlay(std::unique_ptr<Layer> overlay)
{
    Layer* instance = overlay.get();

    if (m_Data->Passes > 0)
    {
        m_Data->Pending.push_back(PendingPush{ std::move(overlay), true });
        return instance;
    }

    Insert(*m_Data, std::move(overlay), true);
    return instance;
}

bool LayerStack::Remove(Layer* layer)
{
    if (layer == nullptr)
        return false;

    for (std::size_t i = 0; i < m_Data->Layers.size(); ++i)
    {
        LayerEntry& entry = m_Data->Layers[i];
        if (entry.Instance.get() != layer || entry.Removing)
            continue;

        if (m_Data->Passes > 0)
        {
            entry.Removing = true;
            return true;
        }

        entry.Instance->OnDetach();
        m_Data->Layers.erase(m_Data->Layers.begin() + static_cast<std::ptrdiff_t>(i));

        if (i < m_Data->LayerInsertIndex)
            --m_Data->LayerInsertIndex;

        return true;
    }

    //Still waiting to join: it never attached, so it is dropped without a
    //detach rather than left to arrive after being removed.
    const auto pending = std::find_if(m_Data->Pending.begin(), m_Data->Pending.end(),
        [layer](const PendingPush& push) { return push.Instance.get() == layer; });

    if (pending != m_Data->Pending.end())
    {
        m_Data->Pending.erase(pending);
        return true;
    }

    return false;
}

std::size_t LayerStack::Count() const
{
    std::size_t count = m_Data->Pending.size();
    for (const LayerEntry& entry : m_Data->Layers)
        count += entry.Removing ? 0 : 1;

    return count;
}

void LayerStack::OnFixedUpdate(Timestep step)
{
    Pass pass(*m_Data);

    //By index and against the count this pass started with: a handler may push,
    //which can move this vector, and anything pushed joins after the pass.
    const std::size_t count = m_Data->Layers.size();
    for (std::size_t i = 0; i < count; ++i)
    {
        if (!m_Data->Layers[i].Removing)
            m_Data->Layers[i].Instance->OnFixedUpdate(step);
    }
}

void LayerStack::OnFrameUpdate(Timestep delta)
{
    Pass pass(*m_Data);

    const std::size_t count = m_Data->Layers.size();
    for (std::size_t i = 0; i < count; ++i)
    {
        if (!m_Data->Layers[i].Removing)
            m_Data->Layers[i].Instance->OnFrameUpdate(delta);
    }
}

void LayerStack::OnRender(float alpha)
{
    Pass pass(*m_Data);

    const std::size_t count = m_Data->Layers.size();
    for (std::size_t i = 0; i < count; ++i)
    {
        if (!m_Data->Layers[i].Removing)
            m_Data->Layers[i].Instance->OnRender(alpha);
    }
}

void LayerStack::OnEvent(Event& event)
{
    Pass pass(*m_Data);

    const std::size_t count = m_Data->Layers.size();
    for (std::size_t i = count; i-- > 0;)
    {
        if (m_Data->Layers[i].Removing)
            continue;

        m_Data->Layers[i].Instance->OnEvent(event);

        if (event.Handled)
            break;
    }
}
