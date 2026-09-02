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
constexpr std::uint32_t ProtocolVersion = 1;

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
    //A counter, not a tick. In Stage 2 the client never steps, so it has no
    //simulation tick to name; the server uses this only to drop stale and
    //duplicate packets on an unordered channel. Stage 3 is where an input
    //acquires a real tick, because that is when replay needs to know where to
    //reinsert it.
    std::uint32_t Sequence = 0;

    CharacterInput Input;
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
