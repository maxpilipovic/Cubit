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

namespace
{
    //Hand-built snapshots, so a test can say exactly where a remote was at
    //exactly which tick. A real server would work too, but then the expected
    //values would have to be read back out of it, and a test that asks the
    //subject what the answer is proves very little.
    SnapshotMessage SnapshotAt(std::uint64_t tick, PlayerId local, PlayerId remote,
        const glm::vec3& remotePosition)
    {
        SnapshotMessage snapshot;
        snapshot.Tick = tick;

        PlayerSnapshot mine;
        mine.Player = local;
        mine.Position = Spawn;
        mine.Grounded = true;

        PlayerSnapshot theirs;
        theirs.Player = remote;
        theirs.Position = remotePosition;
        theirs.Yaw = static_cast<float>(tick);
        theirs.Grounded = true;

        snapshot.Players = { mine, theirs };
        return snapshot;
    }
}

TEST_CASE("A remote player is drawn between the two samples that bracket the interpolation point")
{
    LoopbackNetwork network;
    PeerId peer = InvalidPeer;
    Transport& raw = network.AddClient(peer);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, network.Server());
    MatchClient client(raw, GoodLoader());

    for (int i = 0; i < 5; ++i)
    {
        client.SetInput(CharacterInput{});
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }
    REQUIRE(client.Connected());

    const PlayerId local = client.LocalPlayer();
    const PlayerId remote = PlayerId{ static_cast<std::uint16_t>(local + 1) };

    //Twenty snapshots, the remote walking one block per tick along x, so the
    //expected interpolated x IS the interpolated tick. Any arithmetic error
    //shows up as a number rather than as a wobble somebody has to see.
    for (std::uint64_t tick = 100; tick <= 120; ++tick)
    {
        network.Server().Send(peer,
            Encode(SnapshotAt(tick, local, remote, glm::vec3(static_cast<float>(tick), 2.0f, 8.0f))),
            Channel::Unreliable);

        client.SetInput(CharacterInput{});
        client.Step(FrameClock::FixedStepSeconds);
    }

    REQUIRE(client.ServerTick() == 120);

    //Six ticks behind the newest snapshot (120) is tick 114, but m_RemoteClock
    //has already advanced one tick past it: every one of these iterations
    //delivers a snapshot AND runs one predicted client step in the same
    //client.Step() call, and the advance happens after the snap. So the
    //render clock reads 121, not 120, and the query lands on tick 115 - still
    //an exact sample, so still "on the nose".
    const MatchClient::RemotePose onTick = client.PoseOf(remote, 0.0f);
    CHECK(onTick.Position.x == doctest::Approx(115.0f));

    //And half a tick further on, which must be halfway between two samples
    //rather than either of them.
    const MatchClient::RemotePose halfway = client.PoseOf(remote, 0.5f);
    CHECK(halfway.Position.x == doctest::Approx(115.5f));
}

TEST_CASE("A remote player is held, never extrapolated, when nothing new arrives")
{
    //The rule that keeps a remote honest. Guessing forward is right most of the
    //time and wrong exactly when it matters - at a stop, a turn, or a jump -
    //and being wrong means taking the guess back, which looks precisely like
    //the stutter extrapolation was meant to prevent.
    LoopbackNetwork network;
    PeerId peer = InvalidPeer;
    Transport& raw = network.AddClient(peer);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, network.Server());
    MatchClient client(raw, GoodLoader());

    for (int i = 0; i < 5; ++i)
    {
        client.SetInput(CharacterInput{});
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }
    REQUIRE(client.Connected());

    const PlayerId local = client.LocalPlayer();
    const PlayerId remote = PlayerId{ static_cast<std::uint16_t>(local + 1) };

    for (std::uint64_t tick = 100; tick <= 110; ++tick)
    {
        network.Server().Send(peer,
            Encode(SnapshotAt(tick, local, remote, glm::vec3(static_cast<float>(tick), 2.0f, 8.0f))),
            Channel::Unreliable);

        client.SetInput(CharacterInput{});
        client.Step(FrameClock::FixedStepSeconds);
    }

    //Nothing more arrives for a second.
    for (int i = 0; i < 60; ++i)
    {
        client.SetInput(CharacterInput{});
        client.Step(FrameClock::FixedStepSeconds);

        //Never past the newest thing anybody actually said.
        CHECK(client.PoseOf(remote, 0.0f).Position.x <= doctest::Approx(110.0f));
    }

    //And it settles on the newest sample rather than drifting back to the
    //oldest or to the origin.
    CHECK(client.PoseOf(remote, 0.0f).Position.x == doctest::Approx(110.0f));
}

