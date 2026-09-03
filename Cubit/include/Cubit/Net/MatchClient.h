#pragma once

#include "Cubit/Core.h"
#include "Cubit/Net/Protocol.h"
#include "Cubit/Net/Transport.h"
#include "Cubit/Voxel/BlockEdit.h"
#include "Cubit/Voxel/MatchState.h"
#include "Cubit/Voxel/World.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

//A map the client found on its own disk, and the hash of the bytes it came
//from. The hash is checked against the server's before anything is trusted.
struct LoadedMap
{
    World Map;
    std::uint64_t Hash = 0;
};

//The client half of a match. It NEVER STEPS.
//
//That is the defining constraint of Stage 2, not an omission. Input goes up,
//snapshots come down and are written straight in, so the latency is plainly
//visible instead of hidden behind a guess. It makes Stage 3's diff one
//sentence: start calling Step, and replay unacknowledged inputs after each
//snapshot.
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

    //True when the handshake failed: a protocol mismatch, a missing map, or a
    //map whose bytes differ from the server's. Terminal.
    bool Rejected() const { return m_Rejected; }

    PlayerId LocalPlayer() const { return m_LocalPlayer; }

    const MatchState& Match() const { return m_Match; }
    MatchState& MatchForWrite() { return m_Match; }

    //Yaw in x, pitch in y, as last reported for this player. Zero for anyone
    //not in the last snapshot.
    glm::vec2 ViewAngles(PlayerId player) const;

    double RoundTripTime() const;

private:
    void HandleWelcome(std::span<const std::uint8_t> data);
    void HandleSnapshot(std::span<const std::uint8_t> data);
    void HandleEditApplied(std::span<const std::uint8_t> data);

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

    //Monotonic, and NOT a tick. In this stage the client never steps, so it has
    //no simulation tick to name; the server uses this only to drop stale and
    //duplicated packets on an unordered channel.
    std::uint32_t m_Sequence = 0;
    CharacterInput m_Input;
    bool m_HasInput = false;

    //Highest snapshot tick applied. Jitter reorders packets, and applying an
    //older snapshot after a newer one yanks the world backwards.
    std::uint64_t m_LastSnapshotTick = 0;

    std::map<PlayerId, glm::vec2> m_ViewAngles;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
