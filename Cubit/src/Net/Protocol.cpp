#include "cub.h"

#include "Cubit/Net/Protocol.h"

namespace
{
    //Bytes each entry costs on the wire. Used to reject an absurd count before
    //reserving for it, which is what stops a tiny hostile packet claiming a
    //huge collection from becoming a denial of service.
    //
    //The trailing 1 is Health, added in version 3. Keep this at the encoder's
    //true entry width, but for the reason that is easy to get backwards: the
    //guard below is `count > Remaining() / PlayerSnapshotBytes`, so a value
    //SMALLER than the true width only raises that threshold and makes the
    //guard MORE permissive - it cannot reject a complete, validly-encoded
    //packet, only weaken the defence against a hostile oversized-count claim.
    //A value LARGER than the true width is what would wrongly reject valid
    //packets; this constant has never been set that way. Verified by
    //mutation, not assumed: setting this to 35 (the pre-Health width) and
    //rebuilding left the full suite green, not red - a resource guard gone
    //slack is invisible to every test's return value on a well-formed packet,
    //the same way the guard in Decode(SnapshotMessage&) below is invisible to
    //one. Do not chase a red test by lowering this number; there isn't one.
    constexpr std::size_t PlayerSnapshotBytes = 2 + 12 + 4 + 4 + 4 + 1 + 8 + 1;
    constexpr std::size_t BlockEditBytes = 12 + 2;
    constexpr std::size_t CharacterInputBytes = 4 + 4 + 4 + 4 + 1;

    //The smallest an input entry can be on the wire: the input and its edit
    //flag, with no edit. The guard in Decode(InputMessage&) divides by this -
    //see the note on PlayerSnapshotBytes for why a guard constant must be the
    //true minimum width and never larger.
    constexpr std::size_t InputEntryMinBytes = CharacterInputBytes + 1;

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
    writer.U8(static_cast<std::uint8_t>(message.Inputs.size()));
    writer.U64(message.FirstTick);

    for (std::size_t i = 0; i < message.Inputs.size(); ++i)
    {
        const CharacterInput& input = message.Inputs[i];
        writer.F32(input.Move.x);
        writer.F32(input.Move.y);
        writer.F32(input.Yaw);
        writer.F32(input.Pitch);
        writer.Bool(input.Jump);

        const bool hasEdit = i < message.Edits.size() && message.Edits[i].has_value();
        writer.Bool(hasEdit);
        if (hasEdit)
            WriteEdit(writer, *message.Edits[i]);
    }

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
        writer.U64(player.LastInputTick);
        writer.U8(player.Health);
    }

    return writer.Bytes();
}

std::vector<std::uint8_t> EncodeEditApplied(const EditMessage& message)
{
    ByteWriter writer;
    writer.U8(static_cast<std::uint8_t>(MessageId::EditApplied));
    WriteEdit(writer, message.Edit);
    return writer.Bytes();
}

std::vector<std::uint8_t> Encode(const FireMessage& message)
{
    ByteWriter writer;
    writer.U8(static_cast<std::uint8_t>(MessageId::Fire));
    writer.U64(message.ClientTick);
    writer.U64(message.RenderTick);
    writer.F32(message.RenderAlpha);
    writer.F32(message.Yaw);
    writer.F32(message.Pitch);
    return writer.Bytes();
}

std::vector<std::uint8_t> Encode(const ShotResolvedMessage& message)
{
    ByteWriter writer;
    writer.U8(static_cast<std::uint8_t>(MessageId::ShotResolved));
    writer.U16(message.Shooter);
    writer.U16(message.Victim);
    writer.Vec3(message.Impact);
    writer.U8(message.VictimHealth);
    writer.Bool(message.Killed);
    return writer.Bytes();
}

