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

TEST_CASE("A same-tick batch of reliable packets arrives in send order")
{
    //Proves a same-tick batch of reliable packets is delivered in send
    //order - exactly what a server broadcasting to every peer, or a burst of
    //terrain edits, actually produces, so this is a real shape rather than a
    //contrived one. With no jitter, every send below computes the same raw
    //due time, so this is delivery order guaranteed purely by Queue()'s
    //Channel::Reliable ordering rule (see SimulatedTransport.h's comment on
    //Pending::Serial for why that guarantee does not depend on which
    //std::sort implementation happens to be running it).
    //
    //Note on what this test does NOT prove: on MSVC today, this passes even
    //with the Serial tie-break deleted, because MSVC's std::sort happens not
    //to permute an already-ordered, all-equal-key range at this size. So
    //this test does not falsify Serial's removal - it asserts the guarantee
    //holds, not which mechanism is holding it. Do not read a pass here as
    //grounds to remove Serial.
    LoopbackNetwork network;
    PeerId peer = InvalidPeer;
    Transport& rawClient = network.AddClient(peer);

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    SimulatedTransport client(rawClient, sim);
    Drain(network.Server());
    Drain(client);

    constexpr int Count = 200;
    for (int i = 0; i < Count; ++i)
        client.Send(LoopbackNetwork::ServerPeer, Bytes(static_cast<std::uint8_t>(i % 256)), Channel::Reliable);

    std::vector<std::uint8_t> arrival;
    for (int tick = 0; tick < 10; ++tick)
    {
        client.Advance(FrameClock::FixedStepSeconds);
        DrainPayloads(network.Server(), arrival);
    }

    std::vector<std::uint8_t> expected;
    for (int i = 0; i < Count; ++i)
        expected.push_back(static_cast<std::uint8_t>(i % 256));

    CHECK(arrival == expected);
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

TEST_CASE("Broadcast draws loss once per call, so all recipients share its fate - a known divergence from ENet")
{
    //Documented limitation, not a desired property. Queue(InvalidPeer, true,
    //...) makes exactly one loss draw and one jitter draw for the whole
    //Broadcast() call, then - if not lost - delivers through a single
    //m_Inner.Broadcast(...), so every recipient of one broadcast either gets
    //it or none of them do. Real ENet does not work this way: a broadcast is
    //a send to each peer, and each peer's copy is lost independently. This
    //test pins the ACTUAL behaviour (shared fate) rather than the desired
    //one, so that if Broadcast is ever changed to draw per recipient, this
    //goes red and whoever changed it lands on this comment. A caller that
    //needs independent per-client loss - a server broadcasting snapshots
    //every tick, say - must send per peer with Send(), not Broadcast().
    LoopbackNetwork network;
    PeerId peerA = InvalidPeer;
    PeerId peerB = InvalidPeer;
    Transport& clientA = network.AddClient(peerA);
    Transport& clientB = network.AddClient(peerB);
    Drain(network.Server());
    Drain(clientA);
    Drain(clientB);

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    sim.Loss = 0.5f;
    sim.Seed = 1;
    SimulatedTransport server(network.Server(), sim);

    constexpr int Count = 100;
    for (int i = 0; i < Count; ++i)
    {
        server.Broadcast(Bytes(static_cast<std::uint8_t>(i)), Channel::Unreliable);
        server.Advance(FrameClock::FixedStepSeconds);
    }
    for (int tick = 0; tick < 10; ++tick)
        server.Advance(FrameClock::FixedStepSeconds);

    std::vector<std::uint8_t> receivedA;
    std::vector<std::uint8_t> receivedB;
    DrainPayloads(clientA, receivedA);
    DrainPayloads(clientB, receivedB);

    //Shared fate: whichever broadcasts got through, both clients received
    //exactly the same set.
    CHECK(receivedA == receivedB);

    //Not a vacuous agreement between two empty (or two full) vectors - loss
    //genuinely happened, and genuinely didn't happen every time.
    CHECK(receivedA.size() > 0);
    CHECK(receivedA.size() < Count);
}
