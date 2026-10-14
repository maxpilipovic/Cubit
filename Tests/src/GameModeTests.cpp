#include <doctest.h>

#include "Cubit/FrameClock.h"
#include "Cubit/Net/GameMode.h"
#include "Cubit/Net/LoopbackTransport.h"
#include "Cubit/Net/MatchClient.h"
#include "Cubit/Net/MatchServer.h"
#include "Cubit/Net/Protocol.h"

#include <optional>
#include <string>
#include <vector>

namespace
{
    constexpr std::uint64_t MapHash = 0xFEEDFACEull;
    const glm::vec3 Spawn{ 8.0f, 2.0f, 8.0f };
    const glm::vec3 Elsewhere{ 20.0f, 2.0f, 20.0f };

    World FlatWorld()
    {
        World world(2, 2, 2);
        for (int z = 0; z < world.GetDepth(); ++z)
            for (int x = 0; x < world.GetWidth(); ++x)
                world.SetBlock(x, 0, z, BlockId{ 1 });
        return world;
    }

    MatchClient::MapLoader GoodLoader()
    {
        return [](const std::string&) -> std::optional<LoadedMap>
        {
            return LoadedMap{ FlatWorld(), MapHash };
        };
    }

    //Records every call, and does what a test tells it to.
    struct RecordingMode final : GameMode
    {
        std::vector<PlayerId> Joined, Left, Killed, Killers;
        std::vector<std::vector<std::uint8_t>> Commands;
        int Steps = 0;
        bool AllowDamage = true;
        bool RespawnOnJoin = true;
        std::vector<std::uint8_t> State{ 1 };
        bool Changed = false;

        void OnJoin(GameModeHost& host, PlayerId player) override
        {
            Joined.push_back(player);
            if (RespawnOnJoin)
                host.Respawn(player, Spawn);
        }
        void OnLeave(GameModeHost&, PlayerId player) override { Left.push_back(player); }
        bool CanDamage(const GameModeHost&, PlayerId, PlayerId) const override { return AllowDamage; }
        void OnKilled(GameModeHost&, PlayerId victim, PlayerId killer) override
        {
            Killed.push_back(victim);
            Killers.push_back(killer);
        }
        void OnCommand(GameModeHost&, PlayerId, std::span<const std::uint8_t> bytes) override
        {
            Commands.emplace_back(bytes.begin(), bytes.end());
        }
        void Step(GameModeHost&) override { ++Steps; }
        bool TakeStateChanged() override { const bool was = Changed; Changed = false; return was; }
        std::vector<std::uint8_t> EncodeState() const override { return State; }
    };

    struct Rig
    {
        LoopbackNetwork Network;
        RecordingMode Mode;
        MatchServer Server;

        explicit Rig(bool withMode = true)
            : Server(FlatWorld(), "flat.vox", MapHash, Spawn, Network.Server(),
                MatchRules{}, withMode ? &Mode : nullptr) {}

        void Step(std::vector<MatchClient*> clients, int ticks = 1)
        {
            for (int t = 0; t < ticks; ++t)
            {
                for (MatchClient* client : clients)
                {
                    client->SetInput(CharacterInput{});
                    client->Step(FrameClock::FixedStepSeconds);
                }
                Server.Step(FrameClock::FixedStepSeconds);
            }
        }
    };
}

TEST_CASE("A mode hears the join, and the joiner appears where the mode says")
{
    Rig rig;
    PeerId peer = InvalidPeer;
    MatchClient client(rig.Network.AddClient(peer), GoodLoader());
    rig.Step({ &client }, 10);

    REQUIRE(rig.Mode.Joined.size() == 1);
    CHECK(rig.Mode.Joined[0] == client.LocalPlayer());
    CHECK(rig.Server.Match().IsAlive(client.LocalPlayer()));
    CHECK(rig.Mode.Steps >= 5);
}

TEST_CASE("A joiner the mode does not respawn stays dead and unstepped")
{
    Rig rig;
    rig.Mode.RespawnOnJoin = false;
    PeerId peer = InvalidPeer;
    MatchClient client(rig.Network.AddClient(peer), GoodLoader());
    rig.Step({ &client }, 30);

    const PlayerId player = client.LocalPlayer();
    CHECK_FALSE(rig.Server.Match().IsAlive(player));
    CHECK_FALSE(client.IsAlive(player));
}

