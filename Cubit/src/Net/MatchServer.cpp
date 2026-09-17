#include "cub.h"

#include "Cubit/Net/MatchServer.h"

#include "Cubit/Logger.h"
#include "Cubit/Voxel/Heading.h"
#include "Cubit/Voxel/ResolveShot.h"

#include <algorithm>
#include <optional>

namespace
{
    //How many unapplied inputs one client may have waiting.
    //
    //Eight is comfortably more than the two or three a healthy client keeps
    //there - it sends three at a time and the server takes one per tick - and
    //small enough that a client running far ahead is refused rather than
    //buffered. Overflow is dropped and logged: absorbing it silently would
    //show up as unexplained corrections much later.
    constexpr std::size_t MaxQueuedInputs = 8;

    //How far behind its newest input the server remembers which ticks a client
    //has sent it - the width of Client::SeenInputs. 64 ticks is over a second;
    //an input arriving later than that has its edit refused without checking.
    constexpr std::uint64_t RememberedInputTicks = 64;
}

MatchServer::MatchServer(World world, std::string mapName, std::uint64_t mapHash,
    const glm::vec3& spawn, Transport& transport, const MatchRules& rules)
    : m_Match(std::move(world)),
      m_MapName(std::move(mapName)),
      m_MapHash(mapHash),
      m_Spawn(spawn),
      m_Transport(transport),
      m_Rules(rules)
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

    std::vector<PlayerCommand> commands;
    commands.reserve(m_Clients.size());

    //One client's edit, taken off the queue with its input.
    struct TakenEdit
    {
        PlayerId Player = InvalidPlayer;
        PeerId Peer = InvalidPeer;
        std::uint64_t ClientTick = 0;
        BlockEdit Edit;
    };
    std::vector<TakenEdit> takenEdits;

    for (Client& client : m_Clients)
    {
        //Before the take, so an input that arrived just in time counts as a
        //depth of one - and every step, empty queue or not, because a starved
        //step is the thinnest moment there is.
        if (client.Player != InvalidPlayer)
        {
            client.DepthSamples[client.NextDepthSample] =
                static_cast<std::uint8_t>(std::min<std::size_t>(client.Queue.size(), 255));
            client.NextDepthSample = (client.NextDepthSample + 1) % client.DepthSamples.size();
            client.DepthSampleCount = std::min(client.DepthSampleCount + 1, client.DepthSamples.size());
        }

        //An empty queue means no input this tick, exactly as in Stage 2 when a
        //packet was lost. The player simply does not move; the client sees a
        //correction of one step of walking, 0.083 blocks, which is inside the
        //threshold and invisible. That is the whole reason bundling exists: it
        //makes this rare rather than routine.
        if (client.Player == InvalidPlayer || client.Queue.empty())
            continue;

        //THE OLDEST, not the newest. Taking the newest would discard intent the
        //client has already predicted on and shown on screen, guaranteeing a
        //correction every time a bundle arrived after a gap - precisely the
        //case bundling exists to survive.
        const Client::QueuedInput queued = client.Queue.front();
        client.Queue.pop_front();

        //The queue has room again: the next overflow is a new episode and
        //earns its own warning.
        if (client.Queue.size() < MaxQueuedInputs)
            client.QueueOverflowWarned = false;

        PassInput(client, queued.Tick);
        client.Yaw = queued.Input.Yaw;
        client.Pitch = queued.Input.Pitch;

        commands.push_back(PlayerCommand{ client.Player, queued.Input });

        if (queued.Edit.has_value())
            takenEdits.push_back(TakenEdit{ client.Player, client.Peer, queued.Tick, *queued.Edit });
    }

    //Sorted by player id so the step order does not depend on connection order
    //or on how the transport happened to schedule this tick's packets.
    std::sort(commands.begin(), commands.end(),
        [](const PlayerCommand& a, const PlayerCommand& b) { return a.Player < b.Player; });

    //BEFORE the step, and in player-id order - the order the client copies.
    //A client predicts its edit and then steps; applying edits after the
    //step here would leave a player standing on a block they have already
    //broken on their own screen.
    std::stable_sort(takenEdits.begin(), takenEdits.end(),
        [](const TakenEdit& a, const TakenEdit& b) { return a.Player < b.Player; });

    for (const TakenEdit& taken : takenEdits)
        ApplyInputEdit(taken.Player, taken.Peer, taken.ClientTick, taken.Edit);

    m_Match.Step(commands, static_cast<float>(seconds));

    //AFTER the step and BEFORE the snapshot, so the recorded position is the
    //one this tick's snapshot reports. Recording before the step would store
    //last tick's position under this tick's number, putting every rewind one
    //step in the past - which would look exactly like a rewind that is
    //slightly too aggressive rather than like an off-by-one.
    //
    //Filed under Tick(), NOT Tick() - 1. SendSnapshots, two lines below, labels
    //this same position with snapshot.Tick == m_Match.Tick() - and the wire's
    //label is the only one the client ever sees: MatchClient::HandleSnapshot
    //sets its clock straight from snapshot.Tick, and every instant it later
    //hands back to the server - PoseOf's, Fire's - is a number on that same
    //timeline. A history that filed this position under Tick() - 1 would be
    //using a tick number the client has no way to name, so an instant that
    //names this position on the wire would land one tick short of it here.
    const std::uint64_t recordedTick = m_Match.Tick();
    for (const auto& [player, character] : m_Match.Players())
        m_History.Record(player, recordedTick, character.Position());

    SendSnapshots();
}

