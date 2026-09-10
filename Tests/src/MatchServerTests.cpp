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

    //Sends one input for a client's own tick, the way MatchClient does.
    //Unreliable, matching the real client - inputs are the one thing on this
    //wire that is cheaper to lose than to delay.
    void SendInput(Transport& client, std::uint64_t tick, const CharacterInput& input)
    {
        InputMessage message;
        message.FirstTick = tick;
        message.Inputs.push_back(input);
        client.Send(LoopbackNetwork::ServerPeer, Encode(message), Channel::Unreliable);
    }

    //Fires one shot, claiming an instant and an aim.
    void SendFire(Transport& client, std::uint64_t clientTick, std::uint64_t renderTick,
        float renderAlpha, float yaw, float pitch)
    {
        FireMessage fire;
        fire.ClientTick = clientTick;
        fire.RenderTick = renderTick;
        fire.RenderAlpha = renderAlpha;
        fire.Yaw = yaw;
        fire.Pitch = pitch;
        client.Send(LoopbackNetwork::ServerPeer, Encode(fire), Channel::Reliable);
    }

    //The newest shot ruling waiting on this endpoint, and how many arrived.
    //The count matters on its own for the fire rate, where the question is
    //whether a second ruling exists at all.
    std::optional<ShotResolvedMessage> LastShotResolved(Transport& transport, int& count)
    {
        std::optional<ShotResolvedMessage> latest;
        count = 0;

        NetEvent event;
        while (transport.Poll(event))
        {
            if (event.Type != NetEventType::Message)
                continue;

            ShotResolvedMessage resolved;
            if (Decode(event.Data, resolved))
            {
                latest = resolved;
                ++count;
            }
        }

        return latest;
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

    for (std::uint64_t i = 1; i <= 30; ++i)
    {
        InputMessage input;
        input.FirstTick = i;
        CharacterInput held;
        held.Move = glm::vec2(0.0f, 1.0f);
        held.Yaw = 0.0f;
        input.Inputs = { held };
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
    newer.FirstTick = 10;
    CharacterInput newerInput;
    newerInput.Move = glm::vec2(0.0f, 1.0f);
    newer.Inputs = { newerInput };
    client.Send(LoopbackNetwork::ServerPeer, Encode(newer), Channel::Unreliable);
    server.Step(FrameClock::FixedStepSeconds);

    const glm::vec3 afterNewer = server.Match().Player(player).Position();

    //Tick 9 arrives late. It must be dropped, so this step applies no
    //input at all and the player stands still.
    InputMessage stale;
    stale.FirstTick = 9;
    CharacterInput staleInput;
    staleInput.Move = glm::vec2(0.0f, -1.0f);
    stale.Inputs = { staleInput };
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

TEST_CASE("A snapshot acknowledges the input the server applied")
{
    //The ack is what makes replay possible: a client keeps every input the
    //server has not confirmed and replays them on top of each correction. An
    //ack that named the wrong input would have the client replay something
    //already applied, which is a permanent divergence rather than a glitch.
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& client = network.AddClient(peer);
    const PlayerId player = Join(server, client);
    REQUIRE(player != InvalidPlayer);

    CharacterInput walking;
    walking.Move = glm::vec2(0.0f, 1.0f);

    //One input per step, ticks 1..5. Driven a step at a time rather than sent
    //in one burst so this stays true both now and once inputs are queued and
    //consumed one per tick.
    for (std::uint64_t tick = 1; tick <= 5; ++tick)
    {
        InputMessage input;
        input.FirstTick = tick;
        input.Inputs = { walking };
        client.Send(LoopbackNetwork::ServerPeer, Encode(input), Channel::Unreliable);
        server.Step(FrameClock::FixedStepSeconds);
    }

    //Stepped until the ack catches up rather than checked immediately: how many
    //ticks the server takes to work through what it has been sent is its own
    //property, not something this test should pin.
    std::uint64_t acked = 0;
    for (int step = 0; step < 16 && acked < 5; ++step)
    {
        server.Step(FrameClock::FixedStepSeconds);

        const std::optional<SnapshotMessage> snapshot = LastSnapshot(client);
        if (!snapshot.has_value())
            continue;

        REQUIRE(snapshot->Players.size() == 1);
        CHECK(snapshot->Players[0].Player == player);
        acked = snapshot->Players[0].LastInputTick;
    }

    CHECK(acked == 5);
}

namespace
{
    //Where a character starting at the spawn ends up after `steps` walking
    //steps, computed with no server involved. The reference every queue test
    //below is checked against: "moved a bit" would pass under a queue that
    //dropped half its inputs.
    glm::vec3 WalkedFromSpawn(int steps)
    {
        World world = FlatWorld();
        CharacterController character;
        character.Teleport(Spawn);

        CharacterInput walking;
        walking.Move = glm::vec2(0.0f, 1.0f);

        for (int i = 0; i < steps; ++i)
            character.Step(world, walking, FrameClock::FixedStepSeconds);

        return character.Position();
    }

    //Ground-plane equality between a server position and this file's oracle.
    //
    //Deliberately x/z only, never y. MatchState::Step gives every player a
    //physics tick every server tick, including one with no command at all
    //(that is what leaves a laggy player falling instead of frozen); and Join
    //mints its player and hands out the Welcome inside the very same
    //MatchServer::Step call that then runs that tick's physics. So a joined
    //server character has always taken one more gravity tick than a
    //WalkedFromSpawn oracle, which is built fresh and only ever stepped with
    //walking input. Spawn sits a few centimetres above the ground, so that
    //extra tick is a measurable, real y offset until the character lands -
    //orthogonal to which input the queue applied and when, which is the only
    //thing this file is testing. x and z carry no such offset: an idle or
    //airborne tick moves a character sideways by exactly as much as a
    //grounded one, so the walking count alone - not the total tick count -
    //decides them, and that count always matches the oracle's.
    bool SameGroundPlane(const glm::vec3& actual, const glm::vec3& oracle)
    {
        return actual.x == oracle.x && actual.z == oracle.z;
    }

    InputMessage Bundle(std::uint64_t firstTick, int count)
    {
        CharacterInput walking;
        walking.Move = glm::vec2(0.0f, 1.0f);

        InputMessage message;
        message.FirstTick = firstTick;
        message.Inputs.assign(count, walking);
        return message;
    }
}

TEST_CASE("A bundle's inputs are applied one per tick, however many arrive")
{
    //One tick, one input - the contract that makes reconciliation converge at
    //all. If the server applied a whole bundle on one tick, or dropped all but
    //the newest, its state would stop being a prefix of what the client
    //predicted and the difference would never go away.
    //
    //This test cannot tell oldest-first from newest-first apart - every tick
    //in a Bundle() carries the same walking input, so which of the three is
    //consumed on which step is invisible to position. The order claim belongs
    //to "A snapshot acknowledges the oldest input of the bundle first" and to
    //the reordered-bundle test below, both of which assert the ack sequence
    //directly.
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& client = network.AddClient(peer);
    REQUIRE(Join(server, client) != InvalidPlayer);
    const PlayerId player = server.Match().Players().begin()->first;

    client.Send(LoopbackNetwork::ServerPeer, Encode(Bundle(1, 3)), Channel::Unreliable);

    //One step: exactly one input applied, however many arrived.
    server.Step(FrameClock::FixedStepSeconds);
    CHECK(SameGroundPlane(server.Match().Player(player).Position(), WalkedFromSpawn(1)));

    server.Step(FrameClock::FixedStepSeconds);
    CHECK(SameGroundPlane(server.Match().Player(player).Position(), WalkedFromSpawn(2)));

    server.Step(FrameClock::FixedStepSeconds);
    CHECK(SameGroundPlane(server.Match().Player(player).Position(), WalkedFromSpawn(3)));

    //Queue empty: this tick has no input at all, exactly as when a packet is
    //lost. On flat ground a walking character with no input simply stands
    //still, which is why the oracle is unchanged.
    server.Step(FrameClock::FixedStepSeconds);
    CHECK(SameGroundPlane(server.Match().Player(player).Position(), WalkedFromSpawn(3)));
}

TEST_CASE("An input repeated by the next bundle is applied once")
{
    //The redundancy is only free if duplicates are dropped. Applying tick 2
    //twice would walk the player a step further than it ever asked to go, and
    //the client would be corrected for the server's mistake.
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& client = network.AddClient(peer);
    REQUIRE(Join(server, client) != InvalidPlayer);
    const PlayerId player = server.Match().Players().begin()->first;

    //Ticks 1,2,3 then 2,3,4 - four distinct inputs across two bundles.
    client.Send(LoopbackNetwork::ServerPeer, Encode(Bundle(1, 3)), Channel::Unreliable);
    client.Send(LoopbackNetwork::ServerPeer, Encode(Bundle(2, 3)), Channel::Unreliable);

    for (int i = 0; i < 10; ++i)
        server.Step(FrameClock::FixedStepSeconds);

    CHECK(SameGroundPlane(server.Match().Player(player).Position(), WalkedFromSpawn(4)));
}

TEST_CASE("An input already applied is never applied again")
{
    //Stage 2's staleness rule, now expressed against the queue. A bundle that
    //arrives late and repeats what has already been stepped must change
    //nothing at all.
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& client = network.AddClient(peer);
    REQUIRE(Join(server, client) != InvalidPlayer);
    const PlayerId player = server.Match().Players().begin()->first;

    client.Send(LoopbackNetwork::ServerPeer, Encode(Bundle(1, 3)), Channel::Unreliable);
    for (int i = 0; i < 5; ++i)
        server.Step(FrameClock::FixedStepSeconds);

    const glm::vec3 settled = server.Match().Player(player).Position();
    REQUIRE(SameGroundPlane(settled, WalkedFromSpawn(3)));

    //The same three inputs arrive again, late.
    client.Send(LoopbackNetwork::ServerPeer, Encode(Bundle(1, 3)), Channel::Unreliable);
    for (int i = 0; i < 5; ++i)
        server.Step(FrameClock::FixedStepSeconds);

    CHECK(server.Match().Player(player).Position() == settled);
}

TEST_CASE("The input queue is capped, and overflow is dropped rather than absorbed")
{
    //A client running further ahead than this design assumes is a fault worth
    //seeing. Silently absorbing its backlog would present as unexplained
    //corrections much later, on a machine nobody is debugging.
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& client = network.AddClient(peer);
    REQUIRE(Join(server, client) != InvalidPlayer);
    const PlayerId player = server.Match().Players().begin()->first;

    //Twenty inputs with no step in between: the queue can hold eight.
    for (std::uint64_t tick = 1; tick <= 20; ++tick)
        client.Send(LoopbackNetwork::ServerPeer, Encode(Bundle(tick, 1)), Channel::Unreliable);

    for (int i = 0; i < 40; ++i)
        server.Step(FrameClock::FixedStepSeconds);

    //Eight applied, twelve dropped. Not "fewer than twenty": the exact number
    //is what distinguishes a cap from a leak.
    CHECK(server.Match().Player(player).Position() == WalkedFromSpawn(8));
}

TEST_CASE("A snapshot acknowledges the oldest input of the bundle first")
{
    //Which end of the queue is consumed, asserted directly. Popping the newest
    //would ack 3 on the first step; popping the oldest acks 1, then 2, then 3.
    //This is the one assertion that tells those two implementations apart, and
    //the difference between them is a correction on every gap.
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& client = network.AddClient(peer);
    REQUIRE(Join(server, client) != InvalidPlayer);

    client.Send(LoopbackNetwork::ServerPeer, Encode(Bundle(1, 3)), Channel::Unreliable);

    for (std::uint64_t expected = 1; expected <= 3; ++expected)
    {
        server.Step(FrameClock::FixedStepSeconds);

        const std::optional<SnapshotMessage> snapshot = LastSnapshot(client);
        REQUIRE(snapshot.has_value());
        REQUIRE(snapshot->Players.size() == 1);
        CHECK(snapshot->Players[0].LastInputTick == expected);
    }
}

TEST_CASE("Nothing is recorded for a player who does not exist")
{
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    //Enough ticks to fill and overflow the ring, had anybody been in it.
    for (int tick = 0; tick < 40; ++tick)
        server.Step(FrameClock::FixedStepSeconds);

    CHECK(server.History().SampleCount(1) == 0);
}

TEST_CASE("A joined player's recorded history matches where the match stepped them")
{
    //The history must hold the positions the match actually produced, not an
    //approximation of them: a rewind is only honest if it replays the server's
    //own past.
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& client = network.AddClient(peer);
    const PlayerId player = Join(server, client);
    REQUIRE(player != InvalidPlayer);

    //Tick numbers paired with the position that tick produced, so the check
    //below is against a middle tick rather than only the newest - the newest
    //is the one value an off-by-one in the recorded tick can still get right.
    std::vector<std::pair<std::uint64_t, glm::vec3>> stepped;
    for (int tick = 0; tick < 10; ++tick)
    {
        server.Step(FrameClock::FixedStepSeconds);
        stepped.emplace_back(server.Match().Tick() - 1,
            server.Match().Player(player).Position());
    }

    const glm::vec3 halfExtents(0.3f, 0.9f, 0.3f);

    for (const auto& [tick, position] : stepped)
    {
        CAPTURE(tick);

        Aabb box;
        REQUIRE(server.History().BoxAt(player, static_cast<double>(tick), halfExtents, box));

        const glm::vec3 centre = (box.Min + box.Max) * 0.5f;
        CHECK(centre.x == doctest::Approx(position.x));
        CHECK(centre.y == doctest::Approx(position.y));
        CHECK(centre.z == doctest::Approx(position.z));
    }
}

TEST_CASE("A bundle carrying older ticks than are already queued is still applied oldest first")
{
    //Every other test in this file sends bundles whose ticks are already
    //ascending relative to what is queued - the ingest sort is a no-op for
    //all of them, so deleting it would leave the rest of the suite green.
    //Sending the higher ticks first and the lower ones after is what actually
    //forces a re-sort: the unreliable channel this models can and does
    //deliver a later-sent bundle whose ticks are older than ones the queue
    //already holds, and the front still has to end up the oldest.
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& client = network.AddClient(peer);
    REQUIRE(Join(server, client) != InvalidPlayer);

    //Ticks 5,6,7 arrive first; ticks 2,3,4 arrive second, entirely before the
    //server ever steps. A queue that trusted arrival order would apply 5
    //first; the front has to be 2.
    client.Send(LoopbackNetwork::ServerPeer, Encode(Bundle(5, 3)), Channel::Unreliable);
    client.Send(LoopbackNetwork::ServerPeer, Encode(Bundle(2, 3)), Channel::Unreliable);

    for (std::uint64_t expected = 2; expected <= 7; ++expected)
    {
        server.Step(FrameClock::FixedStepSeconds);

        const std::optional<SnapshotMessage> snapshot = LastSnapshot(client);
        REQUIRE(snapshot.has_value());
        REQUIRE(snapshot->Players.size() == 1);
        CHECK(snapshot->Players[0].LastInputTick == expected);
    }
}

TEST_CASE("A shot claiming an ancient instant is clamped into the window")
{
    //A lying client gets aimed at a quarter-second-old world, which is exactly
    //what an honest 250 ms player gets. The lie buys nothing, and that is the
    //entire trust story for RenderTick.
    //
    //Falsifiable because of how BoxAt answers an instant it has no record of:
    //WITHOUT the clamp, tick 0 falls before the target's oldest sample, so the
    //target is not a candidate and the shot comes back a miss.
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId shooterPeer = InvalidPeer;
    Transport& shooter = network.AddClient(shooterPeer);
    const PlayerId shooterId = Join(server, shooter);

    PeerId targetPeer = InvalidPeer;
    Transport& target = network.AddClient(targetPeer);
    const PlayerId targetId = Join(server, target);

    REQUIRE(shooterId != InvalidPlayer);
    REQUIRE(targetId != InvalidPlayer);

    //Walk the target away along +x, then let them stand. Yaw 0 faces +x and
    //Move.y walks forward, per Heading.h and CharacterInput.
    CharacterInput walk;
    walk.Move = glm::vec2(0.0f, 1.0f);
    walk.Yaw = 0.0f;

    for (std::uint64_t tick = 0; tick < 30; ++tick)
    {
        SendInput(target, tick, walk);
        server.Step(FrameClock::FixedStepSeconds);
    }

    //Standing still for longer than the ring is deep, so every instant in the
    //window reports the same position and the two shots below are comparable.
    for (int tick = 0; tick < 20; ++tick)
        server.Step(FrameClock::FixedStepSeconds);

    int ignored = 0;
    LastShotResolved(shooter, ignored);

    //An honest claim: the oldest instant the window allows.
    SendFire(shooter, 1, server.Match().Tick() - MaxRewindTicks, 0.0f, 0.0f, 0.0f);
    server.Step(FrameClock::FixedStepSeconds);

    int honestCount = 0;
    const std::optional<ShotResolvedMessage> honest = LastShotResolved(shooter, honestCount);
    REQUIRE(honest.has_value());

    for (int tick = 0; tick < TicksBetweenShots; ++tick)
        server.Step(FrameClock::FixedStepSeconds);

    //A claim from before the match had any history at all.
    SendFire(shooter, 2, 0, 0.0f, 0.0f, 0.0f);
    server.Step(FrameClock::FixedStepSeconds);

    int liarCount = 0;
    const std::optional<ShotResolvedMessage> liar = LastShotResolved(shooter, liarCount);
    REQUIRE(liar.has_value());

    CHECK(honest->Victim == targetId);
    CHECK(liar->Victim == targetId);
}

TEST_CASE("A second shot within the fire rate is dropped")
{
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& shooter = network.AddClient(peer);
    REQUIRE(Join(server, shooter) != InvalidPlayer);

    int ignored = 0;
    LastShotResolved(shooter, ignored);

    SendFire(shooter, 1, server.Match().Tick(), 0.0f, 0.0f, 0.0f);
    server.Step(FrameClock::FixedStepSeconds);

    SendFire(shooter, 2, server.Match().Tick(), 0.0f, 0.0f, 0.0f);
    server.Step(FrameClock::FixedStepSeconds);

    int count = 0;
    LastShotResolved(shooter, count);

    //One ruling, not two: the second shot came a tick after the first, and the
    //weapon fires once every ten.
    CHECK(count == 1);
}

TEST_CASE("A fire before the handshake is ignored")
{
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& stranger = network.AddClient(peer);

    //No Hello. A peer exists; a player does not.
    SendFire(stranger, 1, 0, 0.0f, 0.0f, 0.0f);
    server.Step(FrameClock::FixedStepSeconds);

    int count = 0;
    LastShotResolved(stranger, count);

    CHECK(count == 0);
    CHECK(server.Match().Players().empty());
}

TEST_CASE("The clamp applies to the combined instant, not to RenderTick before RenderAlpha is added")
{
    //A claim of RenderTick 0 is so far outside a 15-tick window that no
    //RenderAlpha in [0, 1) can pull it back in - clamping the combined instant
    //lands on exactly the same tick regardless of what RenderAlpha says. A
    //clamp that clamped RenderTick alone and added RenderAlpha afterwards
    //would let that alpha survive as a fractional offset nobody claimed to be
    //at, and a target walking the whole time would be hit somewhere else for
    //it.
    //
    //"A shot claiming an ancient instant is clamped into the window" cannot
    //catch that bug: it lets the target come to rest well before firing, so
    //the entire rewind window reports one position and a fractional offset
    //inside it is invisible no matter what RenderAlpha claims. This one never
    //lets the target stop.
    const auto fireAndGetImpact = [](float alpha) -> glm::vec3
    {
        LoopbackNetwork network;
        MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

        PeerId shooterPeer = InvalidPeer;
        Transport& shooter = network.AddClient(shooterPeer);
        const PlayerId shooterId = Join(server, shooter);

        PeerId targetPeer = InvalidPeer;
        Transport& target = network.AddClient(targetPeer);
        const PlayerId targetId = Join(server, target);

        REQUIRE(shooterId != InvalidPlayer);
        REQUIRE(targetId != InvalidPlayer);

        CharacterInput walk;
        walk.Move = glm::vec2(0.0f, 1.0f);
        walk.Yaw = 0.0f;

        for (std::uint64_t tick = 0; tick < 50; ++tick)
        {
            SendInput(target, tick, walk);
            server.Step(FrameClock::FixedStepSeconds);
        }

        int ignored = 0;
        LastShotResolved(shooter, ignored);

        SendFire(shooter, 1, 0, alpha, 0.0f, 0.0f);
        server.Step(FrameClock::FixedStepSeconds);

        int count = 0;
        const std::optional<ShotResolvedMessage> resolved = LastShotResolved(shooter, count);
        REQUIRE(resolved.has_value());
        REQUIRE(resolved->Victim == targetId);
        return resolved->Impact;
    };

    const glm::vec3 zeroAlpha = fireAndGetImpact(0.0f);
    const glm::vec3 nearWholeAlpha = fireAndGetImpact(0.99f);

    CHECK(zeroAlpha.x == doctest::Approx(nearWholeAlpha.x));
    CHECK(zeroAlpha.y == doctest::Approx(nearWholeAlpha.y));
    CHECK(zeroAlpha.z == doctest::Approx(nearWholeAlpha.z));
}

TEST_CASE("A shot accepted on the server's tick zero still guards the next one")
{
    //Hello and Fire both arrive before this server has stepped even once, so
    //both are handled inside the SAME Step call, before m_Match.Step has
    //incremented the tick off its starting value - HandleFire therefore runs
    //with Tick() == 0. That is a real tick a shot can land on, not a spare
    //value free for "never fired" to mean.
    //
    //Join() cannot set this scenario up: it steps at least once waiting for
    //Welcome, so Tick() >= 1 in every fire this file sends elsewhere.
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& shooter = network.AddClient(peer);

    shooter.Send(LoopbackNetwork::ServerPeer, Encode(HelloMessage{}), Channel::Reliable);
    SendFire(shooter, 1, 0, 0.0f, 0.0f, 0.0f);
    server.Step(FrameClock::FixedStepSeconds);

    int firstCount = 0;
    const std::optional<ShotResolvedMessage> first = LastShotResolved(shooter, firstCount);
    REQUIRE(first.has_value());

    //One tick later - well inside the ten-tick fire rate - a second shot must
    //still be dropped. A LastShotTick of 0 mistaken for "has not fired" would
    //let this one through.
    SendFire(shooter, 2, 0, 0.0f, 0.0f, 0.0f);
    server.Step(FrameClock::FixedStepSeconds);

    int secondCount = 0;
    LastShotResolved(shooter, secondCount);

    CHECK(secondCount == 0);
}
