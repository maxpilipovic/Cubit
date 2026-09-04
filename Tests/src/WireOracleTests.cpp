#include <doctest.h>

#include "Cubit/FrameClock.h"
#include "Cubit/Net/LoopbackTransport.h"
#include "Cubit/Net/MatchClient.h"
#include "Cubit/Net/MatchServer.h"
#include "Cubit/Net/SimulatedTransport.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace
{
    //50 ms one-way at 60 Hz is exactly 3 ticks, so the oracle asserts exact
    //equality against an offset history rather than equality within a
    //tolerance. Every latency in this suite is a whole tick multiple.
    constexpr int LatencyTicks = 3;
    constexpr double OneWayLatency = LatencyTicks * FrameClock::FixedStepSeconds;

    //How far the client's clock trails the server's, measured after both have
    //stepped. NOT the same number as LatencyTicks, and neither bound is a
    //fault - see the comment on the skew assertion for where each comes from.
    constexpr std::uint64_t MinSkew = LatencyTicks + 1;
    constexpr std::uint64_t MaxSkew = LatencyTicks + 2;

    constexpr std::uint64_t MapHash = 0xFEEDFACEull;
    const glm::vec3 Spawn{ 8.0f, 2.0f, 8.0f };

    World FlatWorld()
    {
        World world(2, 2, 2);

        for (int z = 0; z < world.GetDepth(); ++z)
            for (int x = 0; x < world.GetWidth(); ++x)
                world.SetBlock(x, 0, z, BlockId{ 1 });

        return world;
    }

    //A loader that always succeeds with the same world the server is running.
    MatchClient::MapLoader GoodLoader()
    {
        return [](const std::string&) -> std::optional<LoadedMap>
        {
            return LoadedMap{ FlatWorld(), MapHash };
        };
    }

    bool WorldsMatch(const World& a, const World& b)
    {
        if (a.GetWidth() != b.GetWidth() ||
            a.GetHeight() != b.GetHeight() ||
            a.GetDepth() != b.GetDepth())
            return false;

        for (int y = 0; y < a.GetHeight(); ++y)
            for (int z = 0; z < a.GetDepth(); ++z)
                for (int x = 0; x < a.GetWidth(); ++x)
                    if (a.GetBlock(x, y, z) != b.GetBlock(x, y, z))
                        return false;

        return true;
    }

    CharacterInput Walking(float yaw)
    {
        CharacterInput input;
        input.Move = glm::vec2(0.0f, 1.0f);
        input.Yaw = yaw;
        return input;
    }
}