void MatchServer::SkipTicks(int ticks)
{
    if (ticks <= 0)
        return;

    for (Client& client : m_Clients)
    {
        const std::size_t skip = std::min(client.Queue.size(), static_cast<std::size_t>(ticks));
        if (skip == 0)
            continue;

        for (std::size_t i = 0; i < skip; ++i)
        {
            const Client::QueuedInput skipped = client.Queue.front();
            client.Queue.pop_front();

            //Past this tick now, so a bundle that repeats it is not queued again.
            PassInput(client, skipped.Tick);

            if (skipped.Edit.has_value())
                RefuseDiscardedEdit(client.Peer, skipped.Tick, *skipped.Edit);
        }

        CB_WARN("Skipped " + std::to_string(skip) + " queued inputs for player "
            + std::to_string(client.Player) + ": the server lost "
            + std::to_string(ticks) + " ticks");
    }
}

void MatchServer::RefuseDiscardedEdit(PeerId peer, std::uint64_t clientTick, const BlockEdit& edit)
{
    //The client predicted this edit and keeps showing it until it hears the
    //edit's fate, so an input thrown away with an edit on it still owes an
    //answer. Refused with the server's block, exactly as an illegal edit is -
    //the client already knows how to take one of those back.
    EditResultMessage result;
    result.ClientTick = clientTick;
    result.Accepted = false;
    result.Edit.Position = edit.Position;

    const glm::ivec3& at = edit.Position;
    result.Edit.Block = m_Match.GetWorld().GetBlock(at.x, at.y, at.z);

    m_Transport.Send(peer, Encode(result), Channel::Reliable);
}

void MatchServer::PassInput(Client& client, std::uint64_t tick)
{
    //Only ever forward: the queue admits nothing at or below LastInputTick.
    const std::uint64_t advance = tick - client.LastInputTick;

    //Shifting in zeros marks every tick jumped over as never received, which
    //is right: the queue is sorted, so any of them that had arrived would have
    //been taken before this one.
    client.SeenInputs = advance >= RememberedInputTicks ? 0 : client.SeenInputs << advance;
    client.SeenInputs |= 1;
    client.LastInputTick = tick;
}

