#include <doctest.h>

#include "Cubit/Net/Protocol.h"

#include <glm/glm.hpp>
#include <utility>
#include <vector>

namespace
{
    SnapshotMessage TwoPlayerSnapshot()
    {
        SnapshotMessage snapshot;
        snapshot.Tick = 4242;

        PlayerSnapshot first;
        first.Player = PlayerId{ 1 };
        first.Position = glm::vec3(240.5f, 26.9f, 300.5f);
        first.Yaw = -135.0f;
        first.Pitch = 12.5f;
        first.VerticalVelocity = -3.25f;
        first.Grounded = true;
        first.LastInputTick = 4294967301ull;

        PlayerSnapshot second;
        second.Player = PlayerId{ 2 };
        second.Position = glm::vec3(-1.0f, 0.0f, 7.5f);
        second.Yaw = 90.0f;
        second.Pitch = -45.0f;
        second.VerticalVelocity = 0.0f;
        second.Grounded = false;
        second.LastInputTick = 0;

        snapshot.Players = { first, second };
        return snapshot;
    }
}

TEST_CASE("Hello round-trips and carries the protocol version")
{
    HelloMessage sent;
    HelloMessage received;

    REQUIRE(Decode(Encode(sent), received));
    CHECK(received.Version == ProtocolVersion);
}

TEST_CASE("Welcome round-trips, edit log and all")
{
    WelcomeMessage sent;
    sent.You = PlayerId{ 7 };
    sent.MapName = "battlefield512.vox";
    sent.MapHash = 0x0123456789ABCDEFull;
    sent.Tick = 900;
    sent.Edits = {
        BlockEdit{ glm::ivec3(1, 2, 3), BlockId{ 0 } },
        BlockEdit{ glm::ivec3(-4, 5, -6), BlockId{ 9 } }
    };

    WelcomeMessage received;
    REQUIRE(Decode(Encode(sent), received));

    CHECK(received.You == PlayerId{ 7 });
    CHECK(received.MapName == "battlefield512.vox");
    CHECK(received.MapHash == 0x0123456789ABCDEFull);
    CHECK(received.Tick == 900);
    REQUIRE(received.Edits.size() == 2);
    CHECK(received.Edits[0].Position == glm::ivec3(1, 2, 3));
    CHECK(received.Edits[1].Block == BlockId{ 9 });
}

TEST_CASE("Input round-trips a bundle and the tick it starts at")
{
    InputMessage sent;
    sent.FirstTick = 4294967300ull;   //Past a u32, so a narrowed field shows up.

    for (int i = 0; i < InputBundleSize; ++i)
    {
        CharacterInput input;
        input.Move = glm::vec2(0.25f * i, -1.0f);
        input.Yaw = 90.0f + i;
        input.Pitch = -12.5f - i;
        input.Jump = (i % 2) == 0;
        sent.Inputs.push_back(input);
    }

    InputMessage received;
    REQUIRE(Decode(Encode(sent), received));

    CHECK(received.FirstTick == 4294967300ull);
    REQUIRE(received.Inputs.size() == static_cast<std::size_t>(InputBundleSize));

    for (int i = 0; i < InputBundleSize; ++i)
    {
        CHECK(received.Inputs[i].Move == sent.Inputs[i].Move);
        CHECK(received.Inputs[i].Yaw == sent.Inputs[i].Yaw);
        CHECK(received.Inputs[i].Pitch == sent.Inputs[i].Pitch);
        CHECK(received.Inputs[i].Jump == sent.Inputs[i].Jump);
    }
}

TEST_CASE("A three-input bundle is 61 bytes")
{
    //Pinned because it is the number the stage's upstream cost is quoted from:
    //1 id + 1 count + 8 tick + 3 x 17 = 61 bytes, 3,660 B/s per client at
    //60 Hz. A field silently widening is a bandwidth regression nobody would
    //otherwise notice until a real network was involved.
    InputMessage message;
    message.FirstTick = 1;
    message.Inputs.assign(InputBundleSize, CharacterInput{});

    CHECK(Encode(message).size() == 61);
}

