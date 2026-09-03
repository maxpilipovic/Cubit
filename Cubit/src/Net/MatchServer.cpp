#include "cub.h"

#include "Cubit/Net/MatchServer.h"

#include "Cubit/Logger.h"

#include <algorithm>
#include <optional>

MatchServer::MatchServer(World world, std::string mapName, std::uint64_t mapHash,
    const glm::vec3& spawn, Transport& transport)
    : m_Match(std::move(world)),
      m_MapName(std::move(mapName)),
      m_MapHash(mapHash),
      m_Spawn(spawn),
      m_Transport(transport)
{
}

void MatchServer::Step(double seconds)
{
    m_Transport.Advance(seconds);

    NetEvent event;
    while (m_Transport.Poll(event))
    {
        switch (event.Type)
        {
        case NetEventType::Connected:    HandleConnected(event.Peer); break;
        case NetEventType::Disconnected: HandleDisconnected(event.Peer); break;
        case NetEventType::Message:      HandleMessage(event.Peer, event.Data); break;
        case NetEventType::None:         break;
        }
    }

    //Before the step, so an edit and the movement that follows it in the same
    //tick see the same world.
    ApplyPendingEdits();

    std::vector<PlayerCommand> commands;
    commands.reserve(m_Clients.size());

    for (Client& client : m_Clients)
    {
        if (client.Player == InvalidPlayer || !client.HasInput)
            continue;

        commands.push_back(PlayerCommand{ client.Player, client.Input });
        client.HasInput = false;
    }

    //Sorted by player id so the step order does not depend on connection order
    //or on how the transport happened to schedule this tick's packets.
    std::sort(commands.begin(), commands.end(),
        [](const PlayerCommand& a, const PlayerCommand& b) { return a.Player < b.Player; });

    m_Match.Step(commands, static_cast<float>(seconds));

    SendSnapshots();
}

void MatchServer::HandleConnected(PeerId peer)
{
    //A socket, not yet a player. The player is minted when Hello is accepted,
    //so a peer that connects and says nothing costs a slot and no simulation.
    Client client;
    client.Peer = peer;
    m_Clients.push_back(client);
}

void MatchServer::HandleDisconnected(PeerId peer)
{
    const auto found = std::find_if(m_Clients.begin(), m_Clients.end(),
        [peer](const Client& candidate) { return candidate.Peer == peer; });

    if (found == m_Clients.end())
        return;

    if (found->Player != InvalidPlayer)
        m_Match.RemovePlayer(found->Player);

    m_Clients.erase(found);
}

void MatchServer::HandleMessage(PeerId peer, std::span<const std::uint8_t> data)
{
    Client* client = Find(peer);
    if (client == nullptr)
        return;

    MessageId id = MessageId::Hello;
    if (!PeekMessageId(data, id))
        return;

    switch (id)
    {
    case MessageId::Hello:
    {
        HelloMessage hello;
        if (!Decode(data, hello))
            return;

        if (hello.Version != ProtocolVersion)
        {
            //Loud, and terminal. Continuing on a best-effort basis with a build
            //that disagrees about field widths produces garbage positions,
            //which read as a physics bug rather than a handshake failure.
            CB_WARN("Rejecting a client speaking a different protocol version");
            m_Transport.Disconnect(peer);

            //Looks redundant over loopback, where Disconnect queues a
            //Disconnected event this same drain loop will pick up. It is not:
            //ENet's disconnect_now generates no local event, so without this
            //the ejected peer would linger in m_Clients for ever. The second
            //call, when it happens, finds nothing and does nothing.
            //
            //`client` dangles from here on - nothing below touches it.
            HandleDisconnected(peer);
            return;
        }

        //Already joined: a repeated Hello is ignored rather than minting a
        //second player for one socket.
        if (client->Player != InvalidPlayer)
            return;

        client->Player = m_Match.AddPlayer(m_Spawn);

        WelcomeMessage welcome;
        welcome.You = client->Player;
        welcome.MapName = m_MapName;
        welcome.MapHash = m_MapHash;
        welcome.Tick = m_Match.Tick();
        welcome.Edits = m_EditLog;
        m_Transport.Send(peer, Encode(welcome), Channel::Reliable);
        return;
    }

    case MessageId::Input:
    {
        InputMessage input;
        if (!Decode(data, input) || client->Player == InvalidPlayer)
            return;

        //Strictly greater, so a duplicate is dropped alongside a stale one.
        if (input.Sequence <= client->LastSequence)
            return;

        client->LastSequence = input.Sequence;
        client->HasInput = true;
        client->Input = input.Input;
        client->Yaw = input.Input.Yaw;
        client->Pitch = input.Input.Pitch;
        return;
    }

    case MessageId::EditRequest:
    {
        EditMessage edit;
        if (!Decode(data, edit) || client->Player == InvalidPlayer)
            return;

        m_PendingEdits.push_back(PendingEdit{ client->Player, edit.Edit });
        return;
    }

    //Server-to-client messages arriving at a server are malformed traffic, not
    //something to act on.
    case MessageId::Welcome:
    case MessageId::Snapshot:
    case MessageId::EditApplied:
        return;
    }
}