TEST_CASE("On a clean link, prediction is never corrected")
{
    //THE GATE THAT CATCHES A REAL DEFECT. With no loss and no jitter the server
    //never steps a tick with an empty queue, so its state stays a prefix of
    //what this client predicted, and replaying what it has not seen yet
    //should leave nothing worth showing. A single correction here means
    //prediction and the authoritative step disagree about the simulation
    //itself - which is not a network condition and must not be absorbed by
    //widening the threshold.
    //
    //One thing this gate cannot see: Corrections().Count only counts
    //disagreements over CorrectionThreshold (0.15 blocks), by design - see
    //MatchClient.h. A permanent divergence smaller than that reports the same
    //clean zero as no divergence at all. The direct-position check below,
    //after both sides have had time to settle on the same idle state, is what
    //closes that gap: it asserts there was nothing to show, not merely that
    //nothing was shown.
    //
    //If this goes red, the three suspects, in order: the server stepped without
    //an input (look at the queue depth), replay ran at a different step length
    //from prediction, or the ack is off by one and replay is reapplying an
    //input the server already consumed.
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = OneWayLatency;

    SimulatedTransport serverNet(network.Server(), sim);
    PeerId peer = InvalidPeer;
    SimulatedTransport clientNet(network.AddClient(peer), sim);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, serverNet);
    MatchClient client(clientNet, GoodLoader());

    //Warm-up. Joining is a transient: for the first few ticks this client has
    //no player yet, then it has one whose inputs the server has not
    //acknowledged, and the corrections that fall out of that are about the
    //handshake rather than about prediction.
    for (int tick = 0; tick < 120; ++tick)
    {
        client.SetInput(InputForTick(tick));
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }
    REQUIRE(client.Connected());

    const std::uint64_t settled = client.Corrections().Count;
    const std::uint64_t settledSnapshots = client.Corrections().Snapshots;

    for (int tick = 120; tick < 1120; ++tick)
    {
        client.SetInput(InputForTick(tick));
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }

    //A thousand ticks of varied input, including jumps, and not one
    //disagreement worth showing.
    CHECK(client.Corrections().Count == settled);

    //And the denominator is real: a client that stopped receiving snapshots
    //entirely would also report no corrections.
    CHECK(client.Corrections().Snapshots > settledSnapshots + 900);

    //Nothing worth showing is not the same claim as nothing to show: drain to
    //a stop with no input and compare positions directly, so a divergence
    //under the 0.15 threshold - invisible to Corrections().Count by design -
    //cannot hide behind the deadzone.
    for (int tick = 1120; tick < 1150; ++tick)
    {
        client.SetInput(CharacterInput{});
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }

    CHECK(glm::distance(client.Match().Player(client.LocalPlayer()).Position(),
        server.Match().Player(client.LocalPlayer()).Position()) < 1e-3f);

    MESSAGE("clean link: corrections during warm-up = " << settled);
}