TEST_CASE("An input declaring more entries than it carries is refused")
{
    //The same shape of guard as the snapshot's, and the same class: a u8 count
    //cannot demand more than 255 entries, so the trailing Ok() check would
    //refuse this packet anyway - this makes the refusal instant. Read the note
    //in Decode(WelcomeMessage&) before touching any of the three; that one is
    //the guard that is not optional.
    InputMessage message;
    message.FirstTick = 7;
    message.Inputs.assign(3, CharacterInput{});

    std::vector<std::uint8_t> bytes = Encode(message);

    //Claim 200 inputs in a packet carrying three.
    bytes[1] = 200;

    InputMessage received;
    CHECK_FALSE(Decode(bytes, received));
}

TEST_CASE("An empty bundle is legal to decode and carries nothing")
{
    //Not something the client sends, but a decoder that threw or half-filled
    //on it would be a crash reachable from one hostile byte.
    InputMessage message;
    message.FirstTick = 99;

    InputMessage received;
    received.Inputs.assign(2, CharacterInput{});

    REQUIRE(Decode(Encode(message), received));
    CHECK(received.FirstTick == 99);
    CHECK(received.Inputs.empty());
}

TEST_CASE("Snapshot round-trips every player")
{
    const SnapshotMessage sent = TwoPlayerSnapshot();

    SnapshotMessage received;
    REQUIRE(Decode(Encode(sent), received));

    CHECK(received.Tick == 4242);
    REQUIRE(received.Players.size() == 2);
    CHECK(received.Players[0].Player == PlayerId{ 1 });
    CHECK(received.Players[0].Position == glm::vec3(240.5f, 26.9f, 300.5f));
    CHECK(received.Players[0].Grounded);
    CHECK(received.Players[1].Player == PlayerId{ 2 });
    CHECK(received.Players[1].Yaw == doctest::Approx(90.0f));
    CHECK_FALSE(received.Players[1].Grounded);
    CHECK(received.Players[0].LastInputTick == 4294967301ull);
    CHECK(received.Players[1].LastInputTick == 0);
}

TEST_CASE("A two-player snapshot is 83 bytes")
{
    //Pinned for the same reason the input bundle's size is: this is where the
    //stage's per-client bandwidth is quoted from. 1 id + 8 tick + 2 count +
    //2 x 36 = 83. The per-entry width grew from 35 to 36 in protocol version 3,
    //when PlayerSnapshot gained Health.
    CHECK(Encode(TwoPlayerSnapshot()).size() == 83);
}

TEST_CASE("An empty roster is a legal snapshot")
{
    //A server with no clients still ticks and still broadcasts. A decoder that
    //assumes at least one player turns an idle server into a parse failure.
    SnapshotMessage sent;
    sent.Tick = 5;

    SnapshotMessage received;
    REQUIRE(Decode(Encode(sent), received));

    CHECK(received.Tick == 5);
    CHECK(received.Players.empty());
}

TEST_CASE("Edit messages round-trip and keep their own identity")
{
    EditMessage sent;
    sent.Edit = BlockEdit{ glm::ivec3(300, 40, -12), BlockId{ 3 } };

    std::vector<std::uint8_t> request = EncodeEditRequest(sent);
    std::vector<std::uint8_t> applied = EncodeEditApplied(sent);

    MessageId id = MessageId::Hello;
    REQUIRE(PeekMessageId(request, id));
    CHECK(id == MessageId::EditRequest);
    REQUIRE(PeekMessageId(applied, id));
    CHECK(id == MessageId::EditApplied);

    EditMessage received;
    REQUIRE(Decode(request, received));
    CHECK(received.Edit.Position == glm::ivec3(300, 40, -12));
    CHECK(received.Edit.Block == BlockId{ 3 });
}

TEST_CASE("A message of the wrong type is refused")
{
    HelloMessage hello;
    SnapshotMessage snapshot;

    CHECK_FALSE(Decode(Encode(hello), snapshot));
}