TEST_CASE("A mode's state reaches every client when it changes, and a late joiner's welcome")
{
    Rig rig;
    PeerId peerA = InvalidPeer;
    MatchClient a(rig.Network.AddClient(peerA), GoodLoader());
    rig.Step({ &a }, 10);

    rig.Mode.State = { 4, 5, 6 };
    rig.Mode.Changed = true;
    rig.Step({ &a }, 3);
    CHECK(a.GameState() == std::vector<std::uint8_t>{ 4, 5, 6 });
    const std::uint64_t serial = a.GameStateSerial();

    PeerId peerB = InvalidPeer;
    MatchClient b(rig.Network.AddClient(peerB), GoodLoader());
    rig.Step({ &a, &b }, 10);
    CHECK(b.GameState() == std::vector<std::uint8_t>{ 4, 5, 6 });

    //Unchanged state is not resent.
    CHECK(a.GameStateSerial() == serial);
}

TEST_CASE("A client's command reaches the mode with its bytes")
{
    Rig rig;
    PeerId peer = InvalidPeer;
    MatchClient client(rig.Network.AddClient(peer), GoodLoader());
    rig.Step({ &client }, 10);

    const std::uint8_t switchTeam[] = { 1, 2 };
    client.SendCommand(switchTeam);
    rig.Step({ &client }, 3);

    REQUIRE(rig.Mode.Commands.size() == 1);
    CHECK(rig.Mode.Commands[0] == std::vector<std::uint8_t>{ 1, 2 });
}

TEST_CASE("Leaving tells the mode")
{
    Rig rig;
    PeerId peer = InvalidPeer;
    {
        MatchClient client(rig.Network.AddClient(peer), GoodLoader());
        rig.Step({ &client }, 10);
    }
    rig.Network.RemoveClient(peer);
    rig.Step({}, 3);
    CHECK(rig.Mode.Left.size() == 1);
}

TEST_CASE("Respawn and Kill through the host")
{
    Rig rig;
    PeerId peer = InvalidPeer;
    MatchClient client(rig.Network.AddClient(peer), GoodLoader());
    rig.Step({ &client }, 10);
    const PlayerId player = client.LocalPlayer();

    rig.Server.Kill(player);
    CHECK_FALSE(rig.Server.IsAlive(player));
    CHECK(rig.Mode.Killed.empty());   //Kill is not a ruled death

    rig.Server.Respawn(player, Elsewhere);
    CHECK(rig.Server.IsAlive(player));
    CHECK(rig.Server.Match().Player(player).Position() == Elsewhere);
    CHECK(rig.Server.HealthOf(player) == MatchRules{}.StartingHealth);

    rig.Step({ &client }, 20);
    CHECK(client.IsAlive(player));
}

TEST_CASE("Without a mode, nothing changes: joiners appear at the spawn, alive")
{
    Rig rig(false);
    PeerId peer = InvalidPeer;
    MatchClient client(rig.Network.AddClient(peer), GoodLoader());
    rig.Step({ &client }, 10);
    CHECK(rig.Server.Match().IsAlive(client.LocalPlayer()));
    CHECK(client.GameState().empty());
}

namespace
{
    //Raw endpoints rather than MatchClients for the shooting cases, so a shot's
    //claimed instant and aim are exactly what the test says. The same approach
    //MatchServerTests takes: both players stand on the spawn, so the shot is
    //point blank and a ray starting inside a box hits it whatever its aim.
    PlayerId Join(MatchServer& server, Transport& client)
    {
        client.Send(LoopbackNetwork::ServerPeer, Encode(HelloMessage{}), Channel::Reliable);

        for (int step = 0; step < 8; ++step)
        {
            server.Step(FrameClock::FixedStepSeconds);

            NetEvent event;
            while (client.Poll(event))
            {
                WelcomeMessage welcome;
                if (event.Type == NetEventType::Message && Decode(event.Data, welcome))
                    return welcome.You;
            }
        }

        return InvalidPlayer;
    }

    void Settle(MatchServer& server, PlayerId player)
    {
        for (int i = 0; i < 60 && !server.Match().Player(player).Grounded(); ++i)
            server.Step(FrameClock::FixedStepSeconds);
    }

    void SendFire(Transport& client, std::uint64_t renderTick)
    {
        FireMessage fire;
        fire.ClientTick = 1;
        fire.RenderTick = renderTick;
        client.Send(LoopbackNetwork::ServerPeer, Encode(fire), Channel::Reliable);
    }

    //Every message of one kind waiting on an endpoint. Poll consumes, so each
    //call drains everything and keeps what it was asked for.
    template<typename Message>
    std::vector<Message> Drain(Transport& transport, MessageId id)
    {
        std::vector<Message> found;

        NetEvent event;
        while (transport.Poll(event))
        {
            MessageId peeked = MessageId::Hello;
            if (event.Type != NetEventType::Message || !PeekMessageId(event.Data, peeked) || peeked != id)
                continue;

            Message message;
            if (Decode(event.Data, message))
                found.push_back(message);
        }

        return found;
    }

