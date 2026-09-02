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

    //Drains message events in Poll() order, keeping only their first payload
    //byte. Used where a test cares about delivery ORDER, not just a count.
    void DrainPayloads(Transport& transport, std::vector<std::uint8_t>& out)
    {
        NetEvent event;
        while (transport.Poll(event))
        {
            if (event.Type == NetEventType::Message)
                out.push_back(event.Data.at(0));
        }
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

TEST_CASE("Jitter never reorders Channel::Reliable packets")
{
    //ENet's reliable channel is sequenced: reliable packets to one peer
    //always arrive in send order, no matter how their individual timing
    //jitters. All 20 are sent in the same instant with jitter (4 ticks)
    //bigger than the latency itself (3 ticks), which would make an unclamped
    //schedule sort into something other than send order with overwhelming
    //probability - 1 in 20! if it were a uniformly random permutation.
    LoopbackNetwork network;
    PeerId peer = InvalidPeer;
    Transport& rawClient = network.AddClient(peer);

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    sim.Jitter = 4.0 * FrameClock::FixedStepSeconds;
    sim.Seed = 1;
    SimulatedTransport client(rawClient, sim);
    Drain(network.Server());
    Drain(client);

    constexpr int Count = 20;
    for (int i = 0; i < Count; ++i)
        client.Send(LoopbackNetwork::ServerPeer, Bytes(static_cast<std::uint8_t>(i)), Channel::Reliable);

    std::vector<std::uint8_t> arrival;
    for (int tick = 0; tick < 60; ++tick)
    {
        client.Advance(FrameClock::FixedStepSeconds);
        DrainPayloads(network.Server(), arrival);
    }

    std::vector<std::uint8_t> expected;
    for (int i = 0; i < Count; ++i)
        expected.push_back(static_cast<std::uint8_t>(i));

    CHECK(arrival == expected);
}

TEST_CASE("Two reliable packets due on the same tick arrive in send order")
{
    //With no jitter, both sends below compute the exact same raw due time,
    //so this specifically exercises the Serial tie-break rather than the
    //reliable-ordering clamp (which a single shared due time satisfies
    //trivially either way). Once reliable delivery is guaranteed ordered,
    //this is the case the tie-break exists for.
    LoopbackNetwork network;
    PeerId peer = InvalidPeer;
    Transport& rawClient = network.AddClient(peer);

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    SimulatedTransport client(rawClient, sim);
    Drain(network.Server());
    Drain(client);

    client.Send(LoopbackNetwork::ServerPeer, Bytes(1), Channel::Reliable);
    client.Send(LoopbackNetwork::ServerPeer, Bytes(2), Channel::Reliable);

    std::vector<std::uint8_t> arrival;
    for (int tick = 0; tick < 5; ++tick)
    {
        client.Advance(FrameClock::FixedStepSeconds);
        DrainPayloads(network.Server(), arrival);
    }

    CHECK(arrival == std::vector<std::uint8_t>{ 1, 2 });
}

TEST_CASE("The canonical network's delivery schedule is pinned")
{
    //A reference oracle, same pattern as SkyLight and MatchState: literal
    //values observed from a real run and then pinned, not derived from the
    //implementation under test. This is the one test in the suite that can
    //tell "deterministic" from "deterministically different" - Run(1)==Run(1)
    //in the test above only proves self-consistency and would not notice a
    //refactor that draws the RNG in a different order, since that would
    //still agree with itself. These literals would notice.
    //
    //Deliberately pinned: if the RNG, its draw order, or the ordering/loss
    //rules change on purpose, these numbers are EXPECTED to change too -
    //re-observe and re-pin rather than assume the test rotted.
    LoopbackNetwork network;
    PeerId peer = InvalidPeer;
    Transport& rawClient = network.AddClient(peer);

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    sim.Loss = 0.05f;
    sim.Seed = 1;
    SimulatedTransport client(rawClient, sim);
    Drain(network.Server());
    Drain(client);

    //10 packets, alternating channel, one per tick, distinct payloads so
    //delivery order is visible.
    constexpr int Count = 10;
    struct Arrival { int Tick; std::uint8_t Payload; };
    std::vector<Arrival> arrivals;

    for (int i = 0; i < Count; ++i)
    {
        const Channel channel = (i % 2 == 0) ? Channel::Reliable : Channel::Unreliable;
        client.Send(LoopbackNetwork::ServerPeer, Bytes(static_cast<std::uint8_t>(i)), channel);
        client.Advance(FrameClock::FixedStepSeconds);

        std::vector<std::uint8_t> payloads;
        DrainPayloads(network.Server(), payloads);
        for (std::uint8_t payload : payloads)
            arrivals.push_back(Arrival{ i, payload });
    }

    //Long enough for every retransmit to land too.
    for (int tick = Count; tick < 30; ++tick)
    {
        client.Advance(FrameClock::FixedStepSeconds);

        std::vector<std::uint8_t> payloads;
        DrainPayloads(network.Server(), payloads);
        for (std::uint8_t payload : payloads)
            arrivals.push_back(Arrival{ tick, payload });
    }

    REQUIRE(arrivals.size() == Count);

    const std::vector<int> expectedTicks{ 2, 3, 5, 6, 7, 7, 8, 9, 11, 12 };
    const std::vector<std::uint8_t> expectedPayloads{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };

    std::vector<int> actualTicks;
    std::vector<std::uint8_t> actualPayloads;
    for (const Arrival& arrival : arrivals)
    {
        actualTicks.push_back(arrival.Tick);
        actualPayloads.push_back(arrival.Payload);
    }

    CHECK(actualTicks == expectedTicks);
    CHECK(actualPayloads == expectedPayloads);
}
