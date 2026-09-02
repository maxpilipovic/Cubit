#pragma once

#include "Cubit/Core.h"
#include "Cubit/Voxel/CharacterController.h"
#include "Cubit/Voxel/World.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <map>
#include <span>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

//Identifies one participant for the length of a match. Never reused, so a
//stale message naming someone who left is not applied to whoever joined next.
using PlayerId = std::uint16_t;

//Ids start at 1, so a zeroed or default-constructed id names nobody rather
//than naming the first player.
constexpr PlayerId InvalidPlayer = 0;

//One player's intent for one step.
struct PlayerCommand
{
    PlayerId Player = InvalidPlayer;
    CharacterInput Input;
};

//The whole simulated state of a match: the world, everyone in it, and how far
//it has been stepped.
//
//The point of this type is that there is exactly one implementation of a step
//and both a server and a client call it. If they ever step differently,
//reconciliation fights the player forever, and the symptom looks like a
//network fault rather than a simulation one.
//
//Deliberately not an entity system. There is one kind of actor, and an
//abstraction over one kind is a guess - the same reason a general entity
//system is still not built. See
//docs/superpowers/specs/2026-08-27-networking-design.md.
class CB_API MatchState
{
public:
    explicit MatchState(World world);

    //Adds a player standing at this position and returns their id.
    PlayerId AddPlayer(const glm::vec3& spawn);

    //Adds a player under an id chosen by someone else - the server, over a
    //wire. Returns the id it was given.
    //
    //Throws when the id is InvalidPlayer or already present: silently
    //replacing a player would drop whoever held that id, and silently
    //ignoring the call would leave the caller believing in a player that does
    //not exist.
    PlayerId AddPlayer(PlayerId id, const glm::vec3& spawn);

    //Removes a player. Removing one who is not present does nothing.
    void RemovePlayer(PlayerId player);

    bool HasPlayer(PlayerId player) const;

    //Advances every present player by one fixed step, then increments the
    //tick. Commands naming absent players are ignored; see the note in
    //MatchState.cpp for why that is not an error.
    void Step(std::span<const PlayerCommand> commands, float seconds);

    //How many steps this match has taken. The authoritative clock a server
    //and a client agree on; it lives here rather than on FrameClock because
    //it is simulation state, not wall-clock state.
    std::uint64_t Tick() const { return m_Tick; }

    //Aligns this match's clock to somebody else's - a client adopting the
    //server's tick from a snapshot.
    void SetTick(std::uint64_t tick) { m_Tick = tick; }

    //Throws when the player is not present: there is no honest character to
    //return, and a default-constructed one reads as somebody standing at the
    //origin.
    const CharacterController& Player(PlayerId player) const;

    //Everyone in the match, in id order.
    //
    //Returns the ordered container itself rather than a copied list of ids.
    //MatchState's reproducibility depends on iteration order, and handing back
    //the ordered map makes that promise visible to callers instead of hiding
    //it behind a copy.
    const std::map<PlayerId, CharacterController>& Players() const { return m_Players; }

    //Moves a player without interpolating through the space in between.
    void TeleportPlayer(PlayerId player, const glm::vec3& position);

    //Non-const access to a player, for callers that own game rules the match
    //itself does not - respawning, and clearing fall speed with it.
    CharacterController& PlayerForWrite(PlayerId player);

    //Swaps the terrain, keeping every player and the tick. What reloading a
    //map mid-session does.
    void ReplaceWorld(World world);

    World& GetWorld() { return m_World; }
    const World& GetWorld() const { return m_World; }

private:
    World m_World;

    //Ordered rather than hashed, and that is load-bearing: iteration order is
    //part of what makes a step reproducible, and an unordered container makes
    //no promise about it across builds or insertion histories.
    std::map<PlayerId, CharacterController> m_Players;

    PlayerId m_NextPlayer = 1;
    std::uint64_t m_Tick = 0;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
