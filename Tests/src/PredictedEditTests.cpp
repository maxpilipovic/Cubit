#include <doctest.h>

#include "Cubit/FrameClock.h"
#include "Cubit/Net/LoopbackTransport.h"
#include "Cubit/Net/MatchClient.h"
#include "Cubit/Net/MatchServer.h"
#include "Cubit/Net/Protocol.h"
#include "Cubit/Net/SimulatedTransport.h"
#include "Cubit/Voxel/CharacterController.h"

#include <glm/glm.hpp>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace
{
    //The engine's own placeholder rules. Tests ask about mechanisms, not about
    //a game's tuning, so they all read one instance rather than repeating
    //numbers that now live in the game.
    constexpr MatchRules TestRules{};

    constexpr std::uint64_t MapHash = 0xFEEDFACEull;
    const glm::vec3 Spawn{ 8.0f, 2.0f, 8.0f };

    //166.7 ms RTT: five ticks each way, the figure every gate in this stage uses.
    constexpr double OneWayLatency = 5 * FrameClock::FixedStepSeconds;

    World FlatWorld()
    {
        World world(2, 2, 2);

        for (int z = 0; z < world.GetDepth(); ++z)
            for (int x = 0; x < world.GetWidth(); ++x)
                world.SetBlock(x, 0, z, BlockId{ 1 });

        return world;
    }

    MatchClient::MapLoader GoodLoader()
    {
        return [](const std::string&) -> std::optional<LoadedMap>
        {
            return LoadedMap{ FlatWorld(), MapHash };
        };
    }

    void StepBoth(MatchClient& client, MatchServer& server)
    {
        client.SetInput(CharacterInput{});
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }

    //Connected, and standing: the local player exists (a snapshot has named
    //it) and has landed.
    void ConnectAndSettle(MatchClient& client, MatchServer& server)
    {
        for (int i = 0; i < 200; ++i)
        {
            StepBoth(client, server);

            if (client.Connected() && client.Match().HasPlayer(client.LocalPlayer())
                && client.Match().Player(client.LocalPlayer()).Grounded() && i > 60)
                return;
        }
    }

    BlockId ClientBlock(const MatchClient& client, const glm::ivec3& at)
    {
        return client.Match().GetWorld().GetBlock(at.x, at.y, at.z);
    }

    BlockId ServerBlock(const MatchServer& server, const glm::ivec3& at)
    {
        return server.Match().GetWorld().GetBlock(at.x, at.y, at.z);
    }

    //A world tall enough to pillar thirty blocks: 32 x 64 x 32, floor at y = 0.
    World TallWorld()
    {
        World world(2, 4, 2);

        for (int z = 0; z < world.GetDepth(); ++z)
            for (int x = 0; x < world.GetWidth(); ++x)
                world.SetBlock(x, 0, z, BlockId{ 1 });

        return world;
    }

    MatchClient::MapLoader TallLoader()
    {
        return [](const std::string&) -> std::optional<LoadedMap>
        {
            return LoadedMap{ TallWorld(), MapHash };
        };
    }

    //A solid 4 x 4 column, 21 blocks deep, centred under the spawn corner.
    //Spawn (8, 2, 8) is a block corner, so the player stands on the four cells
    //x, z in {7, 8} and a dig has to take all four to drop them.
    constexpr int ColumnTop = 20;
    const glm::vec3 ColumnSpawn{ 8.0f, 23.0f, 8.0f };

    World ColumnWorld()
    {
        World world(2, 2, 2);

        for (int y = 0; y <= ColumnTop; ++y)
            for (int z = 6; z <= 9; ++z)
                for (int x = 6; x <= 9; ++x)
                    world.SetBlock(x, y, z, BlockId{ 1 });

        return world;
    }

    MatchClient::MapLoader ColumnLoader()
    {
        return [](const std::string&) -> std::optional<LoadedMap>
        {
            return LoadedMap{ ColumnWorld(), MapHash };
        };
    }

    //The feet of the client's own predicted player - what the person playing
    //sees, and so what they react to.
    float PredictedFeet(const MatchClient& client)
    {
        const CharacterController& self = client.Match().Player(client.LocalPlayer());
        return self.Position().y - self.Config().HalfExtents.y;
    }

    struct PillarOutcome
    {
        int Placed = 0;
        std::uint64_t Corrections = 0;
        float MaxCorrection = 0.0f;
        bool ServerHasPillar = false;
        bool ClientHasPillar = false;
        std::size_t PendingAfter = 0;
    };

    //Hold jump and fill the cell under the predicted feet each time they clear
    //it, `height` blocks up, over a link shaped by `sim`.
    PillarOutcome RunPillar(const NetworkSim& sim, int height)
    {
        LoopbackNetwork network;
        SimulatedTransport serverNet(network.Server(), sim);

        PeerId peer = InvalidPeer;
        SimulatedTransport clientNet(network.AddClient(peer), sim);

        MatchServer server(TallWorld(), "tall.vox", MapHash, Spawn, serverNet);
        MatchClient client(clientNet, TallLoader());

        ConnectAndSettle(client, server);
        REQUIRE(client.Connected());

        const std::uint64_t correctionsBefore = client.Corrections().Count;

        CharacterInput jumping;
        jumping.Jump = true;

        PillarOutcome outcome;

        for (int tick = 0; tick < height * 60 && outcome.Placed < height; ++tick)
        {
            if (PredictedFeet(client) > static_cast<float>(outcome.Placed + 2))
            {
                ++outcome.Placed;
                client.RequestEdit(BlockEdit{ glm::ivec3(8, outcome.Placed, 8), BlockId{ 2 } });
            }

            client.SetInput(jumping);
            client.Step(FrameClock::FixedStepSeconds);
            server.Step(FrameClock::FixedStepSeconds);
        }

        for (int i = 0; i < 120; ++i)
            StepBoth(client, server);

        outcome.ServerHasPillar = true;
        outcome.ClientHasPillar = true;
        for (int y = 1; y <= outcome.Placed; ++y)
        {
            outcome.ServerHasPillar = outcome.ServerHasPillar && ServerBlock(server, glm::ivec3(8, y, 8)) == BlockId{ 2 };
            outcome.ClientHasPillar = outcome.ClientHasPillar && ClientBlock(client, glm::ivec3(8, y, 8)) == BlockId{ 2 };
        }

        outcome.Corrections = client.Corrections().Count - correctionsBefore;
        outcome.MaxCorrection = client.Corrections().Max;
        outcome.PendingAfter = client.PendingEditCount();
        return outcome;
    }
}

