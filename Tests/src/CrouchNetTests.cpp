#include <doctest.h>

#include "Cubit/FrameClock.h"
#include "Cubit/Net/LoopbackTransport.h"
#include "Cubit/Net/MatchClient.h"
#include "Cubit/Net/MatchServer.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <optional>
#include <string>

//Crouching across the wire: the server steps the smaller box, remembers each
//tick's box for the rewind, and tells every client, so the one crouching
//reconciles against it and the others draw it.
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

    void StepAll(MatchClient& a, MatchClient& b, MatchServer& server, bool aCrouches)
    {
        CharacterInput input;
        input.Crouch = aCrouches;
        a.SetInput(input);
        a.Step(FrameClock::FixedStepSeconds);

        b.SetInput(CharacterInput{});
        b.Step(FrameClock::FixedStepSeconds);

        server.Step(FrameClock::FixedStepSeconds);
    }
}

TEST_CASE("A crouch reaches the server, its rewind, and the other client")
{
    LoopbackNetwork network;
    PeerId peerA = InvalidPeer;
    Transport& rawA = network.AddClient(peerA);
    PeerId peerB = InvalidPeer;
    Transport& rawB = network.AddClient(peerB);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, network.Server());
    MatchClient a(rawA, GoodLoader());
    MatchClient b(rawB, GoodLoader());

    for (int i = 0; i < 120; ++i)
        StepAll(a, b, server, false);
    REQUIRE(a.Connected());
    REQUIRE(b.Connected());

    const PlayerId crouching = a.LocalPlayer();
    const std::uint64_t standingTick = server.Match().Tick();

    Aabb standing;
    REQUIRE(server.History().BoxAt(crouching, static_cast<double>(standingTick), standing));
    CHECK(standing.Max.y - standing.Min.y == doctest::Approx(1.8f));

    for (int i = 0; i < 10; ++i)
        StepAll(a, b, server, true);

    //The server stepped the crouched box.
    CHECK(server.Match().Player(crouching).Crouched());

    //Its rewind at the tick it last saw them standing is still standing height:
    //a shot aimed then is judged against the box they had then.
    Aabb then;
    REQUIRE(server.History().BoxAt(crouching, static_cast<double>(standingTick), then));
    CHECK(then.Max.y - then.Min.y == doctest::Approx(1.8f));

    Aabb now;
    REQUIRE(server.History().BoxAt(crouching, static_cast<double>(server.Match().Tick()), now));
    CHECK(now.Max.y - now.Min.y == doctest::Approx(1.2f));
    CHECK(now.Min.y == doctest::Approx(then.Min.y));

    //The crouching client agrees with the server without correcting.
    CHECK(a.Match().Player(crouching).Crouched());
    CHECK(a.Corrections().Count == 0);

    //And the other client draws them crouched.
    CHECK(b.PoseOf(crouching, 1.0f).Crouch == doctest::Approx(1.0f));
    CHECK(b.Match().Player(crouching).Crouched());

    //Standing up again reaches everyone the same way.
    for (int i = 0; i < 10; ++i)
        StepAll(a, b, server, false);

    CHECK_FALSE(server.Match().Player(crouching).Crouched());
    CHECK(b.PoseOf(crouching, 1.0f).Crouch == doctest::Approx(0.0f));
}
