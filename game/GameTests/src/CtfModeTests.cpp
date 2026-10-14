#include <doctest.h>

#include "CtfMode.h"
#include "FakeHost.h"

#include "Cubit/Voxel/CharacterController.h"
#include "Cubit/Voxel/TerrainGen.h"

using namespace CubitGame;

namespace
{
    World Floor()
    {
        World world(4, 1, 2);   //64 x 16 x 32
        for (int z = 0; z < world.GetDepth(); ++z)
            for (int x = 0; x < world.GetWidth(); ++x)
                world.SetBlock(x, 0, z, BlockId{ 1 });
        return world;
    }

    const glm::ivec3 Size{ 64, 16, 32 };

    glm::vec2 Base(Team team)
    {
        const auto centres = TerrainGen::FortCentres(Size);
        return glm::vec2(centres[IndexOf(team)]) + glm::vec2(0.5f);
    }

    float DistanceFromBase(const FakeHost& host, PlayerId player, Team team)
    {
        const glm::vec3 p = host.Match().Player(player).Position();
        return glm::distance(glm::vec2(p.x, p.z), Base(team));
    }

    Team TeamOf(const CtfMode& mode, PlayerId player)
    {
        return mode.State().Find(player)->Side;
    }
}

TEST_CASE("Joiners are balanced onto the smaller team and spawn at their own base")
{
    CtfMode mode(CtfRules{}, Size, 1);
    FakeHost host(Floor());

    const PlayerId a = host.Join(mode);
    const PlayerId b = host.Join(mode);
    const PlayerId c = host.Join(mode);

    CHECK(TeamOf(mode, a) != TeamOf(mode, b));
    CHECK(mode.State().TeamSize(Team::Blue) + mode.State().TeamSize(Team::Green) == 3);
    CHECK(std::abs(mode.State().TeamSize(Team::Blue) - mode.State().TeamSize(Team::Green)) == 1);

    for (PlayerId p : { a, b, c })
    {
        CHECK(host.IsAlive(p));
        CHECK(DistanceFromBase(host, p, TeamOf(mode, p)) < 8.0f);
    }
}

TEST_CASE("The same seed balances the same way")
{
    CtfMode one(CtfRules{}, Size, 7), two(CtfRules{}, Size, 7);
    FakeHost hostOne(Floor()), hostTwo(Floor());
    const PlayerId a = hostOne.Join(one);
    const PlayerId b = hostTwo.Join(two);
    CHECK(TeamOf(one, a) == TeamOf(two, b));
}

TEST_CASE("A switch is granted only when it keeps the teams within one")
{
    CtfMode mode(CtfRules{}, Size, 1);
    FakeHost host(Floor());
    const PlayerId a = host.Join(mode);
    const PlayerId b = host.Join(mode);
    const Team before = TeamOf(mode, a);

    //1 v 1: switching would make it 2 v 0. Refused.
    const std::uint8_t command[] = { SwitchTeamCommand };
    mode.OnCommand(host, a, command);
    CHECK(TeamOf(mode, a) == before);
    CHECK(host.IsAlive(a));

    //2 v 1 after a third joins the smaller side... make the joiner's team the
    //bigger one and switch them back.
    const PlayerId c = host.Join(mode);
    const Team big = TeamOf(mode, c);
    PlayerId mover = TeamOf(mode, a) == big ? a : (TeamOf(mode, b) == big ? b : c);
    mode.OnCommand(host, mover, command);
    CHECK(TeamOf(mode, mover) == Other(big));

    //Switching kills without a death, and respawns at the new base later.
    CHECK_FALSE(host.IsAlive(mover));
    CHECK(mode.State().Find(mover)->Deaths == 0);
    host.Run(mode, CtfRules{}.RespawnDelayTicks + 1);
    CHECK(host.IsAlive(mover));
    CHECK(DistanceFromBase(host, mover, Other(big)) < 8.0f);
}

TEST_CASE("A killed player respawns after the delay, at their own base")
{
    CtfMode mode(CtfRules{}, Size, 1);
    FakeHost host(Floor());
    const PlayerId a = host.Join(mode);
    const PlayerId b = host.Join(mode);

    host.RuleKill(mode, a, b);
    CHECK(mode.State().Find(a)->RespawnTick == host.Now + CtfRules{}.RespawnDelayTicks);

    host.Run(mode, CtfRules{}.RespawnDelayTicks - 1);
    CHECK_FALSE(host.IsAlive(a));
    host.Run(mode, 1);
    CHECK(host.IsAlive(a));
    CHECK(mode.State().Find(a)->RespawnTick == 0);
    CHECK(DistanceFromBase(host, a, TeamOf(mode, a)) < 8.0f);
}

