#include <doctest.h>

#include "Cubit/Voxel/ResolveShot.h"
#include "Cubit/Voxel/World.h"

#include <glm/glm.hpp>
#include <vector>

namespace
{
    //An empty world, so terrain never interferes unless a test puts a block in.
    World EmptyWorld()
    {
        return World(2, 2, 2);
    }

    Aabb BoxAt(float x, float y, float z)
    {
        const glm::vec3 centre(x, y, z);
        const glm::vec3 half(0.3f, 0.9f, 0.3f);
        return Aabb{ centre - half, centre + half };
    }
}

TEST_CASE("A ray through a box reports that player and where it entered")
{
    World world = EmptyWorld();
    const std::vector<ShotCandidate> candidates{ ShotCandidate{ 2, BoxAt(10.0f, 1.0f, 0.0f) } };

    const ShotResult result = ResolveShot(
        world, candidates, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 128.0f);

    CHECK(result.Victim == 2);
    CHECK(result.Distance == doctest::Approx(9.7f));
    CHECK(result.Impact.x == doctest::Approx(9.7f));
}

TEST_CASE("A ray that misses every box reports no victim and the end of its range")
{
    World world = EmptyWorld();
    const std::vector<ShotCandidate> candidates{ ShotCandidate{ 2, BoxAt(10.0f, 1.0f, 5.0f) } };

    const ShotResult result = ResolveShot(
        world, candidates, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 128.0f);

    CHECK(result.Victim == InvalidPlayer);
    CHECK(result.Distance == doctest::Approx(128.0f));
    CHECK(result.Impact.x == doctest::Approx(128.0f));
}

TEST_CASE("The nearest of two boxes is the one that is hit")
{
    World world = EmptyWorld();
    const std::vector<ShotCandidate> candidates{
        ShotCandidate{ 2, BoxAt(20.0f, 1.0f, 0.0f) },
        ShotCandidate{ 3, BoxAt(10.0f, 1.0f, 0.0f) }
    };

    const ShotResult result = ResolveShot(
        world, candidates, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 128.0f);

    CHECK(result.Victim == 3);
}

TEST_CASE("Terrain in front of a player stops the shot")
{
    //Nobody may be shot through a wall. Ties go to terrain for the same reason.
    World world = EmptyWorld();
    world.SetBlock(5, 1, 0, BlockId{ 1 });

    const std::vector<ShotCandidate> candidates{ ShotCandidate{ 2, BoxAt(10.0f, 1.0f, 0.0f) } };

    const ShotResult result = ResolveShot(
        world, candidates, glm::vec3(0.0f, 1.5f, 0.5f), glm::vec3(1.0f, 0.0f, 0.0f), 128.0f);

    CHECK(result.Victim == InvalidPlayer);
    CHECK(result.Distance == doctest::Approx(5.0f));
}

TEST_CASE("Terrain behind a player does not stop the shot")
{
    World world = EmptyWorld();
    world.SetBlock(15, 1, 0, BlockId{ 1 });

    const std::vector<ShotCandidate> candidates{ ShotCandidate{ 2, BoxAt(10.0f, 1.5f, 0.5f) } };

    const ShotResult result = ResolveShot(
        world, candidates, glm::vec3(0.0f, 1.5f, 0.5f), glm::vec3(1.0f, 0.0f, 0.0f), 128.0f);

    CHECK(result.Victim == 2);
}

TEST_CASE("A box beyond the range is not hit")
{
    World world = EmptyWorld();
    const std::vector<ShotCandidate> candidates{ ShotCandidate{ 2, BoxAt(10.0f, 1.0f, 0.0f) } };

    const ShotResult result = ResolveShot(
        world, candidates, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 5.0f);

    CHECK(result.Victim == InvalidPlayer);
    CHECK(result.Distance == doctest::Approx(5.0f));
}

TEST_CASE("A tie between terrain and a box goes to terrain")
{
    //The box's near face sits exactly on the block's boundary, so the ray
    //reaches both at the same distance. Terrain must win the tie: a player
    //standing flush against a wall is not shot through it.
    World world = EmptyWorld();
    world.SetBlock(10, 1, 0, BlockId{ 1 });

    const Aabb box{ glm::vec3(10.0f, 0.5f, 0.0f), glm::vec3(10.6f, 1.5f, 1.0f) };
    const std::vector<ShotCandidate> candidates{ ShotCandidate{ 2, box } };

    const ShotResult result = ResolveShot(
        world, candidates, glm::vec3(0.0f, 1.0f, 0.5f), glm::vec3(1.0f, 0.0f, 0.0f), 128.0f);

    CHECK(result.Victim == InvalidPlayer);
    CHECK(result.Distance == doctest::Approx(10.0f));
}

TEST_CASE("A box behind the shooter is not hit")
{
    World world = EmptyWorld();
    const std::vector<ShotCandidate> candidates{ ShotCandidate{ 2, BoxAt(-10.0f, 1.0f, 0.0f) } };

    const ShotResult result = ResolveShot(
        world, candidates, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 128.0f);

    CHECK(result.Victim == InvalidPlayer);
}
