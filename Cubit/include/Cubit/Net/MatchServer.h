#pragma once

#include "Cubit/Core.h"
#include "Cubit/Net/Protocol.h"
#include "Cubit/Net/Transport.h"
#include "Cubit/Voxel/BlockEdit.h"
#include "Cubit/Voxel/EditRules.h"
#include "Cubit/Voxel/HitboxHistory.h"
#include "Cubit/Voxel/MatchState.h"
#include "Cubit/Voxel/Support.h"
#include "Cubit/Voxel/World.h"

#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

//Minimum ticks between one client's shots. Ten is six shots a second.
//
//It is the weapon's rate of fire and, at the same time, the flood answer for a
//new reliable client-to-server message: a client that spams Fire has its extras
//dropped rather than queued.
constexpr int TicksBetweenShots = 10;

//How far a shot carries, in blocks.
constexpr float ShotRange = 128.0f;

//Health a player starts and respawns with.
constexpr std::uint8_t StartingHealth = 100;

//Damage one shot does. Three shots kill, with the third overshooting by two -
//health is clamped at zero rather than wrapping, which an unsigned type makes
//worth stating.
constexpr std::uint8_t ShotDamage = 34;

//The authority. Owns the only MatchState anybody is entitled to believe.
//
//Holds no window, no renderer and no GL context, so it runs anywhere a World
//does. Server.exe is a bare main() around this, on the MapGen precedent.
class CB_API MatchServer
{
public:
    //`mapName` and `mapHash` are what joining clients are told to load and
    //check against. `spawn` is where every joining player is placed - players
    //do not collide with each other in this stage, so one point is enough.
    //`transport` must outlive this.
    MatchServer(World world, std::string mapName, std::uint64_t mapHash,
        const glm::vec3& spawn, Transport& transport);

    //One authoritative tick: service the transport, admit joiners, collect this
    //tick's inputs and edits, apply the edits in player-id order, step the
    //match, send a snapshot to each joined client.
    void Step(double seconds);

    //Tells the server it lost `ticks` steps it will never run - a stall longer
    //than FrameClock repays. Each of those steps would have taken one input from
    //every client, so up to that many of each client's oldest queued inputs are
    //skipped. Left queued they would never drain, since a step takes one input
    //and a client sends one, and every later input would be applied that many
    //ticks late for the rest of the session.
    //
    //Call after the frame's steps have run: the first of them is what reads the
    //inputs sent during the stall off the transport.
    void SkipTicks(int ticks);

    //Changes many blocks as one operation, for a game rule - an explosion,
    //terrain giving way - and tells every joined client. Returns how many of the
    //edits changed a block.
    //
    //Applied now and in order, following ApplyBlockEdit's rules: an out-of-range
    //or no-op edit is skipped. NOT checked with IsEditLegal: reach and overlap
    //are rules for what a player may do, and a rule that places blocks answers
    //for where it puts them, players included. Every change enters the edit log,
    //so a later joiner sees it, and goes out reliably in as many EditApplied
    //messages as MaxEditsPerMessage needs. Reliable messages to a client stay in
    //order, so a client sees these and its own EditResults in the order the
    //server made them.
    //
    //Whatever the batch leaves with nothing holding it up is cleared in the same
    //call and carried by the same messages, so the count includes those cells.
    //See FindUnsupported for the rule.
    std::size_t ApplyEdits(std::span<const BlockEdit> edits);

    const MatchState& Match() const { return m_Match; }

    //Every cell whose block differs from the loaded map, each with the block it
    //holds now. Sent to joiners so a client arriving after somebody dug a hole
    //sees the hole.
    //
    //A diff, not a history: editing a cell again replaces its entry, and putting
    //it back to the map's block removes it. So its size follows how much of the
    //map differs, not how long the match has run. In no particular order - each
    //entry sets a different cell, so the order a joiner replays them in cannot
    //change the world it ends up with.
    const std::vector<BlockEdit>& EditLog() const { return m_EditLog; }

    //How many edits have been accepted since construction. For tests and
    //diagnostics: the log no longer says how many edits happened, because it
    //collapses repeated edits to one cell and drops cells put back to the map.
    std::uint64_t AcceptedEditCount() const { return m_AcceptedEditCount; }

    //Where everybody has recently been, which is what a shot is resolved
    //against. Exposed for tests and for nothing else: the rewind happens in
    //here.
    const HitboxHistory& History() const { return m_History; }

