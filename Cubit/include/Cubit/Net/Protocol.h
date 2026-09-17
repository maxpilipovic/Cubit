#pragma once

#include "Cubit/Core.h"
#include "Cubit/Net/ByteReader.h"
#include "Cubit/Net/ByteWriter.h"
#include "Cubit/Voxel/BlockEdit.h"
#include "Cubit/Voxel/CharacterController.h"
#include "Cubit/Voxel/MatchState.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

//Every message the wire carries. Eight, and deliberately no join or leave
//messages among them: a snapshot carries the whole roster every tick and ids
//are never reused, so a client derives both by diffing what it held last.
enum class MessageId : std::uint8_t
{
    Hello = 1,
    Welcome = 2,
    Input = 3,
    Snapshot = 4,
    //5 was EditRequest, retired in version 4. Never reuse it.
    EditApplied = 6,
    Fire = 7,
    ShotResolved = 8,
    EditResult = 9
};

//Bumped whenever any message's layout changes. A mismatch is a disconnect with
//a logged reason: two builds of a hand-rolled wire format disagreeing about
//field widths produce garbage positions, which read as a physics bug and cost
//a day.
//
//2: inputs became a bundle carrying a real client tick, and PlayerSnapshot
//gained the ack that makes replay possible. Both are on the per-tick path,
//which is exactly the case this counter exists for.
//
//3: shooting. A Fire message, a ShotResolved answer, and a Health byte on
//PlayerSnapshot. The last is on the per-tick path.
//
//4: predicted edits. An input entry may carry one edit, and the editor hears
//its fate from EditResult. On the per-tick path.
//
//5: PlayerSnapshot gained SpareInputs, the backlog report a client catches up
//on. On the per-tick path.
constexpr std::uint32_t ProtocolVersion = 5;

//How many server ticks a spare-input report looks back over - ten seconds.
//
//Chosen by measurement, not reasoning: at two seconds, the suite's 5%-loss
//links reported inputs to spare over and over, because the rare moments their
//queues ran thinnest fell outside the window. So a backlog costs up to ten
//seconds of extra delay before it starts to drain. Shared because the client
//needs it too: a report is only news once a whole window of inputs made after
//its last catch-up skip has been taken.
constexpr std::uint64_t SpareInputWindowTicks = 600;

//How many inputs one InputMessage carries at most.
//
//The redundancy is the entire defence against a starved server step: the
//server advances at a fixed rate whether or not an input arrived, so a late
//input means it steps once without one and its state stops being a prefix of
//what the client predicted. Sending the last three means a single lost or late
//packet is covered by the next one, at a cost of 34 bytes per message.
constexpr std::uint8_t InputBundleSize = 3;

struct HelloMessage
{
    std::uint32_t Version = ProtocolVersion;
};

struct WelcomeMessage
{
    PlayerId You = InvalidPlayer;

    //A name, never the data - the shipped map is 23.8 MB. The client loads it
    //from its own assets and checks the hash.
    std::string MapName;
    std::uint64_t MapHash = 0;

    std::uint64_t Tick = 0;

    //Every edit applied since the map loaded, in application order. This is
    //what makes joining late correct: without it a client arriving after
    //somebody dug a hole would get a pristine world.
    std::vector<BlockEdit> Edits;
};

struct InputMessage
{
    //The tick of the OLDEST input in the bundle, in the CLIENT's own
    //numbering. The client produces exactly one input per tick, so a bundle's
    //ticks are consecutive and only the first needs sending.
    //
    //A real tick now, not Stage 2's counter: replay has to know where to
    //reinsert an input, and this is the number the server echoes back in
    //PlayerSnapshot::LastInputTick.
    std::uint64_t FirstTick = 0;

    //Oldest first. Never longer than InputBundleSize when this client sent it,
    //but a decoder must not assume that of a packet off a socket.
    std::vector<CharacterInput> Inputs;

    //At most one edit per input, riding with the tick it was made on so the
    //server applies it at exactly that step - the whole of what makes a
    //predicted edit agree with the server.
    //
    //Entry i carries an edit exactly when i < Edits.size() and Edits[i] has a
    //value, so a sender with no edits may leave this empty. Decode always
    //fills one element per input, so a receiver can index the two together.
    std::vector<std::optional<BlockEdit>> Edits;
};

struct PlayerSnapshot
{
    PlayerId Player = InvalidPlayer;
    glm::vec3 Position{ 0.0f };

    //Carried because CharacterController does not store them - they live in
    //CharacterInput - and a client needs them to draw a remote player facing
    //the right way.
    float Yaw = 0.0f;
    float Pitch = 0.0f;

    float VerticalVelocity = 0.0f;
    bool Grounded = false;

    //The newest input from this player that the server has applied, in the
    //CLIENT's own tick numbering, echoed back untouched. The client replays
    //everything above it on top of the state in this snapshot.
    //
    //Per-player rather than per-recipient so the server still encodes one
    //snapshot and sends identical bytes to everybody. Two clients need not
    //agree about each other's numbering: each reads only its own entry, and
    //nobody else's is meaningful to it.
    std::uint64_t LastInputTick = 0;

    //Current health, 0 to 100. Never predicted: a client displays what arrives
    //and nothing more, because health changes only when a game rule fires and
    //the client owns no game rules.
    std::uint8_t Health = 0;

