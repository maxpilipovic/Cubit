#include "cub.h"

#include "Cubit/Net/EnetTransport.h"

#include "Cubit/Logger.h"

#include <enet/enet.h>

#include <algorithm>

namespace
{
    //Initialised once for the process and torn down at exit. ENet's own
    //counterpart to this is global state, so wrapping it in a static keeps the
    //ordering correct whether the first host is a server or a client.
    struct EnetLibrary
    {
        bool Ready = false;

        EnetLibrary() { Ready = enet_initialize() == 0; }
        ~EnetLibrary() { if (Ready) enet_deinitialize(); }
    };

    EnetLibrary& Library()
    {
        static EnetLibrary library;
        return library;
    }

    //Two channels, matching Channel. Unreliable snapshots are also unsequenced:
    //a snapshot that arrives after a newer one is worthless, and MatchClient
    //discards it by tick anyway.
    constexpr std::size_t ChannelCount = 2;

    enet_uint32 FlagsFor(Channel channel)
    {
        return channel == Channel::Reliable
            ? ENET_PACKET_FLAG_RELIABLE
            : ENET_PACKET_FLAG_UNSEQUENCED;
    }
}

std::unique_ptr<EnetTransport> EnetTransport::Listen(std::uint16_t port, std::size_t maxClients)
{
    if (!Library().Ready)
    {
        CB_ERROR("ENet failed to initialise");
        return nullptr;
    }

    ENetAddress address{};
    address.host = ENET_HOST_ANY;
    address.port = port;

    ENetHost* host = enet_host_create(&address, maxClients, ChannelCount, 0, 0);
    if (host == nullptr)
    {
        CB_ERROR("Could not bind the server port");
        return nullptr;
    }

    std::unique_ptr<EnetTransport> transport(new EnetTransport());
    transport->m_Host = host;
    return transport;
}

std::unique_ptr<EnetTransport> EnetTransport::Connect(const std::string& host, std::uint16_t port)
{
    if (!Library().Ready)
    {
        CB_ERROR("ENet failed to initialise");
        return nullptr;
    }

    //One outgoing connection, which is all a client ever has.
    ENetHost* client = enet_host_create(nullptr, 1, ChannelCount, 0, 0);
    if (client == nullptr)
    {
        CB_ERROR("Could not create the client host");
        return nullptr;
    }

    ENetAddress address{};
    address.port = port;
    if (enet_address_set_host(&address, host.c_str()) != 0)
    {
        CB_ERROR("Could not resolve the server address");
        enet_host_destroy(client);
        return nullptr;
    }

    ENetPeer* peer = enet_host_connect(client, &address, ChannelCount, 0);
    if (peer == nullptr)
    {
        CB_ERROR("No peer slot available for the connection");
        enet_host_destroy(client);
        return nullptr;
    }

    std::unique_ptr<EnetTransport> transport(new EnetTransport());
    transport->m_Host = client;

    //Registered now rather than on the Connected event, so Send has somewhere
    //to address before the handshake finishes. The Connected event still
    //arrives from Poll when ENet says so.
    transport->m_Peers.push_back(PeerSlot{ transport->m_NextPeer++, peer });
    return transport;
}

EnetTransport::~EnetTransport()
{
    if (m_Host == nullptr)
        return;

    //enet_host_destroy destroys the SOCKET first and only then resets each
    //peer, so it cannot tell anybody it is going - read in host.c rather than
    //assumed. Left to itself, a Sandbox that closes its window leaves a player
    //standing on the server until ENet's own timeout expires, which is five to
    //thirty seconds of a ghost that other players can see and walk through.
    //Quit and rejoin inside that window and there are two of you.
    //
    //disconnect_now queues an unsequenced DISCONNECT and flushes it inside the
    //call, so the packet is on the wire before the socket goes. It is the right
    //one of the two: a graceful enet_peer_disconnect would need the host
    //serviced for another round trip afterwards, and nothing services a host
    //that is being destroyed.
    for (const PeerSlot& slot : m_Peers)
    {
        if (slot.Peer != nullptr)
            enet_peer_disconnect_now(slot.Peer, 0);
    }

    enet_host_destroy(m_Host);
}