TEST_CASE("Under loss and jitter, corrections are bounded and do not grow")
{
    //THE RECORDED NUMBER, at a realistic loss rate. Nothing here is compared
    //against a target invented in advance - nobody knows the right value yet,
    //and a threshold guessed here would be a number to argue with rather than
    //evidence. What is asserted is the shape: bounded, and not growing.
    //
    //At 5% loss this comes back at zero (see the spec), which the case below
    //at 20% loss exists to put in context: the counting and snapping
    //machinery is exercised there instead, since here it mostly is not.
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = 5 * FrameClock::FixedStepSeconds;   //166.7 ms RTT, a whole tick multiple.
    sim.Jitter = FrameClock::FixedStepSeconds;
    sim.Loss = 0.05f;
    sim.Seed = 1;

    SimulatedTransport serverNet(network.Server(), sim);
    PeerId peer = InvalidPeer;
    SimulatedTransport clientNet(network.AddClient(peer), sim);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, serverNet);
    MatchClient client(clientNet, GoodLoader());

    for (int tick = 0; tick < 120; ++tick)
    {
        client.SetInput(InputForTick(tick));
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }
    REQUIRE(client.Connected());

    const MatchClient::CorrectionStats start = client.Corrections();

    for (int tick = 120; tick < 1120; ++tick)
    {
        client.SetInput(InputForTick(tick));
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }

    const MatchClient::CorrectionStats half = client.Corrections();

    for (int tick = 1120; tick < 2120; ++tick)
    {
        client.SetInput(InputForTick(tick));
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }

    const MatchClient::CorrectionStats end = client.Corrections();

    MESSAGE("166.7 ms RTT, 5% loss, jitter: corrections per 1000 ticks = "
        << static_cast<float>(end.Count - start.Count) / 2.0f << ", mean = " << end.Mean
        << ", max = " << end.Max);

    //NOT GROWING, in principle: Max is cumulative and monotone by
    //construction (it can only ever be set higher, never lowered), so
    //end.Max >= half.Max always holds and this can only fail if the second
    //half sets a new record by more than one threshold's worth above the
    //first. At the observed 0.0 this reduces to CHECK(0 <= 0.15) and proves
    //nothing on its own here - it is kept for the shape of the assertion, and
    //the case below is where a nonzero Max actually puts it to work.
    CHECK(end.Max <= half.Max + CorrectionThreshold);

    //At an observed 0.0, the assertion that actually carries information is
    //that not one of the 2,000 ticks in this run produced a correction -
    //Count == 0, not a magnitude bound with nothing to bound. See the 20%
    //loss case below for a run where a magnitude bound has something to say.
    CHECK(end.Count == 0);
}

