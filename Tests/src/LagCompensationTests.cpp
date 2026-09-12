#include <doctest.h>

#include "Cubit/FrameClock.h"
#include "Cubit/Net/LoopbackTransport.h"
#include "Cubit/Net/MatchClient.h"
#include "Cubit/Net/MatchServer.h"
#include "Cubit/Net/Protocol.h"
#include "Cubit/Net/SimulatedTransport.h"
#include "Cubit/Voxel/CharacterController.h"
#include "Cubit/Voxel/Heading.h"
#include "Cubit/Voxel/HitboxHistory.h"
#include "Cubit/Voxel/ResolveShot.h"
#include "Cubit/Voxel/World.h"

#include <glm/glm.hpp>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace
{
    const glm::vec3 PlayerHalfExtents{ 0.3f, 0.9f, 0.3f };

    World EmptyWorld()
    {
        return World(4, 2, 4);
    }

    //A target strafing along z at 5 blocks a second, sampled once per tick.
    glm::vec3 TargetAt(std::uint64_t tick)
    {
        const float seconds = static_cast<float>(tick) / 60.0f;
        return glm::vec3(20.0f, 1.0f, seconds * 5.0f);
    }
}

TEST_CASE("Rewinding to the instant the shooter saw is what turns a miss into a hit")
{
    //THE ORACLE FOR THIS STAGE. Everything else is plumbing that arranges for
    //these two resolutions to happen with the right numbers.
    //
    //A target strafes past. The shooter's screen is six ticks behind the
    //server plus three ticks of latency, so they aim at where the target was
    //nine ticks ago - which is where the target genuinely was, on their screen.
    //The server, stepping in the present, sees the target 0.75 blocks further
    //along. That is more than the 0.6-block width of the box, so the two
    //answers MUST differ. If they do not, this stage has nothing to build.
    HitboxHistory history;
    for (std::uint64_t tick = 0; tick <= 100; ++tick)
        history.Record(2, tick, TargetAt(tick));

    const std::uint64_t serverTick = 100;
    const double renderedInstant = 91.0;

    //Aim at exactly where the shooter's screen showed the target: the centre
    //of the box the history rebuilds at that instant. This is the same
    //computation MatchClient::PoseOf performs to draw them, which is the point
    //- the test aims at the rendered position, not at a guess about it.
    Aabb seen;
    REQUIRE(history.BoxAt(2, renderedInstant, PlayerHalfExtents, seen));
    const glm::vec3 aimPoint = (seen.Min + seen.Max) * 0.5f;

    const glm::vec3 eye(0.0f, 1.0f, aimPoint.z);
    const glm::vec3 direction = glm::normalize(aimPoint - eye);

    World world = EmptyWorld();

    //WITH the rewind: resolve against the box as it was at the rendered instant.
    std::vector<ShotCandidate> rewound{ ShotCandidate{ 2, seen } };
    const ShotResult compensated =
        ResolveShot(world, rewound, eye, direction, 128.0f);

    //WITHOUT the rewind: resolve against the box as it is now. This is the
    //mutation the gate exists to catch, run as a branch rather than left to a
    //reviewer to perform by hand.
    Aabb present;
    REQUIRE(history.BoxAt(2, static_cast<double>(serverTick), PlayerHalfExtents, present));
    std::vector<ShotCandidate> live{ ShotCandidate{ 2, present } };
    const ShotResult uncompensated =
        ResolveShot(world, live, eye, direction, 128.0f);

    CHECK(compensated.Victim == 2);

    //THE CLAIM THE WHOLE DESIGN RESTS ON. If this fails, the rewind is not
    //doing anything and the stage is pointless - either the target is too slow,
    //the latency too small, or the design is wrong. Report before continuing.
    CHECK(uncompensated.Victim == InvalidPlayer);
}

