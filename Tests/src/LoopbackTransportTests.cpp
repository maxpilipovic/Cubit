#include <doctest.h>

#include "Cubit/Net/LoopbackTransport.h"

#include <cstdint>
#include <vector>

namespace
{
    std::vector<std::uint8_t> Bytes(std::initializer_list<std::uint8_t> values)
    {
        return std::vector<std::uint8_t>(values);
    }

    //Drains a transport into a list, which is what every test here wants and
    //what nothing in the interface gives you.
    std::vector<NetEvent> Drain(Transport& transport)
    {
        std::vector<NetEvent> events;
        NetEvent event;
        while (transport.Poll(event))
            events.push_back(event);
        return events;
    }
}

TEST_CASE("Adding a client connects both sides")
{
    LoopbackNetwork network;
    PeerId peer = InvalidPeer;
    Transport& client = network.AddClient(peer);

    CHECK(peer != InvalidPeer);

    const std::vector<NetEvent> serverEvents = Drain(network.Server());
    REQUIRE(serverEvents.size() == 1);
    CHECK(serverEvents[0].Type == NetEventType::Connected);
    CHECK(serverEvents[0].Peer == peer);

    const std::vector<NetEvent> clientEvents = Drain(client);
    REQUIRE(clientEvents.size() == 1);
    CHECK(clientEvents[0].Type == NetEventType::Connected);
    CHECK(clientEvents[0].Peer == LoopbackNetwork::ServerPeer);
}

TEST_CASE("A client's message reaches the server tagged with the sender")
{
    LoopbackNetwork network;
    PeerId peer = InvalidPeer;
    Transport& client = network.AddClient(peer);
    Drain(network.Server());
    Drain(client);

    client.Send(LoopbackNetwork::ServerPeer, Bytes({ 1, 2, 3 }), Channel::Reliable);

    const std::vector<NetEvent> events = Drain(network.Server());
    REQUIRE(events.size() == 1);
    CHECK(events[0].Type == NetEventType::Message);
    CHECK(events[0].Peer == peer);
    CHECK(events[0].Data == Bytes({ 1, 2, 3 }));
}

TEST_CASE("A broadcast reaches every client and nobody else")
{
    LoopbackNetwork network;
    PeerId first = InvalidPeer;
    PeerId second = InvalidPeer;
    Transport& clientA = network.AddClient(first);
    Transport& clientB = network.AddClient(second);
    Drain(network.Server());
    Drain(clientA);
    Drain(clientB);

    network.Server().Broadcast(Bytes({ 9 }), Channel::Unreliable);

    const std::vector<NetEvent> a = Drain(clientA);
    const std::vector<NetEvent> b = Drain(clientB);
    REQUIRE(a.size() == 1);
    REQUIRE(b.size() == 1);
    CHECK(a[0].Data == Bytes({ 9 }));
    CHECK(b[0].Data == Bytes({ 9 }));

    //And the server did not deliver its own broadcast to itself.
    CHECK(Drain(network.Server()).empty());
}

TEST_CASE("A targeted send reaches only its addressee")
{
    LoopbackNetwork network;
    PeerId first = InvalidPeer;
    PeerId second = InvalidPeer;
    Transport& clientA = network.AddClient(first);
    Transport& clientB = network.AddClient(second);
    Drain(network.Server());
    Drain(clientA);
    Drain(clientB);

    network.Server().Send(first, Bytes({ 4 }), Channel::Reliable);

    CHECK(Drain(clientA).size() == 1);
    CHECK(Drain(clientB).empty());
}

TEST_CASE("Removing a client tells both sides and stops delivery")
{
    LoopbackNetwork network;
    PeerId peer = InvalidPeer;
    Transport& client = network.AddClient(peer);
    Drain(network.Server());
    Drain(client);

    network.RemoveClient(peer);

    const std::vector<NetEvent> serverEvents = Drain(network.Server());
    REQUIRE(serverEvents.size() == 1);
    CHECK(serverEvents[0].Type == NetEventType::Disconnected);
    CHECK(serverEvents[0].Peer == peer);

    //The client sees its own Disconnected too - confirmed independently by
    //"Disconnecting from either end removes the client" below.
    const std::vector<NetEvent> clientEvents = Drain(client);
    REQUIRE(clientEvents.size() == 1);
    CHECK(clientEvents[0].Type == NetEventType::Disconnected);

    //A broadcast after the departure must not reach the departed.
    network.Server().Broadcast(Bytes({ 7 }), Channel::Unreliable);
    CHECK(Drain(client).empty());
}

TEST_CASE("Disconnecting from either end removes the client")
{
    //Both directions, because both are used: a server ejects a client that
    //fails the handshake, and a client drops a server whose map it does not
    //have.
    LoopbackNetwork network;

    PeerId fromServer = InvalidPeer;
    Transport& ejected = network.AddClient(fromServer);
    network.Server().Disconnect(fromServer);
    CHECK(Drain(ejected).size() == 2);  //Connected, then Disconnected

    PeerId fromClient = InvalidPeer;
    Transport& leaving = network.AddClient(fromClient);
    Drain(network.Server());
    leaving.Disconnect(LoopbackNetwork::ServerPeer);

    const std::vector<NetEvent> serverEvents = Drain(network.Server());
    REQUIRE(serverEvents.size() == 1);
    CHECK(serverEvents[0].Type == NetEventType::Disconnected);
    CHECK(serverEvents[0].Peer == fromClient);
}

TEST_CASE("Loopback has no latency of its own")
{
    //The whole reason latency lives in SimulatedTransport: a wire with no
    //properties is a wire whose tests never argue about timing.
    LoopbackNetwork network;
    PeerId peer = InvalidPeer;
    Transport& client = network.AddClient(peer);
    Drain(network.Server());
    Drain(client);

    client.Send(LoopbackNetwork::ServerPeer, Bytes({ 1 }), Channel::Reliable);

    //No Advance call at all, and it has already arrived.
    CHECK(Drain(network.Server()).size() == 1);
    CHECK(client.RoundTripTime(LoopbackNetwork::ServerPeer) == doctest::Approx(0.0));
}