TEST_CASE("A client's state is the server's state, delayed by exactly the one-way latency")
{
    //THE ORACLE FOR THIS STAGE.
    //
    //Stage 1's was "MatchState must agree with bare CharacterControllers fed
    //the same inputs". This is its successor: a client driven only by
    //snapshots must equal the server's own history, offset by how long a
    //snapshot takes to arrive.
    //
    //It is written as two separate exact assertions rather than one combined
    //one, because they fail for different reasons. The first catches a wire
    //that corrupts or reorders state. The second catches a wire that delivers
    //the right state at the wrong time.
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = OneWayLatency;

    SimulatedTransport serverNet(network.Server(), sim);

    PeerId peer = InvalidPeer;
    SimulatedTransport clientNet(network.AddClient(peer), sim);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, serverNet);
    MatchClient client(clientNet, GoodLoader());

    //Tick -> the server's own record of where everybody was at that tick.
    std::map<std::uint64_t, std::vector<std::pair<PlayerId, glm::vec3>>> history;

    std::vector<std::uint64_t> observedSkew;

    for (int i = 0; i < 400; ++i)
    {
        //ORDER MATTERS AND IS PART OF THE ASSERTION. The client sends and
        //applies first, then the server receives and steps. If the skew below
        //leaves its bounds, do NOT simply widen them - confirm this loop order
        //first, because an unexpected offset means a packet is being serviced
        //in the wrong phase, which is a real bug.
        client.SetInput(Walking(90.0f));
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);

        std::vector<std::pair<PlayerId, glm::vec3>> row;
        for (const auto& [player, character] : server.Match().Players())
            row.emplace_back(player, character.Position());
        history[server.Match().Tick()] = row;

        if (!client.Connected())
            continue;

        //ASSERTION ONE: whatever tick the client believes it is at, its state
        //must be the server's state at that exact tick. Exact equality, not
        //Approx: these are the same floats, round-tripped through the codec,
        //not two independent computations that might drift.
        const auto recorded = history.find(client.Match().Tick());
        if (recorded != history.end())
        {
            std::vector<std::pair<PlayerId, glm::vec3>> mine;
            for (const auto& [player, character] : client.Match().Players())
                mine.emplace_back(player, character.Position());

            CHECK(mine == recorded->second);
        }

        //Sampled after warm-up, once the pipeline is full.
        if (i > 60)
            observedSkew.push_back(server.Match().Tick() - client.Match().Tick());
    }

    REQUIRE_FALSE(observedSkew.empty());

    //ASSERTION TWO: the delay is BOUNDED, and the bound is the one-way latency
    //plus one tick, plus at most one more. The plan predicted a single exact
    //value; measuring it found two, 4 and 5, in a stable 280:59 split. Both
    //bounds are accounted for, and neither is a phase bug:
    //
    //  +1 always, and it is where the measurement is taken rather than
    //  latency. A snapshot describing tick T is queued at the END of the
    //  server's step, comes due LatencyTicks later, and is picked up by the
    //  client's next Step - which in this loop runs BEFORE the server steps
    //  again. So when the skew is read, the server has advanced once beyond
    //  the snapshot the client is holding.
    //
    //  +1 more, sometimes, and it is floating point. SimulatedTransport's
    //  clock accumulates by repeated `m_Now += seconds`, while a packet's due
    //  time is computed once as `m_Now + Latency`. For a latency that is an
    //  exact tick multiple those two sums are not the same double: for about
    //  17% of ticks the accumulated clock lands one ULP (~1e-17) BELOW the
    //  due time, `Due <= m_Now` fails, and the packet waits one more tick.
    //  Verified independently by replaying the same additions outside the
    //  test: 67 of 400 slip, against 59 of 339 observed here.
    //
    //  So "50 ms at 60 Hz is exactly 3 ticks" is true in arithmetic and false
    //  in doubles. The property worth protecting is not a magic constant but
    //  that the lag is bounded and does not GROW: a client falling steadily
    //  behind - a queue building up, a snapshot backlog - is what this catches,
    //  and it would blow the upper bound within a few ticks.
    //
    //Your own keypress takes about twice this to show up, because the input
    //must go up before the snapshot reflecting it can come down. That second
    //number is what the stage makes you feel, and it is deliberately not
    //hidden.
    for (const std::uint64_t skew : observedSkew)
    {
        CHECK(skew >= MinSkew);
        CHECK(skew <= MaxSkew);
    }
}

TEST_CASE("The client never steps the simulation itself")
{
    //The defining constraint of Stage 2. If this fails, prediction has grown
    //by accident, the latency is being hidden, and Stage 3 will begin from a
    //half-built reconciliation loop instead of a clean one.
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    SimulatedTransport serverNet(network.Server(), sim);

    PeerId peer = InvalidPeer;
    SimulatedTransport clientNet(network.AddClient(peer), sim);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, serverNet);
    MatchClient client(clientNet, GoodLoader());

    for (int i = 0; i < 200; ++i)
    {
        client.SetInput(Walking(90.0f));
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }
    REQUIRE(client.Connected());

    //Comparing the two ticks CANNOT prove this, which is worth stating because
    //it is what this test originally did. HandleSnapshot calls SetTick on every
    //snapshot, so a client that also stepped would have its tick dragged back
    //to the server's number on the very next packet - and since the client runs
    //several ticks BEHIND, one local increment per tick never overtakes it. The
    //assertion would hold whether or not the client stepped. It is the same
    //near-tautology the Stage 1 determinism test was, found the same way: by
    //making the mutation and watching the test stay green.
    //
    //What does prove it: stop the server, let everything in flight drain, and
    //then keep driving the client with walking input. With nothing arriving,
    //a client that does not step cannot move. A client that steps walks away.
    for (int i = 0; i < 30; ++i)
    {
        client.SetInput(Walking(90.0f));
        client.Step(FrameClock::FixedStepSeconds);
    }

    const glm::vec3 frozen = client.Match().Player(client.LocalPlayer()).Position();
    const std::uint64_t frozenTick = client.Match().Tick();

    for (int i = 0; i < 120; ++i)
    {
        client.SetInput(Walking(90.0f));
        client.Step(FrameClock::FixedStepSeconds);

        CHECK(client.Match().Player(client.LocalPlayer()).Position() == frozen);
        CHECK(client.Match().Tick() == frozenTick);
    }
}

