#include "cub.h"

#include "Cubit/Voxel/MatchState.h"

#include <stdexcept>
#include <utility>

MatchState::MatchState(World world)
    : m_World(std::move(world))
{
}

PlayerId MatchState::AddPlayer(const glm::vec3& spawn)
{
    const PlayerId player = m_NextPlayer++;

    CharacterController character;

    // Teleport rather than assign, so both positions are written and the
    // first frame after a join does not interpolate the character in from
    // wherever a default-constructed one happened to be.
    character.Teleport(spawn);

    m_Players.emplace(player, character);
    return player;
}

PlayerId MatchState::AddPlayer(PlayerId id, const glm::vec3& spawn)
{
    if (id == InvalidPlayer)
        throw std::invalid_argument("AddPlayer: InvalidPlayer names nobody");

    if (m_Players.contains(id))
        throw std::invalid_argument("AddPlayer: id already present");

    CharacterController character;
    character.Teleport(spawn);
    m_Players.emplace(id, character);

    //Keeps the auto-minting overload from ever handing out an id that arrived
    //from outside. Without this a server that accepts id 7 and later mints one
    //itself produces two players sharing an id, and the map keeps one.
    if (id >= m_NextPlayer)
        m_NextPlayer = static_cast<PlayerId>(id + 1);

    return id;
}

void MatchState::RemovePlayer(PlayerId player)
{
    m_Players.erase(player);
}

bool MatchState::HasPlayer(PlayerId player) const
{
    return m_Players.find(player) != m_Players.end();
}

const CharacterController& MatchState::Player(PlayerId player) const
{
    const auto found = m_Players.find(player);

    if (found == m_Players.end())
        throw std::out_of_range("No such player in this match");

    return found->second;
}

void MatchState::TeleportPlayer(PlayerId player, const glm::vec3& position)
{
    const auto found = m_Players.find(player);

    if (found == m_Players.end())
        throw std::out_of_range("No such player in this match");

    found->second.Teleport(position);
}

CharacterController& MatchState::PlayerForWrite(PlayerId player)
{
    const auto found = m_Players.find(player);

    if (found == m_Players.end())
        throw std::out_of_range("No such player in this match");

    return found->second;
}

void MatchState::ReplaceWorld(World world)
{
    m_World = std::move(world);
}

void MatchState::Step(std::span<const PlayerCommand> commands, float seconds)
{
    // Everyone gets a step, including players nobody sent a command for this
    // tick: a dropped or late packet must leave a character falling under
    // gravity rather than frozen in the air, and on a real connection that is
    // the normal case rather than an edge one.
    for (auto& entry : m_Players)
    {
        const PlayerId player = entry.first;

        CharacterInput input;

        for (const PlayerCommand& command : commands)
        {
            if (command.Player == player)
            {
                input = command.Input;
                break;
            }
        }

        entry.second.Step(m_World, input, seconds);
    }

    // A command naming a player who is not here is ignored rather than
    // rejected. Once these arrive off a socket a stale id is malformed input,
    // not a caller bug - the same reasoning that makes ApplyBlockEdit return
    // nullopt for an out-of-range position instead of throwing.

    ++m_Tick;
}
