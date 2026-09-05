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
    //stepped. NOT the same number as LatencyTicks - see the comment on the
    //skew assertion for where it comes from.
    constexpr std::uint64_t MinSkew = LatencyTicks + 1;
    constexpr std::uint64_t MaxSkew = LatencyTicks + 1;

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

TEST_CASE("A client's view of a remote player is the server's, delayed by the one-way latency")
{
    //STAGE 2'S ORACLE, NARROWED TO WHAT IS STILL TRUE. It used to cover every
    //player, including this client's own. Prediction makes the local player
    //deliberately ahead of the server, so asserting it here would be asserting
    //the stage had not happened. Remote players are still written straight from
    //snapshots and must still match the server exactly, offset by flight time.
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    SimulatedTransport serverNet(network.Server(), sim);

    PeerId firstPeer = InvalidPeer;
    PeerId secondPeer = InvalidPeer;
    SimulatedTransport firstNet(network.AddClient(firstPeer), sim);
    SimulatedTransport secondNet(network.AddClient(secondPeer), sim);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, serverNet);
    MatchClient walker(firstNet, GoodLoader());
    MatchClient watcher(secondNet, GoodLoader());

    //Server tick -> where the walker stood at the end of it.
    std::map<std::uint64_t, glm::vec3> history;
    std::vector<std::uint64_t> observedSkew;

    for (int i = 0; i < 400; ++i)
    {
        //ORDER MATTERS AND IS PART OF THE ASSERTION. The clients send and
        //apply first, then the server receives and steps. If the skew below
        //leaves its bound, do NOT widen it - confirm this loop order first,
        //because an unexpected offset means a packet is being serviced in the
        //wrong phase.
        walker.SetInput(Walking(90.0f));
        watcher.SetInput(CharacterInput{});
        walker.Step(FrameClock::FixedStepSeconds);
        watcher.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);

        if (server.Match().HasPlayer(walker.LocalPlayer()))
            history[server.Match().Tick()] = server.Match().Player(walker.LocalPlayer()).Position();

        if (!watcher.Connected() || !watcher.Match().HasPlayer(walker.LocalPlayer()))
            continue;

        //ASSERTION ONE: at whatever server tick the watcher last heard about,
        //its picture of the walker must be the server's picture at that exact
        //tick. Exact equality, not Approx: these are the same floats
        //round-tripped through the codec, not two computations of one number.
        const auto recorded = history.find(watcher.ServerTick());
        if (recorded != history.end())
            CHECK(watcher.Match().Player(walker.LocalPlayer()).Position() == recorded->second);

        if (i > 60)
            observedSkew.push_back(server.Match().Tick() - watcher.ServerTick());
    }

    REQUIRE_FALSE(observedSkew.empty());

    //ASSERTION TWO: the delay is BOUNDED. See the note where MinSkew and
    //MaxSkew are defined for where the constant comes from and why it is not
    //simply the latency.
    for (const std::uint64_t skew : observedSkew)
    {
        CHECK(skew >= MinSkew);
        CHECK(skew <= MaxSkew);
    }
}

