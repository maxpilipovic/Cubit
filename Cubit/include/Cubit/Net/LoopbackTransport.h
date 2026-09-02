#pragma once

#include "Cubit/Core.h"
#include "Cubit/Net/Transport.h"

#include <deque>
#include <memory>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

class LoopbackNetwork;

//One endpoint of an in-process network. Created only by LoopbackNetwork.
class CB_API LoopbackTransport final : public Transport
{
public:
    LoopbackTransport(LoopbackNetwork& network, PeerId self);

    void Send(PeerId peer, std::span<const std::uint8_t> data, Channel channel) override;
    void Broadcast(std::span<const std::uint8_t> data, Channel channel) override;
    void Disconnect(PeerId peer) override;
    bool Poll(NetEvent& out) override;
    void Advance(double seconds) override;
    double RoundTripTime(PeerId peer) const override;

    //Queues an event for this endpoint to Poll. Called by the network.
    void Deliver(NetEvent event);

private:
    LoopbackNetwork& m_Network;

    //Who this endpoint is, from everyone else's point of view.
    PeerId m_Self = InvalidPeer;

    std::deque<NetEvent> m_Inbox;
};

//A server endpoint and any number of client endpoints, delivering to each other
//with no socket.
//
//Delivery is INSTANT and lossless, deliberately. Every network property -
//latency, jitter, loss - lives in SimulatedTransport, which wraps this. Keeping
//the two apart is what makes both testable: this is the wire with no behaviour,
//that is the behaviour with no wire.
class CB_API LoopbackNetwork
{
public:
    LoopbackNetwork();
    ~LoopbackNetwork();

    LoopbackNetwork(const LoopbackNetwork&) = delete;
    LoopbackNetwork& operator=(const LoopbackNetwork&) = delete;

    //The id every client uses to address the server.
    static constexpr PeerId ServerPeer = 1;

    Transport& Server();

    //Adds a client already connected to the server. Both sides see a Connected
    //event. `peer` receives the id the server will know this client by.
    Transport& AddClient(PeerId& peer);

    //Disconnects a client. Both sides see a Disconnected event, and nothing
    //further is delivered to it.
    void RemoveClient(PeerId peer);

    //Routes one message. Called by LoopbackTransport.
    void Route(PeerId from, PeerId to, std::span<const std::uint8_t> data);

    //Routes one message to every client. Called by the server's endpoint.
    void RouteBroadcast(std::span<const std::uint8_t> data);

private:
    //Returns the endpoint for an id, or nullptr once it has been removed.
    LoopbackTransport* Find(PeerId peer);

    std::unique_ptr<LoopbackTransport> m_Server;

    struct ClientSlot
    {
        PeerId Peer = InvalidPeer;
        std::unique_ptr<LoopbackTransport> Endpoint;
    };

    std::vector<ClientSlot> m_Clients;

    //Removed endpoints, kept alive here rather than destroyed. AddClient hands
    //callers a Transport& with no promise of when they stop using it - a test
    //still Polls a just-removed client to see its Disconnected event, and a
    //caller may hold the reference longer than that. Absent from m_Clients, so
    //Find() and RouteBroadcast() never reach a retired endpoint; the object
    //itself only goes away with the LoopbackNetwork.
    std::vector<std::unique_ptr<LoopbackTransport>> m_Retired;

    //Never reused, matching PlayerId's rule and for the same reason: a stale
    //message naming somebody who left must not be applied to whoever arrived
    //next. Starts past ServerPeer.
    PeerId m_NextPeer = ServerPeer + 1;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