TEST_CASE("Under heavy loss, corrections stay bounded and do not grow")
{
    //THE LOSS-SIDE BASELINE. At 5% loss, above, the run comes back at zero -
    //not because the counting or snapping machinery is untested (the deadzone
    //tests earlier in this file pin that directly, by teleporting the
    //client), but because nothing in the network conditions there disagrees
    //with the server often enough to clear the threshold. This case exists so
    //something in the suite has actually watched the server and the client
    //disagree BECAUSE OF a network condition, and the client snap in
    //response, rather than because of an injected teleport.
    //
    //20% loss means every one of the three copies of a tick's input is lost
    //together with probability 0.2^3 = 8e-3 - about 16 fully-dropped input
    //ticks across 2,000. A single dropped tick's ~0.083-block offset never
    //gets corrected on its own, because Reconcile discards anything under
    //threshold - but it is NOT a random walk that takes all 16 drops to reach
    //a correction. `error` is the distance between the CURRENT predicted
    //position, which still carries every not-yet-corrected drop, and this
    //call's freshly replayed authoritative state, which carries none - so
    //every reconciliation re-measures the running TOTAL, not an increment,
    //and the moment that total first crosses CorrectionThreshold the snap
    //resets it to zero. At ~0.083 blocks a drop, the total crosses 0.15 after
    //roughly three or four drops, not after all sixteen - which is why the
    //run below reports 6 corrections (3 per 1000 ticks) with a mean of 0.193,
    //close to the threshold, rather than the single ~0.33-block correction a
    //sqrt(16) random-walk model would predict. Corrections here come from the
    //network, not from a teleport.
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = 5 * FrameClock::FixedStepSeconds;   //166.7 ms RTT, a whole tick multiple.
    sim.Jitter = FrameClock::FixedStepSeconds;
    sim.Loss = 0.20f;
    sim.Seed = 1;

    SimulatedTransport serverNet(network.Server(), sim);
    PeerId peer = InvalidPeer;
    SimulatedTransport clientNet(network.AddClient(peer), sim);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, serverNet);
    MatchClient client(clientNet, GoodLoader());

    for (int tick = 0; tick < 120; ++tick)
    {
        client.SetInput(InputForTick(tick));
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }
    REQUIRE(client.Connected());

    const MatchClient::CorrectionStats start = client.Corrections();

    for (int tick = 120; tick < 1120; ++tick)
    {
        client.SetInput(InputForTick(tick));
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }

    const MatchClient::CorrectionStats half = client.Corrections();

    for (int tick = 1120; tick < 2120; ++tick)
    {
        client.SetInput(InputForTick(tick));
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }

    const MatchClient::CorrectionStats end = client.Corrections();

    MESSAGE("166.7 ms RTT, 20% loss, jitter: corrections per 1000 ticks = "
        << static_cast<float>(end.Count - start.Count) / 2.0f << ", mean = " << end.Mean
        << ", max = " << end.Max);

    //THE PROOF OF LIFE: at this loss rate the network itself must produce at
    //least one visible disagreement, or this case is measuring nothing that
    //the 5%-loss case does not already measure.
    CHECK(end.Count > 0);

    //NOT GROWING, doing real work this time: a nonzero half.Max means this
    //can actually fail if the second half's disagreements run further than
    //the first half's by more than one threshold's worth.
    CHECK(end.Max <= half.Max + CorrectionThreshold);

    //BOUNDED: pin this at a round number above what the run actually reports.
    //Seed 1 (recorded above) reported max 0.291; seeds 2 and 3, tried before
    //pinning this so the figure is not one seed's luck, reported 0.288 and
    //0.227 - all comfortably under 0.5, none close to it. 0.5 is the round
    //number above all three, on 2026-09-05.
    CHECK(end.Max < 0.5f);
}

TEST_CASE("A replay uses the step length prediction used")
{
    //Every other test in this file drives MatchClient at
    //FrameClock::FixedStepSeconds, so m_StepSeconds could be deleted and
    //hardcoded to that constant and nothing in the suite would go red - even
    //though its own comment says "a replay at a different step length is a
    //different simulation." This test is the one that would notice: it drives
    //the client at 1/50 s, well away from the fixed 1/60 s step, and lets
    //latency leave a couple dozen inputs unacknowledged before a snapshot
    //lands and Reconcile replays them.
    //
    //`predictedInputs` records, in order, exactly the inputs this client
    //actually predicted with - so `reference` below is not a second guess at
    //what the client did, it is a straight re-run of the same input sequence
    //at the same step length, with no networking at all.
    constexpr double OddStep = 1.0 / 50.0;

    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = 12 * OddStep;   //A big enough round trip that several dozen
                                  //inputs sit unacked at any moment - plenty
                                  //for a wrong replay length to add up past
                                  //CorrectionThreshold rather than hide under it.
    SimulatedTransport serverNet(network.Server(), sim);

    PeerId peer = InvalidPeer;
    SimulatedTransport clientNet(network.AddClient(peer), sim);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, serverNet);
    MatchClient client(clientNet, GoodLoader());

    std::optional<MatchState> reference;
    PlayerId localPlayer = InvalidPlayer;
    std::uint64_t lastTick = 0;
    int sequence = 0;
    std::vector<CharacterInput> predictedInputs;

    for (int i = 0; i < 400; ++i)
    {
        ++sequence;
        const CharacterInput input = InputForTick(sequence);
        client.SetInput(input);
        client.Step(OddStep);
        server.Step(OddStep);

        //Tick only advances on an iteration that actually predicted - see
        //MatchClient::Step. Skip any iteration before the handshake completes.
        const std::uint64_t tick = client.Match().Tick();
        if (tick == lastTick)
            continue;
        lastTick = tick;

        if (!reference.has_value())
        {
            localPlayer = client.LocalPlayer();
            reference.emplace(FlatWorld());
            reference->AddPlayer(localPlayer, Spawn);
        }

        predictedInputs.push_back(input);
        reference->StepPlayer(localPlayer, input, static_cast<float>(OddStep));
    }

    REQUIRE(client.Connected());
    REQUIRE(reference.has_value());

    //Enough predicted ticks for several snapshots, each with a couple dozen
    //inputs unacked, to have been reconciled.
    REQUIRE(predictedInputs.size() > 100u);

    //THE PROPERTY: a client stepped at 1/50 s and reconciled against a server
    //stepped at 1/50 s ends up exactly where a single straight simulation at
    //1/50 s does, with no networking or reconciliation involved at all.
    CHECK(client.Match().Player(localPlayer).Position() == reference->Player(localPlayer).Position());

    //THE MUTATION GUARD. The same input sequence, replayed at
    //FrameClock::FixedStepSeconds instead of OddStep, must land somewhere
    //different - otherwise the two step lengths are not different enough for
    //the CHECK above to prove anything.
    MatchState atFixedStep(FlatWorld());
    atFixedStep.AddPlayer(localPlayer, Spawn);
    for (const CharacterInput& input : predictedInputs)
        atFixedStep.StepPlayer(localPlayer, input, static_cast<float>(FrameClock::FixedStepSeconds));

    CHECK(client.Match().Player(localPlayer).Position() != atFixedStep.Player(localPlayer).Position());
}