TEST_CASE("The client steps its own player and nobody else's")
{
    //The replacement for Stage 2's "the client never steps the simulation
    //itself", which this stage deliberately makes false. It is replaced rather
    //than deleted so the record survives that the old constraint was a choice:
    //the client now steps, and what must still be true is that it steps only
    //itself. A client that predicted a remote would simulate them under gravity
    //between snapshots and then stamp over the result, which is exactly the
    //stepping artifact 60 Hz snapshots were chosen to avoid.
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
        second.SetInput(CharacterInput{});
        first.Step(FrameClock::FixedStepSeconds);
        second.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }

    REQUIRE(first.Connected());
    REQUIRE(second.Connected());
    REQUIRE(first.Match().HasPlayer(second.LocalPlayer()));

    //Stop the server and let everything in flight drain, so nothing arrives
    //from now on and the only motion left is what this client produces itself.
    for (int i = 0; i < 30; ++i)
    {
        first.SetInput(Walking(90.0f));
        first.Step(FrameClock::FixedStepSeconds);
    }

    const glm::vec3 mineBefore = first.Match().Player(first.LocalPlayer()).Position();
    const glm::vec3 theirsBefore = first.Match().Player(second.LocalPlayer()).Position();

    for (int i = 0; i < 120; ++i)
    {
        first.SetInput(Walking(90.0f));
        first.Step(FrameClock::FixedStepSeconds);

        //Not one millimetre. A remote is never predicted and never
        //extrapolated: with nothing arriving, there is nothing to say about
        //where they are, and guessing is a wrong answer that has to be taken
        //back.
        CHECK(first.Match().Player(second.LocalPlayer()).Position() == theirsBefore);
    }

    //And the local player kept walking with no server at all. This is the half
    //of the assertion that fails on Stage 2's code.
    CHECK(first.Match().Player(first.LocalPlayer()).Position() != mineBefore);
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

    //200 ticks, not 300: this test predates prediction, when 5% loss quietly
    //cost a fraction of the walk and happened to leave the character short of
    //FlatWorld's far edge (32 blocks from a spawn at z=8) by the time it went
    //idle. Bundled resends (this task) recover most of that lost input, so the
    //same 300 ticks now walk far enough to leave the 32x32 floor entirely and
    //free-fall - reproducibly, even against unmodified Stage 2 code with loss
    //set to 0, so this was never a property of prediction. 200 ticks keeps the
    //walk (and the loss/jitter it is meant to exercise) comfortably clear of
    //the edge in every case.
    for (int i = 0; i < 200; ++i)
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

    //Exact equality was Stage 2's assertion and cannot hold here: the deadzone
    //deliberately leaves a sub-threshold disagreement uncorrected, because
    //showing a two-centimetre correction is worse than carrying it. What must
    //hold is that the disagreement is BOUNDED by that threshold - past it, the
    //client snaps - which is the property the deadzone is only acceptable
    //because of.
    CHECK(glm::distance(client.Match().Player(client.LocalPlayer()).Position(),
        server.Match().Player(client.LocalPlayer()).Position()) <= CorrectionThreshold);
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

    //ServerTick(), not Match().Tick(): this test never calls SetInput, so
    //under prediction the client's own tick never leaves the value Welcome
    //set it to - it only advances inside the predicted-step path, which is
    //gated on having an input to step with. The staleness this test is
    //pinning belongs to the snapshot pipeline, which ServerTick() tracks.
    const std::uint64_t reached = client.ServerTick();

    //Hand-deliver a snapshot from the past, straight into the client's inbox.
    SnapshotMessage old;
    old.Tick = reached - 5;
    PlayerSnapshot entry;
    entry.Player = client.LocalPlayer();
    entry.Position = glm::vec3(999.0f, 999.0f, 999.0f);
    old.Players.push_back(entry);

    network.Server().Send(peer, Encode(old), Channel::Unreliable);
    client.Step(FrameClock::FixedStepSeconds);

    CHECK(client.ServerTick() >= reached);
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

    //ServerTick(), not Match().Tick(): this test never calls SetInput, so the
    //client's own predicted tick never leaves Welcome's starting value. "New
    //enough to be applied" is judged against the snapshot pipeline's own
    //notion of freshness, which is ServerTick().
    const std::uint64_t reached = client.ServerTick();

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

TEST_CASE("Being welcomed is not the same as being in the roster")
{
    //A CHARACTERISATION TEST, and it says so rather than pretending otherwise.
    //
    //Connected() goes true when Welcome is accepted, but Welcome carries no
    //position - the local character comes into existence only when the first
    //SNAPSHOT naming it is applied. MatchServer sends both inside one Step, so
    //they normally arrive together and Connected() looks like it implies a
    //roster entry. It does not: the welcome is reliable and the snapshot is
    //not, so one drop or one reorder opens a window where the client is
    //connected and its own player does not exist.
    //
    //This is what makes the Sandbox's HaveLocalPlayer guard necessary rather
    //than defensive: Player_() runs on a fixed-step callback, and MatchState
    //throws for an absent id. Nothing in this suite could catch that, because
    //Sandbox.exe is not linked into Tests - so the window itself is pinned
    //here, where it can be seen, instead of only in a comment over there.
    //
    //Hand-delivering the welcome alone IS the lost-snapshot case with the loss
    //made deterministic; there is nothing else in it.
    LoopbackNetwork network;

    PeerId peer = InvalidPeer;
    Transport& clientNet = network.AddClient(peer);
    MatchClient client(clientNet, GoodLoader());

    client.Step(FrameClock::FixedStepSeconds);

    WelcomeMessage welcome;
    welcome.You = 7;
    welcome.MapName = "flat.vox";
    welcome.MapHash = MapHash;
    welcome.Tick = 100;
    network.Server().Send(peer, Encode(welcome), Channel::Reliable);

    client.Step(FrameClock::FixedStepSeconds);

    REQUIRE(client.Connected());
    REQUIRE(client.LocalPlayer() == PlayerId{ 7 });

    //The window. A caller reading Connected() and then Player_() lands here.
    CHECK_FALSE(client.Match().HasPlayer(client.LocalPlayer()));
    CHECK_THROWS(client.Match().Player(client.LocalPlayer()));

    //And it closes on the first snapshot that names the player, not before.
    SnapshotMessage snapshot;
    snapshot.Tick = 101;
    PlayerSnapshot entry;
    entry.Player = 7;
    entry.Position = glm::vec3(1.0f, 2.0f, 3.0f);
    snapshot.Players.push_back(entry);

    network.Server().Send(peer, Encode(snapshot), Channel::Unreliable);
    client.Step(FrameClock::FixedStepSeconds);

    CHECK(client.Match().HasPlayer(client.LocalPlayer()));
    CHECK(client.Match().Player(client.LocalPlayer()).Position() == glm::vec3(1.0f, 2.0f, 3.0f));
}
