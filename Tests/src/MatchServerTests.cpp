#include <doctest.h>

#include "Cubit/FrameClock.h"
#include "Cubit/Net/LoopbackTransport.h"
#include "Cubit/Net/MatchServer.h"
#include "Cubit/Net/Protocol.h"
#include "Cubit/Net/SimulatedTransport.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <optional>
#include <vector>

namespace
{
    World FlatWorld()
    {
        World world(2, 2, 2);

        for (int z = 0; z < world.GetDepth(); ++z)
            for (int x = 0; x < world.GetWidth(); ++x)
                world.SetBlock(x, 0, z, BlockId{ 1 });

        return world;
    }

    const glm::vec3 Spawn{ 8.0f, 2.0f, 8.0f };

    //Reads whatever the raw endpoint has waiting and returns the last snapshot
    //among it, which is what a test almost always wants to look at.
    std::optional<SnapshotMessage> LastSnapshot(Transport& transport)
    {
        std::optional<SnapshotMessage> latest;

        NetEvent event;
        while (transport.Poll(event))
        {
            if (event.Type != NetEventType::Message)
                continue;

            SnapshotMessage snapshot;
            if (Decode(event.Data, snapshot))
                latest = snapshot;
        }

        return latest;
    }

    std::optional<WelcomeMessage> FindWelcome(Transport& transport)
    {
        std::optional<WelcomeMessage> found;

        NetEvent event;
        while (transport.Poll(event))
        {
            if (event.Type != NetEventType::Message)
                continue;

            WelcomeMessage welcome;
            if (Decode(event.Data, welcome))
                found = welcome;
        }

        return found;
    }

    //Every snapshot tick waiting on this endpoint, in arrival order. Which
    //ticks arrived is what separates independent loss from shared-fate loss;
    //how many arrived is not, because two independent draws can tie.
    std::vector<std::uint64_t> SnapshotTicks(Transport& transport)
    {
        std::vector<std::uint64_t> ticks;

        NetEvent event;
        while (transport.Poll(event))
        {
            if (event.Type != NetEventType::Message)
                continue;

            SnapshotMessage snapshot;
            if (Decode(event.Data, snapshot))
                ticks.push_back(snapshot.Tick);
        }

        return ticks;
    }

    //Completes the handshake and returns the player it was given, or
    //InvalidPlayer if no welcome arrived.
    //
    //Steps until the welcome lands rather than exactly once, because how many
    //ticks that takes is a property of the server's transport, not of the
    //handshake. Over raw loopback delivery is instant and the first step is
    //enough; through SimulatedTransport a packet sent during a step is only
    //delivered by the next Advance, so it takes two.
    PlayerId Join(MatchServer& server, Transport& client)
    {
        client.Send(LoopbackNetwork::ServerPeer, Encode(HelloMessage{}), Channel::Reliable);

        for (int step = 0; step < 8; ++step)
        {
            server.Step(FrameClock::FixedStepSeconds);

            const std::optional<WelcomeMessage> welcome = FindWelcome(client);
            if (welcome.has_value())
                return welcome->You;
        }

        return InvalidPlayer;
    }
}

TEST_CASE("A server with no clients still ticks")
{
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    server.Step(FrameClock::FixedStepSeconds);

    CHECK(server.Match().Tick() == 1);
    CHECK(server.ClientCount() == 0);
}

TEST_CASE("Saying hello gets a welcome, a player and a place to stand")
{
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& client = network.AddClient(peer);
    client.Send(LoopbackNetwork::ServerPeer, Encode(HelloMessage{}), Channel::Reliable);

    server.Step(FrameClock::FixedStepSeconds);

    const std::optional<WelcomeMessage> welcome = FindWelcome(client);
    REQUIRE(welcome.has_value());
    CHECK(welcome->You != InvalidPlayer);
    CHECK(welcome->MapName == "flat.vox");
    CHECK(welcome->MapHash == 0xABCD);
    CHECK(welcome->Edits.empty());

    REQUIRE(server.Match().HasPlayer(welcome->You));

    //The spawn column, not the spawn point. The handshake is handled at the top
    //of a tick that then steps the match, so by the time this is observable the
    //player has had exactly one step of gravity applied and sits fractionally
    //below where it was placed. Asserting equality here would be asserting that
    //MatchServer does not simulate on the tick a player joins, which is neither
    //true nor desirable - a joiner that skipped a tick would be a tick behind
    //everybody else for the rest of the match. How far it falls belongs to
    //CharacterControllerTests.
    const glm::vec3 position = server.Match().Player(welcome->You).Position();
    CHECK(position.x == doctest::Approx(Spawn.x));
    CHECK(position.z == doctest::Approx(Spawn.z));
    CHECK(position.y <= Spawn.y);
    CHECK(position.y > Spawn.y - 0.1f);
}