std::uint8_t MatchServer::SpareInputsOf(const Client& client)
{
    //A backlog is only a backlog if it lasts a whole window.
    if (client.DepthSampleCount < client.DepthSamples.size())
        return 0;

    //The thinnest moment, not the latest or the average: a jitter buffer runs
    //down to one input at its worst and is doing its job, and trimming it would
    //starve the next worst moment. One is what an input arriving just in time
    //leaves.
    const auto [thinnestAt, deepestAt] =
        std::minmax_element(client.DepthSamples.begin(), client.DepthSamples.end());
    const int thinnest = *thinnestAt;

    //And one more in reserve if the depth moved at all. A link that loses
    //packets runs its queue down furthest when two or three bundles in a row are
    //lost, which can be longer apart than any window worth waiting for: measured
    //on the suite's 5%-loss links, a window with no reserve reported inputs to
    //spare 24 times in 35,000 ticks, each a starved tick waiting to happen, and
    //with the reserve none. A queue that held one depth for the whole window is
    //arriving like clockwork, and gets back everything above one.
    const int keep = *thinnestAt == *deepestAt ? 1 : 2;

    return static_cast<std::uint8_t>(std::max(thinnest - keep, 0));
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
    {
        m_Match.RemovePlayer(found->Player);
        m_History.Forget(found->Player);
    }

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
        client->Health = m_Rules.StartingHealth;

        WelcomeMessage welcome;
        welcome.You = client->Player;
        welcome.MapName = m_MapName;
        welcome.MapHash = m_MapHash;
        welcome.Tick = m_Match.Tick();
        welcome.Edits = m_EditLog;

        const std::vector<std::uint8_t> payload = Encode(welcome);

        //The one message with no size bound: 14 bytes for every cell that
        //differs from the map. Past what the transport carries - 32 MB over
        //ENet, about 2.4 million changed cells, or 14% of a 512x64x512 map - the
        //send would be dropped and the joiner left waiting on a Welcome that
        //never comes. An accepted limit rather than a solved one: sending that
        //much before a player can move is its own problem well before it gets
        //here, and a join split into chunks is the recorded answer if a match
        //ever needs one. So the joiner is refused out loud, the way a wrong
        //protocol version is, and the client reports a refusal.
        if (payload.size() > m_Transport.MaxMessageBytes())
        {
            CB_ERROR("Refusing a joiner: the welcome carries " + std::to_string(m_EditLog.size())
                + " changed cells in " + std::to_string(payload.size()) + " bytes, and the transport carries at most "
                + std::to_string(m_Transport.MaxMessageBytes()));

            m_Transport.Disconnect(peer);

            //`client` dangles from here on, as in the version refusal above.
            HandleDisconnected(peer);
            return;
        }

        m_Transport.Send(peer, payload, Channel::Reliable);
        return;
    }

    case MessageId::Input:
    {
        InputMessage input;
        if (!Decode(data, input) || client->Player == InvalidPlayer)
            return;

        for (std::size_t i = 0; i < input.Inputs.size(); ++i)
        {
            const std::uint64_t tick = input.FirstTick + i;

            //Behind the server. Never stepped, whichever case it is: applying
            //it now would rewind the player.
            if (tick <= client->LastInputTick)
            {
                const std::uint64_t age = client->LastInputTick - tick;
                const std::uint64_t bit = age < RememberedInputTicks ? std::uint64_t{ 1 } << age : 0;

                //Already had. Bundles repeat, so this is the common case rather
                //than an anomaly, and dropping it here is what makes the
                //redundancy free instead of a rewind.
                if ((client->SeenInputs & bit) != 0)
                    continue;

                //Never had: reordering held back every bundle carrying it until
                //the server had taken a newer tick. Its edit has had no answer,
                //and the client shows it until one comes.
                //
                //Past the window the server cannot tell, and refuses anyway. A
                //refusal carries the server's block, which is true whether or not
                //this edit was answered before; silence is the desync.
                client->SeenInputs |= bit;

                if (i < input.Edits.size() && input.Edits[i].has_value())
                    RefuseDiscardedEdit(client->Peer, tick, *input.Edits[i]);

                continue;
            }

            const bool waiting = std::any_of(client->Queue.begin(), client->Queue.end(),
                [tick](const Client::QueuedInput& queued) { return queued.Tick == tick; });

            if (waiting)
                continue;

            client->Queue.push_back(Client::QueuedInput{ tick, input.Inputs[i],
                i < input.Edits.size() ? input.Edits[i] : std::nullopt });
        }

        //The unreliable channel reorders, so a bundle can arrive carrying ticks
        //older than ones already queued. A step takes the front, so the front
        //has to be the oldest.
        std::sort(client->Queue.begin(), client->Queue.end(),
            [](const Client::QueuedInput& a, const Client::QueuedInput& b)
            {
                return a.Tick < b.Tick;
            });

        //Over the cap, the OLDEST go, not the newest. A full queue means the
        //server has fallen behind this client - a stall is the case that
        //reaches it - and the newest inputs are the ones a caught-up server
        //needs. Keeping the oldest instead leaves the gap the dropped newest
        //inputs made, and the next bundle refills it with ticks the client sent
        //before that bundle, standing in the queue as permanent delay.
        if (client->Queue.size() > MaxQueuedInputs)
        {
            //Once per overflow episode, not once per dropped input: a client
            //that stays three ticks ahead drops up to three inputs a tick, and
            //this runs every tick it stays that way.
            if (!client->QueueOverflowWarned)
            {
                CB_WARN("Dropping inputs for player " + std::to_string(client->Player)
                    + ": its queue is full");
                client->QueueOverflowWarned = true;
            }

            while (client->Queue.size() > MaxQueuedInputs)
            {
                const Client::QueuedInput dropped = client->Queue.front();
                client->Queue.pop_front();

                //Past this tick now, as if taken, so a bundle that repeats it is
                //not queued again.
                PassInput(*client, dropped.Tick);

                if (dropped.Edit.has_value())
                    RefuseDiscardedEdit(client->Peer, dropped.Tick, *dropped.Edit);
            }
        }

        return;
    }

    case MessageId::Fire:
    {
        FireMessage fire;
        if (!Decode(data, fire) || client->Player == InvalidPlayer)
            return;

        HandleFire(*client, fire);
        return;
    }

    //Server-to-client messages arriving at a server are malformed traffic, not
    //something to act on.
    case MessageId::Welcome:
    case MessageId::Snapshot:
    case MessageId::EditApplied:
    case MessageId::ShotResolved:
        return;
    }
}