TEST_CASE("A spawn still works after the base has been dug out")
{
    CtfMode mode(CtfRules{}, Size, 1);
    FakeHost host(Floor());

    //Dig a pit at both bases.
    for (const glm::ivec2 centre : TerrainGen::FortCentres(Size))
        for (int dz = -3; dz <= 3; ++dz)
            for (int dx = -3; dx <= 3; ++dx)
                host.Terrain().SetBlock(centre.x + dx, 0, centre.y + dz, BlockId{ 0 });

    const PlayerId a = host.Join(mode);
    CHECK(host.IsAlive(a));
    CHECK(host.Match().Player(a).Position().y > 0.0f);
}

TEST_CASE("No friendly fire, and no damage during spawn protection")
{
    CtfMode mode(CtfRules{}, Size, 1);
    FakeHost host(Floor());
    const PlayerId a = host.Join(mode);
    const PlayerId b = host.Join(mode);
    const PlayerId c = host.Join(mode);
    const PlayerId mate = TeamOf(mode, c) == TeamOf(mode, a) ? a : b;

    CHECK_FALSE(mode.CanDamage(host, c, mate));

    const PlayerId enemy = TeamOf(mode, c) == TeamOf(mode, a) ? b : a;
    CHECK_FALSE(mode.CanDamage(host, c, enemy));   //just spawned
    host.Run(mode, CtfRules{}.SpawnProtectionTicks);
    CHECK(mode.CanDamage(host, c, enemy));
}

TEST_CASE("Leaving removes the player from the roster")
{
    CtfMode mode(CtfRules{}, Size, 1);
    FakeHost host(Floor());
    const PlayerId a = host.Join(mode);
    host.Leave(mode, a);
    CHECK(mode.State().Find(a) == nullptr);
}

TEST_CASE("Every change is reported once")
{
    CtfMode mode(CtfRules{}, Size, 1);
    FakeHost host(Floor());
    host.Join(mode);
    CHECK(mode.TakeStateChanged());
    CHECK_FALSE(mode.TakeStateChanged());
}

namespace
{
    CtfRules Quick()
    {
        CtfRules rules;
        rules.CountdownTicks = 10;
        rules.TimeLimitTicks = 1000;
        rules.EndScreenTicks = 20;
        return rules;
    }

    //Two players, one each side, run through the countdown into the match.
    struct Match
    {
        CtfMode Mode;
        FakeHost Host;
        PlayerId Blue = InvalidPlayer, Green = InvalidPlayer;

        explicit Match(const CtfRules& rules = Quick()) : Mode(rules, Size, 1), Host(Floor())
        {
            const PlayerId a = Host.Join(Mode);
            const PlayerId b = Host.Join(Mode);
            Blue = TeamOf(Mode, a) == Team::Blue ? a : b;
            Green = Blue == a ? b : a;
        }

        void Begin() { Host.Run(Mode, Quick().CountdownTicks + 2); }
    };
}

TEST_CASE("Warmup waits for both teams, then counts down into the match")
{
    CtfMode mode(Quick(), Size, 1);
    FakeHost host(Floor());
    host.Join(mode);
    host.Run(mode, 100);
    CHECK(mode.State().Stage == Phase::Warmup);

    host.Join(mode);
    host.Run(mode, 1);
    CHECK(mode.State().Stage == Phase::Countdown);
    host.Run(mode, Quick().CountdownTicks + 1);
    CHECK(mode.State().Stage == Phase::Active);
    CHECK(mode.State().Events.back().Kind == EventKind::MatchStarted);
}

TEST_CASE("The countdown starts over if a team empties")
{
    CtfMode mode(Quick(), Size, 1);
    FakeHost host(Floor());
    host.Join(mode);
    const PlayerId b = host.Join(mode);
    host.Run(mode, 3);
    host.Leave(mode, b);
    host.Run(mode, 1);
    CHECK(mode.State().Stage == Phase::Warmup);
}

TEST_CASE("Kills count in the match and not in warmup")
{
    Match m;
    m.Host.RuleKill(m.Mode, m.Green, m.Blue);
    CHECK(m.Mode.State().Find(m.Blue)->Kills == 0);

    m.Host.Run(m.Mode, 100);
    m.Begin();
    REQUIRE(m.Mode.State().Stage == Phase::Active);
    m.Host.RuleKill(m.Mode, m.Green, m.Blue);
    CHECK(m.Mode.State().Find(m.Blue)->Kills == 1);
    CHECK(m.Mode.State().Find(m.Green)->Deaths == 1);
}

TEST_CASE("The time limit ends the match on captures, a draw when level")
{
    Match m;
    m.Begin();
    m.Host.Run(m.Mode, Quick().TimeLimitTicks + 1);
    CHECK(m.Mode.State().Stage == Phase::Ended);
    CHECK(m.Mode.State().Winner == Team::None);
    CHECK(m.Mode.State().Events.back().Kind == EventKind::Draw);
}

