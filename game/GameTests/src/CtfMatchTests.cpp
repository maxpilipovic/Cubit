#include <doctest.h>

#include "CtfMode.h"

#include "Cubit/FrameClock.h"
#include "Cubit/Net/LoopbackTransport.h"
#include "Cubit/Net/MatchClient.h"
#include "Cubit/Net/MatchServer.h"
#include "Cubit/Voxel/TerrainGen.h"

#include <optional>
#include <string>
#include <vector>

using namespace CubitGame;

//The match end to end: the real rules behind a real server, played by two real
//clients over the loopback. What the network-free tests cannot see is what each
//client counts, and a respawn the client takes for a correction is only visible
//here.
namespace
{
    constexpr std::uint64_t MapHash = 0xC0FFEEull;

    World Floor()
    {
        World world(4, 1, 2);   //64 x 16 x 32, the size CtfModeTests uses
        for (int z = 0; z < world.GetDepth(); ++z)
            for (int x = 0; x < world.GetWidth(); ++x)
                world.SetBlock(x, 0, z, BlockId{ 1 });
        return world;
    }

    const glm::ivec3 Size{ 64, 16, 32 };

    MatchClient::MapLoader Loader()
    {
        return [](const std::string&) -> std::optional<LoadedMap>
        {
            return LoadedMap{ Floor(), MapHash };
        };
    }

    //Every stage a few seconds at most, so the whole match runs in a test.
    CtfRules Short()
    {
        CtfRules rules;
        rules.CountdownTicks = 2 * TicksPerSecond;
        rules.TimeLimitTicks = 5 * TicksPerSecond;
        rules.EndScreenTicks = 2 * TicksPerSecond;
        rules.RespawnDelayTicks = 1 * TicksPerSecond;
        return rules;
    }

    struct Rig
    {
        LoopbackNetwork Network;
        CtfMode Mode;
        MatchServer Server;

        Rig() : Mode(Short(), Size, 1),
            Server(Floor(), "floor.vox", MapHash, glm::vec3(32.0f, 2.0f, 16.0f), Network.Server(),
                MatchRules{}, &Mode) {}

        //Standing still unless told otherwise, as GameApp sends while dead.
        void Step(std::vector<MatchClient*> clients, std::uint64_t ticks = 1,
            const CharacterInput& input = CharacterInput{})
        {
            for (std::uint64_t t = 0; t < ticks; ++t)
            {
                for (MatchClient* client : clients)
                {
                    client->SetInput(input);
                    client->Step(FrameClock::FixedStepSeconds);
                }
                Server.Step(FrameClock::FixedStepSeconds);
            }
        }

        //Steps until the stage is reached, or gives up after `limit` ticks.
        bool StepUntil(std::vector<MatchClient*> clients, Phase stage, std::uint64_t limit)
        {
            for (std::uint64_t t = 0; t < limit && Mode.State().Stage != stage; ++t)
                Step(clients);
            return Mode.State().Stage == stage;
        }

        float DistanceFromOwnBase(const MatchClient& client) const
        {
            const PlayerId player = client.LocalPlayer();
            const glm::ivec2 base = TerrainGen::FortCentres(Size)[IndexOf(Mode.State().Find(player)->Side)];
            const glm::vec3 at = client.Match().Player(player).Position();
            return glm::distance(glm::vec2(at.x, at.z), glm::vec2(base) + glm::vec2(0.5f));
        }
    };
}

TEST_CASE("A whole match over the network: no client counts a respawn as a correction")
{
    Rig rig;
    PeerId peerA = InvalidPeer, peerB = InvalidPeer;
    MatchClient a(rig.Network.AddClient(peerA), Loader());
    MatchClient b(rig.Network.AddClient(peerB), Loader());
    const std::vector<MatchClient*> both{ &a, &b };

    //Warmup, both join, the countdown runs out, and the match starts.
    rig.Step(both, 30);
    REQUIRE(a.LocalPlayer() != InvalidPlayer);
    REQUIRE(b.LocalPlayer() != InvalidPlayer);
    REQUIRE(rig.Mode.State().Find(a.LocalPlayer())->Side != rig.Mode.State().Find(b.LocalPlayer())->Side);
    REQUIRE(rig.StepUntil(both, Phase::Countdown, 30));

    //Wandering off during the countdown, so the start has somewhere to move
    //them from. A respawn that moves nobody cannot be taken for a correction.
    CharacterInput walk;
    walk.Move = glm::vec2(0.0f, 1.0f);
    rig.Step(both, TicksPerSecond, walk);
    REQUIRE(rig.Mode.State().Stage == Phase::Countdown);
    CHECK(rig.DistanceFromOwnBase(a) > 2.0f);
    CHECK(rig.DistanceFromOwnBase(b) > 2.0f);

    REQUIRE(rig.StepUntil(both, Phase::Active, Short().CountdownTicks + 5));

    //Everyone is held dead, then appears at base.
    rig.Step(both, 5);
    CHECK_FALSE(a.IsAlive(a.LocalPlayer()));
    rig.Step(both, Short().ResetHoldTicks + 30);
    CHECK(a.IsAlive(a.LocalPlayer()));
    CHECK(b.IsAlive(b.LocalPlayer()));

    //A ruled kill, the way the server makes one, and the mode's respawn.
    const PlayerId victim = a.LocalPlayer();
    rig.Server.Kill(victim);
    rig.Mode.OnKilled(rig.Server, victim, b.LocalPlayer());
    rig.Step(both, 10);
    CHECK_FALSE(a.IsAlive(victim));
    CHECK_FALSE(b.IsAlive(victim));
    rig.Step(both, Short().RespawnDelayTicks + 30);
    CHECK(a.IsAlive(victim));
    CHECK(b.IsAlive(victim));
    CHECK(rig.Mode.State().Find(victim)->Deaths == 1);

    //Off base again before the end, for the reset to bring them back.
    rig.Step(both, TicksPerSecond, walk);
    REQUIRE(rig.Mode.State().Stage == Phase::Active);
    CHECK(rig.DistanceFromOwnBase(a) > 2.0f);
    CHECK(rig.DistanceFromOwnBase(b) > 2.0f);

    //The time runs out, the end screen, and the reset into a new warmup.
    REQUIRE(rig.StepUntil(both, Phase::Ended, Short().TimeLimitTicks));
    REQUIRE(rig.StepUntil(both, Phase::Warmup, Short().EndScreenTicks + 5));
    rig.Step(both, Short().ResetHoldTicks + 30);
    REQUIRE(rig.Mode.State().Stage == Phase::Countdown);

    for (MatchClient* client : both)
    {
        CHECK(client->IsAlive(client->LocalPlayer()));
        CHECK(rig.Server.IsAlive(client->LocalPlayer()));
        CHECK(rig.DistanceFromOwnBase(*client) < 8.0f);
        CHECK(client->Corrections().Count == 0);

        //And the client reads the same match the server rules.
        const std::optional<CtfState> seen = DecodeCtfState(client->GameState());
        REQUIRE(seen.has_value());
        CHECK(seen->Stage == Phase::Countdown);
    }
}