TEST_CASE("Every message truncated at every length is refused without crashing")
{
    //The stage's hostile-input sweep. This is the first data in the project's
    //history that arrives from a socket, so a short read is a routine wire
    //condition and must produce `false`, never a crash and never a half-filled
    //output the caller might act on.
    std::vector<std::vector<std::uint8_t>> messages;
    {
        HelloMessage hello;
        messages.push_back(Encode(hello));

        WelcomeMessage welcome;
        welcome.You = PlayerId{ 3 };
        welcome.MapName = "map.vox";
        welcome.Edits = { BlockEdit{ glm::ivec3(1, 1, 1), BlockId{ 2 } } };
        messages.push_back(Encode(welcome));

        InputMessage input;
        input.FirstTick = 9;
        input.Inputs.assign(InputBundleSize, CharacterInput{});
        messages.push_back(Encode(input));

        messages.push_back(Encode(TwoPlayerSnapshot()));

        EditMessage edit;
        edit.Edit = BlockEdit{ glm::ivec3(2, 2, 2), BlockId{ 1 } };
        messages.push_back(EncodeEditRequest(edit));
    }

    for (const std::vector<std::uint8_t>& whole : messages)
    {
        for (std::size_t length = 0; length < whole.size(); ++length)
        {
            const std::span<const std::uint8_t> truncated(whole.data(), length);

            MessageId id = MessageId::Hello;
            if (!PeekMessageId(truncated, id))
                continue;

            HelloMessage hello;
            WelcomeMessage welcome;
            InputMessage input;
            SnapshotMessage snapshot;
            EditMessage edit;

            //Whichever decoder matches the id must refuse; the rest refuse on
            //the id alone. Either way nothing throws and nothing is trusted.
            switch (id)
            {
            case MessageId::Hello:       CHECK_FALSE(Decode(truncated, hello)); break;
            case MessageId::Welcome:     CHECK_FALSE(Decode(truncated, welcome)); break;
            case MessageId::Input:       CHECK_FALSE(Decode(truncated, input)); break;
            case MessageId::Snapshot:    CHECK_FALSE(Decode(truncated, snapshot)); break;
            case MessageId::EditRequest:
            case MessageId::EditApplied: CHECK_FALSE(Decode(truncated, edit)); break;
            }
        }
    }
}

TEST_CASE("A snapshot declaring more players than it carries is refused")
{
    //Proves only the outcome: a count the buffer cannot back comes back
    //false. It does not, and cannot, prove this is refused cheaply - a
    //decoder that ignored the count entirely and just let the read loop run
    //dry would return the same false, because ByteReader's Ok() latches
    //false the moment a read comes up short and every later read inherits
    //that. Whatever makes rejecting this packet cheap instead of merely
    //correct is proven by reading Protocol.cpp, not by this assertion.
    ByteWriter writer;
    writer.U8(static_cast<std::uint8_t>(MessageId::Snapshot));
    writer.U64(1);
    writer.U16(60000);

    SnapshotMessage received;
    CHECK_FALSE(Decode(writer.Span(), received));
}

TEST_CASE("A welcome declaring an edit count near its type's limit is refused without throwing")
{
    //Unlike the snapshot test above, this one is genuinely falsifiable:
    //WelcomeMessage's edit count is a u32, not a u16, so nothing bounds how
    //much Edits.reserve() could be asked for except the guard in
    //Decode(WelcomeMessage&). Remove that guard and this packet no longer
    //comes back promptly or safely - see the comment on that guard in
    //Protocol.cpp for what was actually observed when it was removed.
    ByteWriter writer;
    writer.U8(static_cast<std::uint8_t>(MessageId::Welcome));
    writer.U16(0);
    writer.String("");
    writer.U64(0);
    writer.U64(0);
    writer.U32(0xFFFFFFFFu);

    WelcomeMessage received;
    bool ok = true;
    CHECK_NOTHROW(ok = Decode(writer.Span(), received));
    CHECK_FALSE(ok);
}