std::vector<std::uint8_t> Encode(const EditResultMessage& message)
{
    ByteWriter writer;
    writer.U8(static_cast<std::uint8_t>(MessageId::EditResult));
    writer.U64(message.ClientTick);
    writer.Bool(message.Accepted);
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
    const std::uint8_t count = reader.U8();
    message.FirstTick = reader.U64();

    //A resource guard, not a correctness one, and the same class as the
    //snapshot's: a u8 count tops out at 255 entries, so the trailing Ok() check
    //below refuses an over-claiming packet on its own once the loop runs out of
    //real bytes. What this line changes is refusing instantly rather than
    //reserving for 255 first. Decode(WelcomeMessage&) above is the one where
    //the same-shaped guard is NOT optional - its count is a u32, and deleting
    //it does not make Decode wrong, it makes Decode not return. Read that note
    //before touching any of the three.
    if (!reader.Ok() || count > reader.Remaining() / InputEntryMinBytes)
        return false;

    message.Inputs.reserve(count);
    message.Edits.reserve(count);
    for (std::uint8_t i = 0; i < count; ++i)
    {
        CharacterInput input;
        input.Move.x = reader.F32();
        input.Move.y = reader.F32();
        input.Yaw = reader.F32();
        input.Pitch = reader.F32();
        input.Jump = reader.Bool();
        message.Inputs.push_back(input);

        std::optional<BlockEdit> edit;
        if (reader.Bool())
            edit = ReadEdit(reader);
        message.Edits.push_back(edit);
    }

    if (!reader.Ok())
        return false;

    out = std::move(message);
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
    //65535 x PlayerSnapshotBytes allocation - 65535 being the largest count
    //a u16 can carry.
    //
    //A resource guard, not a correctness one, and worth keeping straight:
    //ByteReader's sticky Ok() already guarantees the trailing Ok() check
    //below refuses the same packet even without this line, once the loop
    //runs out of real bytes to read. No test's return value tells the two
    //apart, and none can - they agree on every input, because a u16 count
    //can never demand more than that same 65535 x PlayerSnapshotBytes (a
    //couple of megabytes at today's entry size), which any real machine
    //allocates and iterates over instantly either way. What this line
    //changes is making the rejection instant instead of doing the
    //65535-iteration loop and the reserve() for it first. Contrast
    //Decode(WelcomeMessage&) above, where the same-shaped guard is not
    //optional for exactly this reason: its count is a u32, not a u16, and
    //the worst case is nowhere near that small.
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
        player.LastInputTick = reader.U64();
        player.Health = reader.U8();
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
    if (!OpenAs(reader, MessageId::EditApplied))
        return false;

    EditMessage message;
    message.Edit = ReadEdit(reader);

    if (!reader.Ok())
        return false;

    out = message;
    return true;
}

bool Decode(std::span<const std::uint8_t> bytes, FireMessage& out)
{
    ByteReader reader(bytes);
    if (!OpenAs(reader, MessageId::Fire))
        return false;

    FireMessage message;
    message.ClientTick = reader.U64();
    message.RenderTick = reader.U64();
    message.RenderAlpha = reader.F32();
    message.Yaw = reader.F32();
    message.Pitch = reader.F32();

    //No count field and no variable-length field, so there is nothing here to
    //reserve on a hostile packet's word and no allocation guard to write. Worth
    //saying so, because the guards in Decode(WelcomeMessage&) and
    //Decode(SnapshotMessage&) are not decoration and must not be deleted by
    //analogy with this one.
    if (!reader.Ok())
        return false;

    out = message;
    return true;
}

bool Decode(std::span<const std::uint8_t> bytes, ShotResolvedMessage& out)
{
    ByteReader reader(bytes);
    if (!OpenAs(reader, MessageId::ShotResolved))
        return false;

    ShotResolvedMessage message;
    message.Shooter = reader.U16();
    message.Victim = reader.U16();
    message.Impact = reader.Vec3();
    message.VictimHealth = reader.U8();
    message.Killed = reader.Bool();

    if (!reader.Ok())
        return false;

    out = message;
    return true;
}

bool Decode(std::span<const std::uint8_t> bytes, EditResultMessage& out)
{
    ByteReader reader(bytes);
    if (!OpenAs(reader, MessageId::EditResult))
        return false;

    EditResultMessage message;
    message.ClientTick = reader.U64();
    message.Accepted = reader.Bool();
    message.Edit = ReadEdit(reader);

    //Fixed width, no count: nothing to reserve on a hostile packet's word.
    if (!reader.Ok())
        return false;

    out = message;
    return true;
}

bool PeekMessageId(std::span<const std::uint8_t> bytes, MessageId& out)
{
    if (bytes.empty())
        return false;

    //A list, not a range: a retired id sits inside the range, and a range check
    //would wave it through to dispatch.
    switch (static_cast<MessageId>(bytes[0]))
    {
    case MessageId::Hello:
    case MessageId::Welcome:
    case MessageId::Input:
    case MessageId::Snapshot:
    case MessageId::EditApplied:
    case MessageId::Fire:
    case MessageId::ShotResolved:
    case MessageId::EditResult:
        out = static_cast<MessageId>(bytes[0]);
        return true;
    }

    return false;
}
