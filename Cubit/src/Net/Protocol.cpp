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
    if (!reader.Ok() || count > reader.Remaining() / BlockEditBytes)
        return false;

    message.Edits.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i)
        message.Edits.push_back(ReadEdit(reader));

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
    //Trusting the count and reserving on its word is how a 13-byte packet
    //becomes a 1.7 MB allocation.
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