TEST_CASE("A fire message round-trips")
{
    FireMessage sent;
    sent.ClientTick = 4321;
    sent.RenderTick = 4300;
    sent.RenderAlpha = 0.25f;
    sent.Yaw = -137.5f;
    sent.Pitch = 12.25f;

    FireMessage received;
    REQUIRE(Decode(Encode(sent), received));

    CHECK(received.ClientTick == 4321);
    CHECK(received.RenderTick == 4300);
    CHECK(received.RenderAlpha == doctest::Approx(0.25f));
    CHECK(received.Yaw == doctest::Approx(-137.5f));
    CHECK(received.Pitch == doctest::Approx(12.25f));
}

TEST_CASE("A shot resolution round-trips, hit and miss alike")
{
    ShotResolvedMessage hit;
    hit.Shooter = 1;
    hit.Victim = 2;
    hit.Impact = glm::vec3(1.5f, -2.25f, 300.0f);
    hit.VictimHealth = 66;
    hit.Killed = false;

    ShotResolvedMessage received;
    REQUIRE(Decode(Encode(hit), received));
    CHECK(received.Shooter == 1);
    CHECK(received.Victim == 2);
    CHECK(received.Impact.z == doctest::Approx(300.0f));
    CHECK(received.VictimHealth == 66);
    CHECK_FALSE(received.Killed);

    ShotResolvedMessage miss;
    miss.Shooter = 1;
    miss.Victim = InvalidPlayer;
    miss.Impact = glm::vec3(50.0f, 0.0f, 0.0f);
    miss.Killed = false;

    REQUIRE(Decode(Encode(miss), received));
    CHECK(received.Victim == InvalidPlayer);
}

TEST_CASE("A snapshot carries health")
{
    SnapshotMessage sent;
    sent.Tick = 9;
    PlayerSnapshot entry;
    entry.Player = 3;
    entry.Health = 32;
    sent.Players.push_back(entry);

    SnapshotMessage received;
    REQUIRE(Decode(Encode(sent), received));
    REQUIRE(received.Players.size() == 1);
    CHECK(received.Players[0].Health == 32);
}

TEST_CASE("A fire message is not mistaken for a shot resolution")
{
    //The two directions must never be confused, which is why they are separate
    //ids rather than one payload with a flag.
    FireMessage fire;
    ShotResolvedMessage resolved;

    CHECK_FALSE(Decode(Encode(fire), resolved));
    CHECK_FALSE(Decode(Encode(resolved), fire));
}

TEST_CASE("A truncated fire message is refused rather than half-read")
{
    const std::vector<std::uint8_t> whole = Encode(FireMessage{});

    for (std::size_t length = 0; length < whole.size(); ++length)
    {
        FireMessage out;
        const std::span<const std::uint8_t> truncated(whole.data(), length);
        CHECK_FALSE(Decode(truncated, out));
    }
}

TEST_CASE("Every message id the wire carries is recognised")
{
    //PeekMessageId gates dispatch: an id it refuses is a packet that vanishes
    //with no error anywhere. Adding a message and forgetting this bound is the
    //natural mistake, so every id is checked rather than only the new ones.
    const std::vector<std::pair<std::vector<std::uint8_t>, MessageId>> cases{
        { Encode(HelloMessage{}),                   MessageId::Hello },
        { Encode(WelcomeMessage{}),                 MessageId::Welcome },
        { Encode(InputMessage{}),                   MessageId::Input },
        { Encode(SnapshotMessage{}),                MessageId::Snapshot },
        { EncodeEditRequest(EditMessage{}),         MessageId::EditRequest },
        { EncodeEditApplied(EditMessage{}),         MessageId::EditApplied },
        { Encode(FireMessage{}),                    MessageId::Fire },
        { Encode(ShotResolvedMessage{}),            MessageId::ShotResolved }
    };

    for (const auto& [bytes, expected] : cases)
    {
        MessageId id = MessageId::Hello;
        CAPTURE(static_cast<int>(expected));
        REQUIRE(PeekMessageId(bytes, id));
        CHECK(id == expected);
    }
}
