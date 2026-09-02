#pragma once

#include "Cubit/Core.h"
#include "Cubit/Net/Transport.h"

#include <cstdint>
#include <deque>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

//How bad the network is. Latency and Jitter are ONE-WAY seconds.
//
//The --latency command-line flag is round-trip milliseconds and is halved on
//the way in. Two units for one idea invites exactly one bug, so the split is
//stated everywhere it appears.
struct NetworkSim
{
    double Latency = 0.0;
    double Jitter = 0.0;

    //Fraction of unreliable packets discarded. Reliable packets selected by
    //this are delayed instead, modelling retransmission.
    float Loss = 0.0f;

    std::uint64_t Seed = 1;
};

//Adds deterministic latency, jitter and loss to any Transport.
//
//This is the piece that makes netcode testable to this project's standard. With
//it, a doctest runs a server and two clients under 100 ms RTT and 5% loss in one
//process with no socket, identically every run. Without it, netcode gets
//"verified" by somebody saying it feels fine.
//
//It earns its keep twice: the same class wraps EnetTransport in the Sandbox, so
//`--latency 150` makes a localhost demo show real latency. Loopback has none of
//its own, so without this the demo would prove nothing.
//
//DELAY IS OUTBOUND ONLY. Both endpoints wrap themselves, so each direction is
//delayed exactly once. Delaying inbound as well would double every number.
class CB_API SimulatedTransport final : public Transport
{
public:
    //Keeps a reference to `inner`, which must outlive this.
    SimulatedTransport(Transport& inner, const NetworkSim& sim);

    void Send(PeerId peer, std::span<const std::uint8_t> data, Channel channel) override;
    void Broadcast(std::span<const std::uint8_t> data, Channel channel) override;
    void Disconnect(PeerId peer) override;
    bool Poll(NetEvent& out) override;
    void Advance(double seconds) override;
    double RoundTripTime(PeerId peer) const override;

private:
    //A packet waiting out its delay.
    struct Pending
    {
        double Due = 0.0;

        //Breaks ties in Due so ordering is total and therefore reproducible.
        //Without it two packets due on the same tick sort by whatever the sort
        //happens to do, and "deterministic" quietly stops being true.
        std::uint64_t Serial = 0;

        PeerId Peer = InvalidPeer;
        bool IsBroadcast = false;
        Channel Sent = Channel::Unreliable;
        std::vector<std::uint8_t> Data;
    };

    //Queues one packet, applying loss and jitter. `broadcast` chooses which of
    //the inner transport's two send paths it takes when it comes due.
    void Queue(PeerId peer, bool broadcast, std::span<const std::uint8_t> data, Channel channel);

    //xorshift64*, hand-rolled rather than <random>, because the standard
    //distributions are not required to draw identically across
    //implementations and this must be reproducible to be worth anything.
    std::uint64_t NextRandom();

    //A draw in [0, 1).
    double NextUnit();

    Transport& m_Inner;
    NetworkSim m_Sim;

    double m_Now = 0.0;
    std::uint64_t m_NextSerial = 0;
    std::uint64_t m_RandomState = 1;

    std::vector<Pending> m_Outbound;
    std::deque<NetEvent> m_Inbox;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
