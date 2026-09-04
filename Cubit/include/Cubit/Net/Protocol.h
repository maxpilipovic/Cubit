#pragma once

#include "Cubit/Core.h"
#include "Cubit/Net/ByteReader.h"
#include "Cubit/Net/ByteWriter.h"
#include "Cubit/Voxel/BlockEdit.h"
#include "Cubit/Voxel/CharacterController.h"
#include "Cubit/Voxel/MatchState.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

//Every message the wire carries. Six, and deliberately not seven: there are no
//join or leave messages, because a snapshot carries the whole roster every tick
//and ids are never reused, so a client derives both by diffing what it held
//last.
enum class MessageId : std::uint8_t
{
    Hello = 1,
    Welcome = 2,
    Input = 3,
    Snapshot = 4,
    EditRequest = 5,
    EditApplied = 6
};

//Bumped whenever any message's layout changes. A mismatch is a disconnect with
//a logged reason: two builds of a hand-rolled wire format disagreeing about
//field widths produce garbage positions, which read as a physics bug and cost
//a day.
//
//2: inputs became a bundle carrying a real client tick, and PlayerSnapshot
//gained the ack that makes replay possible. Both are on the per-tick path,
//which is exactly the case this counter exists for.
constexpr std::uint32_t ProtocolVersion = 2;

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
};

struct SnapshotMessage
{
    std::uint64_t Tick = 0;
    std::vector<PlayerSnapshot> Players;
};

//One edit, in either direction. The two directions share a payload but not a
//MessageId, because a client must never mistake its own request coming back
//for the server's authoritative answer.
struct EditMessage
{
    BlockEdit Edit;
};

CB_API std::vector<std::uint8_t> Encode(const HelloMessage& message);
CB_API std::vector<std::uint8_t> Encode(const WelcomeMessage& message);
CB_API std::vector<std::uint8_t> Encode(const InputMessage& message);
CB_API std::vector<std::uint8_t> Encode(const SnapshotMessage& message);
CB_API std::vector<std::uint8_t> EncodeEditRequest(const EditMessage& message);
CB_API std::vector<std::uint8_t> EncodeEditApplied(const EditMessage& message);

//Each returns false and leaves `out` untouched when the bytes are truncated,
//malformed, or of the wrong type. Malformed input is a routine wire condition
//rather than a caller's bug, so nothing here throws.
CB_API bool Decode(std::span<const std::uint8_t> bytes, HelloMessage& out);
CB_API bool Decode(std::span<const std::uint8_t> bytes, WelcomeMessage& out);
CB_API bool Decode(std::span<const std::uint8_t> bytes, InputMessage& out);
CB_API bool Decode(std::span<const std::uint8_t> bytes, SnapshotMessage& out);
CB_API bool Decode(std::span<const std::uint8_t> bytes, EditMessage& out);

//Reads the leading id without consuming anything, so a receiver can pick a
//decoder. False when the buffer is empty or the id is not one of the six.
CB_API bool PeekMessageId(std::span<const std::uint8_t> bytes, MessageId& out);

#ifdef _MSC_VER
#pragma warning(pop)
#endif
