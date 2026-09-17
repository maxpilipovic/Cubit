#include <doctest.h>

#include "GameRules.h"

TEST_CASE("The game states its rules rather than taking the engine's placeholders")
{
    const MatchRules rules = CubitGame::Rules();

    //The numbers the game has shipped with since shooting landed. They live in
    //the game now, so this is the file that changes when one is tuned - and the
    //engine is not rebuilt for it.
    CHECK(rules.StartingHealth == 100);
    CHECK(rules.ShotDamage == 34);
    CHECK(rules.ShotRange == doctest::Approx(128.0f));
    CHECK(rules.ReachDistance == doctest::Approx(12.0f));
    CHECK(rules.TicksBetweenShots == 10);
}

TEST_CASE("Three shots kill, and the third overshoots rather than the second")
{
    //What the numbers above are for. Two shots to kill makes a duel a coin
    //toss; four drags every fight out.
    const MatchRules rules = CubitGame::Rules();

    CHECK(rules.ShotDamage * 2 < rules.StartingHealth);
    CHECK(rules.ShotDamage * 3 > rules.StartingHealth);
}
