#include "cub.h"

#include "Cubit/Net/Protocol.h"

namespace
{
    //Bytes each entry costs on the wire. Used to reject an absurd count before
    //reserving for it, which is what stops a tiny hostile packet claiming a
    //huge collection from becoming a denial of service.
    constexpr std::size_t PlayerSnapshotBytes = 2 + 12 + 4 + 4 + 4 + 1;
    constexpr std::size_t BlockEditBytes = 12 + 2;

    void WriteEdit(ByteWriter& writer, const BlockEdit& edit)
    {
        writer.IVec3(edit.Position);
        writer.U16(static_cast<std::uint16_t>(edit.Block));
    }

    BlockEdit ReadEdit(ByteReader& reader)
    {
        BlockEdit edit;
        edit.Position = reader.IVec3();
        edit.Block = static_cast<BlockId>(reader.U16());
        return edit;
    }

    //Confirms the buffer opens with the expected id and leaves the reader
    //positioned just past it.
    bool OpenAs(ByteReader& reader, MessageId expected)
    {
        return reader.U8() == static_cast<std::uint8_t>(expected) && reader.Ok();
    }
}

std::vector<std::uint8_t> Encode(const HelloMessage& message)
{
    ByteWriter writer;
    writer.U8(static_cast<std::uint8_t>(MessageId::Hello));
    writer.U32(message.Version);
    return writer.Bytes();
}

std::vector<std::uint8_t> Encode(const WelcomeMessage& message)
{
    ByteWriter writer;
    writer.U8(static_cast<std::uint8_t>(MessageId::Welcome));
    writer.U16(message.You);
    writer.String(message.MapName);
    writer.U64(message.MapHash);
    writer.U64(message.Tick);
    writer.U32(static_cast<std::uint32_t>(message.Edits.size()));

    for (const BlockEdit& edit : message.Edits)
        WriteEdit(writer, edit);

    return writer.Bytes();
}

std::vector<std::uint8_t> Encode(const InputMessage& message)
{
    ByteWriter writer;
    writer.U8(static_cast<std::uint8_t>(MessageId::Input));
    writer.U32(message.Sequence);
    writer.F32(message.Input.Move.x);
    writer.F32(message.Input.Move.y);
    writer.F32(message.Input.Yaw);
    writer.F32(message.Input.Pitch);
    writer.Bool(message.Input.Jump);
    return writer.Bytes();
}

std::vector<std::uint8_t> Encode(const SnapshotMessage& message)
{
    ByteWriter writer;
    writer.U8(static_cast<std::uint8_t>(MessageId::Snapshot));
    writer.U64(message.Tick);
    writer.U16(static_cast<std::uint16_t>(message.Players.size()));

    for (const PlayerSnapshot& player : message.Players)
    {
        writer.U16(player.Player);
        writer.Vec3(player.Position);
        writer.F32(player.Yaw);
        writer.F32(player.Pitch);
        writer.F32(player.VerticalVelocity);
        writer.Bool(player.Grounded);
    }

    return writer.Bytes();
}

std::vector<std::uint8_t> EncodeEditRequest(const EditMessage& message)
{
    ByteWriter writer;
    writer.U8(static_cast<std::uint8_t>(MessageId::EditRequest));
    WriteEdit(writer, message.Edit);
    return writer.Bytes();
}

std::vector<std::uint8_t> EncodeEditApplied(const EditMessage& message)
{
    ByteWriter writer;
    writer.U8(static_cast<std::uint8_t>(MessageId::EditApplied));
    WriteEdit(writer, message.Edit);
    return writer.Bytes();
}

bool Decode(std::span<const std::uint8_t> bytes, HelloMessage& out)
{
    ByteReader reader(bytes);
    if (!OpenAs(reader, MessageId::Hello))
        return false;

    HelloMessage message;
    message.Version = reader.U32();

    if (!reader.Ok())
        return false;

    out = message;
    return true;
}

