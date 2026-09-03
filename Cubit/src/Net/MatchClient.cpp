#include "cub.h"

#include "Cubit/Net/MatchClient.h"

#include "Cubit/Logger.h"

#include <algorithm>
#include <string>
#include <vector>

MatchClient::MatchClient(Transport& transport, MapLoader loadMap)
    : m_Transport(transport), m_LoadMap(std::move(loadMap))
{
}

void MatchClient::Step(double seconds)
{
    m_Transport.Advance(seconds);

    NetEvent event;
    while (m_Transport.Poll(event))
    {
        switch (event.Type)
        {
        case NetEventType::Connected:
        {
            m_ServerPeer = event.Peer;

            if (!m_SaidHello)
            {
                m_Transport.Send(m_ServerPeer, Encode(HelloMessage{}), Channel::Reliable);
                m_SaidHello = true;
            }
            break;
        }

        case NetEventType::Disconnected:
            m_Connected = false;
            break;

        case NetEventType::Message:
        {
            MessageId id = MessageId::Hello;
            if (!PeekMessageId(event.Data, id))
                break;

            switch (id)
            {
            case MessageId::Welcome:     HandleWelcome(event.Data); break;
            case MessageId::Snapshot:    HandleSnapshot(event.Data); break;
            case MessageId::EditApplied: HandleEditApplied(event.Data); break;

            //Client-to-server messages arriving at a client are malformed
            //traffic, not something to act on.
            case MessageId::Hello:
            case MessageId::Input:
            case MessageId::EditRequest:
                break;
            }
            break;
        }

        case NetEventType::None:
            break;
        }
    }

    if (!m_Connected || !m_HasInput)
        return;

    InputMessage message;
    message.Sequence = ++m_Sequence;
    message.Input = m_Input;

    //Unreliable: a lost input costs one step of movement, which is a small
    //stutter and is honest. Resending it would deliver an intent the player
    //has already replaced.
    m_Transport.Send(m_ServerPeer, Encode(message), Channel::Unreliable);
    m_HasInput = false;
}

void MatchClient::SetInput(const CharacterInput& input)
{
    m_Input = input;
    m_HasInput = true;
}

void MatchClient::RequestEdit(const BlockEdit& edit)
{
    if (!m_Connected)
        return;

    EditMessage message;
    message.Edit = edit;

    //Reliable: an edit that arrives late is still correct, but one that
    //vanishes is a permanent world desync.
    m_Transport.Send(m_ServerPeer, EncodeEditRequest(message), Channel::Reliable);
}

void MatchClient::HandleWelcome(std::span<const std::uint8_t> data)
{
    WelcomeMessage welcome;
    if (!Decode(data, welcome))
        return;

    //A second Welcome is a server fault or a replayed packet; either way the
    //world has already been built and rebuilding it would discard live state.
    if (m_Connected)
        return;

    //Nobody is player zero, so a Welcome naming it cannot be honoured: the
    //client would hold a LocalPlayer that never appears in any snapshot and
    //sit there apparently connected to nothing.
    if (welcome.You == InvalidPlayer)
    {
        Reject("the server assigned an id that names nobody");
        return;
    }

    std::optional<LoadedMap> loaded = m_LoadMap(welcome.MapName);
    if (!loaded.has_value())
    {
        Reject("the server's map is not present on this machine");
        return;
    }

    if (loaded->Hash != welcome.MapHash)
    {
        //Now, loudly. A mismatched map desyncs silently an hour later as
        //movement that disagrees with the server, which reads as a netcode bug
        //and is not one.
        Reject("this machine's copy of the map differs from the server's");
        return;
    }

    m_Match.ReplaceWorld(std::move(loaded->Map));
    m_Match.SetTick(welcome.Tick);
    m_LastSnapshotTick = welcome.Tick;

    //Replay the edits applied since the map loaded. Without this, a client
    //joining after somebody dug a hole gets a pristine world.
    for (const BlockEdit& edit : welcome.Edits)
        ApplyBlockEdit(m_Match.GetWorld(), edit);

    m_LocalPlayer = welcome.You;
    m_Connected = true;
}

void MatchClient::HandleSnapshot(std::span<const std::uint8_t> data)
{
    if (!m_Connected)
        return;

    SnapshotMessage snapshot;
    if (!Decode(data, snapshot))
        return;

    //Jitter reorders the unreliable channel. An older snapshot arriving after
    //a newer one must be discarded, not applied: rewinding the world reads on
    //screen as a physics fault.
    if (snapshot.Tick < m_LastSnapshotTick)
        return;

    m_LastSnapshotTick = snapshot.Tick;
    m_Match.SetTick(snapshot.Tick);

    std::vector<PlayerId> present;
    present.reserve(snapshot.Players.size());

    for (const PlayerSnapshot& entry : snapshot.Players)
    {
        //PlayerSnapshot::Player is a raw u16 off the wire and Decode has no
        //reason to reject any value of it, but MatchState::AddPlayer throws on
        //InvalidPlayer - so without this one malformed packet becomes an
        //uncaught exception. Dropped rather than reported, matching every other
        //decoder here: malformed input is a routine wire condition. Skipped
        //before `present` too, or the entry would keep an id nobody holds alive
        //and cull nothing.
        if (entry.Player == InvalidPlayer)
            continue;

        present.push_back(entry.Player);

        if (!m_Match.HasPlayer(entry.Player))
        {
            //Derived from the roster rather than announced. Ids are never
            //reused, so a player appearing in a snapshot is a join and one
            //disappearing is a leave - which is why the protocol has no
            //message for either.
            m_Match.AddPlayer(entry.Player, entry.Position);
        }

        //The previous position is the last one this client knew about, so the
        //renderer's existing alpha interpolation smooths between snapshots
        //rather than snapping. At 60 Hz that gap is exactly one fixed step,
        //which is what the interpolation already assumes.
        const glm::vec3 previous = m_Match.Player(entry.Player).Position();

        m_Match.PlayerForWrite(entry.Player).SetState(
            entry.Position, previous, entry.VerticalVelocity, entry.Grounded);

        m_ViewAngles[entry.Player] = glm::vec2(entry.Yaw, entry.Pitch);
    }

    //Anybody the snapshot did not mention has left.
    std::vector<PlayerId> departed;
    for (const auto& [player, character] : m_Match.Players())
    {
        (void)character;
        if (std::find(present.begin(), present.end(), player) == present.end())
            departed.push_back(player);
    }

    for (const PlayerId player : departed)
    {
        m_Match.RemovePlayer(player);
        m_ViewAngles.erase(player);
    }
}

void MatchClient::HandleEditApplied(std::span<const std::uint8_t> data)
{
    if (!m_Connected)
        return;

    EditMessage message;
    if (!Decode(data, message))
        return;

    ApplyBlockEdit(m_Match.GetWorld(), message.Edit);
}

void MatchClient::Reject(const char* reason)
{
    CB_ERROR(std::string("Refusing to join: ") + reason);

    m_Rejected = true;
    m_Connected = false;

    if (m_ServerPeer != InvalidPeer)
        m_Transport.Disconnect(m_ServerPeer);
}

glm::vec2 MatchClient::ViewAngles(PlayerId player) const
{
    const auto found = m_ViewAngles.find(player);
    return found == m_ViewAngles.end() ? glm::vec2(0.0f) : found->second;
}

double MatchClient::RoundTripTime() const
{
    return m_ServerPeer == InvalidPeer ? 0.0 : m_Transport.RoundTripTime(m_ServerPeer);
}
