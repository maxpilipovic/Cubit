#include <doctest.h>

#include "Cubit/Net/Protocol.h"

#include <glm/glm.hpp>
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

        PlayerSnapshot second;
        second.Player = PlayerId{ 2 };
        second.Position = glm::vec3(-1.0f, 0.0f, 7.5f);
        second.Yaw = 90.0f;
        second.Pitch = -45.0f;
        second.VerticalVelocity = 0.0f;
        second.Grounded = false;

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

TEST_CASE("Input round-trips a sequence and the character's intent")
{
    InputMessage sent;
    sent.Sequence = 123456;
    sent.Input.Move = glm::vec2(-1.0f, 1.0f);
    sent.Input.Yaw = -135.0f;
    sent.Input.Pitch = 30.0f;
    sent.Input.Jump = true;

    InputMessage received;
    REQUIRE(Decode(Encode(sent), received));

    CHECK(received.Sequence == 123456);
    CHECK(received.Input.Move == glm::vec2(-1.0f, 1.0f));
    CHECK(received.Input.Yaw == doctest::Approx(-135.0f));
    CHECK(received.Input.Pitch == doctest::Approx(30.0f));
    CHECK(received.Input.Jump);
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
        input.Sequence = 9;
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