    //How many more of this player's inputs the server has held queued than it
    //needs, at the thinnest moment of the last SpareInputWindowTicks ticks: more
    //than one if the queue's depth held steady over the window, more than two if
    //it moved. Zero until a whole window has been seen.
    //
    //Non-zero means a backlog that is not draining: the server takes one input
    //a tick and the client sends one, so inputs that once bunched up - a clock
    //running fast, a lag spike - stay queued as input delay for good. The owning
    //client catches up by not making that many inputs. Per-player for the same
    //reason LastInputTick is.
    std::uint8_t SpareInputs = 0;
};

struct SnapshotMessage
{
    std::uint64_t Tick = 0;
    std::vector<PlayerSnapshot> Players;
};

//One edit the server has applied, sent to every joined client except the one
//that made it. The editor hears about its own edits from EditResult instead.
struct EditMessage
{
    BlockEdit Edit;
};

//A client asking to shoot.
//
//Carries TWO instants, and conflating them is the mistake this stage is most
//likely to make. RenderTick plus RenderAlpha is the instant the shooter's
//SCREEN was showing, in the SERVER's numbering, and locates everybody else -
//that is the one the rewind actually reads. ClientTick is when the shooter
//fired, in their OWN numbering. Nothing in this stage reads it: the shooter's
//own eye comes from their live server-side position, not from ClientTick.
struct FireMessage
{
    std::uint64_t ClientTick = 0;

    //Whole part of the instant the shooter's screen was showing, in the
    //server's tick numbering. Clamped by the server before it is believed.
    std::uint64_t RenderTick = 0;

    //Fractional part, in [0, 1). Carried because MatchClient::PoseOf
    //interpolates between snapshots, so the screen showed the target BETWEEN
    //two ticks; rewinding to a whole tick would aim at somewhere the target
    //never appeared to be.
    float RenderAlpha = 0.0f;

    //Aim, in degrees, in the Heading.h convention.
    //
    //Sent rather than looked up from the input at ClientTick, because that
    //input may have been dropped and may never arrive - and a shot that
    //silently became a miss because its input packet was lost is
    //indistinguishable from a bug in the rewind. Trusting client aim is
    //already this design's position: yaw is an input, not simulated state.
    float Yaw = 0.0f;
    float Pitch = 0.0f;
};

//The server's ruling on one shot, sent to everybody.
//
//To everybody rather than to the two involved, so every client can draw the
//tracer and the impact. At six shots a second and 19 bytes this is nothing
//beside the 3,900 B/s snapshot stream.
struct ShotResolvedMessage
{
    PlayerId Shooter = InvalidPlayer;

    //InvalidPlayer when the shot hit terrain or nothing.
    PlayerId Victim = InvalidPlayer;

    //Where the ray stopped, whether that was a player, a block, or the end of
    //its range. Always meaningful: something is drawn at the end of every shot.
    glm::vec3 Impact{ 0.0f };

    std::uint8_t VictimHealth = 0;
    bool Killed = false;
};

//The server's ruling on one of a client's own edits, sent to that client only.
//
//Reliable, because a lost refusal would leave a block on one client that the
//server never had - a permanent desync, not a correction. Tagged with the
//client's own tick, which is how the client finds the prediction it answers.
struct EditResultMessage
{
    std::uint64_t ClientTick = 0;
    bool Accepted = false;

    //The position, and the block the server has there AFTER ruling: the
    //requested block when accepted, the unchanged one when refused. Always
    //the server's truth, so the client never has to work it out.
    BlockEdit Edit;
};

CB_API std::vector<std::uint8_t> Encode(const HelloMessage& message);
CB_API std::vector<std::uint8_t> Encode(const WelcomeMessage& message);
CB_API std::vector<std::uint8_t> Encode(const InputMessage& message);
CB_API std::vector<std::uint8_t> Encode(const SnapshotMessage& message);
CB_API std::vector<std::uint8_t> EncodeEditApplied(const EditMessage& message);
CB_API std::vector<std::uint8_t> Encode(const FireMessage& message);
CB_API std::vector<std::uint8_t> Encode(const ShotResolvedMessage& message);
CB_API std::vector<std::uint8_t> Encode(const EditResultMessage& message);

//Each returns false and leaves `out` untouched when the bytes are truncated,
//malformed, or of the wrong type. Malformed input is a routine wire condition
//rather than a caller's bug, so nothing here throws.
CB_API bool Decode(std::span<const std::uint8_t> bytes, HelloMessage& out);
CB_API bool Decode(std::span<const std::uint8_t> bytes, WelcomeMessage& out);
CB_API bool Decode(std::span<const std::uint8_t> bytes, InputMessage& out);
CB_API bool Decode(std::span<const std::uint8_t> bytes, SnapshotMessage& out);
CB_API bool Decode(std::span<const std::uint8_t> bytes, EditMessage& out);
CB_API bool Decode(std::span<const std::uint8_t> bytes, FireMessage& out);
CB_API bool Decode(std::span<const std::uint8_t> bytes, ShotResolvedMessage& out);
CB_API bool Decode(std::span<const std::uint8_t> bytes, EditResultMessage& out);

//Reads the leading id without consuming anything, so a receiver can pick a
//decoder. False when the buffer is empty or the id is not one of the eight.
CB_API bool PeekMessageId(std::span<const std::uint8_t> bytes, MessageId& out);

#ifdef _MSC_VER
#pragma warning(pop)
#endif