TEST_CASE("A client's own edit shows on the step it is made, before the server has heard of it")
{
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    SimulatedTransport serverNet(network.Server(), sim);

    PeerId peer = InvalidPeer;
    SimulatedTransport clientNet(network.AddClient(peer), sim);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, serverNet);
    MatchClient client(clientNet, GoodLoader());

    ConnectAndSettle(client, server);
    REQUIRE(client.Connected());

    const glm::ivec3 cell(4, 0, 4);
    client.RequestEdit(BlockEdit{ cell, BlockId{ 0 } });

    //One client step, no server step.
    client.SetInput(CharacterInput{});
    client.Step(FrameClock::FixedStepSeconds);

    CHECK(ClientBlock(client, cell) == BlockId{ 0 });
    CHECK(ServerBlock(server, cell) == BlockId{ 1 });
    CHECK(client.PendingEditCount() == 1);

    for (int i = 0; i < 60 && client.PendingEditCount() > 0; ++i)
        StepBoth(client, server);

    CHECK(client.PendingEditCount() == 0);
    CHECK(ServerBlock(server, cell) == BlockId{ 0 });
    CHECK(ClientBlock(client, cell) == BlockId{ 0 });
}

TEST_CASE("An edit the rules forbid is never predicted and never sent")
{
    LoopbackNetwork network;
    PeerId peer = InvalidPeer;
    Transport& raw = network.AddClient(peer);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, network.Server());
    MatchClient client(raw, GoodLoader());

    ConnectAndSettle(client, server);
    REQUIRE(client.Connected());

    //Beyond reach, then into the player's own legs.
    const glm::ivec3 far(31, 0, 31);
    const glm::ivec3 legs(8, 1, 8);

    client.RequestEdit(BlockEdit{ far, BlockId{ 0 } });
    StepBoth(client, server);
    client.RequestEdit(BlockEdit{ legs, BlockId{ 2 } });
    StepBoth(client, server);

    CHECK(ClientBlock(client, far) == BlockId{ 1 });
    CHECK(ClientBlock(client, legs) == BlockId{ 0 });
    CHECK(client.PendingEditCount() == 0);

    for (int i = 0; i < 20; ++i)
        StepBoth(client, server);

    CHECK(server.EditLog().empty());
}

