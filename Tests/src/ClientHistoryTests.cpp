#include <doctest.h>

#include "Cubit/FrameClock.h"
#include "Cubit/Net/LoopbackTransport.h"
#include "Cubit/Net/MatchClient.h"
#include "Cubit/Net/MatchServer.h"
#include "Cubit/Net/Protocol.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <optional>
#include <string>

//What a client has shown, kept in order with serials so that something
//reacting to it - sounds - can take each event exactly once, however many
//arrive in one step. LastShot alone could not: it holds the newest ruling, so
//two drained together were one.
namespace
{
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

    MatchClient::MapLoader GoodLoader()
    {
        return [](const std::string&) -> std::optional<LoadedMap>
        {
            return LoadedMap{ FlatWorld(), MapHash };
        };
    }

    void Step(MatchClient& client)
    {
        client.SetInput(CharacterInput{});
        client.Step(FrameClock::FixedStepSeconds);
    }

    void StepBoth(MatchClient& client, MatchServer& server)
    {
        Step(client);
        server.Step(FrameClock::FixedStepSeconds);
    }

    //Connected and standing, as PredictedEditTests does it.
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

    ShotResolvedMessage Ruling(PlayerId shooter, float x)
    {
        ShotResolvedMessage resolved;
        resolved.Shooter = shooter;
        resolved.Impact = glm::vec3(x, 1.0f, 4.0f);
        return resolved;
    }
}

TEST_CASE("Two rulings arriving in one step are both kept, with consecutive serials")
{
    LoopbackNetwork network;
    PeerId peer = InvalidPeer;
    Transport& raw = network.AddClient(peer);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, network.Server());
    MatchClient client(raw, GoodLoader());

    for (int i = 0; i < 5; ++i)
        StepBoth(client, server);
    REQUIRE(client.Connected());
    CHECK(client.RecentShots().empty());

    network.Server().Send(peer, Encode(Ruling(client.LocalPlayer(), 1.0f)), Channel::Reliable);
    network.Server().Send(peer, Encode(Ruling(client.LocalPlayer(), 2.0f)), Channel::Reliable);
    Step(client);

    REQUIRE(client.RecentShots().size() == 2);
    const MatchClient::ShotReport& first = client.RecentShots()[0];
    const MatchClient::ShotReport& second = client.RecentShots()[1];

    CHECK(first.Impact.x == 1.0f);
    CHECK(second.Impact.x == 2.0f);
    CHECK(first.Serial >= 1);
    CHECK(second.Serial == first.Serial + 1);

    //LastShot is still the newest.
    REQUIRE(client.LastShot().has_value());
    CHECK(client.LastShot()->Serial == second.Serial);
}

TEST_CASE("The shot history is bounded and keeps the newest")
{
    LoopbackNetwork network;
    PeerId peer = InvalidPeer;
    Transport& raw = network.AddClient(peer);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, network.Server());
    MatchClient client(raw, GoodLoader());

    for (int i = 0; i < 5; ++i)
        StepBoth(client, server);
    REQUIRE(client.Connected());

    for (int i = 0; i < 40; ++i)
        network.Server().Send(peer, Encode(Ruling(client.LocalPlayer(), static_cast<float>(i))),
            Channel::Reliable);
    Step(client);

    REQUIRE(client.RecentShots().size() == MaxRecentEvents);
    CHECK(client.RecentShots().back().Impact.x == 39.0f);
    CHECK(client.RecentShots().front().Impact.x == 40.0f - MaxRecentEvents);
    CHECK(client.RecentShots().back().Serial - client.RecentShots().front().Serial
        == MaxRecentEvents - 1);
}

TEST_CASE("A client's own predicted edit is recorded as local, when it is shown")
{
    LoopbackNetwork network;
    PeerId peer = InvalidPeer;
    Transport& raw = network.AddClient(peer);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, network.Server());
    MatchClient client(raw, GoodLoader());

    ConnectAndSettle(client, server);
    REQUIRE(client.Connected());
    CHECK(client.RecentEdits().empty());

    const glm::ivec3 cell(4, 0, 4);
    client.RequestEdit(BlockEdit{ cell, BlockId{ 0 } });

    //Queued, not yet shown.
    CHECK(client.RecentEdits().empty());

    Step(client);

    REQUIRE(client.RecentEdits().size() == 1);
    const MatchClient::ShownEdit& shown = client.RecentEdits().back();
    CHECK(shown.Local);
    CHECK(shown.Serial >= 1);
    REQUIRE(shown.Edits.size() == 1);
    CHECK(shown.Edits[0].Position == cell);
    CHECK(shown.Edits[0].Block == BlockId{ 0 });

    //The server's acceptance does not record it a second time.
    for (int i = 0; i < 30; ++i)
        StepBoth(client, server);

    CHECK(client.PendingEditCount() == 0);
    CHECK(client.RecentEdits().size() == 1);
}

TEST_CASE("An edit the rules refuse is never recorded")
{
    LoopbackNetwork network;
    PeerId peer = InvalidPeer;
    Transport& raw = network.AddClient(peer);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, network.Server());
    MatchClient client(raw, GoodLoader());

    ConnectAndSettle(client, server);
    REQUIRE(client.Connected());

    //Beyond reach.
    client.RequestEdit(BlockEdit{ glm::ivec3(31, 0, 31), BlockId{ 0 } });
    StepBoth(client, server);

    CHECK(client.RecentEdits().empty());
}

TEST_CASE("Another player's edit is recorded as not local, one entry per message")
{
    LoopbackNetwork network;

    PeerId peerA = InvalidPeer;
    Transport& rawA = network.AddClient(peerA);
    PeerId peerB = InvalidPeer;
    Transport& rawB = network.AddClient(peerB);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, network.Server());
    MatchClient a(rawA, GoodLoader());
    MatchClient b(rawB, GoodLoader());

    for (int i = 0; i < 200; ++i)
    {
        Step(a);
        Step(b);
        server.Step(FrameClock::FixedStepSeconds);
    }
    REQUIRE(a.Connected());
    REQUIRE(b.Connected());

    //Next to where both stand, so it is within reach and in nobody's way.
    const glm::ivec3 cell(10, 0, 8);
    b.RequestEdit(BlockEdit{ cell, BlockId{ 0 } });

    for (int i = 0; i < 10; ++i)
    {
        Step(a);
        Step(b);
        server.Step(FrameClock::FixedStepSeconds);
    }

    //B made it, and hears it once, as its own.
    REQUIRE(b.RecentEdits().size() == 1);
    CHECK(b.RecentEdits().back().Local);

    //A was told by the server.
    REQUIRE(a.RecentEdits().size() == 1);
    const MatchClient::ShownEdit seen = a.RecentEdits().back();
    CHECK_FALSE(seen.Local);
    REQUIRE(seen.Edits.size() == 1);
    CHECK(seen.Edits[0].Position == cell);

    //A batch - a collapse - is one entry however many cells it holds.
    EditMessage batch;
    batch.Edits = {
        BlockEdit{ glm::ivec3(2, 0, 2), BlockId{ 0 } },
        BlockEdit{ glm::ivec3(3, 0, 2), BlockId{ 0 } },
        BlockEdit{ glm::ivec3(4, 0, 2), BlockId{ 0 } },
    };
    network.Server().Send(peerA, EncodeEditApplied(batch), Channel::Reliable);
    Step(a);

    REQUIRE(a.RecentEdits().size() == 2);
    CHECK(a.RecentEdits().back().Edits.size() == 3);
    CHECK_FALSE(a.RecentEdits().back().Local);
    CHECK(a.RecentEdits().back().Serial == seen.Serial + 1);
}
