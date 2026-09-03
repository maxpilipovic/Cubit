#pragma once

#include "Cubit/Core.h"
#include "Cubit/Net/Transport.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

struct _ENetHost;
struct _ENetPeer;

//A Transport over real UDP sockets, via ENet.
//
//ENet provides exactly the two channel types this design needs, plus handshake,
//timeout, MTU discovery, sequencing, fragmentation and acknowledgement. Hand
//rolling those would be a project before the game part starts, and unlike this
//codebase's other hand-rolled pieces their bugs are non-deterministic and
//awkward to unit test.
//
//The ENet types are forward-declared so no consumer of this header acquires a
//dependency on <enet/enet.h>. That matters more than usual here: ENet's Win32
//header pulls in <winsock2.h>, which fights <windows.h> when it loses the race
//to be included first.
class CB_API EnetTransport final : public Transport
{
public:
    ~EnetTransport() override;

    EnetTransport(const EnetTransport&) = delete;
    EnetTransport& operator=(const EnetTransport&) = delete;

    //The id a client uses to address the server. Clients hold exactly one peer.
    static constexpr PeerId EnetServerPeer = 1;

    //Opens a listening host. Null when the port cannot be bound.
    static std::unique_ptr<EnetTransport> Listen(std::uint16_t port, std::size_t maxClients);

    //Opens a host and begins connecting. Null when the address is unusable.
    //The Connected event arrives from Poll once the handshake completes, which
    //takes several Advance calls.
    static std::unique_ptr<EnetTransport> Connect(const std::string& host, std::uint16_t port);

    void Send(PeerId peer, std::span<const std::uint8_t> data, Channel channel) override;
    void Broadcast(std::span<const std::uint8_t> data, Channel channel) override;
    void Disconnect(PeerId peer) override;
    bool Poll(NetEvent& out) override;

    //`seconds` is ignored, and that is the honest difference between this and
    //SimulatedTransport. ENet keeps its own wall clock for timeouts and
    //retransmission, so this cannot be driven faster or slower than real time.
    //Everything that needed deterministic time was proved without a socket.
    void Advance(double seconds) override;

    double RoundTripTime(PeerId peer) const override;

private:
    EnetTransport() = default;

    //Maps ENet's peer pointers to the stable ids this interface hands out.
    //Ids are never reused, matching PlayerId's rule and for the same reason: a
    //stale message naming somebody who left must not be applied to whoever
    //arrived next.
    struct PeerSlot
    {
        PeerId Id = InvalidPeer;
        _ENetPeer* Peer = nullptr;
    };

    PeerId IdFor(_ENetPeer* peer) const;
    _ENetPeer* PeerFor(PeerId id) const;

    _ENetHost* m_Host = nullptr;
    std::vector<PeerSlot> m_Peers;
    std::deque<NetEvent> m_Inbox;
    PeerId m_NextPeer = EnetServerPeer;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