TEST_CASE("Half a tick of rewind error is enough to miss")
{
    //Why the wire carries a fractional instant rather than a whole tick. At
    //5 blocks a second a half tick is 0.042 blocks, which alone would not miss
    //- so this test uses the edge of the box, where it does. The point is that
    //a whole-tick rewind is not free, and that the error is invisible when you
    //aim at the middle.
    HitboxHistory history;
    for (std::uint64_t tick = 0; tick <= 100; ++tick)
        history.Record(2, tick, TargetAt(tick));

    Aabb exact;
    REQUIRE(history.BoxAt(2, 91.5, PlayerHalfExtents, exact));

    Aabb rounded;
    REQUIRE(history.BoxAt(2, 91.0, PlayerHalfExtents, rounded));

    //Aim just inside the LEADING edge of where the target actually was.
    //
    //The edge matters and picking the wrong one makes this test unpassable.
    //The rounded box is half a tick EARLIER, so it sits 0.042 blocks back
    //along +z: exact spans [7.325, 7.925] and rounded spans [7.283, 7.883].
    //The sliver that is in exact but not in rounded is the leading edge,
    //(7.883, 7.925] - aiming near exact.Min.z lands inside BOTH boxes and the
    //miss never happens.
    const glm::vec3 aimPoint(exact.Min.x + 0.3f, 1.0f, exact.Max.z - 0.01f);
    const glm::vec3 eye(0.0f, 1.0f, aimPoint.z);
    const glm::vec3 direction = glm::normalize(aimPoint - eye);

    World world = EmptyWorld();

    std::vector<ShotCandidate> right{ ShotCandidate{ 2, exact } };
    std::vector<ShotCandidate> wrong{ ShotCandidate{ 2, rounded } };

    CHECK(ResolveShot(world, right, eye, direction, 128.0f).Victim == 2);
    CHECK(ResolveShot(world, wrong, eye, direction, 128.0f).Victim == InvalidPlayer);
}

//----------------------------------------------------------------------------
//THE NETWORKED GATE.
//
//Everything above this line resolves shots by hand, with no clock and no wire:
//it pins what a rewind IS. Everything below runs a real MatchServer and two
//real MatchClients over SimulatedTransport and asks the only question that
//matters - whether the numbers the machinery actually produces agree with each
//other end to end.
//
//A second anonymous namespace rather than one at the top of the file, so this
//harness sits with the cases that use it instead of pushing Task 3's oracle two
//screens down.
//----------------------------------------------------------------------------
namespace
{
    constexpr std::uint64_t MapHash = 0xFEEDFACEull;
    const glm::vec3 Spawn{ 8.0f, 2.0f, 8.0f };

    //64 x 64 of floor - four times WireOracleTests' world. The shooter walks
    //fifteen blocks off the spawn and the target's strafe wanders up to ten
    //blocks either side of it, and both have to stay on solid ground for the
    //whole run: a character that walks off the edge free-falls, and every shot
    //after that measures gravity rather than lag compensation.
    World FloorWorld()
    {
        World world(4, 2, 4);

        for (int z = 0; z < world.GetDepth(); ++z)
            for (int x = 0; x < world.GetWidth(); ++x)
                world.SetBlock(x, 0, z, BlockId{ 1 });

        return world;
    }

    MatchClient::MapLoader GoodLoader()
    {
        return [](const std::string&) -> std::optional<LoadedMap>
        {
            return LoadedMap{ FloorWorld(), MapHash };
        };
    }

    //Ticks of handshake before anybody is asked to do anything.
    constexpr int HandshakeTicks = 30;

    //The shooter walks +x for this long, then stops. Fifteen blocks at walk
    //speed. Both players spawn on the same point - the server has exactly one -
    //so somebody has to move before there is a shot to take, and it has to be
    //the SHOOTER: the target respawns on that spawn every time it dies, so a
    //target that had walked away would have to walk back after every third hit.
    constexpr int SeparationTicks = 180;

    //Then stands still for this long. The eye the test aims from is the
    //shooter's own PREDICTED position, and the server resolves from its own;
    //walking leaves the two a fraction of a tick apart, and the deadzone never
    //corrects a disagreement below 0.15 blocks. Standing still drains the input
    //queue and brings them back together - MaxEyeError, reported by the gate,
    //is what says whether that actually happened.
    constexpr int SettleTicks = 90;

    //The target strafes this many ticks one way, then the same back: five
    //blocks each way at walk speed. Across the shooter's line of sight, not
    //along it - a rewind error along the ray barely moves the answer, and one
    //across it moves the box straight off the ray, which is the error this
    //stage exists to remove.
    constexpr int StrafeHalfPeriod = 60;