TEST_CASE("Two clients see each other move")
{
    //Why two and not one: a snapshot format designed around exactly one
    //character is the same "abstraction over a single instance" mistake this
    //project has declined twice. Two forces the snapshot to be a collection.
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    SimulatedTransport serverNet(network.Server(), sim);

    PeerId firstPeer = InvalidPeer;
    PeerId secondPeer = InvalidPeer;
    SimulatedTransport firstNet(network.AddClient(firstPeer), sim);
    SimulatedTransport secondNet(network.AddClient(secondPeer), sim);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, serverNet);
    MatchClient first(firstNet, GoodLoader());
    MatchClient second(secondNet, GoodLoader());

    for (int i = 0; i < 200; ++i)
    {
        first.SetInput(Walking(90.0f));
        second.SetInput(Walking(-90.0f));
        first.Step(FrameClock::FixedStepSeconds);
        second.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }

    REQUIRE(first.Connected());
    REQUIRE(second.Connected());

    //Each holds both players.
    CHECK(first.Match().Players().size() == 2);
    CHECK(second.Match().Players().size() == 2);

    //And each sees the other somewhere other than the spawn.
    CHECK(first.Match().Player(second.LocalPlayer()).Position() != Spawn);
    CHECK(second.Match().Player(first.LocalPlayer()).Position() != Spawn);

    //The angles travelled too, which is what lets a remote character be drawn
    //facing the right way.
    CHECK(first.ViewAngles(second.LocalPlayer()).x == doctest::Approx(-90.0f));
}

TEST_CASE("An edit takes a round trip and is not applied locally first")
{
    //The visible round trip, asserted rather than felt. The requester's own
    //world changes only when the server's broadcast arrives.
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    SimulatedTransport serverNet(network.Server(), sim);

    PeerId peer = InvalidPeer;
    SimulatedTransport clientNet(network.AddClient(peer), sim);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, serverNet);
    MatchClient client(clientNet, GoodLoader());

    for (int i = 0; i < 30; ++i)
    {
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }
    REQUIRE(client.Connected());

    const glm::ivec3 target(4, 0, 4);
    REQUIRE(client.Match().GetWorld().GetBlock(4, 0, 4) == BlockId{ 1 });

    client.RequestEdit(BlockEdit{ target, BlockId{ 0 } });

    //One tick later nothing has happened locally: the request has not even
    //reached the server yet.
    client.Step(FrameClock::FixedStepSeconds);
    server.Step(FrameClock::FixedStepSeconds);
    CHECK(client.Match().GetWorld().GetBlock(4, 0, 4) == BlockId{ 1 });

    for (int i = 0; i < 30; ++i)
    {
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }

    CHECK(client.Match().GetWorld().GetBlock(4, 0, 4) == BlockId{ 0 });
    CHECK(server.Match().GetWorld().GetBlock(4, 0, 4) == BlockId{ 0 });
    CHECK(server.EditLog().size() == 1);
}

TEST_CASE("A client joining late gets a world matching everyone else's, block for block")
{
    //Without the edit log in Welcome, a client arriving after somebody dug a
    //hole would get a pristine world and then collide against terrain nobody
    //else has. The symptom would look like a prediction bug and is not.
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    SimulatedTransport serverNet(network.Server(), sim);

    PeerId firstPeer = InvalidPeer;
    SimulatedTransport firstNet(network.AddClient(firstPeer), sim);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, serverNet);
    MatchClient first(firstNet, GoodLoader());

    for (int i = 0; i < 30; ++i)
    {
        first.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }
    REQUIRE(first.Connected());

    //Dig a trench.
    for (int x = 0; x < 20; ++x)
    {
        first.RequestEdit(BlockEdit{ glm::ivec3(x, 0, 6), BlockId{ 0 } });
        first.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }

    for (int i = 0; i < 40; ++i)
    {
        first.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }
    REQUIRE(server.EditLog().size() == 20);

    //Now somebody arrives.
    PeerId secondPeer = InvalidPeer;
    SimulatedTransport secondNet(network.AddClient(secondPeer), sim);
    MatchClient second(secondNet, GoodLoader());

    for (int i = 0; i < 40; ++i)
    {
        first.Step(FrameClock::FixedStepSeconds);
        second.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }

    REQUIRE(second.Connected());
    CHECK(WorldsMatch(second.Match().GetWorld(), server.Match().GetWorld()));
    CHECK(WorldsMatch(second.Match().GetWorld(), first.Match().GetWorld()));
}

