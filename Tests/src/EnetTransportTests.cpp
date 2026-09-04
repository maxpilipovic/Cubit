#include <doctest.h>

#include "Cubit/FrameClock.h"
#include "Cubit/Net/EnetTransport.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

TEST_CASE("A server and two clients talk over a real socket")
{
    //THE ONLY TEST IN THE SUITE THAT TOUCHES THE OS NETWORK STACK.
    //
    //It may trip a Windows firewall prompt the first time it runs. It earns
    //that: every other net test runs over LoopbackTransport, so without this
    //one nothing would prove EnetTransport is wired up correctly, and the
    //failure would surface as a Sandbox that silently never connects.
    //
    //Port 27960 is high, unregistered, and not one anything else here uses.
    constexpr std::uint16_t Port = 27960;

    std::unique_ptr<EnetTransport> server = EnetTransport::Listen(Port, 4);
    REQUIRE(server != nullptr);

    std::unique_ptr<EnetTransport> first = EnetTransport::Connect("127.0.0.1", Port);
    std::unique_ptr<EnetTransport> second = EnetTransport::Connect("127.0.0.1", Port);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);

    const auto Pump = [&](int steps)
    {
        for (int i = 0; i < steps; ++i)
        {
            server->Advance(FrameClock::FixedStepSeconds);
            first->Advance(FrameClock::FixedStepSeconds);
            second->Advance(FrameClock::FixedStepSeconds);
        }
    };

    //Bounded rather than open-ended: a hang here must fail the suite, not
    //stall the build for ever.
    std::vector<PeerId> connected;
    for (int attempt = 0; attempt < 600 && connected.size() < 2; ++attempt)
    {
        Pump(1);

        NetEvent event;
        while (server->Poll(event))
        {
            if (event.Type == NetEventType::Connected)
                connected.push_back(event.Peer);
        }
    }

    REQUIRE(connected.size() == 2);
    CHECK(connected[0] != connected[1]);
    CHECK(connected[0] != InvalidPeer);
    CHECK(connected[1] != InvalidPeer);

    //Drain each client's own Connected event.
    NetEvent ignored;
    while (first->Poll(ignored)) {}
    while (second->Poll(ignored)) {}

    const std::vector<std::uint8_t> payload{ 1, 2, 3, 4 };
    server->Broadcast(payload, Channel::Reliable);
    Pump(60);

    const auto ReceivedPayload = [&payload](EnetTransport& transport)
    {
        NetEvent event;
        while (transport.Poll(event))
        {
            if (event.Type == NetEventType::Message && event.Data == payload)
                return true;
        }
        return false;
    };

    CHECK(ReceivedPayload(*first));
    CHECK(ReceivedPayload(*second));

    //The other direction, and the other send path. Broadcast above exercises
    //enet_host_broadcast; nothing so far exercises Send, which is the one the
    //client uses for every input packet and the one that has to map a PeerId
    //back to an ENetPeer. A client addresses the server as EnetServerPeer.
    //
    //Both clients send, with different payloads, and the assertion is that the
    //two arrive attributed to DIFFERENT peer ids. Deliberately not "this
    //payload came from connected[0]": which client the server accepted first
    //is up to the OS, so pinning that would be a flake waiting to happen. What
    //matters is that the server can tell two senders apart at all - one that
    //could not would apply one player's input to another, which is the least
    //debuggable bug this class could produce.
    const std::vector<std::uint8_t> fromFirst{ 9, 8, 7 };
    const std::vector<std::uint8_t> fromSecond{ 6, 5, 4 };
    first->Send(EnetTransport::EnetServerPeer, fromFirst, Channel::Unreliable);
    second->Send(EnetTransport::EnetServerPeer, fromSecond, Channel::Unreliable);
    Pump(60);

    PeerId firstSender = InvalidPeer;
    PeerId secondSender = InvalidPeer;

    NetEvent event;
    while (server->Poll(event))
    {
        if (event.Type != NetEventType::Message)
            continue;

        if (event.Data == fromFirst)
            firstSender = event.Peer;
        else if (event.Data == fromSecond)
            secondSender = event.Peer;
    }

    REQUIRE(firstSender != InvalidPeer);
    REQUIRE(secondSender != InvalidPeer);
    CHECK(firstSender != secondSender);

    //And both are peers the server actually announced, not ids invented on
    //receipt.
    CHECK(std::find(connected.begin(), connected.end(), firstSender) != connected.end());
    CHECK(std::find(connected.begin(), connected.end(), secondSender) != connected.end());
}

TEST_CASE("A transport that closes tells the other end instead of leaving a ghost")
{
    //enet_host_destroy destroys the socket BEFORE it resets its peers, so on
    //its own it cannot say goodbye. Without the destructor's disconnect the
    //server keeps a departed client - and the player standing in the world -
    //until ENet's own timeout expires, which is five to thirty seconds. Long
    //enough to quit, come back, and meet yourself.
    //
    //This is the second and last test in the suite that touches the OS network
    //stack, and it earns that the same way the first does: the ghost is
    //invisible over LoopbackTransport, which has no lifetime of its own to get
    //wrong.
    constexpr std::uint16_t Port = 27961;

    std::unique_ptr<EnetTransport> server = EnetTransport::Listen(Port, 4);
    REQUIRE(server != nullptr);

    std::unique_ptr<EnetTransport> client = EnetTransport::Connect("127.0.0.1", Port);
    REQUIRE(client != nullptr);

    PeerId joined = InvalidPeer;
    for (int attempt = 0; attempt < 600 && joined == InvalidPeer; ++attempt)
    {
        server->Advance(FrameClock::FixedStepSeconds);
        client->Advance(FrameClock::FixedStepSeconds);

        NetEvent event;
        while (server->Poll(event))
        {
            if (event.Type == NetEventType::Connected)
                joined = event.Peer;
        }
    }

    REQUIRE(joined != InvalidPeer);

    //The whole point. Not Disconnect() - a client that quits does not get the
    //chance to be polite, it just stops existing.
    client.reset();

    //Bounded, and the bound is what makes the assertion mean anything. These
    //iterations are a tight loop, not a wall clock, so they run through in
    //milliseconds - far short of the timeout that would eventually notice a
    //silent departure. Only a goodbye actually sent can arrive inside this.
    bool departed = false;
    for (int attempt = 0; attempt < 600 && !departed; ++attempt)
    {
        server->Advance(FrameClock::FixedStepSeconds);

        NetEvent event;
        while (server->Poll(event))
        {
            if (event.Type == NetEventType::Disconnected && event.Peer == joined)
                departed = true;
        }
    }

    CHECK(departed);
}