    //Ticks between shots: three times TicksBetweenShots (10), the weapon's own
    //limit, and the reason is measured rather than chosen.
    //
    //SimulatedTransport models the loss of a RELIABLE packet as a
    //retransmission - one extra round trip - and Fire is reliable, so on the
    //300 ms row a shot can reach the server 18 ticks after it would have. The
    //reliable channel is sequenced, not spaced: the next shot is not delayed
    //with it, so it lands 18 ticks closer behind and the server drops it for
    //firing faster than the weapon allows. At 20 ticks apart that cost 15 of
    //200 shots on that row, which is the shape of a rate limiter and not of a
    //rewind. Thirty is the deepest retransmission here (18) plus the weapon's
    //limit (10) plus slack, so the crowding cannot reach the limiter.
    //
    //It buys a second thing for free: MatchClient keeps only the NEWEST
    //ruling, so two rulings landing on one client tick would be counted once,
    //and the same 18-tick worst case cannot close a 30-tick gap either.
    constexpr int TicksPerShot = 30;

    //How long after a respawn the shooter holds fire.
    //
    //A death teleports the target back to the spawn and MAKES THE SERVER FORGET
    //its history, so that a shot already in flight cannot rewind across the
    //death. Both halves reach the shooter late: it draws the target L+6 ticks
    //in the past (the client trails the server by L+1, and PoseOf draws six
    //ticks behind that), so for that many ticks after a respawn the instant it
    //declares names a moment the server has deliberately thrown away, and its
    //own sample ring is still interpolating across the teleport. Forty covers
    //the deepest row here (L=9, so 15) with room to spare. Those shots are a
    //documented design decision misfiring on purpose; measuring them would
    //measure the respawn rule, not the rewind.
    constexpr int RespawnQuietTicks = 40;

    struct ShootingRun
    {
        //One way, in whole ticks. Whole by construction: every comparison in
        //this stage is between tick numbers, and a latency that is not a whole
        //multiple of the step makes the rewind depth depend on where inside a
        //tick a packet happened to land.
        int LatencyTicks = 3;

        int Shots = 60;
        float Loss = 0.05f;
    };

    //What one shot looked like when it did not land.
    struct ShotDiagnosis
    {
        int Index = 0;
        float Alpha = 0.0f;
        glm::vec3 Drawn{ 0.0f };
        glm::vec3 Eye{ 0.0f };
        glm::vec3 Impact{ 0.0f };
        glm::vec3 Present{ 0.0f };
    };

    struct RunOutcome
    {
        int Fired = 0;
        int Rulings = 0;

        //Rulings naming the target.
        int Hits = 0;

        //The same rays, resolved against the boxes the server holds AT THE
        //MOMENT it handles the shot - the rewind switched off, and nothing else
        //changed.
        int PresentStateHits = 0;

        int Kills = 0;

        //Largest gap between the eye a shot was aimed from and the eye the
        //server fired it from, in blocks.
        float MaxEyeError = 0.0f;

        //Shots whose declared instant sat inside the rewind window, so the
        //server's clamp left it alone and the history had a box at exactly
        //that instant - and, over those, the largest distance in blocks
        //between the pose the shooter drew and the box the server rebuilt.
        //
        //THIS is the number that sees a rewind landing a tick off. Hits cannot:
        //one tick at walk speed is 0.083 blocks against a 0.3-block half width,
        //so a server resolving every shot one tick late still lands all of
        //them, and on 2026-09-12 it did - the whole table came back identical
        //with the history filed one tick short.
        int RewindChecked = 0;
        float MaxRewindError = 0.0f;

        std::vector<ShotDiagnosis> Misses;
    };