void MatchServer::ApplyInputEdit(PlayerId player, PeerId peer, std::uint64_t clientTick,
    const BlockEdit& edit)
{
    EditResultMessage result;
    result.ClientTick = clientTick;
    result.Edit.Position = edit.Position;

    std::optional<BlockEdit> inverse;
    if (IsEditLegal(m_Match, player, edit, OtherPlayers::Check))
        inverse = ApplyBlockEdit(m_Match.GetWorld(), edit);

    if (inverse.has_value())
    {
        result.Accepted = true;
        ++m_AcceptedEditCount;

        //The inverse holds what the cell held just before - which, the first
        //time this cell changes, is the map's own block.
        RecordInLog(edit, inverse->Block);

        //Everyone but the editor, which hears its own edit's fate from the
        //result below.
        Broadcast(std::span(&edit, 1), peer);
    }

    //The server's truth either way, so the client never has to work it out.
    const glm::ivec3& at = edit.Position;
    result.Edit.Block = m_Match.GetWorld().GetBlock(at.x, at.y, at.z);

    m_Transport.Send(peer, Encode(result), Channel::Reliable);

    //After the result, so the editor sees its own dig answered before it is told
    //what came down with it. The collapse goes to everyone, the editor included:
    //it is nobody's edit, so nobody is predicting it.
    if (inverse.has_value())
    {
        const std::vector<BlockEdit> fell = ApplyAndLog(CollapseEdits(std::span(&edit, 1)));
        Broadcast(fell);
    }
}

std::size_t MatchServer::ApplyEdits(std::span<const BlockEdit> edits)
{
    std::vector<BlockEdit> changed = ApplyAndLog(edits);

    //In the same call, so the blocks that came loose ride the same messages as
    //the change that loosened them: a client never draws a frame with the hole
    //but not the collapse.
    const std::vector<BlockEdit> fell = ApplyAndLog(CollapseEdits(changed));
    changed.insert(changed.end(), fell.begin(), fell.end());

    Broadcast(changed);
    return changed.size();
}

