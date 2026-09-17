#pragma once

#include "Cubit/MatchRules.h"

//Cubit's own game: the numbers it is played by.
namespace CubitGame
{
    //The rules this game hands the engine, on both ends of a match.
    //
    //Every number that decides how the game feels is here, and the engine holds
    //none of them - see MatchRules. Change one and only the game is rebuilt.
    MatchRules Rules();
}
