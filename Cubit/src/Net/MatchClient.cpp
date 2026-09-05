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
    //Recorded before anything is drained, because a snapshot handled below
    //replays inputs and must replay them at the length prediction used.
    m_StepSeconds = static_cast<float>(seconds);

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
        {
            //A drop BEFORE the welcome is a refusal, and refusal is the only
            //thing it can be. MatchServer turns away a wrong protocol version
            //by calling Transport::Disconnect and sending nothing at all, and
            //a server that is not there produces the same event when ENet's
            //connect attempt gives up. Neither carries a message, so clearing
            //m_Connected on its own would leave Rejected() false and the client
            //sitting apparently idle for ever - the silent failure the loud one
            //exists to prevent, and the case the header promises Rejected()
            //reports.
            //
            //A drop AFTER the welcome is a session that ended rather than a
            //handshake that failed, so it clears Connected and leaves Rejected
            //alone.
            if (!m_Connected && !m_Rejected)
                Reject("the server ended the connection before the welcome");

            m_Connected = false;
            break;
        }

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

    //The tick this step is about to produce. Stamped before the step so an
    //input's tick names the step it caused, which is the number the server
    //echoes back and the number replay reinserts against.
    const std::uint64_t tick = m_Match.Tick() + 1;

    m_Unacked.push_back(PendingInput{ tick, m_Input });

    //A silent server cannot grow this without limit. Dropping the oldest loses
    //replay history for an input that is never going to be acknowledged
    //anyway.
    if (m_Unacked.size() > MaxUnackedInputs)
        m_Unacked.pop_front();

    //PREDICTION. This player only: this machine has no idea what anybody else
    //is about to do, and StepPlayer does nothing at all before the first
    //snapshot has said where this player stands.
    m_Match.StepPlayer(m_LocalPlayer, m_Input, static_cast<float>(seconds));
    m_Match.SetTick(tick);
    m_RemoteClock += 1.0;

    //The last three, oldest first. The redundancy is the whole defence against
    //the server stepping a tick with nothing to apply: one lost or late packet
    //is covered by the next one.
    const std::size_t count = std::min<std::size_t>(m_Unacked.size(), InputBundleSize);
    const std::size_t begin = m_Unacked.size() - count;

    InputMessage message;
    message.FirstTick = m_Unacked[begin].Tick;
    for (std::size_t i = begin; i < m_Unacked.size(); ++i)
        message.Inputs.push_back(m_Unacked[i].Input);

    //Unreliable: a resend would deliver an intent the player has already
    //replaced, and the bundle already covers the loss.
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

    //Starts this client's own clock here. From this point m_Match.Tick() is
    //the client's, free-running and advanced once per predicted Step - not
    //the server's, which HandleSnapshot tracks separately in m_ServerTick.
    m_Match.SetTick(welcome.Tick);
    m_ServerTick = welcome.Tick;
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

    //The client's own tick is not the server's any more - it free-runs and is
    //what an input is stamped with. Adopting the server's number here would
    //rewind it every snapshot and stamp two different inputs with one tick.
    m_ServerTick = snapshot.Tick;
    m_RemoteClock = static_cast<double>(snapshot.Tick);

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

        //The local player is predicted, not overwritten: reconcile against
        //this snapshot and move on before the generic SetState below can
        //stomp the replayed position with the authoritative one. HasPlayer is
        //true by construction here (added above if it was ever false), so the
        //check is belt and braces.
        if (entry.Player == m_LocalPlayer && m_Match.HasPlayer(entry.Player))
        {
            Reconcile(entry);
            continue;
        }

        //The previous position is the last one this client knew about. Nothing
        //currently renders a remote character's PreviousPosition - drawing
        //uses PoseOf and the sample ring below instead - so this argument's
        //only remaining job is to keep the two positions independent rather
        //than collapsed to the same value, for whatever next reads it.
        const glm::vec3 previous = m_Match.Player(entry.Player).Position();

        m_Match.PlayerForWrite(entry.Player).SetState(
            entry.Position, previous, entry.VerticalVelocity, entry.Grounded);

        std::deque<RemoteSample>& samples = m_RemoteSamples[entry.Player];
        samples.push_back(RemoteSample{ snapshot.Tick, entry.Position, entry.Yaw, entry.Pitch });

        if (samples.size() > MaxRemoteSamples)
            samples.pop_front();
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
        m_RemoteSamples.erase(player);
    }
}

