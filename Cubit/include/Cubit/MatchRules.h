#pragma once

#include <cstdint>

//The numbers a match is played by, handed to the engine rather than held by it.
//
//A game's feel lives in these five values: how hard a shot hits, how often one
//can be fired, how far it carries, how much a player can take, and how far they
//can reach to change the world. None of that is the engine's business - it
//implements shooting and digging, not how deadly or how generous they are - and
//keeping them here means a game can be tuned without the engine being rebuilt.
//
//The values below are placeholders, not the game's answer: they exist so that
//nothing ever reads an uninitialised rule, and so the engine's own tests can
//construct a match without stating rules they are not testing. A game states
//its own, explicitly; Cubit's does in `GameRules.h`.
struct MatchRules
{
    //How far a player can reach to edit a block, in blocks, measured from the
    //eye to the nearest point of the cell.
    //
    //One value for the aim ray, the client's prediction and the server's ruling.
    //Two copies would drift, and a client whose reach is a hair longer than the
    //server's predicts edits the server refuses - a correction on every click at
    //the edge of reach.
    float ReachDistance = 12.0f;

    //How far a shot carries, in blocks.
    float ShotRange = 128.0f;

    //Health a player starts and respawns with.
    std::uint8_t StartingHealth = 100;

    //Damage one shot does. With the values here three shots kill, the third
    //overshooting by two - health is clamped at zero rather than wrapping, which
    //an unsigned type makes worth stating.
    std::uint8_t ShotDamage = 34;

    //Fewest ticks between one client's shots. Ten is six shots a second.
    //
    //It is the weapon's rate of fire and, at the same time, the flood answer for
    //a reliable client-to-server message: a client that spams Fire has its
    //extras dropped rather than queued.
    int TicksBetweenShots = 10;
};