TEST_CASE("Two clients editing the same block on the same tick converge")
{
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    SimulatedTransport serverNet(network.Server(), sim);

    PeerId firstPeer = InvalidPeer;
    PeerId secondPeer = InvalidPeer;
    SimulatedTransport firstNet(network.AddClient(firstPeer), sim);
    SimulatedTransport secondNet(network.AddClient(secondPeer), sim);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, serverNet);
    MatchClient first(firstNet, GoodLoader());
    MatchClient second(secondNet, GoodLoader());

    for (int i = 0; i < 30; ++i)
    {
        first.Step(FrameClock::FixedStepSeconds);
        second.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }
    REQUIRE(first.Connected());
    REQUIRE(second.Connected());

    const glm::ivec3 contested(5, 0, 5);
    first.RequestEdit(BlockEdit{ contested, BlockId{ 0 } });
    second.RequestEdit(BlockEdit{ contested, BlockId{ 2 } });

    for (int i = 0; i < 40; ++i)
    {
        first.Step(FrameClock::FixedStepSeconds);
        second.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }

    //Both agree with the server, whichever won. Player-id order decides, and
    //it decides the same way every run.
    CHECK(WorldsMatch(first.Match().GetWorld(), server.Match().GetWorld()));
    CHECK(WorldsMatch(second.Match().GetWorld(), server.Match().GetWorld()));
}

TEST_CASE("A client with the wrong map is refused at the handshake")
{
    //Loudly, and now - not silently, an hour later, as movement that disagrees
    //with the server for reasons that look like a netcode bug.
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& clientNet = network.AddClient(peer);

    MatchClient client(clientNet,
        [](const std::string&) -> std::optional<LoadedMap>
        {
            return LoadedMap{ FlatWorld(), MapHash + 1 };
        });

    for (int i = 0; i < 20; ++i)
    {
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }

    CHECK(client.Rejected());
    CHECK_FALSE(client.Connected());
}

TEST_CASE("A client that cannot find the map at all is refused too")
{
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& clientNet = network.AddClient(peer);

    MatchClient client(clientNet,
        [](const std::string&) -> std::optional<LoadedMap> { return std::nullopt; });

    for (int i = 0; i < 20; ++i)
    {
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }

    CHECK(client.Rejected());
    CHECK_FALSE(client.Connected());
}

TEST_CASE("The wire survives 5% loss and 150 ms RTT with jitter")
{
    //The bad-network run. Snapshots are unreliable, so some are simply lost;
    //the next one supersedes them, and the client must end up where the server
    //says regardless.
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    sim.Jitter = FrameClock::FixedStepSeconds;
    sim.Loss = 0.05f;
    sim.Seed = 1;

    SimulatedTransport serverNet(network.Server(), sim);
    PeerId peer = InvalidPeer;
    SimulatedTransport clientNet(network.AddClient(peer), sim);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, serverNet);
    MatchClient client(clientNet, GoodLoader());

    for (int i = 0; i < 300; ++i)
    {
        client.SetInput(Walking(90.0f));
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }

    //Stop moving and let everything drain, so the two must agree exactly.
    for (int i = 0; i < 120; ++i)
    {
        client.SetInput(CharacterInput{});
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }

    REQUIRE(client.Connected());
    CHECK(client.Match().Player(client.LocalPlayer()).Position()
        == server.Match().Player(client.LocalPlayer()).Position());
}

TEST_CASE("A stale snapshot never overwrites a newer one")
{
    //Jitter reorders packets on the unreliable channel. Applying an older
    //snapshot after a newer one would yank the world backwards, which on screen
    //is indistinguishable from a physics fault.
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& raw = network.AddClient(peer);
    MatchClient client(raw, GoodLoader());

    for (int i = 0; i < 10; ++i)
    {
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }
    REQUIRE(client.Connected());

    const std::uint64_t reached = client.Match().Tick();

    //Hand-deliver a snapshot from the past, straight into the client's inbox.
    SnapshotMessage old;
    old.Tick = reached - 5;
    PlayerSnapshot entry;
    entry.Player = client.LocalPlayer();
    entry.Position = glm::vec3(999.0f, 999.0f, 999.0f);
    old.Players.push_back(entry);

    network.Server().Send(peer, Encode(old), Channel::Unreliable);
    client.Step(FrameClock::FixedStepSeconds);

    CHECK(client.Match().Tick() >= reached);
    CHECK(client.Match().Player(client.LocalPlayer()).Position()
        != glm::vec3(999.0f, 999.0f, 999.0f));
}