    //Fires once, returns every ruling it caused, and waits out the fire rate
    //so the next shot is not dropped for it.
    std::vector<ShotResolvedMessage> FireOnce(MatchServer& server, Transport& shooter)
    {
        Drain<ShotResolvedMessage>(shooter, MessageId::ShotResolved);

        SendFire(shooter, server.Match().Tick());
        server.Step(FrameClock::FixedStepSeconds);

        const std::vector<ShotResolvedMessage> rulings =
            Drain<ShotResolvedMessage>(shooter, MessageId::ShotResolved);

        for (int tick = 0; tick < MatchRules{}.TicksBetweenShots; ++tick)
            server.Step(FrameClock::FixedStepSeconds);

        return rulings;
    }

    struct Duel
    {
        Rig Match;
        PeerId ShooterPeer = InvalidPeer;
        PeerId VictimPeer = InvalidPeer;
        Transport& ShooterEnd;
        Transport& VictimEnd;
        PlayerId Shooter = InvalidPlayer;
        PlayerId Victim = InvalidPlayer;

        Duel()
            : ShooterEnd(Match.Network.AddClient(ShooterPeer)),
              VictimEnd(Match.Network.AddClient(VictimPeer))
        {
            Shooter = Join(Match.Server, ShooterEnd);
            Victim = Join(Match.Server, VictimEnd);
            Settle(Match.Server, Shooter);
            Settle(Match.Server, Victim);
        }
    };
}

TEST_CASE("A ruled kill makes the victim dead and tells the mode who killed them")
{
    Duel duel;
    REQUIRE(duel.Shooter != InvalidPlayer);
    REQUIRE(duel.Victim != InvalidPlayer);
    REQUIRE(duel.Match.Server.IsAlive(duel.Victim));
    const glm::vec3 standing = duel.Match.Server.Match().Player(duel.Victim).Position();

    //Enough hits to kill from full health, whatever the placeholder damage is.
    const int hits = (MatchRules{}.StartingHealth + MatchRules{}.ShotDamage - 1) / MatchRules{}.ShotDamage;

    bool killed = false;
    for (int shot = 0; shot < hits; ++shot)
    {
        const std::vector<ShotResolvedMessage> rulings = FireOnce(duel.Match.Server, duel.ShooterEnd);
        REQUIRE(rulings.size() == 1);
        CHECK(rulings[0].Victim == duel.Victim);
        killed = rulings[0].Killed;
    }

    CHECK(killed);
    CHECK_FALSE(duel.Match.Server.IsAlive(duel.Victim));
    CHECK(duel.Match.Mode.Killed == std::vector<PlayerId>{ duel.Victim });
    CHECK(duel.Match.Mode.Killers == std::vector<PlayerId>{ duel.Shooter });

    //Where the mode left them: a mode-run match does not teleport the dead back
    //to the engine's spawn, which is a block above where they stood.
    CHECK(duel.Match.Server.Match().Player(duel.Victim).Position() == standing);

    //And not stepped: a living player walks on these inputs, a dead one stays.
    CharacterInput walking;
    walking.Move = glm::vec2(0.0f, 1.0f);
    for (std::uint64_t tick = 1; tick <= 20; ++tick)
    {
        InputMessage input;
        input.FirstTick = tick;
        input.Inputs.push_back(walking);
        duel.VictimEnd.Send(LoopbackNetwork::ServerPeer, Encode(input), Channel::Unreliable);
        duel.Match.Server.Step(FrameClock::FixedStepSeconds);
    }

    CHECK(duel.Match.Server.Match().Player(duel.Victim).Position() == standing);
    CHECK(duel.Match.Mode.Killed.size() == 1);
}

TEST_CASE("A mode that refuses damage leaves the target unhurt")
{
    Duel duel;
    REQUIRE(duel.Victim != InvalidPlayer);
    duel.Match.Mode.AllowDamage = false;

    for (int shot = 0; shot < 4; ++shot)
    {
        const std::vector<ShotResolvedMessage> rulings = FireOnce(duel.Match.Server, duel.ShooterEnd);
        REQUIRE(rulings.size() == 1);
        CHECK(rulings[0].Victim == InvalidPlayer);
        CHECK_FALSE(rulings[0].Killed);
    }

    CHECK(duel.Match.Server.HealthOf(duel.Victim) == MatchRules{}.StartingHealth);
    CHECK(duel.Match.Server.IsAlive(duel.Victim));
    CHECK(duel.Match.Mode.Killed.empty());
}