namespace
{
    //Same shape as MatchServerTests.cpp's LastShotResolved, but reading a
    //FireMessage off the SERVER end of the loopback - what the client actually
    //put on the wire, not what it meant to send.
    std::optional<FireMessage> LastFire(Transport& transport, int& count)
    {
        std::optional<FireMessage> latest;
        count = 0;

        NetEvent event;
        while (transport.Poll(event))
        {
            if (event.Type != NetEventType::Message)
                continue;

            FireMessage fire;
            if (Decode(event.Data, fire))
            {
                latest = fire;
                ++count;
            }
        }

        return latest;
    }
}

TEST_CASE("A fired shot declares the instant the client is rendering")
{
    //The client must send the SAME instant PoseOf is drawing at, because that
    //is the whole contract: the server reproduces the shooter's screen. Any
    //other number and the server aims at a target the shooter never saw.
    //
    //m_RemoteClock has no accessor, and re-deriving it from ServerTick() here
    //would make this a second reading of the same formula rather than a test
    //of it - the same standard HeadingTests already holds AimDirection to,
    //pinned against the camera rather than against a re-reading of the
    //trigonometry. So this reuses the oracle from "A remote player is drawn
    //between the two samples that bracket the interpolation point" below: a
    //remote walking one block per tick along x, so PoseOf's returned
    //Position.x IS the interpolated tick number, in units PoseOf actually
    //drew rather than in the formula's own terms. If Fire declares the same
    //instant PoseOf is drawing at, the two numbers must agree.
    LoopbackNetwork network;
    PeerId peer = InvalidPeer;
    Transport& raw = network.AddClient(peer);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, network.Server());
    MatchClient client(raw, GoodLoader());

    for (int i = 0; i < 5; ++i)
    {
        client.SetInput(CharacterInput{});
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }
    REQUIRE(client.Connected());

    const PlayerId local = client.LocalPlayer();
    const PlayerId remote = PlayerId{ static_cast<std::uint16_t>(local + 1) };

    //Identical setup to the interpolation test below: twenty snapshots, the
    //remote walking one block per tick along x, landing m_RemoteClock at 121
    //for the same reason explained there.
    for (std::uint64_t tick = 100; tick <= 120; ++tick)
    {
        network.Server().Send(peer,
            Encode(SnapshotAt(tick, local, remote, glm::vec3(static_cast<float>(tick), 2.0f, 8.0f))),
            Channel::Unreliable);

        client.SetInput(CharacterInput{});
        client.Step(FrameClock::FixedStepSeconds);
    }

    REQUIRE(client.ServerTick() == 120);

    //A nonzero alpha, so the fractional part - RenderAlpha - is actually
    //exercised. A whole-number alpha would leave it at zero and prove nothing
    //about the split between RenderTick and RenderAlpha.
    constexpr float alpha = 0.5f;

    //THE ORACLE: what PoseOf actually drew for this alpha, read back as a tick
    //number because the remote walks one block per tick.
    const double drawnInstant = static_cast<double>(client.PoseOf(remote, alpha).Position.x);

    //Twenty ticks of this client's own unreliable Input traffic are sitting
    //unconsumed on the server end, because this test never calls
    //server.Step() past the handshake - draining them here first means the
    //helper below finds only the shot.
    int ignored = 0;
    LastFire(network.Server(), ignored);

    client.Fire(alpha);

    int count = 0;
    const std::optional<FireMessage> fire = LastFire(network.Server(), count);
    REQUIRE(fire.has_value());
    CHECK(count == 1);

    const double declaredInstant =
        static_cast<double>(fire->RenderTick) + static_cast<double>(fire->RenderAlpha);

    //THE PROPERTY: the instant Fire put on the wire is the same instant PoseOf
    //actually drew, pinned through what was rendered rather than through the
    //formula the two share.
    CHECK(declaredInstant == doctest::Approx(drawnInstant));
}