TEST_CASE("A snapshot naming player zero is dropped rather than thrown on")
{
    //PlayerSnapshot::Player is a raw u16 off the wire and Decode has no reason
    //to reject any value of it, but MatchState::AddPlayer(InvalidPlayer, ...)
    //THROWS - so an unguarded client turns one malformed packet into an
    //uncaught std::invalid_argument. Every other decoder on this wire treats
    //malformed input as routine; the roster application has to as well.
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& raw = network.AddClient(peer);
    MatchClient client(raw, GoodLoader());

    for (int i = 0; i < 10; ++i)
    {
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }
    REQUIRE(client.Connected());

    const PlayerId localPlayer = client.LocalPlayer();
    const std::uint64_t reached = client.Match().Tick();

    //One valid entry and one naming nobody, in a snapshot new enough to be
    //applied. The valid half must land; the bogus half must not be minted and
    //must not take the process down with it.
    SnapshotMessage hostile;
    hostile.Tick = reached + 1;

    PlayerSnapshot nobody;
    nobody.Player = InvalidPlayer;
    nobody.Position = glm::vec3(1.0f, 2.0f, 3.0f);
    hostile.Players.push_back(nobody);

    PlayerSnapshot real;
    real.Player = localPlayer;
    real.Position = glm::vec3(12.0f, 3.0f, 12.0f);
    hostile.Players.push_back(real);

    network.Server().Send(peer, Encode(hostile), Channel::Unreliable);

    CHECK_NOTHROW(client.Step(FrameClock::FixedStepSeconds));

    CHECK_FALSE(client.Match().HasPlayer(InvalidPlayer));
    REQUIRE(client.Match().HasPlayer(localPlayer));
    CHECK(client.Match().Player(localPlayer).Position() == glm::vec3(12.0f, 3.0f, 12.0f));

    //And the bogus entry must not have counted as "present", or the real
    //player would be culled as departed on the next snapshot.
    CHECK(client.Match().Players().size() == 1);
}

TEST_CASE("A client the server drops at the handshake knows it was refused")
{
    //MatchServer refuses a wrong protocol version by calling
    //Transport::Disconnect and sending NOTHING - see the version-mismatch case
    //in HandleMessage, and MatchServerTests' "A client speaking the wrong
    //protocol version is disconnected, not tolerated", which asserts the server
    //half of it. This is the client half, and it was missing: a bare
    //Disconnected is all the refused end ever sees, so a client that only
    //cleared m_Connected would sit unwelcomed for ever with Rejected() false
    //and nothing on screen to say why.
    //
    //Driven through the transport rather than through a mismatched
    //ProtocolVersion because MatchClient always speaks the current one, and
    //that costs nothing: the ejection MatchServer performs IS one
    //Transport::Disconnect and no reply, which is exactly what happens here.
    //An unreachable server reaches the client identically - ENet's connect
    //attempt gives up and raises the same event with no reply attached.
    LoopbackNetwork network;

    PeerId peer = InvalidPeer;
    Transport& clientNet = network.AddClient(peer);
    MatchClient client(clientNet, GoodLoader());

    //One step to drain the Connected event and put a Hello on the wire, so the
    //drop lands on a client that is mid-handshake rather than one that has not
    //started.
    client.Step(FrameClock::FixedStepSeconds);
    REQUIRE_FALSE(client.Connected());
    REQUIRE_FALSE(client.Rejected());

    network.Server().Disconnect(peer);
    client.Step(FrameClock::FixedStepSeconds);

    CHECK(client.Rejected());
    CHECK_FALSE(client.Connected());
}

TEST_CASE("A session that ends after the welcome is not a refused handshake")
{
    //The other side of the branch above, and the reason it is a branch at all.
    //Rejected() is documented as a HANDSHAKE failure and is terminal; a server
    //going away mid-match is a session ending, and reporting that as a refusal
    //would tell a player their build or their map is wrong when neither is.
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& clientNet = network.AddClient(peer);
    MatchClient client(clientNet, GoodLoader());

    for (int i = 0; i < 10; ++i)
    {
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }
    REQUIRE(client.Connected());
    REQUIRE_FALSE(client.Rejected());

    network.RemoveClient(peer);
    client.Step(FrameClock::FixedStepSeconds);

    CHECK_FALSE(client.Connected());
    CHECK_FALSE(client.Rejected());
}
