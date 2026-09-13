#include <doctest.h>

#include "Cubit/FrameClock.h"
#include "Cubit/Net/LoopbackTransport.h"
#include "Cubit/Net/MatchClient.h"
#include "Cubit/Net/MatchServer.h"
#include "Cubit/Net/Protocol.h"
#include "Cubit/Net/SimulatedTransport.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <optional>
#include <string>

namespace
{
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
    theirs.Edit = BlockEdit{ cell, BlockId{ 3 } };
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