    //Connected peers, whether or not they have completed the handshake.
    std::size_t ClientCount() const { return m_Clients.size(); }

    //A player's current health, or zero if nobody holds that id.
    std::uint8_t HealthOf(PlayerId player) const;

private:
    //One connected participant. A peer exists from the moment the socket
    //connects; a Player only once Hello has been accepted.
    struct Client
    {
        PeerId Peer = InvalidPeer;
        PlayerId Player = InvalidPlayer;

        //One input waiting for a tick to consume it, in the client's own tick
        //numbering.
        struct QueuedInput
        {
            std::uint64_t Tick = 0;
            CharacterInput Input;

            //The edit made on this tick, if any. Queued with the input rather
            //than applied on arrival, so it lands on exactly the step the
            //client predicted it on.
            std::optional<BlockEdit> Edit;
        };

        //Newest input tick APPLIED, not the newest received. This is what a
        //snapshot acknowledges and what the client replays on top of: naming
        //something merely received would have the client discard an input the
        //server has not stepped yet.
        std::uint64_t LastInputTick = 0;

        //Which ticks at or below LastInputTick this server has had - taken,
        //skipped or dropped. Bit k is tick LastInputTick - k.
        //
        //What tells a repeat apart from an input that arrives after the server
        //moved past its tick. Both are behind LastInputTick, but only the repeat
        //has been answered: the other was never received, so an edit on it is
        //still owed a refusal.
        std::uint64_t SeenInputs = 0;

        //Oldest first. Inputs arrive bundled and out of order on an unordered
        //channel; a step takes the front.
        //
        //A queue rather than Stage 2's single slot because the client now
        //predicts: it has already simulated each of these and is waiting to be
        //told they were right. Dropping all but the newest, which is what the
        //single slot did, would throw away intent that has already been shown
        //on somebody's screen.
        std::deque<QueuedInput> Queue;

        //Set once an input has been dropped for a full queue and cleared once
        //the queue has room again, so a client that stays ahead for many ticks
        //logs one warning per overflow episode instead of one per dropped
        //input.
        bool QueueOverflowWarned = false;

        //The queue's depth at the start of each of the last
        //SpareInputWindowTicks steps, before that step took an input - a ring,
        //NextDepthSample the slot the next step overwrites. What SpareInputs is
        //worked out from.
        std::array<std::uint8_t, SpareInputWindowTicks> DepthSamples{};
        std::size_t DepthSampleCount = 0;
        std::size_t NextDepthSample = 0;

        //Last reported view angles, resent in every snapshot so remote
        //characters are drawn facing the right way.
        float Yaw = 0.0f;
        float Pitch = 0.0f;

        //Server tick of this client's last accepted shot, for the fire rate.
        //Meaningless until HasFired is set - tick 0 is a real tick a shot can
        //land on (a Hello and a Fire can both be handled inside the very
        //first Step, before m_Match.Step has incremented the counter even
        //once), so it cannot double as its own "never fired" sentinel.
        std::uint64_t LastShotTick = 0;

        //Set on this client's first accepted shot and never cleared, so the
        //fire rate can tell "never fired" apart from "fired on tick 0" - the
        //two are otherwise indistinguishable and the first shot of a match is
        //therefore always allowed regardless of which one it was.
        bool HasFired = false;

        //Set once a shot has been dropped for the fire rate and cleared once
        //one is accepted, so a client holding the button down logs one warning
        //per episode rather than one per dropped shot. Same shape as
        //QueueOverflowWarned.
        bool FireRateWarned = false;

        //Health lives here rather than on MatchState because MatchState is the
        //whole SIMULATED state and health is not simulated by Step - it changes
        //only when a game rule fires. MatchState::PlayerForWrite exists for
        //exactly this kind of caller, and says so in its own comment.
        std::uint8_t Health = StartingHealth;
    };

    void HandleConnected(PeerId peer);
    void HandleDisconnected(PeerId peer);
    void HandleMessage(PeerId peer, std::span<const std::uint8_t> data);

    //Rules on one client's edit, applies it if legal, answers the editor with
    //EditResult and tells everyone else with EditApplied. A dig that leaves
    //blocks with nothing holding them up clears those too, in the same step, and
    //tells everybody - the editor included, since nobody predicted them.
    void ApplyInputEdit(PlayerId player, PeerId peer, std::uint64_t clientTick,
        const BlockEdit& edit);