TEST_CASE("A client speaking the wrong protocol version is disconnected, not tolerated")
{
    //Two builds disagreeing about field widths produce garbage positions, which
    //read as a physics bug and cost a day. Failing at the handshake is the
    //cheap version of that discovery.
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& client = network.AddClient(peer);

    HelloMessage wrong;
    wrong.Version = ProtocolVersion + 1;
    client.Send(LoopbackNetwork::ServerPeer, Encode(wrong), Channel::Reliable);

    server.Step(FrameClock::FixedStepSeconds);

    CHECK_FALSE(FindWelcome(client).has_value());
    CHECK(server.ClientCount() == 0);
    CHECK(server.Match().Players().empty());
}

TEST_CASE("The snapshot carries the whole roster, so joins and leaves need no message")
{
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId firstPeer = InvalidPeer;
    PeerId secondPeer = InvalidPeer;
    Transport& first = network.AddClient(firstPeer);
    Transport& second = network.AddClient(secondPeer);

    first.Send(LoopbackNetwork::ServerPeer, Encode(HelloMessage{}), Channel::Reliable);
    second.Send(LoopbackNetwork::ServerPeer, Encode(HelloMessage{}), Channel::Reliable);
    server.Step(FrameClock::FixedStepSeconds);

    //Drain the welcomes so only snapshots remain.
    FindWelcome(first);
    FindWelcome(second);
    server.Step(FrameClock::FixedStepSeconds);

    const std::optional<SnapshotMessage> snapshot = LastSnapshot(first);
    REQUIRE(snapshot.has_value());
    CHECK(snapshot->Players.size() == 2);
    CHECK(snapshot->Tick == server.Match().Tick());

    network.RemoveClient(secondPeer);
    server.Step(FrameClock::FixedStepSeconds);

    const std::optional<SnapshotMessage> afterLeaving = LastSnapshot(first);
    REQUIRE(afterLeaving.has_value());
    CHECK(afterLeaving->Players.size() == 1);
    CHECK(server.Match().Players().size() == 1);
}

TEST_CASE("A peer that has not finished the handshake is sent nothing")
{
    //A snapshot before the Welcome is unreadable - the client does not yet know
    //which player is its own, and has not been told which map to load. Worse
    //for an edit: Welcome carries the whole edit log, so an EditApplied sent
    //before it would be applied twice by a client that then reads the log.
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId joined = InvalidPeer;
    Transport& speaker = network.AddClient(joined);
    REQUIRE(Join(server, speaker) != InvalidPlayer);

    //Connects and says nothing.
    PeerId silentPeer = InvalidPeer;
    Transport& silent = network.AddClient(silentPeer);

    EditMessage edit;
    edit.Edit.Position = glm::ivec3(8, 1, 8);
    edit.Edit.Block = BlockId{ 1 };
    speaker.Send(LoopbackNetwork::ServerPeer, EncodeEditRequest(edit), Channel::Reliable);

    server.Step(FrameClock::FixedStepSeconds);

    CHECK(server.ClientCount() == 2);

    std::size_t messages = 0;
    NetEvent event;
    while (silent.Poll(event))
    {
        if (event.Type == NetEventType::Message)
            ++messages;
    }

    CHECK(messages == 0);
}

TEST_CASE("Input moves the player it came from")
{
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& client = network.AddClient(peer);
    const PlayerId player = Join(server, client);
    REQUIRE(player != InvalidPlayer);

    const glm::vec3 before = server.Match().Player(player).Position();

    for (std::uint32_t i = 1; i <= 30; ++i)
    {
        InputMessage input;
        input.Sequence = i;
        input.Input.Move = glm::vec2(0.0f, 1.0f);
        input.Input.Yaw = 0.0f;
        client.Send(LoopbackNetwork::ServerPeer, Encode(input), Channel::Unreliable);
        server.Step(FrameClock::FixedStepSeconds);
    }

    CHECK(server.Match().Player(player).Position() != before);
}

TEST_CASE("A stale or duplicated input is ignored")
{
    //The unreliable channel is unordered, so an old packet arriving after a
    //newer one is routine. Applying it would rewind the player by one step for
    //no visible reason.
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& client = network.AddClient(peer);
    const PlayerId player = Join(server, client);
    REQUIRE(player != InvalidPlayer);

    InputMessage newer;
    newer.Sequence = 10;
    newer.Input.Move = glm::vec2(0.0f, 1.0f);
    client.Send(LoopbackNetwork::ServerPeer, Encode(newer), Channel::Unreliable);
    server.Step(FrameClock::FixedStepSeconds);

    const glm::vec3 afterNewer = server.Match().Player(player).Position();

    //Sequence 9 arrives late. It must be dropped, so this step applies no
    //input at all and the player stands still.
    InputMessage stale;
    stale.Sequence = 9;
    stale.Input.Move = glm::vec2(0.0f, -1.0f);
    client.Send(LoopbackNetwork::ServerPeer, Encode(stale), Channel::Unreliable);
    server.Step(FrameClock::FixedStepSeconds);

    CHECK(server.Match().Player(player).Position().x == doctest::Approx(afterNewer.x));
    CHECK(server.Match().Player(player).Position().z == doctest::Approx(afterNewer.z));
}