TEST_CASE("A server change beneath a pending prediction does not show until the prediction resolves")
{
    //THE CONFIRMED LAYER. The server's messages are hand-built and the server is
    //not stepped after the edit, so the order in which things reach the client
    //is exactly the order this test says.
    LoopbackNetwork network;
    PeerId peer = InvalidPeer;
    Transport& raw = network.AddClient(peer);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, network.Server());
    MatchClient client(raw, GoodLoader());

    ConnectAndSettle(client, server);
    REQUIRE(client.Connected());

    const glm::ivec3 cell(4, 1, 4);
    client.RequestEdit(BlockEdit{ cell, BlockId{ 2 } });
    client.SetInput(CharacterInput{});
    client.Step(FrameClock::FixedStepSeconds);

    const std::uint64_t predictedTick = client.Match().Tick();
    REQUIRE(ClientBlock(client, cell) == BlockId{ 2 });

    //Somebody else's edit to the same cell reaches this client first.
    EditMessage theirs;
    theirs.Edits.push_back(BlockEdit{ cell, BlockId{ 3 } });
    network.Server().Send(peer, EncodeEditApplied(theirs), Channel::Reliable);

    client.SetInput(CharacterInput{});
    client.Step(FrameClock::FixedStepSeconds);

    //Still this client's prediction on screen.
    CHECK(ClientBlock(client, cell) == BlockId{ 2 });

    //Then the server refuses the prediction: the cell is theirs.
    EditResultMessage refused;
    refused.ClientTick = predictedTick;
    refused.Accepted = false;
    refused.Edit = BlockEdit{ cell, BlockId{ 3 } };
    network.Server().Send(peer, Encode(refused), Channel::Reliable);

    client.SetInput(CharacterInput{});
    client.Step(FrameClock::FixedStepSeconds);

    CHECK(ClientBlock(client, cell) == BlockId{ 3 });
    CHECK(client.PendingEditCount() == 0);
}

TEST_CASE("A quick place-then-break never shows the placed block again")
{
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    SimulatedTransport serverNet(network.Server(), sim);

    PeerId peer = InvalidPeer;
    SimulatedTransport clientNet(network.AddClient(peer), sim);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, serverNet);
    MatchClient client(clientNet, GoodLoader());

    ConnectAndSettle(client, server);
    REQUIRE(client.Connected());

    const glm::ivec3 cell(4, 1, 4);
    client.RequestEdit(BlockEdit{ cell, BlockId{ 2 } });
    StepBoth(client, server);
    REQUIRE(ClientBlock(client, cell) == BlockId{ 2 });

    client.RequestEdit(BlockEdit{ cell, BlockId{ 0 } });
    StepBoth(client, server);
    REQUIRE(ClientBlock(client, cell) == BlockId{ 0 });

    //The placement's accepted result arrives while the break is still pending.
    //Applied straight to the world, it would put the block back for a round
    //trip.
    int steps = 0;
    for (; steps < 80 && client.PendingEditCount() > 0; ++steps)
    {
        StepBoth(client, server);
        CAPTURE(steps);
        CHECK(ClientBlock(client, cell) == BlockId{ 0 });
    }

    CHECK(client.PendingEditCount() == 0);
    CHECK(ServerBlock(server, cell) == BlockId{ 0 });
}