    //Answers an edit whose input the server threw away - skipped after a stall,
    //dropped from a full queue, or arrived after the server moved past its tick
    //- with a refusal carrying the server's block.
    void RefuseDiscardedEdit(PeerId peer, std::uint64_t clientTick, const BlockEdit& edit);

    //Moves a client's LastInputTick forward to an input just taken off the
    //front of its queue, recording that input as had and every tick it jumped
    //over as never received.
    static void PassInput(Client& client, std::uint64_t tick);

    //How many inputs this client keeps queued beyond what it needs, for its
    //snapshot entry. See PlayerSnapshot::SpareInputs.
    static std::uint8_t SpareInputsOf(const Client& client);

    //Brings the log up to date with an edit just applied. `previous` is the
    //block the cell held immediately before it.
    void RecordInLog(const BlockEdit& edit, BlockId previous);

    //Applies edits in order, logging each that changed a block, and returns
    //those. Sends nothing: what reaches clients is one decision, made once the
    //collapse below is known.
    std::vector<BlockEdit> ApplyAndLog(std::span<const BlockEdit> edits);

    //The edits that clear whatever `applied` has left with nothing holding it
    //up - empty unless it emptied a cell. Decided here and never predicted: a
    //client's world holds edits the server has not ruled on, so a client working
    //this out for itself could reach a different answer and desync.
    std::vector<BlockEdit> CollapseEdits(std::span<const BlockEdit> applied);

    //Tells joined clients about blocks that changed, in as many EditApplied
    //messages as MaxEditsPerMessage needs. `except` skips one peer.
    void Broadcast(std::span<const BlockEdit> changed, PeerId except = InvalidPeer);

    //Resolves one shot against the world as the shooter saw it and tells
    //everybody the answer.
    void HandleFire(Client& shooter, const FireMessage& fire);

    void SendSnapshots();

    //Sends one already-encoded payload to every client that has completed the
    //handshake, one Send per peer.
    //
    //Never Broadcast, for two independent reasons, both of which cost a real
    //day to find during Stage 2 and are recorded in the execution ledger.
    //
    //Loss: SimulatedTransport::Broadcast draws loss ONCE for the whole call, so
    //every recipient shares one fate - all get the packet or none do. Under
    //Broadcast, clients would desync in lockstep, hiding precisely the
    //per-client divergence a deterministic bad network exists to catch. Real
    //ENet loses each peer's copy independently, and a per-peer Send loop is
    //what reproduces that.
    //
    //Ordering: the reliable-ordering guarantee is keyed per destination peer.
    //A reliable broadcast is keyed InvalidPeer, so it sits in a different
    //ordering stream from a reliable unicast to the same peer - and Welcome is
    //a unicast. A broadcast EditApplied could therefore overtake the Welcome
    //that established the connection it belongs to.
    //
    //Skipping peers that have not completed the handshake is part of the same
    //answer rather than tidiness. A snapshot is unreadable before Welcome (the
    //client does not yet know which player is its own), and an EditApplied
    //before Welcome would be applied twice by a client that then reads the
    //edit log Welcome carries.
    //
    //`except` skips one peer - the editor, for an edit it already knows the
    //fate of. The default names no real peer, so it skips nobody.
    void SendToJoined(const std::vector<std::uint8_t>& payload, Channel channel,
        PeerId except = InvalidPeer);

    //Returns the client for a peer, or nullptr when it has gone.
    Client* Find(PeerId peer);

    MatchState m_Match;
    std::string m_MapName;
    std::uint64_t m_MapHash = 0;
    glm::vec3 m_Spawn{ 0.0f };
    Transport& m_Transport;

    std::vector<Client> m_Clients;
    std::vector<BlockEdit> m_EditLog;

    //The map's own block for each cell in the log, recorded the first time the
    //cell changes - the one moment it is known without keeping a copy of the
    //whole map. Dropped again when the cell goes back to it.
    std::map<glm::ivec3, BlockId, IVec3Less> m_MapBlock;

    //Where each logged cell's entry sits in m_EditLog, so an update or a
    //removal does not search the log.
    std::map<glm::ivec3, std::size_t, IVec3Less> m_LogIndex;

    std::uint64_t m_AcceptedEditCount = 0;
    HitboxHistory m_History;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
