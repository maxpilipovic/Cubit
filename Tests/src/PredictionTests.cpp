#include <doctest.h>

#include "Cubit/FrameClock.h"
#include "Cubit/Net/LoopbackTransport.h"
#include "Cubit/Net/MatchClient.h"
#include "Cubit/Net/MatchServer.h"
#include "Cubit/Net/SimulatedTransport.h"
#include "Cubit/Voxel/CharacterController.h"
#include "Cubit/Voxel/MatchState.h"

#include <glm/glm.hpp>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace
{
    constexpr int LatencyTicks = 3;
    constexpr double OneWayLatency = LatencyTicks * FrameClock::FixedStepSeconds;
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

    //A varied input sequence. Constant input would let a replay that ignored
    //its arguments still agree with a straight simulation.
    CharacterInput InputForTick(int tick)
    {
        CharacterInput input;
        input.Move = glm::vec2(std::sin(tick * 0.3f), std::cos(tick * 0.17f));
        input.Yaw = static_cast<float>(tick) * 3.0f;
        input.Pitch = -10.0f;
        input.Jump = (tick % 23) == 0;
        return input;
    }
}

TEST_CASE("Replaying inputs onto a stale state is simulating them in the first place")
{
    //THE ORACLE FOR THIS STAGE, and there is no network in it at all.
    //
    //Reconciliation is exactly this: take the last state the server vouched
    //for, apply everything it has not seen yet, and you are where you should
    //be. That it can be tested with no transport, no server and no client is
    //the whole reason CharacterController::Step was made a pure function of
    //(state, input, world) back in August.
    //
    //If this fails, nothing downstream is worth debugging: the client is not
    //running the same simulation as the server, and no amount of correcting
    //will settle it.
    MatchState straight(FlatWorld());
    const PlayerId player = straight.AddPlayer(Spawn);

    MatchState replayed(FlatWorld());
    replayed.AddPlayer(player, Spawn);

    constexpr int Total = 60;
    constexpr int Authoritative = 24;   //Where the "server" got to.

    glm::vec3 authoritativePosition{ 0.0f };
    glm::vec3 authoritativePrevious{ 0.0f };
    float authoritativeVelocity = 0.0f;
    bool authoritativeGrounded = false;

    for (int tick = 1; tick <= Total; ++tick)
    {
        straight.StepPlayer(player, InputForTick(tick), FrameClock::FixedStepSeconds);

        if (tick == Authoritative)
        {
            const CharacterController& character = straight.Player(player);
            authoritativePosition = character.Position();
            authoritativePrevious = character.PreviousPosition();
            authoritativeVelocity = character.VerticalVelocity();
            authoritativeGrounded = character.Grounded();
        }
    }

    //The client's picture: the authoritative state, plus everything above it.
    replayed.PlayerForWrite(player).SetState(authoritativePosition, authoritativePrevious,
        authoritativeVelocity, authoritativeGrounded);

    for (int tick = Authoritative + 1; tick <= Total; ++tick)
        replayed.StepPlayer(player, InputForTick(tick), FrameClock::FixedStepSeconds);

    //Bit-exact. One simulation, run twice.
    CHECK(replayed.Player(player).Position() == straight.Player(player).Position());
    CHECK(replayed.Player(player).PreviousPosition() == straight.Player(player).PreviousPosition());
    CHECK(replayed.Player(player).VerticalVelocity() == straight.Player(player).VerticalVelocity());
    CHECK(replayed.Player(player).Grounded() == straight.Player(player).Grounded());
}