    std::string Show(const glm::vec3& value)
    {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "(%.3f, %.3f, %.3f)", value.x, value.y, value.z);
        return buffer;
    }

    std::string Percent(int part, int whole)
    {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.1f%%",
            whole == 0 ? 0.0 : 100.0 * static_cast<double>(part) / static_cast<double>(whole));
        return buffer;
    }

    //How far the box the server rebuilds may sit from the pose the shooter drew.
    //The two are the same lerp over the same positions, so the only honest
    //difference is float noise; a tick of error at walk speed is 0.083 blocks,
    //eighty times this.
    constexpr float RewindTolerance = 0.001f;

    //What the server's history held for one shot, at the instant it declared.
    struct ServedShot
    {
        bool InsideWindow = false;
        bool HasBox = false;
        glm::vec3 Centre{ 0.0f };
    };

    //Sits between the server and its link and reads every Fire the moment the
    //server pulls it off. HandleFire runs on that message before anything
    //steps, so the history and the tick read here are the ones it is about to
    //use - which is what lets the test check the box the server rewound to,
    //rather than a box it reconstructs from a guess about when the shot landed.
    class FireTap : public Transport
    {
    public:
        explicit FireTap(Transport& inner) : m_Inner(inner) {}

        //Nothing is read until there is a server and a target to read.
        void Watch(const MatchServer& server, PlayerId target)
        {
            m_Server = &server;
            m_Target = target;
        }

        //One per Fire received, in the order received.
        std::deque<ServedShot>& Served() { return m_Served; }

        void Send(PeerId peer, std::span<const std::uint8_t> data, Channel channel) override
        {
            m_Inner.Send(peer, data, channel);
        }

        void Broadcast(std::span<const std::uint8_t> data, Channel channel) override
        {
            m_Inner.Broadcast(data, channel);
        }

        void Disconnect(PeerId peer) override { m_Inner.Disconnect(peer); }
        void Advance(double seconds) override { m_Inner.Advance(seconds); }
        double RoundTripTime(PeerId peer) const override { return m_Inner.RoundTripTime(peer); }

        bool Poll(NetEvent& out) override
        {
            if (!m_Inner.Poll(out))
                return false;

            MessageId id = MessageId::Hello;
            FireMessage fire;
            if (m_Server != nullptr && out.Type == NetEventType::Message
                && PeekMessageId(out.Data, id) && id == MessageId::Fire && Decode(out.Data, fire))
            {
                //The window test is HandleFire's clamp, restated: inside it the
                //clamp is the identity and the server rewinds to exactly this.
                const double claimed =
                    static_cast<double>(fire.RenderTick) + static_cast<double>(fire.RenderAlpha);
                const double now = static_cast<double>(m_Server->Match().Tick());

                ServedShot served;
                served.InsideWindow =
                    claimed >= now - static_cast<double>(MaxRewindTicks) && claimed <= now;

                Aabb box;
                served.HasBox = m_Server->History().BoxAt(m_Target, claimed, PlayerHalfExtents, box);
                served.Centre = (box.Min + box.Max) * 0.5f;

                m_Served.push_back(served);
            }

            return true;
        }

    private:
        Transport& m_Inner;
        const MatchServer* m_Server = nullptr;
        PlayerId m_Target = InvalidPlayer;
        std::deque<ServedShot> m_Served;
    };

    //Two clients on one link. One strafes; the other aims at the pose it is
    //DRAWING and fires. The shot goes over the wire, the server rules on it,
    //and the ruling comes back over the wire.
    RunOutcome FireAtAStrafingTarget(const ShootingRun& settings)
    {
        const double step = FrameClock::FixedStepSeconds;

        NetworkSim sim;
        sim.Latency = settings.LatencyTicks * step;
        sim.Loss = settings.Loss;
        sim.Seed = 1;

        //No jitter, and that is a choice worth stating. Jitter moves a packet
        //off its tick boundary, and a rewind depth measured in whole ticks is
        //exactly what this run exists to measure; the 5% loss still leaves the
        //gaps in the client's sample ring that a bad network really produces.
        LoopbackNetwork network;
        SimulatedTransport serverNet(network.Server(), sim);

        PeerId shooterPeer = InvalidPeer;
        PeerId targetPeer = InvalidPeer;
        SimulatedTransport shooterNet(network.AddClient(shooterPeer), sim);
        SimulatedTransport targetNet(network.AddClient(targetPeer), sim);

        FireTap tap(serverNet);
        MatchServer server(FloorWorld(), "floor.vox", MapHash, Spawn, tap);
        MatchClient shooter(shooterNet, GoodLoader());
        MatchClient target(targetNet, GoodLoader());

        //The shooter never moves once it is in position: Move stays zero and
        //only Yaw and Pitch change, which is the aim. Fire reads them straight
        //off the input, so setting the input IS taking aim.
        CharacterInput shooterInput;

        //Yaw 90 faces +z, so Move.y of +1 walks +z and -1 walks back down it.
        //Walking backwards rather than turning around keeps the yaw constant
        //across a reversal, which keeps the reversal a fact about position
        //alone.
        CharacterInput targetInput;
        targetInput.Yaw = 90.0f;

        auto stepEveryone = [&]()
        {
            shooter.SetInput(shooterInput);
            target.SetInput(targetInput);
            shooter.Step(step);
            target.Step(step);
            server.Step(step);
        };

        for (int i = 0; i < HandshakeTicks; ++i)
            stepEveryone();

        REQUIRE(shooter.Connected());
        REQUIRE(target.Connected());

        const PlayerId shooterId = shooter.LocalPlayer();
        const PlayerId targetId = target.LocalPlayer();
        REQUIRE(shooter.Match().HasPlayer(targetId));
        tap.Watch(server, targetId);

        shooterInput.Move = glm::vec2(0.0f, 1.0f);
        shooterInput.Yaw = 0.0f;
        for (int i = 0; i < SeparationTicks; ++i)
            stepEveryone();

        shooterInput.Move = glm::vec2(0.0f);
        for (int i = 0; i < SettleTicks; ++i)
            stepEveryone();

        //One shot on its way somewhere.
        struct FiredShot
        {
            int Index = 0;
            float Alpha = 0.0f;
            glm::vec3 Direction{ 0.0f };
            glm::vec3 Drawn{ 0.0f };
            glm::vec3 Eye{ 0.0f };

            //The iteration the server handles it on. A packet is queued after
            //this iteration's client Advance has already run, so it cannot be
            //released before the next one however small the latency - which is
            //why a zero-latency link still costs one tick.
            int Due = 0;
        };

        RunOutcome outcome;

        std::deque<FiredShot> awaitingPresent;
        std::deque<FiredShot> awaitingRuling;
        std::deque<glm::vec3> presentPositions;

        int strafeTick = 0;
        int lastFireIteration = -TicksPerShot;
        int lastRespawnIteration = -RespawnQuietTicks;
        std::uint8_t previousHealth = server.HealthOf(targetId);
        std::optional<std::uint64_t> lastRulingTick;

        //A bound, not a schedule. Reaching it means a ruling never came back,
        //which the REQUIRE in every caller turns into a loud failure rather
        //than a quietly short run.
        const int cap = settings.Shots * (TicksPerShot + RespawnQuietTicks) + 600;

        for (int iteration = 0; iteration < cap && outcome.Rulings < settings.Shots; ++iteration)
        {
            targetInput.Move = glm::vec2(0.0f,
                (strafeTick / StrafeHalfPeriod) % 2 == 0 ? 1.0f : -1.0f);
            ++strafeTick;

            shooter.SetInput(shooterInput);
            target.SetInput(targetInput);
            shooter.Step(step);
            target.Step(step);

            //THE CONTRAST, resolved here and not later: HandleFire runs inside
            //the server's poll, BEFORE the match steps, so a shot handled this
            //tick sees the positions the previous tick ended at. Reading them
            //after the step would compare against a tick the server never used.
            while (!awaitingPresent.empty() && awaitingPresent.front().Due <= iteration)
            {
                const FiredShot shot = awaitingPresent.front();
                awaitingPresent.pop_front();

                const CharacterController& live = server.Match().Player(targetId);
                const glm::vec3 half = live.Config().HalfExtents;
                const std::vector<ShotCandidate> candidates{
                    ShotCandidate{ targetId, Aabb{ live.Position() - half, live.Position() + half } } };

                //The server's own eye, not the client's: the only thing this
                //branch changes is WHICH BOX, so that a difference in the
                //answer is the rewind and nothing else.
                const CharacterController& self = server.Match().Player(shooterId);
                const glm::vec3 eye =
                    self.Position() + glm::vec3(0.0f, self.Config().EyeOffset, 0.0f);

                const ShotResult result = ResolveShot(
                    server.Match().GetWorld(), candidates, eye, shot.Direction, ShotRange);

                presentPositions.push_back(live.Position());

                if (result.Victim == targetId)
                    ++outcome.PresentStateHits;
            }

            server.Step(step);

            //Health only ever falls while a player lives, so a rise is the
            //respawn that follows a death.
            const std::uint8_t health = server.HealthOf(targetId);
            if (health > previousHealth)
            {
                ++outcome.Kills;
                lastRespawnIteration = iteration;
            }
            previousHealth = health;

            //ShotResolved is reliable, so exactly one ruling comes back per
            //accepted shot, in the order the shots were fired. A ruling is new
            //when it arrived on a client tick this loop has not already
            //counted; shots are spaced wider than the deepest retransmission
            //here so that two can never share one.
            const std::optional<MatchClient::ShotReport>& ruling = shooter.LastShot();
            if (ruling.has_value()
                && (!lastRulingTick.has_value() || *lastRulingTick != ruling->ReceivedAtTick))
            {
                lastRulingTick = ruling->ReceivedAtTick;
                ++outcome.Rulings;

                const FiredShot shot = awaitingRuling.empty() ? FiredShot{} : awaitingRuling.front();
                if (!awaitingRuling.empty())
                    awaitingRuling.pop_front();

                const glm::vec3 present =
                    presentPositions.empty() ? glm::vec3(0.0f) : presentPositions.front();
                if (!presentPositions.empty())
                    presentPositions.pop_front();

                //Fire reached the server before its ruling left it, so the
                //box for this shot is already at the front.
                const ServedShot served = tap.Served().empty() ? ServedShot{} : tap.Served().front();
                if (!tap.Served().empty())
                    tap.Served().pop_front();

                if (served.InsideWindow && served.HasBox)
                {
                    ++outcome.RewindChecked;
                    outcome.MaxRewindError = glm::max(outcome.MaxRewindError,
                        glm::distance(shot.Drawn, served.Centre));
                }

                if (ruling->Victim == targetId)
                {
                    ++outcome.Hits;
                }
                else
                {
                    outcome.Misses.push_back(ShotDiagnosis{
                        shot.Index, shot.Alpha, shot.Drawn, shot.Eye, ruling->Impact, present });
                }
            }

            if (outcome.Fired >= settings.Shots
                || iteration - lastFireIteration < TicksPerShot
                || iteration - lastRespawnIteration < RespawnQuietTicks)
                continue;

            //THE AIM, and the whole point of the gate: the alpha handed to
            //PoseOf is the alpha handed to Fire, with no Step in between to
            //move the clock underneath them. Cycling it exercises the
            //fractional instant rather than only whole ticks.
            const float alpha = 0.25f * static_cast<float>(outcome.Fired % 4);
            const MatchClient::RemotePose pose = shooter.PoseOf(targetId, alpha);

            const CharacterController& self = shooter.Match().Player(shooterId);
            const glm::vec3 eye =
                self.Position() + glm::vec3(0.0f, self.Config().EyeOffset, 0.0f);

            //Yaw and pitch rather than a vector, because that is what the wire
            //carries and what the server turns back into a direction with
            //AimDirection. This inverts AimDirection rather than reading its
            //formula a second time: yaw 0 faces +x and yaw grows toward +z.
            const glm::vec3 toTarget = pose.Position - eye;
            const float flat = glm::length(glm::vec2(toTarget.x, toTarget.z));
            shooterInput.Yaw = glm::degrees(std::atan2(toTarget.z, toTarget.x));
            shooterInput.Pitch = glm::degrees(std::atan2(toTarget.y, flat));

            const CharacterController& serverSelf = server.Match().Player(shooterId);
            const glm::vec3 serverEye = serverSelf.Position()
                + glm::vec3(0.0f, serverSelf.Config().EyeOffset, 0.0f);
            outcome.MaxEyeError = glm::max(outcome.MaxEyeError, glm::distance(eye, serverEye));

            shooter.SetInput(shooterInput);
            shooter.Fire(alpha);

            FiredShot fired;
            fired.Index = outcome.Fired;
            fired.Alpha = alpha;
            fired.Direction = AimDirection(shooterInput.Yaw, shooterInput.Pitch);
            fired.Drawn = pose.Position;
            fired.Eye = eye;
            fired.Due = iteration + glm::max(settings.LatencyTicks, 1);

            awaitingPresent.push_back(fired);
            awaitingRuling.push_back(fired);

            ++outcome.Fired;
            lastFireIteration = iteration;
        }

        return outcome;
    }

    void ReportMisses(const RunOutcome& outcome)
    {
        //Printed before anything is changed, per the standing instruction that
        //a failing gate is evidence. Each of the ways this can fail - an alpha
        //mismatch, an eye offset applied on one side only, an aim convention
        //inverted against AimDirection - produces a NEAR miss, and only the
        //numbers tell them apart.
        for (const ShotDiagnosis& miss : outcome.Misses)
        {
            MESSAGE("MISS shot " << miss.Index << " alpha " << miss.Alpha
                << ": drawn " << Show(miss.Drawn)
                << ", eye " << Show(miss.Eye)
                << ", impact " << Show(miss.Impact)
                << ", target when resolved " << Show(miss.Present));
        }
    }
}