TEST_CASE("The dead cannot shoot or edit")
{
    Duel duel;
    REQUIRE(duel.Shooter != InvalidPlayer);
    REQUIRE(duel.Victim != InvalidPlayer);

    duel.Match.Server.Kill(duel.Shooter);
    REQUIRE_FALSE(duel.Match.Server.IsAlive(duel.Shooter));

    //No ruling at all: a dead player's shot is not a miss, it never happened.
    for (int shot = 0; shot < 3; ++shot)
        CHECK(FireOnce(duel.Match.Server, duel.ShooterEnd).empty());

    CHECK(duel.Match.Server.HealthOf(duel.Victim) == MatchRules{}.StartingHealth);

    //A dig well inside reach, which a living player at the spawn has accepted
    //in MatchServerTests.
    Drain<EditResultMessage>(duel.ShooterEnd, MessageId::EditResult);

    const BlockEdit dig{ glm::ivec3(4, 0, 4), BlockId{ 0 } };
    InputMessage input;
    input.FirstTick = 1;
    input.Inputs.push_back(CharacterInput{});
    input.Edits.push_back(dig);
    duel.ShooterEnd.Send(LoopbackNetwork::ServerPeer, Encode(input), Channel::Unreliable);
    duel.Match.Server.Step(FrameClock::FixedStepSeconds);

    const std::vector<EditResultMessage> results =
        Drain<EditResultMessage>(duel.ShooterEnd, MessageId::EditResult);
    REQUIRE(results.size() == 1);
    CHECK_FALSE(results[0].Accepted);
    CHECK(results[0].Edit.Block == BlockId{ 1 });
    CHECK(duel.Match.Server.Match().GetWorld().GetBlock(4, 0, 4) == BlockId{ 1 });
    CHECK(duel.Match.Server.EditLog().empty());
}

TEST_CASE("Resetting the world restores every edited cell, for clients and joiners")
{
    Rig rig;
    PeerId peerA = InvalidPeer;
    MatchClient a(rig.Network.AddClient(peerA), GoodLoader());
    rig.Step({ &a }, 10);

    const BlockEdit dug{ glm::ivec3(4, 0, 4), BlockId{ 0 } };
    const BlockEdit built{ glm::ivec3(6, 1, 6), BlockId{ 3 } };
    rig.Server.ApplyEdits(std::vector<BlockEdit>{ dug, built });
    rig.Step({ &a }, 3);
    REQUIRE(a.Match().GetWorld().GetBlock(4, 0, 4) == BlockId{ 0 });

    rig.Server.ResetWorld();
    rig.Step({ &a }, 3);

    CHECK(rig.Server.Match().GetWorld().GetBlock(4, 0, 4) == BlockId{ 1 });
    CHECK(rig.Server.Match().GetWorld().GetBlock(6, 1, 6) == BlockId{ 0 });
    CHECK(a.Match().GetWorld().GetBlock(4, 0, 4) == BlockId{ 1 });
    CHECK(a.Match().GetWorld().GetBlock(6, 1, 6) == BlockId{ 0 });
    CHECK(rig.Server.EditLog().empty());

    //A joiner after the reset is told of no edits at all.
    PeerId peerB = InvalidPeer;
    MatchClient b(rig.Network.AddClient(peerB), GoodLoader());
    rig.Step({ &a, &b }, 10);
    CHECK(b.Match().GetWorld().GetBlock(4, 0, 4) == BlockId{ 1 });
}

TEST_CASE("A client neither predicts nor corrects while dead, and a respawn is a teleport")
{
    Rig rig;
    PeerId peer = InvalidPeer;
    MatchClient client(rig.Network.AddClient(peer), GoodLoader());
    rig.Step({ &client }, 60);
    const PlayerId player = client.LocalPlayer();

    rig.Server.Kill(player);
    rig.Step({ &client }, 10);
    CHECK_FALSE(client.IsAlive(player));

    //Walking while dead moves nothing on either side.
    const glm::vec3 where = client.Match().Player(player).Position();
    for (int i = 0; i < 30; ++i)
    {
        CharacterInput walk;
        walk.Move = glm::vec2(0.0f, 1.0f);
        client.SetInput(walk);
        client.Step(FrameClock::FixedStepSeconds);
        rig.Server.Step(FrameClock::FixedStepSeconds);
    }
    CHECK(client.Match().Player(player).Position() == where);

    const std::uint64_t correctionsBefore = client.Corrections().Count;
    rig.Server.Respawn(player, Elsewhere);
    rig.Step({ &client }, 10);

    CHECK(client.IsAlive(player));
    CHECK(glm::distance(client.Match().Player(player).Position(), Elsewhere) < 0.5f);
    CHECK(client.Corrections().Count == correctionsBefore);
}

TEST_CASE("The client knows the server's tick")
{
    Rig rig;
    PeerId peer = InvalidPeer;
    MatchClient client(rig.Network.AddClient(peer), GoodLoader());
    rig.Step({ &client }, 20);

    //Once more on its own. A client polls at the start of its own Step, so the
    //snapshot the server has just sent is not read until then.
    client.SetInput(CharacterInput{});
    client.Step(FrameClock::FixedStepSeconds);

    CHECK(client.ServerTick() == rig.Server.Tick());
}