TEST_CASE("Pillar-jumping at 166.7 ms costs no corrections")
{
    //THE STAGE'S GATE. Hold jump, and each time the predicted feet clear the
    //top of the next cell up, place a block in it - the way a person pillars.
    //Every edit is legal, so the bar is exactly zero.
    //
    //The oracle is the correction count the player would see, plus the server's
    //world: a pillar that never reached the server would also cost no
    //corrections.
    NetworkSim sim;
    sim.Latency = OneWayLatency;

    const PillarOutcome outcome = RunPillar(sim, 30);

    //Reported before anything is asserted, so a short pillar still says how
    //far it got and what it cost.
    MESSAGE("pillar 30 blocks at 166.7 ms: placed " << outcome.Placed << ", corrections "
        << outcome.Corrections << ", max " << outcome.MaxCorrection);

    REQUIRE(outcome.Placed == 30);
    CHECK(outcome.ServerHasPillar);
    CHECK(outcome.ClientHasPillar);
    CHECK(outcome.PendingAfter == 0);
    CHECK(outcome.Corrections == 0);
}

TEST_CASE("Digging straight down at 166.7 ms costs no corrections")
{
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    SimulatedTransport serverNet(network.Server(), sim);

    PeerId peer = InvalidPeer;
    SimulatedTransport clientNet(network.AddClient(peer), sim);

    MatchServer server(ColumnWorld(), "column.vox", MapHash, ColumnSpawn, serverNet);
    MatchClient client(clientNet, ColumnLoader());

    ConnectAndSettle(client, server);
    REQUIRE(client.Connected());
    REQUIRE(client.Match().Player(client.LocalPlayer()).Grounded());

    const std::uint64_t correctionsBefore = client.Corrections().Count;
    const float startFeet = PredictedFeet(client);

    constexpr int Levels = 10;
    int dug = 0;
    int requestedLevel = -1;

    for (int tick = 0; tick < Levels * 120 && dug < Levels; ++tick)
    {
        const CharacterController& self = client.Match().Player(client.LocalPlayer());

        //Standing: take the four cells underfoot, one per tick through the queue.
        if (self.Grounded())
        {
            const int level = static_cast<int>(std::floor(PredictedFeet(client) + 0.01f)) - 1;

            if (level != requestedLevel && level > 0)
            {
                requestedLevel = level;
                for (const glm::ivec2 xz : { glm::ivec2(7, 7), glm::ivec2(7, 8), glm::ivec2(8, 7), glm::ivec2(8, 8) })
                    client.RequestEdit(BlockEdit{ glm::ivec3(xz.x, level, xz.y), BlockId{ 0 } });
                ++dug;
            }
        }

        StepBoth(client, server);
    }

    for (int i = 0; i < 120; ++i)
        StepBoth(client, server);

    REQUIRE(dug == Levels);
    CHECK(PredictedFeet(client) <= startFeet - static_cast<float>(Levels) + 0.01f);
    CHECK(client.PendingEditCount() == 0);

    const MatchClient::CorrectionStats stats = client.Corrections();
    MESSAGE("dig " << Levels << " levels at 166.7 ms: corrections " << (stats.Count - correctionsBefore)
        << ", max " << stats.Max);
    CHECK(stats.Count - correctionsBefore == 0);
}

