#include <doctest.h>

#include "CtfMode.h"
#include "FakeHost.h"

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