TEST_CASE("A shot resolution is reported to the caller")
{
    //LastShot exists so the Sandbox can draw a hit marker for as many frames
    //as it likes, without the Sandbox keeping its own callback or its own
    //clock - so what matters here is that decoding populates every field the
    //wire message carries, not that a real shot found a real target. A
    //hand-built ShotResolvedMessage, sent directly the way SnapshotAt is sent
    //above, pins the decode without needing live geometry to actually hit -
    //that geometry is Task 2's and Task 9's, not this one's.
    LoopbackNetwork network;
    PeerId peer = InvalidPeer;
    Transport& raw = network.AddClient(peer);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, network.Server());
    MatchClient client(raw, GoodLoader());

    for (int i = 0; i < 5; ++i)
    {
        client.SetInput(CharacterInput{});
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }
    REQUIRE(client.Connected());

    //Nothing to report before anything has arrived.
    CHECK_FALSE(client.LastShot().has_value());

    const PlayerId shooter = client.LocalPlayer();
    const PlayerId victim = PlayerId{ static_cast<std::uint16_t>(shooter + 1) };

    //Every field set away from ShotReport's own default - InvalidPlayer,
    //the zero vector, 0, false - so a HandleShotResolved that forgot to copy
    //one would leave it reading the default and this test would not notice.
    //VictimHealth is 51 rather than the 0 a real kill would carry, on
    //purpose: this message is hand-built rather than a real server's ruling,
    //so there is no reason to let Killed's realistic correlation with health
    //quietly cover for VictimHealth never being copied at all.
    ShotResolvedMessage resolved;
    resolved.Shooter = shooter;
    resolved.Victim = victim;
    resolved.Impact = glm::vec3(4.0f, 2.0f, 6.0f);
    resolved.VictimHealth = 51;
    resolved.Killed = true;

    network.Server().Send(peer, Encode(resolved), Channel::Reliable);

    const std::uint64_t tickBeforeArrival = client.Match().Tick();

    client.SetInput(CharacterInput{});
    client.Step(FrameClock::FixedStepSeconds);

    REQUIRE(client.LastShot().has_value());
    const MatchClient::ShotReport& report = *client.LastShot();

    CHECK(report.Shooter == shooter);
    CHECK(report.Victim == victim);
    CHECK(report.Impact == resolved.Impact);
    CHECK(report.VictimHealth == resolved.VictimHealth);
    CHECK(report.Killed == resolved.Killed);

    //The message is decoded during Step's drain, before that step's own tick
    //advance - so the tick it is stamped with is the one this client was on
    //when the ruling arrived, not the one it reaches by the time Step returns.
    CHECK(report.ReceivedAtTick == tickBeforeArrival);
}
