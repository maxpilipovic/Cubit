#include "GameRules.h"

MatchRules CubitGame::Rules()
{
    MatchRules rules;

    //Twelve blocks, about four paces: far enough to dig a trench from standing,
    //short enough that a wall is cover rather than a suggestion.
    rules.ReachDistance = 12.0f;

    //128 blocks of a 512-wide map, so a rifle crosses a quarter of it.
    rules.ShotRange = 128.0f;

    //Three shots kill, the third overshooting by two. Two would make a duel a
    //coin toss; four drags every fight out.
    rules.StartingHealth = 100;
    rules.ShotDamage = 34;

    //Six shots a second at the fixed 60 Hz step.
    rules.TicksBetweenShots = 10;

    return rules;
}