TEST_CASE("A shot aimed where the client renders a target hits it, through the wire")
{
    //THE STAGE'S GATE. The shooter aims at exactly what PoseOf reports - the
    //same function that draws the target - and the server must agree.
    //
    //Two clients on a 3-tick one-way link (100 ms RTT, 5% loss, seed 1). One
    //strafes across the other's line of sight; the other aims at the pose it is
    //DRAWING and fires. Every shot must connect.
    //
    //Sixty shots rather than a handful: thirty ticks apart, and with a forty-tick
    //pause after each death, they span about 2,600 ticks - some forty strafe
    //reversals and, as the run reports, twenty deaths. A rewind that only works
    //away from a turn, or only before the first respawn, has nowhere to hide.
    const RunOutcome outcome = FireAtAStrafingTarget(ShootingRun{ 3, 60, 0.05f });

    REQUIRE(outcome.Fired == 60);

    //Every shot was ruled on. Rulings are reliable and one per shot; a shortfall
    //means a shot was dropped by the fire rate or two rulings shared a tick, and
    //either would make the hit rate a fraction of the wrong thing.
    REQUIRE(outcome.Rulings == outcome.Fired);

    MESSAGE("100 ms RTT, 5% loss: " << outcome.Hits << "/" << outcome.Fired
        << " hits, " << outcome.Kills << " kills, max eye error "
        << outcome.MaxEyeError << " blocks");

    ReportMisses(outcome);

    CHECK(outcome.Hits == outcome.Fired);

    //Landing every shot is necessary and not enough: a rewind a whole tick
    //short lands every one of these too. The box the server rewound to has to
    //be the pose the shooter drew.
    MESSAGE("rewind error: max " << outcome.MaxRewindError << " blocks over "
        << outcome.RewindChecked << " shots inside the window");
    REQUIRE(outcome.RewindChecked > 0);
    CHECK(outcome.MaxRewindError < RewindTolerance);
}

