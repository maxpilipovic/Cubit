#include "cub.h"

#include "Cubit/Net/LoopbackTransport.h"

#include <algorithm>

LoopbackTransport::LoopbackTransport(LoopbackNetwork& network, PeerId self)
    : m_Network(network), m_Self(self)
{
}

void LoopbackTransport::Send(PeerId peer, std::span<const std::uint8_t> data, Channel channel)
{
    //Loopback loses nothing, so the two channels behave identically here. The
    //distinction is real in EnetTransport and modelled in SimulatedTransport;
    //pretending it mattered at this layer would be theatre.
    (void)channel;
    m_Network.Route(m_Self, peer, data);
}

void LoopbackTransport::Broadcast(std::span<const std::uint8_t> data, Channel channel)
{
    (void)channel;
    m_Network.RouteBroadcast(data);
}

bool LoopbackTransport::Poll(NetEvent& out)
{
    if (m_Inbox.empty())
        return false;

    out = std::move(m_Inbox.front());
    m_Inbox.pop_front();
    return true;
}

void LoopbackTransport::Disconnect(PeerId peer)
{
    //A client addresses the server, so "disconnect the server" means "remove
    //me". A server names the client it is ejecting.
    m_Network.RemoveClient(peer == LoopbackNetwork::ServerPeer ? m_Self : peer);
}

void LoopbackTransport::Advance(double seconds)
{
    //Nothing to service: delivery already happened inside Send.
    (void)seconds;
}

double LoopbackTransport::RoundTripTime(PeerId peer) const
{
    (void)peer;
    return 0.0;
}

void LoopbackTransport::Deliver(NetEvent event)
{
    m_Inbox.push_back(std::move(event));
}

LoopbackNetwork::LoopbackNetwork()
    : m_Server(std::make_unique<LoopbackTransport>(*this, ServerPeer))
{
}

LoopbackNetwork::~LoopbackNetwork() = default;

Transport& LoopbackNetwork::Server()
{
    return *m_Server;
}

Transport& LoopbackNetwork::AddClient(PeerId& peer)
{
    const PeerId assigned = m_NextPeer++;

    ClientSlot slot;
    slot.Peer = assigned;
    slot.Endpoint = std::make_unique<LoopbackTransport>(*this, assigned);
    LoopbackTransport& endpoint = *slot.Endpoint;
    m_Clients.push_back(std::move(slot));

    NetEvent toServer;
    toServer.Type = NetEventType::Connected;
    toServer.Peer = assigned;
    m_Server->Deliver(std::move(toServer));

    NetEvent toClient;
    toClient.Type = NetEventType::Connected;
    toClient.Peer = ServerPeer;
    endpoint.Deliver(std::move(toClient));

    peer = assigned;
    return endpoint;
}

void LoopbackNetwork::RemoveClient(PeerId peer)
{
    const auto slot = std::find_if(m_Clients.begin(), m_Clients.end(),
        [peer](const ClientSlot& candidate) { return candidate.Peer == peer; });

    if (slot == m_Clients.end())
        return;

    NetEvent toClient;
    toClient.Type = NetEventType::Disconnected;
    toClient.Peer = ServerPeer;
    slot->Endpoint->Deliver(std::move(toClient));

    //Moved out, not destroyed: a caller may still hold this endpoint's
    //Transport& and Poll it for the Disconnected event just queued above.
    //Removed from m_Clients before the server is told, so a handler that
    //reacts by broadcasting cannot reach the endpoint that just left.
    m_Retired.push_back(std::move(slot->Endpoint));
    m_Clients.erase(slot);

    NetEvent toServer;
    toServer.Type = NetEventType::Disconnected;
    toServer.Peer = peer;
    m_Server->Deliver(std::move(toServer));
}

void LoopbackNetwork::Route(PeerId from, PeerId to, std::span<const std::uint8_t> data)
{
    LoopbackTransport* target = Find(to);
    if (target == nullptr)
        return;

    NetEvent event;
    event.Type = NetEventType::Message;
    event.Peer = from;
    event.Data.assign(data.begin(), data.end());
    target->Deliver(std::move(event));
}

void LoopbackNetwork::RouteBroadcast(std::span<const std::uint8_t> data)
{
    for (const ClientSlot& slot : m_Clients)
    {
        NetEvent event;
        event.Type = NetEventType::Message;
        event.Peer = ServerPeer;
        event.Data.assign(data.begin(), data.end());
        slot.Endpoint->Deliver(std::move(event));
    }
}

LoopbackTransport* LoopbackNetwork::Find(PeerId peer)
{
    if (peer == ServerPeer)
        return m_Server.get();

    for (const ClientSlot& slot : m_Clients)
    {
        if (slot.Peer == peer)
            return slot.Endpoint.get();
    }

    return nullptr;
}