TEST_CASE("Replaying a pending edit remeshes nothing")
{
    //Replay undoes and redoes pending edits on every snapshot. Through the
    //ordinary write path that would mark chunks dirty sixty times a second.
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    SimulatedTransport serverNet(network.Server(), sim);

    PeerId peer = InvalidPeer;
    SimulatedTransport clientNet(network.AddClient(peer), sim);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, serverNet);
    MatchClient client(clientNet, GoodLoader());

    ConnectAndSettle(client, server);
    REQUIRE(client.Connected());

    client.RequestEdit(BlockEdit{ glm::ivec3(4, 0, 4), BlockId{ 0 } });
    StepBoth(client, server);
    REQUIRE(client.PendingEditCount() == 1);

    //The prediction itself remeshed, once. Everything from here is replay.
    client.MatchForWrite().GetWorld().ClearDirty();
    const std::uint64_t snapshotsBefore = client.Corrections().Snapshots;

    for (int i = 0; i < 60 && client.PendingEditCount() > 0; ++i)
    {
        StepBoth(client, server);
        CAPTURE(i);
        CHECK(client.Match().GetWorld().DirtyChunks().empty());
    }

    CHECK(client.PendingEditCount() == 0);

    //Replay actually ran while the edit was pending, or this proved nothing.
    CHECK(client.Corrections().Snapshots >= snapshotsBefore + 5);
}

TEST_CASE("An edit that a correction puts out of reach stops showing, and the server's answer decides")
{
    //Replay re-checks the editor's own conditions. A snapshot that moves the
    //player three blocks back puts a cell at the edge of reach out of it, so
    //the replayed edit is withdrawn: this client stops showing a block it can
    //no longer justify, and the server's EditResult settles the cell.
    LoopbackNetwork network;
    PeerId peer = InvalidPeer;
    Transport& raw = network.AddClient(peer);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, network.Server());
    MatchClient client(raw, GoodLoader());

    ConnectAndSettle(client, server);
    REQUIRE(client.Connected());

    //About 11.1 from the spawn eye, inside reach.
    const glm::ivec3 edge(19, 0, 8);
    client.RequestEdit(BlockEdit{ edge, BlockId{ 0 } });
    client.SetInput(CharacterInput{});
    client.Step(FrameClock::FixedStepSeconds);

    const std::uint64_t predictedTick = client.Match().Tick();
    REQUIRE(ClientBlock(client, edge) == BlockId{ 0 });

    //The server says the player is at x = 5, and has not yet applied the tick
    //the edit rode on. From x = 5 the cell is about 14 away.
    PlayerSnapshot mine;
    mine.Player = client.LocalPlayer();
    mine.Position = glm::vec3(5.0f, client.Match().Player(client.LocalPlayer()).Position().y, 8.0f);
    mine.Grounded = true;
    mine.LastInputTick = predictedTick - 1;
    mine.Health = TestRules.StartingHealth;

    SnapshotMessage snapshot;
    snapshot.Tick = server.Match().Tick() + 100;
    snapshot.Players = { mine };
    network.Server().Send(peer, Encode(snapshot), Channel::Unreliable);

    client.MatchForWrite().GetWorld().ClearDirty();
    client.SetInput(CharacterInput{});
    client.Step(FrameClock::FixedStepSeconds);

    CHECK(ClientBlock(client, edge) == BlockId{ 1 });
    CHECK(client.PendingEditCount() == 1);

    //Withdrawn is a real change on screen, so it does remesh.
    CHECK_FALSE(client.Match().GetWorld().DirtyChunks().empty());

    //The server accepted it anyway - from where it believed the player stood.
    EditResultMessage accepted;
    accepted.ClientTick = predictedTick;
    accepted.Accepted = true;
    accepted.Edit = BlockEdit{ edge, BlockId{ 0 } };
    network.Server().Send(peer, Encode(accepted), Channel::Reliable);

    client.SetInput(CharacterInput{});
    client.Step(FrameClock::FixedStepSeconds);

    CHECK(ClientBlock(client, edge) == BlockId{ 0 });
    CHECK(client.PendingEditCount() == 0);
}