bool Decode(std::span<const std::uint8_t> bytes, WelcomeMessage& out)
{
    ByteReader reader(bytes);
    if (!OpenAs(reader, MessageId::Welcome))
        return false;

    WelcomeMessage message;
    message.You = static_cast<PlayerId>(reader.U16());
    message.MapName = reader.String();
    message.MapHash = reader.U64();
    message.Tick = reader.U64();

    const std::uint32_t count = reader.U32();

    //Correctness-critical, unlike the look-alike guard in
    //Decode(SnapshotMessage&) below. That one is provably safe to delete
    //because a u16 count tops out at 65535 entries - small enough that the
    //trailing Ok() check always catches it too, in a blink. This count is a
    //u32, and a 25-byte packet (id + You + an empty MapName + MapHash + Tick
    //+ count) can declare one near UINT32_MAX. Verified by mutation, not
    //assumed: with this line deleted, decoding one such packet did not throw
    //and did not return - Edits.reserve() accepted a request for roughly
    //68 GB, and the loop that followed was still running three and a half
    //minutes later, having driven a 32 GB machine from ~30 GB free to
    //1.67 GB free, at which point the run was killed to keep the host
    //alive. That is not the "nothing here throws" contract Protocol.h
    //documents - it is worse than throwing, because the caller has no way
    //to know the call will ever return. Deleting this line does not make
    //Decode wrong on more inputs; it makes Decode not return.
    if (!reader.Ok() || count > reader.Remaining() / BlockEditBytes)
        return false;

    message.Edits.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i)
        message.Edits.push_back(ReadEdit(reader));

    //Still earns its keep for You/MapName/MapHash/Tick/count, any of which
    //can fail on a short buffer before the guard above ever runs. It cannot
    //fail here in the edit loop itself: the guard above already proved
    //count * BlockEditBytes <= Remaining(), so the loop can never run short.
    //That does not make this check redundant to delete - it makes it correct
    //to leave, because nothing prevents a future change to Edits or the guard
    //above from making the loop fallible again.
    if (!reader.Ok())
        return false;

    out = std::move(message);
    return true;
}

bool Decode(std::span<const std::uint8_t> bytes, InputMessage& out)
{
    ByteReader reader(bytes);
    if (!OpenAs(reader, MessageId::Input))
        return false;

    InputMessage message;
    message.Sequence = reader.U32();
    message.Input.Move.x = reader.F32();
    message.Input.Move.y = reader.F32();
    message.Input.Yaw = reader.F32();
    message.Input.Pitch = reader.F32();
    message.Input.Jump = reader.Bool();

    if (!reader.Ok())
        return false;

    out = message;
    return true;
}

bool Decode(std::span<const std::uint8_t> bytes, SnapshotMessage& out)
{
    ByteReader reader(bytes);
    if (!OpenAs(reader, MessageId::Snapshot))
        return false;

    SnapshotMessage message;
    message.Tick = reader.U64();

    const std::uint16_t count = reader.U16();

    //Checked against what the buffer can actually hold, before reserving.
    //Trusting the count and reserving on its word is how an 11-byte packet
    //(id + Tick + count, nothing else needed to reach this line) becomes a
    //1.7 MB allocation - 65535, the largest count a u16 can carry, times
    //PlayerSnapshotBytes.
    //
    //A resource guard, not a correctness one, and worth keeping straight:
    //ByteReader's sticky Ok() already guarantees the trailing Ok() check
    //below refuses the same packet even without this line, once the loop
    //runs out of real bytes to read. No test's return value tells the two
    //apart, and none can - they agree on every input, because a u16 count
    //can never demand more than that same 1.7 MB, which any real machine
    //allocates and iterates over instantly either way. What this line
    //changes is making the rejection instant instead of doing the
    //65535-iteration loop and the reserve() for it first. Contrast
    //Decode(WelcomeMessage&) above, where the same-shaped guard is not
    //optional for exactly this reason: its count is a u32, not a u16, and
    //the worst case is not 1.7 MB.
    if (!reader.Ok() || count > reader.Remaining() / PlayerSnapshotBytes)
        return false;

    message.Players.reserve(count);
    for (std::uint16_t i = 0; i < count; ++i)
    {
        PlayerSnapshot player;
        player.Player = static_cast<PlayerId>(reader.U16());
        player.Position = reader.Vec3();
        player.Yaw = reader.F32();
        player.Pitch = reader.F32();
        player.VerticalVelocity = reader.F32();
        player.Grounded = reader.Bool();
        message.Players.push_back(player);
    }

    if (!reader.Ok())
        return false;

    out = std::move(message);
    return true;
}

bool Decode(std::span<const std::uint8_t> bytes, EditMessage& out)
{
    ByteReader reader(bytes);

    const std::uint8_t id = reader.U8();
    if (!reader.Ok())
        return false;

    if (id != static_cast<std::uint8_t>(MessageId::EditRequest) &&
        id != static_cast<std::uint8_t>(MessageId::EditApplied))
        return false;

    EditMessage message;
    message.Edit = ReadEdit(reader);

    if (!reader.Ok())
        return false;

    out = message;
    return true;
}

bool PeekMessageId(std::span<const std::uint8_t> bytes, MessageId& out)
{
    if (bytes.empty())
        return false;

    const std::uint8_t id = bytes[0];
    if (id < static_cast<std::uint8_t>(MessageId::Hello) ||
        id > static_cast<std::uint8_t>(MessageId::EditApplied))
        return false;

    out = static_cast<MessageId>(id);
    return true;
}