void EnetTransport::Send(PeerId peer, std::span<const std::uint8_t> data, Channel channel)
{
    ENetPeer* target = PeerFor(peer);
    if (target == nullptr)
        return;

    ENetPacket* packet = enet_packet_create(data.data(), data.size(), FlagsFor(channel));
    if (packet == nullptr)
        return;

    //enet_peer_send returns -1 WITHOUT taking ownership of the packet - when
    //the peer is not in the connected state, the channel is out of range, or
    //the payload exceeds the host's maximum. The first of those is on the
    //normal path rather than an error case, because Connect() deliberately
    //registers its peer before the handshake completes, so anything sent in
    //that window would leak without this. enet_host_broadcast needs no
    //equivalent: it destroys the packet itself when nobody took a reference.
    if (enet_peer_send(target, static_cast<enet_uint8>(channel), packet) < 0)
        enet_packet_destroy(packet);
}

void EnetTransport::Broadcast(std::span<const std::uint8_t> data, Channel channel)
{
    if (m_Host == nullptr)
        return;

    ENetPacket* packet = enet_packet_create(data.data(), data.size(), FlagsFor(channel));
    if (packet == nullptr)
        return;

    enet_host_broadcast(m_Host, static_cast<enet_uint8>(channel), packet);
}

void EnetTransport::Disconnect(PeerId peer)
{
    ENetPeer* target = PeerFor(peer);
    if (target == nullptr)
        return;

    //Immediate rather than graceful: this is used to refuse a handshake, and a
    //client being refused has nothing left worth flushing. It also generates no
    //local DISCONNECT event, which is why MatchServer calls its own
    //HandleDisconnected alongside this rather than waiting for one.
    enet_peer_disconnect_now(target, 0);

    const auto slot = std::find_if(m_Peers.begin(), m_Peers.end(),
        [peer](const PeerSlot& candidate) { return candidate.Id == peer; });

    if (slot != m_Peers.end())
        m_Peers.erase(slot);
}

bool EnetTransport::Poll(NetEvent& out)
{
    if (m_Inbox.empty())
        return false;

    out = std::move(m_Inbox.front());
    m_Inbox.pop_front();
    return true;
}

void EnetTransport::Advance(double seconds)
{
    (void)seconds;

    if (m_Host == nullptr)
        return;

    //Zero timeout: this is called once per fixed step from a loop that has
    //other work to do, so it must drain what is ready and return rather than
    //block waiting for traffic.
    ENetEvent event;
    while (enet_host_service(m_Host, &event, 0) > 0)
    {
        switch (event.type)
        {
        case ENET_EVENT_TYPE_CONNECT:
        {
            PeerId id = IdFor(event.peer);
            if (id == InvalidPeer)
            {
                id = m_NextPeer++;
                m_Peers.push_back(PeerSlot{ id, event.peer });
            }

            NetEvent connected;
            connected.Type = NetEventType::Connected;
            connected.Peer = id;
            m_Inbox.push_back(std::move(connected));
            break;
        }

        case ENET_EVENT_TYPE_DISCONNECT:
        {
            const PeerId id = IdFor(event.peer);

            NetEvent disconnected;
            disconnected.Type = NetEventType::Disconnected;
            disconnected.Peer = id;
            m_Inbox.push_back(std::move(disconnected));

            const auto slot = std::find_if(m_Peers.begin(), m_Peers.end(),
                [id](const PeerSlot& candidate) { return candidate.Id == id; });

            if (slot != m_Peers.end())
                m_Peers.erase(slot);
            break;
        }

        case ENET_EVENT_TYPE_RECEIVE:
        {
            NetEvent message;
            message.Type = NetEventType::Message;
            message.Peer = IdFor(event.peer);
            message.Data.assign(
                event.packet->data, event.packet->data + event.packet->dataLength);
            m_Inbox.push_back(std::move(message));

            //ENet hands over ownership of the packet with the event.
            enet_packet_destroy(event.packet);
            break;
        }

        case ENET_EVENT_TYPE_NONE:
            break;
        }
    }
}

double EnetTransport::RoundTripTime(PeerId peer) const
{
    const ENetPeer* target = PeerFor(peer);

    //ENet reports milliseconds; this interface promises seconds.
    return target == nullptr ? 0.0 : static_cast<double>(target->roundTripTime) / 1000.0;
}

PeerId EnetTransport::IdFor(_ENetPeer* peer) const
{
    for (const PeerSlot& slot : m_Peers)
    {
        if (slot.Peer == peer)
            return slot.Id;
    }

    return InvalidPeer;
}

_ENetPeer* EnetTransport::PeerFor(PeerId id) const
{
    for (const PeerSlot& slot : m_Peers)
    {
        if (slot.Id == id)
            return slot.Peer;
    }

    return nullptr;
}