TEST_CASE("Pillar-jumping under 5% loss and jitter: the correction count, measured")
{
    //THE RECORDED NUMBER UNDER A BAD LINK. The spec expects zero - an edit is
    //lost only when its whole input is, and then the server does not step that
    //tick either - but that is a prediction, and this case exists to find out.
    NetworkSim sim;
    sim.Latency = OneWayLatency;
    sim.Jitter = FrameClock::FixedStepSeconds;
    sim.Loss = 0.05f;
    sim.Seed = 1;

    const PillarOutcome outcome = RunPillar(sim, 30);

    MESSAGE("pillar 30 blocks at 166.7 ms, 5% loss, jitter: corrections " << outcome.Corrections
        << ", max " << outcome.MaxCorrection);

    REQUIRE(outcome.Placed == 30);
    CHECK(outcome.ServerHasPillar);
    CHECK(outcome.ClientHasPillar);
    CHECK(outcome.PendingAfter == 0);

    //Zero, measured rather than hoped for: seeds 1, 2 and 3 each read 0 on
    //2026-09-13. A non-zero count here is a finding to investigate - start
    //with an EditResult erasing a prediction that a delayed older snapshot
    //then replays past - never a threshold to raise.
    CHECK(outcome.Corrections == 0);
}

TEST_CASE("A placement into a player this client has not seen yet is refused and put right")
{
    //The one way honest play mispredicts: the client checks other players at
    //the positions it last saw them, and the server checks where they are. Here
    //the other player has joined on the server but no snapshot has told this
    //client yet, and they stand in the cell being filled.
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    SimulatedTransport serverNet(network.Server(), sim);

    PeerId builderPeer = InvalidPeer;
    SimulatedTransport builderNet(network.AddClient(builderPeer), sim);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, serverNet);
    MatchClient builder(builderNet, GoodLoader());

    ConnectAndSettle(builder, server);
    REQUIRE(builder.Connected());

    //Off the spawn, so the builder's own box is clear of the cell. Yaw 0 faces +x.
    CharacterInput walking;
    walking.Move = glm::vec2(0.0f, 1.0f);
    for (int i = 0; i < 60; ++i)
    {
        builder.SetInput(walking);
        builder.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }
    for (int i = 0; i < 30; ++i)
        StepBoth(builder, server);

    PeerId arrivalPeer = InvalidPeer;
    SimulatedTransport arrivalNet(network.AddClient(arrivalPeer), sim);
    MatchClient arrival(arrivalNet, GoodLoader());

    const glm::ivec3 spawnCell(8, 1, 8);
    bool requested = false;

    for (int i = 0; i < 200; ++i)
    {
        //The moment the server has the arrival and the builder does not.
        if (!requested && server.Match().Players().size() == 2 && builder.Match().Players().size() == 1)
        {
            builder.RequestEdit(BlockEdit{ spawnCell, BlockId{ 2 } });
            requested = true;
        }

        builder.SetInput(CharacterInput{});
        arrival.SetInput(CharacterInput{});
        builder.Step(FrameClock::FixedStepSeconds);
        arrival.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);

        if (requested && builder.PendingEditCount() == 0)
            break;
    }

    REQUIRE(requested);
    CHECK(builder.PendingEditCount() == 0);
    CHECK(ServerBlock(server, spawnCell) == BlockId{ 0 });
    CHECK(ClientBlock(builder, spawnCell) == BlockId{ 0 });
}

namespace
{
    //Holds back every input bundle carrying the first tick that has an edit on
    //it, until released - reordering at its most unlucky, which on an
    //unsequenced channel is rare but allowed.
    class EditTickHoldBack : public Transport
    {
    public:
        explicit EditTickHoldBack(Transport& inner) : m_Inner(inner) {}

        std::size_t HeldCount() const { return m_Held.size(); }

        void Release()
        {
            for (const Held& held : m_Held)
                m_Inner.Send(held.Peer, held.Data, held.Lane);

            m_Held.clear();
            m_Releasing = true;
        }