TEST_CASE("The same shots miss when the server does not rewind")
{
    //THE CONTRAST. Without this number the gate above proves only that the test
    //is easy to pass.
    //
    //The same run. Every shot is resolved twice: once by the server, which
    //rewinds, and once here against the boxes the server holds at the moment it
    //handles the shot. Same ray, same eye, same world - the only difference is
    //WHICH BOX, which is the mutation Task 3's oracle pinned in the abstract and
    //this one runs through a real wire.
    const RunOutcome outcome = FireAtAStrafingTarget(ShootingRun{ 3, 60, 0.05f });

    REQUIRE(outcome.Fired == 60);
    REQUIRE(outcome.Rulings == outcome.Fired);

    MESSAGE("100 ms RTT, 5% loss: rewound " << outcome.Hits << "/" << outcome.Fired
        << " (" << Percent(outcome.Hits, outcome.Fired) << "), present-state "
        << outcome.PresentStateHits << "/" << outcome.Fired
        << " (" << Percent(outcome.PresentStateHits, outcome.Fired) << ")");

    CHECK(outcome.PresentStateHits < outcome.Hits);
}

TEST_CASE("Hit rate across latencies")
{
    //THE MEASURED NUMBER, reported rather than asserted tightly. 200 shots on
    //each of four links, with the rewind-off column beside it, because a hit
    //rate on its own cannot tell a working rewind from an easy test.
    //
    //166.7 ms rather than the design's 150: 150 ms RTT is 4.5 ticks one way,
    //which is not a whole tick, and Stage 3 substituted the same number for the
    //same reason.
    //
    //The 300 ms row is above the 250 ms cap and is EXPECTED to be worse. How
    //much worse is what says whether the cap was set sensibly, so it is
    //reported and not asserted.
    //
    //THE ARITHMETIC THIS TABLE MEASURES, because the rows are unreadable
    //without it. A shot's rewind depth - how far back the server has to look to
    //find the box the shooter aimed at - is
    //
    //    2L + InterpolationDelayTicks - 1 - alpha ticks
    //
    //for a one-way latency of L ticks: L for the snapshot to arrive, L for the
    //Fire to come back, six because PoseOf deliberately draws that far behind
    //the newest snapshot, and one back because the server handles a shot before
    //it steps. That is 7, 11, 15 and 23 ticks for the four rows here - 7 and
    //not 5 on the first, because this harness cannot deliver a packet sooner
    //than the next tick, so a zero-latency link still costs one each way -
    //against a
    //MaxRewindTicks of 15. The last row is over the cap by eight ticks - 0.67
    //blocks at walk speed, twice the half-width of the box - which is why it
    //collapses rather than degrades.
    struct Row
    {
        int LatencyTicks = 0;
        const char* Label = "";

        //Hits out of 200 this row must produce, or zero where the design
        //promises nothing. 190 is the design's floor of 95%, which it promises
        //at every latency at or under the 250 ms cap.
        int Floor = 0;
    };

    const Row rows[] = {
        Row{ 0, "0 ms RTT", 190 },
        Row{ 3, "100 ms RTT", 190 },

        //185, not 190, because the design's 95% covers shots whose rewind fits
        //inside the window, and at this latency a lost shot's does not.
        //
        //The depth this row needs is 15 ticks: exactly MaxRewindTicks, with
        //nothing to spare. SimulatedTransport models the loss of a reliable
        //packet as a retransmission costing one extra round trip, so the ~5% of
        //Fire messages that are lost reach the server ten ticks late and need a
        //depth of 25. They are clamped to 15, resolve against a box 0.70-0.83
        //blocks further along, and miss: 11 of 200. Every other shot on this
        //row lands - the clean-link run under this table lands all 200.
        //
        //This was held open until the history's off-by-one was fixed, in case
        //that was costing hits too. It was not: fixed and unfixed, the table is
        //identical shot for shot, and all 11 misses are retransmissions. What
        //changed is where a shot inside the window lands, which the rewind error
        //below measures and a hit rate cannot.
        //
        //Raising MaxRewindTicks would buy these back and widen the "shot behind
        //cover" window for every shot to do it, so it was decided against on
        //2026-09-12 and the promise narrowed instead.
        Row{ 5, "166.7 ms RTT", 185 },

        Row{ 9, "300 ms RTT", 0 },
    };

    for (const Row& row : rows)
    {
        const RunOutcome outcome =
            FireAtAStrafingTarget(ShootingRun{ row.LatencyTicks, 200, 0.05f });

        REQUIRE(outcome.Fired == 200);
        REQUIRE(outcome.Rulings == outcome.Fired);

        MESSAGE(std::string(row.Label) << ", 5% loss: rewound " << outcome.Hits
            << "/" << outcome.Fired << " (" << Percent(outcome.Hits, outcome.Fired)
            << "), present-state " << outcome.PresentStateHits << "/" << outcome.Fired
            << " (" << Percent(outcome.PresentStateHits, outcome.Fired) << ")");

        MESSAGE(std::string(row.Label) << ", 5% loss: rewind error max "
            << outcome.MaxRewindError << " blocks over " << outcome.RewindChecked
            << " shots inside the window");

        if (row.Floor > 0)
            CHECK(outcome.Hits >= row.Floor);

        //On every row, the 300 ms one included: a shot that does fit inside
        //the window has to land exactly, however many around it do not.
        CHECK(outcome.MaxRewindError < RewindTolerance);
    }

    //THE ROW ABOVE, WITHOUT THE RETRANSMISSIONS. One extra run, on the one
    //latency where the table falls short of the design's floor, because "the
    //rewind is wrong at 166.7 ms" and "the rewind is exact and the cap is two
    //ticks too shallow to survive a retransmitted shot" are different findings
    //and the table alone cannot tell them apart.
    const RunOutcome clean = FireAtAStrafingTarget(ShootingRun{ 5, 200, 0.0f });

    REQUIRE(clean.Fired == 200);
    MESSAGE("166.7 ms RTT, no loss: rewound " << clean.Hits << "/" << clean.Fired
        << " (" << Percent(clean.Hits, clean.Fired) << "), present-state "
        << clean.PresentStateHits << "/" << clean.Fired
        << " (" << Percent(clean.PresentStateHits, clean.Fired) << ")");

    CHECK(clean.Hits == clean.Fired);

    //With nothing retransmitted, every shot's rewind fits, so every one of
    //them is checked - at a depth of exactly MaxRewindTicks for alpha 0.
    MESSAGE("166.7 ms RTT, no loss: rewind error max " << clean.MaxRewindError
        << " blocks over " << clean.RewindChecked << " shots inside the window");
    CHECK(clean.RewindChecked == clean.Fired);
    CHECK(clean.MaxRewindError < RewindTolerance);
}