TEST_CASE("A garbage packet is ignored rather than fatal")
{
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& client = network.AddClient(peer);

    const std::vector<std::uint8_t> nonsense{ 0xFF, 0x00, 0x42 };
    client.Send(LoopbackNetwork::ServerPeer, nonsense, Channel::Reliable);

    server.Step(FrameClock::FixedStepSeconds);

    CHECK(server.Match().Tick() == 1);
    CHECK(server.Match().Players().empty());
}

TEST_CASE("An applied edit reaches every joined client and is remembered for the next one")
{
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId firstPeer = InvalidPeer;
    Transport& first = network.AddClient(firstPeer);
    REQUIRE(Join(server, first) != InvalidPlayer);

    PeerId secondPeer = InvalidPeer;
    Transport& second = network.AddClient(secondPeer);
    REQUIRE(Join(server, second) != InvalidPlayer);

    EditMessage request;
    request.Edit.Position = glm::ivec3(8, 1, 8);
    request.Edit.Block = BlockId{ 1 };
    first.Send(LoopbackNetwork::ServerPeer, EncodeEditRequest(request), Channel::Reliable);

    server.Step(FrameClock::FixedStepSeconds);

    //The requester included: its own world changes only when this arrives,
    //which is what makes the round trip visible in Stage 2.
    std::size_t sawEdit = 0;
    for (Transport* client : { &first, &second })
    {
        NetEvent event;
        while (client->Poll(event))
        {
            if (event.Type != NetEventType::Message)
                continue;

            MessageId id = MessageId::Hello;
            if (!PeekMessageId(event.Data, id) || id != MessageId::EditApplied)
                continue;

            EditMessage applied;
            if (Decode(event.Data, applied) && applied.Edit.Position == request.Edit.Position)
                ++sawEdit;
        }
    }

    CHECK(sawEdit == 2);
    REQUIRE(server.EditLog().size() == 1);
    CHECK(server.EditLog()[0].Position == request.Edit.Position);

    //A client arriving now must see the hole, so the log rides along in Welcome.
    PeerId latePeer = InvalidPeer;
    Transport& late = network.AddClient(latePeer);
    late.Send(LoopbackNetwork::ServerPeer, Encode(HelloMessage{}), Channel::Reliable);
    server.Step(FrameClock::FixedStepSeconds);

    const std::optional<WelcomeMessage> welcome = FindWelcome(late);
    REQUIRE(welcome.has_value());
    REQUIRE(welcome->Edits.size() == 1);
    CHECK(welcome->Edits[0].Position == request.Edit.Position);
}

TEST_CASE("Snapshot loss is drawn per client, not shared between them")
{
    //The reason MatchServer sends snapshots with a per-peer Send loop rather
    //than Broadcast. SimulatedTransport::Broadcast makes ONE loss draw for the
    //whole call, so under Broadcast every client would lose the same snapshots
    //and desync in lockstep - hiding exactly the per-client divergence a
    //network test exists to catch. Real ENet loses each peer's copy
    //independently; a per-peer Send loop is what reproduces that.
    //
    //Loss is 0.5 rather than a plausible 0.05 so that loss actually happens
    //often enough to separate the two behaviours within the run.
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Loss = 0.5f;
    sim.Seed = 20260903;
    SimulatedTransport serverSide(network.Server(), sim);

    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, serverSide);

    //Clients are raw endpoints, so only the server's outbound traffic is lossy.
    //Welcome is Reliable and therefore delayed rather than dropped, so both
    //handshakes complete regardless.
    PeerId firstPeer = InvalidPeer;
    PeerId secondPeer = InvalidPeer;
    Transport& first = network.AddClient(firstPeer);
    Transport& second = network.AddClient(secondPeer);

    REQUIRE(Join(server, first) != InvalidPlayer);
    REQUIRE(Join(server, second) != InvalidPlayer);

    FindWelcome(first);
    FindWelcome(second);

    constexpr int Steps = 200;
    for (int i = 0; i < Steps; ++i)
        server.Step(FrameClock::FixedStepSeconds);

    const std::vector<std::uint64_t> firstTicks = SnapshotTicks(first);
    const std::vector<std::uint64_t> secondTicks = SnapshotTicks(second);

    //Both halves guard against passing vacuously: all-lost or none-lost would
    //make the streams match for a reason that has nothing to do with the draw.
    CHECK(firstTicks.size() > 0);
    CHECK(secondTicks.size() > 0);
    CHECK(firstTicks.size() < static_cast<std::size_t>(Steps));
    CHECK(secondTicks.size() < static_cast<std::size_t>(Steps));

    CHECK(firstTicks != secondTicks);
}
