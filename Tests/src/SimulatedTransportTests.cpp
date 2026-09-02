#include <doctest.h>

#include "Cubit/FrameClock.h"
#include "Cubit/Net/LoopbackTransport.h"
#include "Cubit/Net/SimulatedTransport.h"

#include <cstdint>
#include <vector>

namespace
{
    //50 ms one-way at 60 Hz is exactly 3 ticks. Every latency in the suite is a
    //whole tick multiple so assertions can be exact rather than approximate.
    constexpr double OneWayLatency = 3.0 * FrameClock::FixedStepSeconds;

    std::vector<std::uint8_t> Bytes(std::uint8_t value)
    {
        return std::vector<std::uint8_t>{ value };
    }

    int Drain(Transport& transport)
    {
        int count = 0;
        NetEvent event;
        while (transport.Poll(event))
        {
            if (event.Type == NetEventType::Message)
                ++count;
        }
        return count;
    }
}

TEST_CASE("A packet arrives after the one-way latency, not before")
{
    LoopbackNetwork network;
    PeerId peer = InvalidPeer;
    Transport& rawClient = network.AddClient(peer);

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    SimulatedTransport client(rawClient, sim);
    Drain(network.Server());
    Drain(client);

    client.Send(LoopbackNetwork::ServerPeer, Bytes(1), Channel::Reliable);

    //Two ticks is not enough.
    client.Advance(FrameClock::FixedStepSeconds);
    CHECK(Drain(network.Server()) == 0);
    client.Advance(FrameClock::FixedStepSeconds);
    CHECK(Drain(network.Server()) == 0);

    //The third delivers it.
    client.Advance(FrameClock::FixedStepSeconds);
    CHECK(Drain(network.Server()) == 1);
}

TEST_CASE("Loss drops unreliable packets and never drops reliable ones")
{
    LoopbackNetwork network;
    PeerId peer = InvalidPeer;
    Transport& rawClient = network.AddClient(peer);

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    sim.Loss = 0.5f;
    SimulatedTransport client(rawClient, sim);
    Drain(network.Server());
    Drain(client);

    for (int i = 0; i < 200; ++i)
        client.Send(LoopbackNetwork::ServerPeer, Bytes(0), Channel::Unreliable);
    for (int i = 0; i < 200; ++i)
        client.Send(LoopbackNetwork::ServerPeer, Bytes(1), Channel::Reliable);

    //Long enough for every retransmit to land too.
    for (int i = 0; i < 60; ++i)
        client.Advance(FrameClock::FixedStepSeconds);

    const int arrived = Drain(network.Server());

    //200 reliable always, plus roughly half the unreliable. The band is wide
    //because this asserts the mechanism, not the RNG's exact draw.
    CHECK(arrived >= 200 + 60);
    CHECK(arrived <= 200 + 140);
}

TEST_CASE("A reliable packet selected for loss arrives late rather than never")
{
    //Modelling reliability as never-delayed would make every test lie about
    //the one property the reliable channel actually costs.
    LoopbackNetwork network;
    PeerId peer = InvalidPeer;
    Transport& rawClient = network.AddClient(peer);

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    sim.Loss = 1.0f;
    SimulatedTransport client(rawClient, sim);
    Drain(network.Server());
    Drain(client);

    client.Send(LoopbackNetwork::ServerPeer, Bytes(1), Channel::Unreliable);
    client.Send(LoopbackNetwork::ServerPeer, Bytes(2), Channel::Reliable);

    for (int i = 0; i < 3; ++i)
        client.Advance(FrameClock::FixedStepSeconds);

    //Neither has arrived: the unreliable one never will, and the reliable one
    //is still waiting out its retransmit.
    CHECK(Drain(network.Server()) == 0);

    for (int i = 0; i < 30; ++i)
        client.Advance(FrameClock::FixedStepSeconds);

    CHECK(Drain(network.Server()) == 1);
}

TEST_CASE("The same seed produces the same delivery schedule")
{
    //Everything else in this suite is worthless without this. A network that
    //differs run to run turns every downstream failure into a coin flip.
    const auto Run = [](std::uint64_t seed)
    {
        LoopbackNetwork network;
        PeerId peer = InvalidPeer;
        Transport& rawClient = network.AddClient(peer);

        NetworkSim sim;
        sim.Latency = OneWayLatency;
        sim.Jitter = FrameClock::FixedStepSeconds;
        sim.Loss = 0.3f;
        sim.Seed = seed;
        SimulatedTransport client(rawClient, sim);
        Drain(network.Server());
        Drain(client);

        std::vector<int> arrivals;
        for (int tick = 0; tick < 300; ++tick)
        {
            client.Send(LoopbackNetwork::ServerPeer, Bytes(1), Channel::Unreliable);
            client.Advance(FrameClock::FixedStepSeconds);
            arrivals.push_back(Drain(network.Server()));
        }
        return arrivals;
    };

    CHECK(Run(1) == Run(1));
    CHECK(Run(1) != Run(2));
}

TEST_CASE("Round-trip time is twice the one-way latency")
{
    LoopbackNetwork network;
    PeerId peer = InvalidPeer;
    Transport& rawClient = network.AddClient(peer);

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    SimulatedTransport client(rawClient, sim);

    CHECK(client.RoundTripTime(LoopbackNetwork::ServerPeer)
        == doctest::Approx(2.0 * OneWayLatency));
}