std::vector<BlockEdit> MatchServer::ApplyAndLog(std::span<const BlockEdit> edits)
{
    std::vector<BlockEdit> changed;
    changed.reserve(edits.size());

    //One at a time rather than through ApplyBlockEdits, because the log needs
    //each edit's previous block, and a batch's undo list cannot say which edit
    //it came from once no-ops have been skipped.
    for (const BlockEdit& edit : edits)
    {
        const std::optional<BlockEdit> inverse = ApplyBlockEdit(m_Match.GetWorld(), edit);
        if (!inverse.has_value())
            continue;

        RecordInLog(edit, inverse->Block);
        changed.push_back(edit);
    }

    return changed;
}

std::vector<BlockEdit> MatchServer::CollapseEdits(std::span<const BlockEdit> applied)
{
    //Only the cells this change emptied can have left anything hanging.
    std::vector<glm::ivec3> emptied;
    for (const BlockEdit& edit : applied)
    {
        const glm::ivec3& at = edit.Position;
        if (!m_Match.GetWorld().IsBlockSolid(at.x, at.y, at.z))
            emptied.push_back(at);
    }

    if (emptied.empty())
        return {};

    std::vector<BlockEdit> falls;
    for (const glm::ivec3& cell : FindUnsupported(m_Match.GetWorld(), emptied))
        falls.push_back(BlockEdit{ cell, BlockId{ 0 } });

    return falls;
}

void MatchServer::Broadcast(std::span<const BlockEdit> changed, PeerId except)
{
    for (std::size_t first = 0; first < changed.size(); first += MaxEditsPerMessage)
    {
        const std::size_t count = std::min(MaxEditsPerMessage, changed.size() - first);

        EditMessage message;
        message.Edits.assign(changed.begin() + first, changed.begin() + first + count);
        SendToJoined(EncodeEditApplied(message), Channel::Reliable, except);
    }
}

void MatchServer::RecordInLog(const BlockEdit& edit, BlockId previous)
{
    const glm::ivec3& cell = edit.Position;

    //Only inserts on the cell's first change, so this is the map's block for
    //every later edit too.
    const BlockId mapBlock = m_MapBlock.try_emplace(cell, previous).first->second;
    const auto indexed = m_LogIndex.find(cell);

    if (edit.Block == mapBlock)
    {
        //Back to the map: nothing left for a joiner to replay here.
        if (indexed != m_LogIndex.end())
        {
            //Move the last entry into this one's slot, so removal does not shift
            //every entry after it - and repoint that entry's index, which is
            //the one thing a swap can leave stale.
            const std::size_t slot = indexed->second;
            const std::size_t last = m_EditLog.size() - 1;
            if (slot != last)
            {
                m_EditLog[slot] = m_EditLog[last];
                m_LogIndex[m_EditLog[slot].Position] = slot;
            }

            m_EditLog.pop_back();
            m_LogIndex.erase(cell);
        }

        m_MapBlock.erase(cell);
        return;
    }

    if (indexed != m_LogIndex.end())
    {
        m_EditLog[indexed->second].Block = edit.Block;
        return;
    }

    m_LogIndex.emplace(cell, m_EditLog.size());
    m_EditLog.push_back(edit);
}

