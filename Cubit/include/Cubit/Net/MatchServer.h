#pragma once

#include "Cubit/Core.h"
#include "Cubit/Net/Protocol.h"
#include "Cubit/Net/Transport.h"
#include "Cubit/Voxel/BlockEdit.h"
#include "Cubit/Voxel/HitboxHistory.h"
#include "Cubit/Voxel/MatchState.h"
#include "Cubit/Voxel/World.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

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

    const MatchState& Match() const { return m_Match; }

    //Every edit applied since construction, in application order. Sent to
    //joiners so a client arriving after somebody dug a hole sees the hole.
    const std::vector<BlockEdit>& EditLog() const { return m_EditLog; }

    //Where everybody has recently been, which is what a shot is resolved
    //against. Exposed for tests and for nothing else: the rewind happens in
    //here.
    const HitboxHistory& History() const { return m_History; }

    //Connected peers, whether or not they have completed the handshake.
    std::size_t ClientCount() const { return m_Clients.size(); }

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
        };

        //Newest input tick APPLIED, not the newest received. This is what a
        //snapshot acknowledges and what the client replays on top of: naming
        //something merely received would have the client discard an input the
        //server has not stepped yet.
        std::uint64_t LastInputTick = 0;

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

        //Last reported view angles, resent in every snapshot so remote
        //characters are drawn facing the right way.
        float Yaw = 0.0f;
        float Pitch = 0.0f;
    };

    //An edit waiting for this tick's ordered application.
    struct PendingEdit
    {
        PlayerId Player = InvalidPlayer;
        BlockEdit Edit;
    };

    void HandleConnected(PeerId peer);
    void HandleDisconnected(PeerId peer);
    void HandleMessage(PeerId peer, std::span<const std::uint8_t> data);

    //Applies this tick's edits in player-id order and tells every joined client
    //about each one that actually changed the world.
    void ApplyPendingEdits();

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
    void SendToJoined(const std::vector<std::uint8_t>& payload, Channel channel);

    //Returns the client for a peer, or nullptr when it has gone.
    Client* Find(PeerId peer);

    MatchState m_Match;
    std::string m_MapName;
    std::uint64_t m_MapHash = 0;
    glm::vec3 m_Spawn{ 0.0f };
    Transport& m_Transport;

    std::vector<Client> m_Clients;
    std::vector<PendingEdit> m_PendingEdits;
    std::vector<BlockEdit> m_EditLog;
    HitboxHistory m_History;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