void MatchClient::Reconcile(const PlayerSnapshot& entry)
{
    CharacterController& character = m_Match.PlayerForWrite(m_LocalPlayer);

    //What prediction believes, kept whole. If the correction turns out to be
    //too small to be worth showing, this is restored in one piece.
    const glm::vec3 before = character.Position();
    const glm::vec3 beforePrevious = character.PreviousPosition();
    const float beforeVelocity = character.VerticalVelocity();
    const bool beforeGrounded = character.Grounded();

    //All four, through SetState rather than Teleport. Teleport writes both
    //positions together, which would flatten the previous position and destroy
    //exactly the interpolation a correction exists to hide.
    //
    //The previous position comes from prediction rather than the wire because
    //the snapshot does not carry one - it is a render-smoothing value, not
    //simulation state anybody else needs. Whenever there is anything at all to
    //replay it is overwritten on the first replayed step; it only survives when
    //the server has caught up completely, and then continuing to interpolate
    //from where this client was drawing is the right answer anyway.
    //
    //NOTE: this argument is currently unpinned by any test in this suite.
    //Deliberately substituting entry.Position here (collapsing both positions
    //to the authoritative one) should only be observable when m_Unacked is
    //empty - the one case where nothing below overwrites PreviousPosition
    //again - and no test in this suite drives the client to that state at the
    //moment a correction lands. Recorded here per the plan rather than forcing
    //a contrived test to pin it.
    character.SetState(entry.Position, beforePrevious, entry.VerticalVelocity, entry.Grounded);

    //Everything the server has confirmed is history now.
    while (!m_Unacked.empty() && m_Unacked.front().Tick <= entry.LastInputTick)
        m_Unacked.pop_front();

    //And everything it has not seen is applied on top. This is reconciliation
    //entire: the authoritative state plus the inputs it does not know about is
    //what this machine should be showing.
    for (const PendingInput& pending : m_Unacked)
        m_Match.StepPlayer(m_LocalPlayer, pending.Input, m_StepSeconds);

    ++m_SnapshotsReconciled;

    const float error = glm::distance(character.Position(), before);

    if (error <= CorrectionThreshold)
    {
        //Thrown away WHOLE - position, previous position, velocity and grounded
        //together. Keeping the predicted position while accepting the
        //authoritative velocity would leave the character in a state neither
        //machine ever simulated, and the next step would compound it.
        character.SetState(before, beforePrevious, beforeVelocity, beforeGrounded);
        return;
    }

    //Over the threshold: the replayed state stands, and the player snaps. There
    //is no smoothing here on purpose - a snap is the one option with no new
    //failure mode and the only one that is cleanly testable.
    ++m_CorrectionCount;
    m_CorrectionTotal += error;
    m_CorrectionMax = std::max(m_CorrectionMax, error);
}

MatchClient::CorrectionStats MatchClient::Corrections() const
{
    CorrectionStats stats;
    stats.Snapshots = m_SnapshotsReconciled;
    stats.Count = m_CorrectionCount;
    stats.Max = m_CorrectionMax;
    stats.Mean = m_CorrectionCount == 0
        ? 0.0f
        : m_CorrectionTotal / static_cast<float>(m_CorrectionCount);
    return stats;
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

double MatchClient::RoundTripTime() const
{
    return m_ServerPeer == InvalidPeer ? 0.0 : m_Transport.RoundTripTime(m_ServerPeer);
}

MatchClient::RemotePose MatchClient::PoseOf(PlayerId player, float alpha) const
{
    const auto found = m_RemoteSamples.find(player);
    if (found == m_RemoteSamples.end() || found->second.empty())
        return RemotePose{};

    const std::deque<RemoteSample>& samples = found->second;

    //Deliberately in the past. Drawing at the newest sample would mean every
    //packet that arrives late is a remote standing still and then jumping.
    //
    //Note m_RemoteClock is at least one tick past the newest applied snapshot
    //whenever this is called in the same Step that processed it - see its
    //declaration. So "six ticks behind" here means six behind that floor, not
    //six behind the newest snapshot's own tick number.
    const double target = m_RemoteClock + alpha - InterpolationDelayTicks;

    //Newer than anything anybody has said: HOLD, do not guess. Extrapolation
    //is right most of the time and wrong exactly at a stop, a turn or a jump,
    //and being wrong means taking it back.
    const RemoteSample& newest = samples.back();
    if (target >= static_cast<double>(newest.ServerTick))
        return RemotePose{ newest.Position, newest.Yaw, newest.Pitch };

    //Older than anything kept: the connection has been quiet for longer than
    //the ring is deep. Hold the oldest for the same reason.
    const RemoteSample& oldest = samples.front();
    if (target <= static_cast<double>(oldest.ServerTick))
        return RemotePose{ oldest.Position, oldest.Yaw, oldest.Pitch };

    for (std::size_t i = 1; i < samples.size(); ++i)
    {
        const RemoteSample& previous = samples[i - 1];
        const RemoteSample& next = samples[i];

        if (target > static_cast<double>(next.ServerTick))
            continue;

        const double span = static_cast<double>(next.ServerTick - previous.ServerTick);
        const float t = span <= 0.0
            ? 0.0f
            : static_cast<float>((target - static_cast<double>(previous.ServerTick)) / span);

        RemotePose pose;
        pose.Position = glm::mix(previous.Position, next.Position, t);

        //Linear on purpose, and it takes the long way round across the ±180°
        //seam. Nothing draws a remote's facing yet - DrawRemotePlayers is an
        //axis-aligned box - so a wrap-aware lerp would be a guess with no way
        //to see it working. Fix this when something first draws a facing.
        pose.Yaw = glm::mix(previous.Yaw, next.Yaw, t);
        pose.Pitch = glm::mix(previous.Pitch, next.Pitch, t);
        return pose;
    }

    return RemotePose{ newest.Position, newest.Yaw, newest.Pitch };
}
