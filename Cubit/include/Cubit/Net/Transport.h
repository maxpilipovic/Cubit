#pragma once

#include <cstdint>
#include <span>
#include <vector>

//Identifies the other end of a connection, for the machine holding this
//transport. A server's peer ids name its clients; a client has exactly one,
//naming the server. Not a PlayerId: a peer exists from the moment a socket
//connects, before anybody has joined a match.
using PeerId = std::uint32_t;
constexpr PeerId InvalidPeer = 0;

//Two, and no more.
//
//Unreliable carries snapshots: a stale snapshot is worthless, so resending one
//spends bandwidth delivering something already superseded. Reliable carries
//joins and edits: an edit that arrives late is still correct, but one that
//vanishes is a permanent world desync.
enum class Channel : std::uint8_t
{
    Unreliable = 0,
    Reliable = 1
};

enum class NetEventType
{
    None,
    Connected,
    Disconnected,
    Message
};

struct NetEvent
{
    NetEventType Type = NetEventType::None;
    PeerId Peer = InvalidPeer;
    std::vector<std::uint8_t> Data;
};

//Packet-level, mirroring ENet rather than inventing a vocabulary over it.
//
//The seam exists for TESTING, not portability. With it, a doctest runs a server
//and two clients under deterministic latency, jitter and loss with no socket
//and no flakiness. Without it, netcode gets verified by somebody saying it
//feels fine.
//
//No CB_API: a pure interface exports nothing, and an exported class must define
//every member it declares.
class Transport
{
public:
    virtual ~Transport() = default;

    virtual void Send(PeerId peer, std::span<const std::uint8_t> data, Channel channel) = 0;
    virtual void Broadcast(std::span<const std::uint8_t> data, Channel channel) = 0;

    //Ends a connection. Used to refuse a client at the handshake - a wrong
    //protocol version, or a map the client does not have - because a rejection
    //that merely goes quiet leaves the other end waiting on a Welcome that will
    //never come, which is the silent failure the loud one exists to prevent.
    virtual void Disconnect(PeerId peer) = 0;

    //Drains one event. False when there is nothing left this step.
    virtual bool Poll(NetEvent& out) = 0;

    //Services the transport for one step. The caller says how much time
    //passed rather than the transport reading a clock, which is what lets a
    //test run a hundred simulated seconds instantly and identically every run.
    virtual void Advance(double seconds) = 0;

    //Round-trip time in seconds, for the HUD. Zero when unknown.
    virtual double RoundTripTime(PeerId peer) const = 0;
};
