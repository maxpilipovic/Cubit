#pragma once

#include "Cubit/Core.h"
#include "Cubit/Voxel/MatchState.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <vector>

//What a game mode may ask of the server. MatchServer implements it; a game's
//tests implement it with no network at all.
class GameModeHost
{
public:
    virtual ~GameModeHost() = default;

    virtual std::uint64_t Tick() const = 0;

    //Positions, the world, the roster.
    virtual const MatchState& Match() const = 0;

    virtual bool IsAlive(PlayerId player) const = 0;

    //Teleports, clears vertical velocity, restores starting health, and makes
    //the player alive. Also how a joiner first appears.
    virtual void Respawn(PlayerId player, const glm::vec3& position) = 0;

    //Dead, with no kill ruled and OnKilled not called: a team switch.
    virtual void Kill(PlayerId player) = 0;

    //Every edited cell back to the map's block, broadcast as one batch.
    virtual void ResetWorld() = 0;
};

//A game's rules, called by the server at fixed moments. The engine knows none
//of what a mode means: its state crosses the wire as bytes the engine carries
//without reading, and its commands arrive the same way.
class GameMode
{
public:
    virtual ~GameMode() = default;

    //The player exists and is DEAD. The mode decides when and where they appear.
    virtual void OnJoin(GameModeHost& host, PlayerId player) = 0;

    //Called before the player is removed.
    virtual void OnLeave(GameModeHost& host, PlayerId player) = 0;

    //Asked before any damage is applied. False means no damage and no kill.
    virtual bool CanDamage(const GameModeHost& host, PlayerId shooter, PlayerId victim) const = 0;

    //The rules killed `victim`: they are already dead and out of the rewind history.
    virtual void OnKilled(GameModeHost& host, PlayerId victim, PlayerId killer) = 0;

    //Bytes a client sent, at most MaxGameCommandBytes. From any joined player,
    //dead or alive: the mode decides what it accepts.
    virtual void OnCommand(GameModeHost& host, PlayerId player,
        std::span<const std::uint8_t> bytes) = 0;

    //Once per tick, after every player has stepped and before the snapshot.
    virtual void Step(GameModeHost& host) = 0;

    //True once after the state changed, so the server sends it to everybody.
    virtual bool TakeStateChanged() = 0;

    virtual std::vector<std::uint8_t> EncodeState() const = 0;
};