void MatchServer::ApplyPendingEdits()
{
    if (m_PendingEdits.empty())
        return;

    //Player-id order, not arrival order. Arrival order is socket scheduling,
    //which is not reproducible - two clients editing one block on one tick
    //would resolve differently run to run, and every test touching edits would
    //be a coin flip. stable_sort so two edits from one player keep the order
    //that player sent them in.
    std::stable_sort(m_PendingEdits.begin(), m_PendingEdits.end(),
        [](const PendingEdit& a, const PendingEdit& b) { return a.Player < b.Player; });

    for (const PendingEdit& pending : m_PendingEdits)
    {
        //Nothing came back: the position was out of range, or the block was
        //already what the client asked for. Either way the world did not
        //change, so there is nothing to log and nothing to tell anybody.
        const std::optional<BlockEdit> inverse = ApplyBlockEdit(m_Match.GetWorld(), pending.Edit);
        if (!inverse.has_value())
            continue;

        m_EditLog.push_back(pending.Edit);

        EditMessage applied;
        applied.Edit = pending.Edit;

        //Everyone joined, the requester included. A client's own world changes
        //only when this arrives, which is what makes the round trip visible.
        SendToJoined(EncodeEditApplied(applied), Channel::Reliable);
    }

    m_PendingEdits.clear();
}

void MatchServer::SendSnapshots()
{
    SnapshotMessage snapshot;
    snapshot.Tick = m_Match.Tick();
    snapshot.Players.reserve(m_Match.Players().size());

    for (const auto& [player, character] : m_Match.Players())
    {
        PlayerSnapshot entry;
        entry.Player = player;
        entry.Position = character.Position();
        entry.VerticalVelocity = character.VerticalVelocity();
        entry.Grounded = character.Grounded();

        //Angles live on the client record rather than on the character, because
        //CharacterController does not store them - they arrive in the input and
        //are consumed by the step.
        const auto owner = std::find_if(m_Clients.begin(), m_Clients.end(),
            [player](const Client& candidate) { return candidate.Player == player; });

        if (owner != m_Clients.end())
        {
            entry.Yaw = owner->Yaw;
            entry.Pitch = owner->Pitch;
        }

        snapshot.Players.push_back(entry);
    }

    //Encoded once and sent per peer. Unreliable: the next one supersedes this
    //one, so resending a stale snapshot spends bandwidth delivering something
    //already out of date.
    SendToJoined(Encode(snapshot), Channel::Unreliable);
}

void MatchServer::SendToJoined(const std::vector<std::uint8_t>& payload, Channel channel)
{
    for (const Client& client : m_Clients)
    {
        if (client.Player == InvalidPlayer)
            continue;

        m_Transport.Send(client.Peer, payload, channel);
    }
}

MatchServer::Client* MatchServer::Find(PeerId peer)
{
    const auto found = std::find_if(m_Clients.begin(), m_Clients.end(),
        [peer](const Client& candidate) { return candidate.Peer == peer; });

    return found == m_Clients.end() ? nullptr : &*found;
}