        void Send(PeerId peer, std::span<const std::uint8_t> data, Channel channel) override
        {
            MessageId id = MessageId::Hello;
            InputMessage input;
            if (!m_Releasing && PeekMessageId(data, id) && id == MessageId::Input && Decode(data, input))
            {
                for (std::size_t i = 0; i < input.Edits.size() && !m_Tick.has_value(); ++i)
                {
                    if (input.Edits[i].has_value())
                        m_Tick = input.FirstTick + i;
                }

                if (m_Tick.has_value() && *m_Tick >= input.FirstTick
                    && *m_Tick < input.FirstTick + input.Inputs.size())
                {
                    m_Held.push_back(Held{ peer, std::vector<std::uint8_t>(data.begin(), data.end()), channel });
                    return;
                }
            }

            m_Inner.Send(peer, data, channel);
        }

        void Broadcast(std::span<const std::uint8_t> data, Channel channel) override
        {
            m_Inner.Broadcast(data, channel);
        }

        void Disconnect(PeerId peer) override { m_Inner.Disconnect(peer); }
        bool Poll(NetEvent& out) override { return m_Inner.Poll(out); }
        void Advance(double seconds) override { m_Inner.Advance(seconds); }
        double RoundTripTime(PeerId peer) const override { return m_Inner.RoundTripTime(peer); }

    private:
        struct Held
        {
            PeerId Peer = InvalidPeer;
            std::vector<std::uint8_t> Data;
            Channel Lane = Channel::Unreliable;
        };

        Transport& m_Inner;
        std::optional<std::uint64_t> m_Tick;
        std::vector<Held> m_Held;
        bool m_Releasing = false;
    };
}

TEST_CASE("An edit whose every bundle arrives after the server moved past its tick is taken back")
{
    //A7's last path, seen from where it matters: the client. The server never
    //had this input, so it never applied the edit - and the client, which
    //predicted it, shows it until it hears otherwise.
    LoopbackNetwork network;

    PeerId peer = InvalidPeer;
    EditTickHoldBack clientNet(network.AddClient(peer));

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, network.Server());
    MatchClient client(clientNet, GoodLoader());

    ConnectAndSettle(client, server);
    REQUIRE(client.Connected());

    const glm::ivec3 cell(4, 0, 4);
    client.RequestEdit(BlockEdit{ cell, BlockId{ 0 } });

    //Three bundles carry the tick; the fourth does not, and the server takes the
    //tick after it. A few more for good measure.
    for (int i = 0; i < 10; ++i)
        StepBoth(client, server);

    //Not vacuous: the edit was predicted, and all three of its bundles are held.
    REQUIRE(clientNet.HeldCount() == InputBundleSize);
    CHECK(ClientBlock(client, cell) == BlockId{ 0 });

    clientNet.Release();
    for (int i = 0; i < 30; ++i)
        StepBoth(client, server);

    CHECK(ServerBlock(server, cell) == BlockId{ 1 });
    CHECK(client.PendingEditCount() == 0);
    CHECK(ClientBlock(client, cell) == BlockId{ 1 });
}

TEST_CASE("An edit on an input dropped from a full queue is taken back on the client")
{
    //A7's done-when, the overflow half, seen from the client. The server does
    //not step while the client makes ten, so ten inputs wait for one step that
    //keeps eight - and the two oldest, the first carrying the edit, are dropped.
    LoopbackNetwork network;

    PeerId peer = InvalidPeer;
    Transport& clientNet = network.AddClient(peer);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, network.Server());
    MatchClient client(clientNet, GoodLoader());

    ConnectAndSettle(client, server);
    REQUIRE(client.Connected());

    const glm::ivec3 cell(4, 0, 4);
    client.RequestEdit(BlockEdit{ cell, BlockId{ 0 } });

    for (int i = 0; i < 10; ++i)
    {
        client.SetInput(CharacterInput{});
        client.Step(FrameClock::FixedStepSeconds);
    }

    //Not vacuous: the edit was predicted.
    REQUIRE(client.PendingEditCount() == 1);
    CHECK(ClientBlock(client, cell) == BlockId{ 0 });

    for (int i = 0; i < 30; ++i)
        StepBoth(client, server);

    CHECK(ServerBlock(server, cell) == BlockId{ 1 });
    CHECK(client.PendingEditCount() == 0);
    CHECK(ClientBlock(client, cell) == BlockId{ 1 });
}

