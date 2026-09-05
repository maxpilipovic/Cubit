#pragma once

#include "Cubit/Core.h"
#include "Cubit/FrameClock.h"
#include "Cubit/Net/Protocol.h"
#include "Cubit/Net/Transport.h"
#include "Cubit/Voxel/BlockEdit.h"
#include "Cubit/Voxel/MatchState.h"
#include "Cubit/Voxel/World.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

//How many unacknowledged inputs a client keeps for replay.
//
//Two seconds at 60 Hz, and far more than the ten or so a healthy connection
//holds (about RTT / 16.7 ms). A client that reaches this has heard nothing from
//the server for two seconds and has a bigger problem than replay accuracy; the
//bound exists so a silent server cannot grow this without limit.
constexpr std::size_t MaxUnackedInputs = 120;

//How far prediction may disagree with the server before the correction is
//shown, in blocks, as a full 3D distance.
//
//One starved server tick while walking costs WalkSpeed / 60 = 0.083 blocks, so
//0.15 absorbs a single dropped input and little more.
//
//It is a deadzone, which has a known cost: a small persistent error is never
//corrected, so the client is not exactly the server between snaps. That is
//bounded by construction - past this it snaps - and deliberate. This is the one
//number in the stage chosen by reasoning rather than measurement; if the
//measured correction rate is bad, suspect this first.
constexpr float CorrectionThreshold = 0.15f;

//A map the client found on its own disk, and the hash of the bytes it came
//from. The hash is checked against the server's before anything is trusted.
struct LoadedMap
{
    World Map;
    std::uint64_t Hash = 0;
};

//The client half of a match. It predicts its own player and nothing else.
//
//Through Stage 2 it never stepped at all, deliberately, so the latency was
//plainly visible rather than hidden behind a guess. This is the stage that
//hides it: input is stepped immediately, kept until the server acknowledges
//it, and replayed on top of every correction.
//
//It holds a MatchState anyway, for two reasons: it needs a World to render and
//a roster to draw, and Stage 3 needs somewhere to start stepping.
class CB_API MatchClient
{
public:
    //Called with the map name from Welcome. Returns the world to display and
    //the hash of the file it came from, or nothing when the map is missing.
    //
    //A callback rather than a path, so a test can hand over a trivial world
    //while the Sandbox reads 23.8 MB of .vox from disk.
    using MapLoader = std::function<std::optional<LoadedMap>(const std::string& mapName)>;

    //`transport` must outlive this.
    MatchClient(Transport& transport, MapLoader loadMap);

    //Services the transport, sends this frame's input, and applies whatever
    //arrived. Deliberately does not advance the simulation.
    void Step(double seconds);

    //What to send on the next Step. Held rather than sent immediately so the
    //caller can set it whenever it likes without deciding the send rate.
    void SetInput(const CharacterInput& input);

    //Asks the server to change a block. Nothing happens locally until the
    //server's answer arrives - that round trip is the point.
    void RequestEdit(const BlockEdit& edit);

    //True once Welcome has been accepted and the world is loaded.
    bool Connected() const { return m_Connected; }

    //True when the handshake failed: a missing map, a map whose bytes differ
    //from the server's, a protocol mismatch, or a server that never answered
    //at all. The last two are indistinguishable from here - both arrive as a
    //bare Disconnected with no reply attached - so they share one branch.
    //Terminal.
    bool Rejected() const { return m_Rejected; }

    PlayerId LocalPlayer() const { return m_LocalPlayer; }

    const MatchState& Match() const { return m_Match; }
    MatchState& MatchForWrite() { return m_Match; }

    //Yaw in x, pitch in y, as last reported for this player. Zero for anyone
    //not in the last snapshot.
    glm::vec2 ViewAngles(PlayerId player) const;

    double RoundTripTime() const;

    //The newest tick any snapshot has reported. NOT this client's own tick:
    //since prediction, Match().Tick() is the client's, free-running from the
    //one Welcome carried and advanced once per predicted step. The server's is
    //tracked separately because remote-player interpolation is expressed in it.
    std::uint64_t ServerTick() const { return m_ServerTick; }

    //What reconciliation has actually been doing. The stage's acceptance
    //number: "corrections per 1000 ticks" replaces "it feels smooth" the way
    //3,900 B/s replaced "bandwidth is fine", and unlike a playtest it can be
    //re-run to catch a regression.
    struct CorrectionStats
    {
        //Snapshots reconciled - the denominator, and not the same as the number
        //of ticks: snapshots are lost.
        std::uint64_t Snapshots = 0;

        //Reconciliations whose disagreement exceeded the threshold and were
        //therefore shown.
        std::uint64_t Count = 0;

        //Mean and largest magnitude of those, in blocks. Zero when there have
        //been none.
        float Mean = 0.0f;
        float Max = 0.0f;
    };

    CorrectionStats Corrections() const;

private:
    void HandleWelcome(std::span<const std::uint8_t> data);
    void HandleSnapshot(std::span<const std::uint8_t> data);
    void HandleEditApplied(std::span<const std::uint8_t> data);

    //Writes the authoritative state in, replays what the server has not
    //acknowledged, and decides whether the difference is worth showing.
    void Reconcile(const PlayerSnapshot& entry);

    //Ends the connection and latches Rejected.
    void Reject(const char* reason);

    Transport& m_Transport;
    MapLoader m_LoadMap;

    //A placeholder until Welcome arrives with the real map, matching what the
    //Sandbox already does. MatchState needs a World to exist at all.
    MatchState m_Match{ World(1, 1, 1) };

    PeerId m_ServerPeer = InvalidPeer;
    PlayerId m_LocalPlayer = InvalidPlayer;

    bool m_SaidHello = false;
    bool m_Connected = false;
    bool m_Rejected = false;

    CharacterInput m_Input;
    bool m_HasInput = false;

    //Highest snapshot tick applied. Jitter reorders packets, and applying an
    //older snapshot after a newer one yanks the world backwards.
    std::uint64_t m_LastSnapshotTick = 0;

    std::map<PlayerId, glm::vec2> m_ViewAngles;

    //One input the server has not yet acknowledged, kept so it can be replayed
    //on top of a correction.
    struct PendingInput
    {
        std::uint64_t Tick = 0;
        CharacterInput Input;
    };

    //Oldest first, and consecutive: one input is produced per tick and they are
    //dropped from the front as they are acknowledged.
    std::deque<PendingInput> m_Unacked;

    std::uint64_t m_ServerTick = 0;

    //The step length prediction used, so replay uses the same one. A replay at
    //a different step length is a different simulation.
    float m_StepSeconds = static_cast<float>(FrameClock::FixedStepSeconds);

    std::uint64_t m_SnapshotsReconciled = 0;
    std::uint64_t m_CorrectionCount = 0;
    float m_CorrectionTotal = 0.0f;
    float m_CorrectionMax = 0.0f;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