TEST_CASE("After the end screen the world, scores and intels reset and warmup begins")
{
    Match m;
    m.Begin();
    m.Host.RuleKill(m.Mode, m.Green, m.Blue);
    m.Host.Run(m.Mode, Quick().TimeLimitTicks + 1);
    REQUIRE(m.Mode.State().Stage == Phase::Ended);

    m.Host.Run(m.Mode, Quick().EndScreenTicks + 1);
    CHECK(m.Host.Resets == 1);
    CHECK(m.Mode.State().Find(m.Blue)->Kills == 0);
    CHECK(m.Mode.State().Captures == std::array<std::uint8_t, 2>{ 0, 0 });
    CHECK(m.Host.IsAlive(m.Blue));
    CHECK(m.Host.IsAlive(m.Green));
    //Both teams are still here, so warmup goes straight to its countdown.
    CHECK((m.Mode.State().Stage == Phase::Warmup || m.Mode.State().Stage == Phase::Countdown));
}

TEST_CASE("The intel's home is on top of each fort")
{
    Match m;
    m.Begin();
    for (int i = 0; i < 2; ++i)
    {
        const Intel& intel = m.Mode.State().Intels[i];
        CHECK(intel.Status == IntelStatus::Home);
        CHECK(intel.HomeSpot.y == doctest::Approx(1.0f));   //on the floor at y 0
        CHECK(intel.Position == intel.HomeSpot);
    }
}

namespace
{
    //Stand a player's box so its feet are on the spot.
    void StandAt(FakeHost& host, PlayerId player, const glm::vec3& spot)
    {
        host.Place(player, spot + glm::vec3(0.0f, CharacterConfig{}.HalfExtents.y, 0.0f));
    }

    const Intel& IntelOf(const CtfMode& mode, Team owner) { return mode.State().Intels[IndexOf(owner)]; }
}

TEST_CASE("An enemy takes the intel by touching it, but not in warmup")
{
    Match m;
    StandAt(m.Host, m.Blue, IntelOf(m.Mode, Team::Green).HomeSpot);
    m.Host.Run(m.Mode, 1);
    CHECK(IntelOf(m.Mode, Team::Green).Status == IntelStatus::Home);

    m.Begin();
    StandAt(m.Host, m.Blue, IntelOf(m.Mode, Team::Green).HomeSpot);
    m.Host.Run(m.Mode, 1);
    CHECK(IntelOf(m.Mode, Team::Green).Status == IntelStatus::Carried);
    CHECK(IntelOf(m.Mode, Team::Green).Carrier == m.Blue);
    CHECK(m.Mode.State().Events.back().Kind == EventKind::PickedUp);
}

TEST_CASE("A player cannot take their own intel")
{
    Match m;
    m.Begin();
    StandAt(m.Host, m.Blue, IntelOf(m.Mode, Team::Blue).HomeSpot);
    m.Host.Run(m.Mode, 1);
    CHECK(IntelOf(m.Mode, Team::Blue).Status == IntelStatus::Home);
}

TEST_CASE("Carrying it home scores a capture and sends it back")
{
    Match m;
    m.Begin();
    StandAt(m.Host, m.Blue, IntelOf(m.Mode, Team::Green).HomeSpot);
    m.Host.Run(m.Mode, 1);

    StandAt(m.Host, m.Blue, IntelOf(m.Mode, Team::Blue).HomeSpot + glm::vec3(2.0f, 0.0f, 0.0f));
    m.Host.Run(m.Mode, 1);

    CHECK(m.Mode.State().Captures[IndexOf(Team::Blue)] == 1);
    CHECK(m.Mode.State().Find(m.Blue)->Captures == 1);
    CHECK(IntelOf(m.Mode, Team::Green).Status == IntelStatus::Home);
    CHECK(m.Mode.State().Events.back().Kind == EventKind::Captured);
}

TEST_CASE("Three captures win")
{
    Match m;
    m.Begin();
    for (int i = 0; i < 3; ++i)
    {
        StandAt(m.Host, m.Blue, IntelOf(m.Mode, Team::Green).HomeSpot);
        m.Host.Run(m.Mode, 1);
        StandAt(m.Host, m.Blue, IntelOf(m.Mode, Team::Blue).HomeSpot);
        m.Host.Run(m.Mode, 1);
    }
    CHECK(m.Mode.State().Stage == Phase::Ended);
    CHECK(m.Mode.State().Winner == Team::Blue);
    CHECK(m.Mode.State().Events.back().Kind == EventKind::MatchWon);
}