TEST_CASE("Grounded is part of the state a replay needs, not decoration")
{
    //Why SetState takes all four. Step consults the previous step's grounded
    //flag when deciding whether a jump fires, so a replay that restored only
    //the position would diverge on the first replayed jump - and diverge
    //silently, since the position it started from was right.
    MatchState withFlag(FlatWorld());
    MatchState withoutFlag(FlatWorld());

    const PlayerId player = withFlag.AddPlayer(Spawn);
    withoutFlag.AddPlayer(player, Spawn);

    //Settle onto the ground so Grounded is genuinely true.
    for (int i = 0; i < 30; ++i)
    {
        withFlag.StepPlayer(player, CharacterInput{}, FrameClock::FixedStepSeconds);
        withoutFlag.StepPlayer(player, CharacterInput{}, FrameClock::FixedStepSeconds);
    }

    const CharacterController& settled = withFlag.Player(player);
    REQUIRE(settled.Grounded());

    withFlag.PlayerForWrite(player).SetState(settled.Position(), settled.PreviousPosition(),
        settled.VerticalVelocity(), true);
    withoutFlag.PlayerForWrite(player).SetState(settled.Position(), settled.PreviousPosition(),
        settled.VerticalVelocity(), false);

    CharacterInput jump;
    jump.Jump = true;

    withFlag.StepPlayer(player, jump, FrameClock::FixedStepSeconds);
    withoutFlag.StepPlayer(player, jump, FrameClock::FixedStepSeconds);

    //One of these jumped and one did not.
    CHECK(withFlag.Player(player).Position().y != withoutFlag.Player(player).Position().y);
}

TEST_CASE("A correction smaller than the threshold is not shown")
{
    //The deadzone, downward. A disagreement of a few centimetres is what one
    //starved server tick costs (WalkSpeed / 60 = 0.083 blocks); showing it
    //would be a visible twitch for something the player cannot have noticed.
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    SimulatedTransport serverNet(network.Server(), sim);

    PeerId peer = InvalidPeer;
    SimulatedTransport clientNet(network.AddClient(peer), sim);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, serverNet);
    MatchClient client(clientNet, GoodLoader());

    for (int i = 0; i < 120; ++i)
    {
        client.SetInput(CharacterInput{});
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }
    REQUIRE(client.Connected());

    //Shove the prediction sideways by less than the threshold, then let one
    //snapshot land on it.
    const glm::vec3 predicted = client.Match().Player(client.LocalPlayer()).Position();
    const glm::vec3 nudged = predicted + glm::vec3(0.05f, 0.0f, 0.0f);
    client.MatchForWrite().TeleportPlayer(client.LocalPlayer(), nudged);

    const std::uint64_t before = client.Corrections().Snapshots;

    for (int i = 0; i < 30 && client.Corrections().Snapshots == before; ++i)
    {
        client.SetInput(CharacterInput{});
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }

    REQUIRE(client.Corrections().Snapshots > before);

    //Kept where prediction had it, not dragged back.
    CHECK(client.Match().Player(client.LocalPlayer()).Position().x == doctest::Approx(nudged.x));
    CHECK(client.Corrections().Count == 0);
}

TEST_CASE("A correction bigger than the threshold snaps")
{
    //The deadzone, upward, and the same setup so the two differ in exactly one
    //number. A deadzone with no ceiling is not a deadzone, it is a divergence.
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    SimulatedTransport serverNet(network.Server(), sim);

    PeerId peer = InvalidPeer;
    SimulatedTransport clientNet(network.AddClient(peer), sim);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, serverNet);
    MatchClient client(clientNet, GoodLoader());

    for (int i = 0; i < 120; ++i)
    {
        client.SetInput(CharacterInput{});
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }
    REQUIRE(client.Connected());

    const glm::vec3 predicted = client.Match().Player(client.LocalPlayer()).Position();
    client.MatchForWrite().TeleportPlayer(client.LocalPlayer(), predicted + glm::vec3(1.0f, 0.0f, 0.0f));

    const std::uint64_t before = client.Corrections().Snapshots;

    for (int i = 0; i < 30 && client.Corrections().Snapshots == before; ++i)
    {
        client.SetInput(CharacterInput{});
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }

    REQUIRE(client.Corrections().Count == 1);
    CHECK(client.Corrections().Max > CorrectionThreshold);

    //Back to where the server says, not left a metre out.
    CHECK(client.Match().Player(client.LocalPlayer()).Position().x
        == doctest::Approx(predicted.x).epsilon(0.01));
}