TEST_CASE("A server batch over a pending prediction leaves the client with the server's world")
{
    //Over the real server and a 166.7 ms link, so a rule's batch reaches the
    //server between the client's prediction and the server applying it - the
    //case where a batch lands on a cell the client is still predicting.
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    SimulatedTransport serverNet(network.Server(), sim);

    PeerId peer = InvalidPeer;
    SimulatedTransport clientNet(network.AddClient(peer), sim);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, serverNet);
    MatchClient client(clientNet, GoodLoader());

    ConnectAndSettle(client, server);
    REQUIRE(client.Connected());

    const glm::ivec3 predicted(10, 1, 8);
    client.RequestEdit(BlockEdit{ predicted, BlockId{ 2 } });
    StepBoth(client, server);
    REQUIRE(ClientBlock(client, predicted) == BlockId{ 2 });

    //The batch fills the predicted cell with something else, and digs a hole
    //the client has not touched.
    const glm::ivec3 hole(12, 0, 12);
    const std::vector<BlockEdit> batch{
        BlockEdit{ predicted, BlockId{ 3 } },
        BlockEdit{ hole, BlockId{ 0 } }
    };
    REQUIRE(server.ApplyEdits(batch) == 2);

    for (int i = 0; i < 60; ++i)
        StepBoth(client, server);

    CHECK(client.PendingEditCount() == 0);
    CHECK(ClientBlock(client, predicted) == ServerBlock(server, predicted));
    CHECK(ClientBlock(client, hole) == BlockId{ 0 });
    CHECK(ServerBlock(server, hole) == BlockId{ 0 });
}

TEST_CASE("A client digging out a pillar's base sees the whole pillar go")
{
    //The collapse is the server's alone, so the client learns it the way it
    //learns anybody's edit: it shows a tick or so after its own dig does.
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    SimulatedTransport serverNet(network.Server(), sim);

    PeerId peer = InvalidPeer;
    SimulatedTransport clientNet(network.AddClient(peer), sim);

    World world = FlatWorld();
    for (int y = 1; y <= 5; ++y)
        world.SetBlock(9, y, 9, BlockId{ 1 });

    const MatchClient::MapLoader pillarLoader = [](const std::string&) -> std::optional<LoadedMap>
    {
        World loaded = FlatWorld();
        for (int y = 1; y <= 5; ++y)
            loaded.SetBlock(9, y, 9, BlockId{ 1 });

        return LoadedMap{ std::move(loaded), MapHash };
    };

    MatchServer server(std::move(world), "flat.vox", MapHash, Spawn, serverNet);
    MatchClient client(clientNet, pillarLoader);

    ConnectAndSettle(client, server);
    REQUIRE(client.Connected());

    const std::uint64_t correctionsBefore = client.Corrections().Count;

    const glm::ivec3 base(9, 1, 9);
    client.RequestEdit(BlockEdit{ base, BlockId{ 0 } });
    StepBoth(client, server);

    //Its own dig is predicted; the rest of the pillar is still standing here.
    CHECK(ClientBlock(client, base) == BlockId{ 0 });
    CHECK(ClientBlock(client, glm::ivec3(9, 5, 9)) == BlockId{ 1 });

    for (int i = 0; i < 60; ++i)
        StepBoth(client, server);

    CHECK(client.PendingEditCount() == 0);
    for (int y = 1; y <= 5; ++y)
    {
        CHECK(ServerBlock(server, glm::ivec3(9, y, 9)) == BlockId{ 0 });
        CHECK(ClientBlock(client, glm::ivec3(9, y, 9)) == BlockId{ 0 });
    }

    //A collapse is not a correction: it arrives as edits, not as the server
    //disagreeing about where the player is.
    CHECK(client.Corrections().Count == correctionsBefore);
}