void MatchServer::HandleFire(Client& shooter, const FireMessage& fire)
{
    const std::uint64_t now = m_Match.Tick();

    //The fire rate, which is also the flood guard.
    if (shooter.HasFired && now - shooter.LastShotTick < static_cast<std::uint64_t>(m_Rules.TicksBetweenShots))
    {
        if (!shooter.FireRateWarned)
        {
            //CB_WARN takes ONE argument and does no formatting - it is
            //`Logger::Warn(message)`. Build the string, matching how the
            //input-queue warning a few lines up already does it.
            CB_WARN("Dropping a shot from player " + std::to_string(shooter.Player)
                + " fired faster than the weapon allows");
            shooter.FireRateWarned = true;
        }

        return;
    }

    shooter.FireRateWarned = false;
    shooter.HasFired = true;
    shooter.LastShotTick = now;

    //THE CLAMP. Applied to the combined fractional instant, never to the whole
    //part alone: clamping the two separately would let a claim of tick 0 with
    //alpha 0.9 survive as a fractional offset on a completely different tick.
    const double claimed = static_cast<double>(fire.RenderTick) + static_cast<double>(fire.RenderAlpha);
    const double newest = static_cast<double>(now);
    const double oldest = newest - static_cast<double>(MaxRewindTicks);
    const double instant = glm::clamp(claimed, oldest, newest);

    //THE TARGETS rewind to the instant the shooter's screen was showing.
    std::vector<ShotCandidate> candidates;
    for (const auto& [player, character] : m_Match.Players())
    {
        //Never a candidate against their own shot.
        if (player == shooter.Player)
            continue;

        Aabb box;
        //False means there is no record of them at that instant - they joined
        //after it, or they have respawned since. Not a hit of zero size.
        if (!m_History.BoxAt(player, instant, character.Config().HalfExtents, box))
            continue;

        candidates.push_back(ShotCandidate{ player, box });
    }

    //THE SHOOTER'S OWN EYE comes from a different instant: the tick the server
    //last stepped them, which is where it already believes they stand. Using
    //the render instant here would put their eye a round trip behind where they
    //believe they are, and every shot fired while moving would leave from the
    //wrong place.
    const CharacterController& character = m_Match.Player(shooter.Player);
    const glm::vec3 eye = character.Position() + glm::vec3(0.0f, character.Config().EyeOffset, 0.0f);
    const glm::vec3 direction = AimDirection(fire.Yaw, fire.Pitch);

    const ShotResult shot = ResolveShot(
        m_Match.GetWorld(), candidates, eye, direction, m_Rules.ShotRange);

    ShotResolvedMessage resolved;
    resolved.Shooter = shooter.Player;
    resolved.Victim = shot.Victim;
    resolved.Impact = shot.Impact;
    resolved.VictimHealth = 0;
    resolved.Killed = false;

    if (shot.Victim != InvalidPlayer)
    {
        const auto found = std::find_if(m_Clients.begin(), m_Clients.end(),
            [&shot](const Client& candidate) { return candidate.Player == shot.Victim; });

        if (found != m_Clients.end())
        {
            Client& victim = *found;

            //Clamped rather than allowed to wrap. Health is unsigned, so
            //34 subtracted from 32 is not -2, it is 254 - a dead player at
            //more than full health.
            victim.Health = victim.Health <= m_Rules.ShotDamage
                ? std::uint8_t{ 0 }
                : static_cast<std::uint8_t>(victim.Health - m_Rules.ShotDamage);

            resolved.VictimHealth = victim.Health;
            resolved.Killed = victim.Health == 0;

            if (resolved.Killed)
            {
                m_Match.TeleportPlayer(victim.Player, m_Spawn);
                m_Match.PlayerForWrite(victim.Player).SetVerticalVelocity(0.0f);
                victim.Health = m_Rules.StartingHealth;

                //THE HISTORY GOES TOO. Without this, a shot already in flight
                //could rewind to before the death, find the victim standing
                //where they fell, and damage the player who has since
                //respawned there.
                m_History.Forget(victim.Player);
            }
        }
    }

    SendToJoined(Encode(resolved), Channel::Reliable);
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
            entry.LastInputTick = owner->LastInputTick;
            entry.Health = owner->Health;
            entry.SpareInputs = SpareInputsOf(*owner);
        }

        snapshot.Players.push_back(entry);
    }

    //Encoded once and sent per peer. Unreliable: the next one supersedes this
    //one, so resending a stale snapshot spends bandwidth delivering something
    //already out of date.
    SendToJoined(Encode(snapshot), Channel::Unreliable);
}

void MatchServer::SendToJoined(const std::vector<std::uint8_t>& payload, Channel channel,
    PeerId except)
{
    for (const Client& client : m_Clients)
    {
        if (client.Player == InvalidPlayer || client.Peer == except)
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

std::uint8_t MatchServer::HealthOf(PlayerId player) const
{
    const auto found = std::find_if(m_Clients.begin(), m_Clients.end(),
        [player](const Client& candidate) { return candidate.Player == player; });

    return found == m_Clients.end() ? 0 : found->Health;
}