TEST_CASE("The carrier's death drops it where they fell; the owner's touch returns it")
{
    Match m;
    m.Begin();
    StandAt(m.Host, m.Blue, IntelOf(m.Mode, Team::Green).HomeSpot);
    m.Host.Run(m.Mode, 1);

    const glm::vec3 fell(30.5f, 1.0f, 16.5f);
    StandAt(m.Host, m.Blue, fell);
    m.Host.Run(m.Mode, 1);
    m.Host.RuleKill(m.Mode, m.Blue, m.Green);

    CHECK(IntelOf(m.Mode, Team::Green).Status == IntelStatus::Dropped);
    CHECK(glm::distance(IntelOf(m.Mode, Team::Green).Position, fell) < 0.01f);

    StandAt(m.Host, m.Green, fell);
    m.Host.Run(m.Mode, 1);
    CHECK(IntelOf(m.Mode, Team::Green).Status == IntelStatus::Home);
    CHECK(m.Mode.State().Events.back().Kind == EventKind::Returned);
}

TEST_CASE("A dropped intel an enemy touches is taken again")
{
    Match m;
    m.Begin();
    StandAt(m.Host, m.Blue, IntelOf(m.Mode, Team::Green).HomeSpot);
    m.Host.Run(m.Mode, 1);
    const glm::vec3 fell(30.5f, 1.0f, 16.5f);
    StandAt(m.Host, m.Blue, fell);
    m.Host.Run(m.Mode, 1);
    m.Host.RuleKill(m.Mode, m.Blue, m.Green);

    //Blue respawns and walks back to it.
    m.Host.Run(m.Mode, Quick().RespawnDelayTicks + 1);
    StandAt(m.Host, m.Blue, fell);
    m.Host.Run(m.Mode, 1);
    CHECK(IntelOf(m.Mode, Team::Green).Status == IntelStatus::Carried);
}

TEST_CASE("An untouched dropped intel goes home after the timeout")
{
    CtfRules rules = Quick();
    rules.DroppedReturnTicks = 100;
    Match m(rules);
    m.Begin();
    StandAt(m.Host, m.Blue, IntelOf(m.Mode, Team::Green).HomeSpot);
    m.Host.Run(m.Mode, 1);
    StandAt(m.Host, m.Blue, glm::vec3(30.5f, 1.0f, 16.5f));
    m.Host.Run(m.Mode, 1);
    m.Host.RuleKill(m.Mode, m.Blue, m.Green);
    StandAt(m.Host, m.Green, glm::vec3(50.5f, 1.0f, 2.5f));   //away from it

    m.Host.Run(m.Mode, rules.DroppedReturnTicks - 1);
    CHECK(IntelOf(m.Mode, Team::Green).Status == IntelStatus::Dropped);
    m.Host.Run(m.Mode, 2);
    CHECK(IntelOf(m.Mode, Team::Green).Status == IntelStatus::Home);
}

TEST_CASE("Leaving or switching while carrying drops it")
{
    Match m;
    m.Begin();
    StandAt(m.Host, m.Blue, IntelOf(m.Mode, Team::Green).HomeSpot);
    m.Host.Run(m.Mode, 1);
    m.Host.Leave(m.Mode, m.Blue);
    CHECK(IntelOf(m.Mode, Team::Green).Status == IntelStatus::Dropped);
}

TEST_CASE("Switching teams while carrying drops it")
{
    Match m;
    const PlayerId c = m.Host.Join(m.Mode);
    m.Begin();

    //The third player makes their side the bigger one, so their switch is granted.
    const Team big = TeamOf(m.Mode, c);
    const Team enemy = Other(big);
    StandAt(m.Host, c, IntelOf(m.Mode, enemy).HomeSpot);
    m.Host.Run(m.Mode, 1);
    REQUIRE(IntelOf(m.Mode, enemy).Status == IntelStatus::Carried);

    const std::uint8_t command[] = { SwitchTeamCommand };
    m.Mode.OnCommand(m.Host, c, command);
    CHECK(TeamOf(m.Mode, c) == enemy);
    CHECK(IntelOf(m.Mode, enemy).Status == IntelStatus::Dropped);
}

TEST_CASE("A drop below the world goes straight home")
{
    Match m;
    m.Begin();
    StandAt(m.Host, m.Blue, IntelOf(m.Mode, Team::Green).HomeSpot);
    m.Host.Run(m.Mode, 1);
    m.Host.Place(m.Blue, glm::vec3(30.5f, -20.0f, 16.5f));
    m.Host.Run(m.Mode, 1);
    m.Host.RuleKill(m.Mode, m.Blue, m.Green);
    CHECK(IntelOf(m.Mode, Team::Green).Status == IntelStatus::Home);
}

TEST_CASE("The dead cannot take or return an intel")
{
    Match m;
    m.Begin();
    m.Host.Kill(m.Blue);
    StandAt(m.Host, m.Blue, IntelOf(m.Mode, Team::Green).HomeSpot);
    m.Host.Run(m.Mode, 1);
    CHECK(IntelOf(m.Mode, Team::Green).Status == IntelStatus::Home);
}
