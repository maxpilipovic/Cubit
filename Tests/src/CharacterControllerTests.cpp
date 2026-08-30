#include <doctest.h>

#include "Cubit/Voxel/CharacterController.h"
#include "Cubit/Voxel/World.h"

#include <glm/glm.hpp>

namespace
{
    constexpr float Step = 1.0f / 60.0f;

    //Whatever index the test worlds below declare as water: present, but not
    //solid, so a box passes through it and OverlapsFluid reports it.
    constexpr BlockId Water{ 7 };

    //A world with a solid floor at y=0 and air above it.
    World FlatWorld(int chunksY = 2)
    {
        World world(2, chunksY, 2);

        //Fluid is derived from palette alpha, and every entry of the default
        //palette is fully opaque - so until a palette says otherwise, no block
        //in a freshly built world is fluid at all. The Sandbox gets its water
        //from the map file's palette; a test has to declare its own.
        Palette palette = DefaultPalette();
        palette[Water] = glm::vec4(0.22f, 0.43f, 0.78f, 0.55f);
        world.SetPalette(palette);

        for (int z = 0; z < world.GetDepth(); ++z)
            for (int x = 0; x < world.GetWidth(); ++x)
                world.SetBlock(x, 0, z, BlockId{ 1 });

        return world;
    }

    //Runs the character until it stops moving vertically, or gives up. Returns
    //the number of steps taken, so a test can tell "landed" from "still going".
    int SettleOnGround(CharacterController& character, const World& world,
        int maxSteps = 600)
    {
        for (int i = 0; i < maxSteps; ++i)
        {
            character.Step(world, CharacterInput{}, Step);
            if (character.Grounded())
                return i + 1;
        }

        return maxSteps;
    }

    //Movement is now in the character's frame, so a test says which way it is
    //facing. Yaw 0 faces +x with right toward +z (see HeadingTests), which is
    //what the position assertions below are written against.
    CharacterInput Walking(const glm::vec2& move, float yaw = 0.0f)
    {
        CharacterInput input;
        input.Move = move;
        input.Yaw = yaw;
        return input;
    }

    CharacterInput Jumping()
    {
        CharacterInput input;
        input.Jump = true;
        return input;
    }
}

TEST_CASE("Water is a fluid the character can be inside")
{
    //The premise every water test below rests on. If the default palette ever
    //stops making index 7 a non-solid fluid, those tests would quietly pass by
    //testing dry physics, so the premise is checked once here out loud.
    World world = FlatWorld();
    world.SetBlock(16, 4, 16, Water);

    CHECK(world.IsBlockFluid(16, 4, 16));
    CHECK_FALSE(world.IsBlockSolid(16, 4, 16));
}

TEST_CASE("A character falls under gravity until it lands")
{
    World world = FlatWorld();
    CharacterController character;
    character.Teleport(glm::vec3(16.0f, 20.0f, 16.0f));

    CHECK_FALSE(character.Grounded());

    const int steps = SettleOnGround(character, world);

    CHECK(steps < 600);
    CHECK(character.Grounded());

    //Standing on top of the block at y=0, so the box centre sits its own half
    //height above y=1.
    CHECK(character.Position().y
        == doctest::Approx(1.0f + character.Config().HalfExtents.y).epsilon(0.02));
}

TEST_CASE("Landing clears the accumulated fall speed")
{
    //Otherwise the next step starts with several seconds of built-up downward
    //velocity and the character sinks through the floor on the first move.
    World world = FlatWorld();
    CharacterController character;
    character.Teleport(glm::vec3(16.0f, 20.0f, 16.0f));

    SettleOnGround(character, world);

    CHECK(character.VerticalVelocity() == doctest::Approx(0.0f));
}

TEST_CASE("A grounded character jumps and comes back down")
{
    World world = FlatWorld();
    CharacterController character;
    character.Teleport(glm::vec3(16.0f, 20.0f, 16.0f));
    SettleOnGround(character, world);

    const float ground = character.Position().y;

    character.Step(world, Jumping(), Step);

    CHECK(character.VerticalVelocity() > 0.0f);
    CHECK(character.Position().y > ground);
    CHECK_FALSE(character.Grounded());

    //Released, so nothing sustains it.
    SettleOnGround(character, world);
    CHECK(character.Position().y == doctest::Approx(ground).epsilon(0.02));
}

TEST_CASE("A character in the air cannot jump again")
{
    //Holding jump must not fly. The rule is "grounded", not "asked".
    World world = FlatWorld();
    CharacterController character;
    character.Teleport(glm::vec3(16.0f, 20.0f, 16.0f));
    SettleOnGround(character, world);

    character.Step(world, Jumping(), Step);
    const float afterFirst = character.VerticalVelocity();

    //Still holding it on the way up.
    character.Step(world, Jumping(), Step);

    CHECK(character.VerticalVelocity() < afterFirst);
}

TEST_CASE("A walking character is stopped by a wall")
{
    World world = FlatWorld();

    //A wall across the whole x=20 plane, tall enough not to be stepped over.
    for (int z = 0; z < world.GetDepth(); ++z)
        for (int y = 1; y <= 4; ++y)
            world.SetBlock(20, y, z, BlockId{ 1 });

    CharacterController character;
    character.Teleport(glm::vec3(16.0f, 20.0f, 16.0f));
    SettleOnGround(character, world);

    for (int i = 0; i < 120; ++i)
        character.Step(world, Walking(glm::vec2(0.0f, 1.0f)), Step);

    //Walked into it, not through it: stopped just short of the block face.
    CHECK(character.Position().x < 20.0f);
    CHECK(character.Position().x > 18.0f);
}

TEST_CASE("A character blocked on one axis still slides along the other")
{
    //Axis-separated resolution. Pressing diagonally into a wall should move
    //the character along it rather than stopping dead.
    World world = FlatWorld();

    for (int z = 0; z < world.GetDepth(); ++z)
        for (int y = 1; y <= 4; ++y)
            world.SetBlock(20, y, z, BlockId{ 1 });

    CharacterController character;
    character.Teleport(glm::vec3(16.0f, 20.0f, 16.0f));
    SettleOnGround(character, world);

    const float startZ = character.Position().z;

    for (int i = 0; i < 120; ++i)
    {
        // normalize(1,1) at yaw 0 is strafe +z and forward +x, the same
        // diagonal into the wall the world-space version described. The
        // normalize is redundant now that Step caps the resolved vector,
        // but it stays to keep the test's intent explicit.
        character.Step(world,
            Walking(glm::normalize(glm::vec2(1.0f, 1.0f))), Step);
    }

    CHECK(character.Position().x < 20.0f);
    CHECK(character.Position().z > startZ + 1.0f);
}

TEST_CASE("Hitting a ceiling ends upward motion")
{
    World world = FlatWorld();

    //A lid two blocks above the floor, low enough that a jump reaches it.
    for (int z = 0; z < world.GetDepth(); ++z)
        for (int x = 0; x < world.GetWidth(); ++x)
            world.SetBlock(x, 4, z, BlockId{ 1 });

    CharacterController character;
    character.Teleport(glm::vec3(16.0f, 2.0f, 16.0f));
    SettleOnGround(character, world);

    character.Step(world, Jumping(), Step);
    CHECK(character.VerticalVelocity() > 0.0f);

    for (int i = 0; i < 30; ++i)
    {
        character.Step(world, CharacterInput{}, Step);
        if (character.VerticalVelocity() <= 0.0f)
            break;
    }

    CHECK(character.VerticalVelocity() <= 0.0f);
    CHECK(character.Position().y < 4.0f);
}

TEST_CASE("A submerged character sinks slowly rather than falling")
{
    //Weakened gravity and a capped sink speed, so water is not just air.
    World world = FlatWorld(4);

    for (int z = 0; z < world.GetDepth(); ++z)
        for (int y = 1; y <= 40; ++y)
            for (int x = 0; x < world.GetWidth(); ++x)
                world.SetBlock(x, y, z, Water);

    CharacterController wet;
    wet.Teleport(glm::vec3(16.0f, 30.0f, 16.0f));

    CharacterController dry;
    dry.Teleport(glm::vec3(16.0f, 30.0f, 16.0f));

    World air = FlatWorld(4);

    for (int i = 0; i < 60; ++i)
    {
        wet.Step(world, CharacterInput{}, Step);
        dry.Step(air, CharacterInput{}, Step);
    }

    CHECK(wet.BodyInFluid());
    CHECK(wet.Position().y > dry.Position().y);
    CHECK(-wet.VerticalVelocity() <= wet.Config().SinkSpeed + 0.001f);
}

TEST_CASE("Water drags a swimmer's walking speed down")
{
    World world = FlatWorld(4);

    for (int z = 0; z < world.GetDepth(); ++z)
        for (int y = 1; y <= 40; ++y)
            for (int x = 0; x < world.GetWidth(); ++x)
                world.SetBlock(x, y, z, Water);

    World air = FlatWorld(4);

    CharacterController wet;
    wet.Teleport(glm::vec3(16.0f, 30.0f, 16.0f));

    CharacterController dry;
    dry.Teleport(glm::vec3(16.0f, 30.0f, 16.0f));

    for (int i = 0; i < 30; ++i)
    {
        wet.Step(world, Walking(glm::vec2(0.0f, 1.0f)), Step);
        dry.Step(air, Walking(glm::vec2(0.0f, 1.0f)), Step);
    }

    CHECK(wet.Position().x < dry.Position().x);
}

TEST_CASE("A swim stroke only fires once the eye is under water")
{
    //The documented subtlety. The box counts as wet with the whole body clear
    //of the surface, so stroking on BodyInFluid alone thrusts the character out
    //of the river and buzzes them above it.
    World world = FlatWorld(4);

    //Water filling y=1..20. A character at y=21.5 has its box overlapping the
    //surface while its eye is well clear.
    for (int z = 0; z < world.GetDepth(); ++z)
        for (int y = 1; y <= 20; ++y)
            for (int x = 0; x < world.GetWidth(); ++x)
                world.SetBlock(x, y, z, Water);

    CharacterController surfacing;
    surfacing.Teleport(glm::vec3(16.0f, 21.5f, 16.0f));
    surfacing.Step(world, Jumping(), Step);

    REQUIRE(surfacing.BodyInFluid());
    REQUIRE_FALSE(surfacing.EyeInFluid());
    CHECK(surfacing.VerticalVelocity() < surfacing.Config().SwimUpSpeed);

    CharacterController submerged;
    submerged.Teleport(glm::vec3(16.0f, 10.0f, 16.0f));
    submerged.Step(world, Jumping(), Step);

    REQUIRE(submerged.EyeInFluid());
    CHECK(submerged.VerticalVelocity() > 0.0f);
}

TEST_CASE("Standing on the riverbed submerged strokes rather than jumping dry")
{
    //Grounded and submerged at once, which is the case the dry-jump branch
    //would otherwise steal: a jump would fire at JumpSpeed instead of a stroke
    //at SwimUpSpeed, launching the character out of the water.
    World world = FlatWorld(4);

    for (int z = 0; z < world.GetDepth(); ++z)
        for (int y = 1; y <= 20; ++y)
            for (int x = 0; x < world.GetWidth(); ++x)
                world.SetBlock(x, y, z, Water);

    CharacterController character;
    character.Teleport(glm::vec3(16.0f, 1.9f, 16.0f));
    SettleOnGround(character, world);

    REQUIRE(character.Grounded());
    REQUIRE(character.EyeInFluid());

    character.Step(world, Jumping(), Step);

    CHECK(character.VerticalVelocity() < character.Config().JumpSpeed * 0.5f);
}

TEST_CASE("Teleporting leaves nothing to interpolate through")
{
    //A respawn or a reload interpolated across the map smears the camera for a
    //frame. Both positions are written together, so the lerp is a no-op.
    World world = FlatWorld();
    CharacterController character;
    character.Teleport(glm::vec3(16.0f, 20.0f, 16.0f));

    for (int i = 0; i < 10; ++i)
        character.Step(world, CharacterInput{}, Step);

    REQUIRE(character.Position() != character.PreviousPosition());

    character.Teleport(glm::vec3(4.0f, 8.0f, 4.0f));

    CHECK(character.Position() == character.PreviousPosition());
    CHECK(character.InterpolatedPosition(0.0f) == character.Position());
    CHECK(character.InterpolatedPosition(1.0f) == character.Position());
    CHECK(character.InterpolatedPosition(0.5f) == character.Position());
}

TEST_CASE("Rendering interpolates between the last two steps")
{
    World world = FlatWorld();
    CharacterController character;
    character.Teleport(glm::vec3(16.0f, 20.0f, 16.0f));
    character.Step(world, CharacterInput{}, Step);

    const glm::vec3 previous = character.PreviousPosition();
    const glm::vec3 current = character.Position();

    REQUIRE(previous != current);

    CHECK(character.InterpolatedPosition(0.0f).y == doctest::Approx(previous.y));
    CHECK(character.InterpolatedPosition(1.0f).y == doctest::Approx(current.y));
    CHECK(character.InterpolatedPosition(0.5f).y
        == doctest::Approx((previous.y + current.y) * 0.5f));
}

TEST_CASE("The eye sits a fixed offset above the interpolated box")
{
    World world = FlatWorld();
    CharacterController character;
    character.Teleport(glm::vec3(16.0f, 20.0f, 16.0f));
    character.Step(world, CharacterInput{}, Step);

    const glm::vec3 body = character.InterpolatedPosition(0.5f);
    const glm::vec3 eye = character.InterpolatedEye(0.5f);

    CHECK(eye.x == doctest::Approx(body.x));
    CHECK(eye.z == doctest::Approx(body.z));
    CHECK(eye.y == doctest::Approx(body.y + character.Config().EyeOffset));
}

TEST_CASE("A heavier configuration falls faster")
{
    //Proves the config is actually consulted rather than shadowed by the old
    //constants: same world, same steps, different gravity, different result.
    World world = FlatWorld(4);

    CharacterConfig heavy;
    heavy.Gravity = 96.0f;

    CharacterController light;
    light.Teleport(glm::vec3(16.0f, 50.0f, 16.0f));

    CharacterController fast(heavy);
    fast.Teleport(glm::vec3(16.0f, 50.0f, 16.0f));

    for (int i = 0; i < 20; ++i)
    {
        light.Step(world, CharacterInput{}, Step);
        fast.Step(world, CharacterInput{}, Step);
    }

    CHECK(fast.Position().y < light.Position().y);
}

TEST_CASE("Yaw turns which way forward is")
{
    //The same input walks a different way when the character faces a
    //different way. This is the property that lets a server resolve movement
    //from yaw instead of trusting a direction a client sent.
    World world = FlatWorld();

    CharacterController east;
    east.Teleport(glm::vec3(16.0f, 20.0f, 16.0f));
    SettleOnGround(east, world);

    CharacterController south;
    south.Teleport(glm::vec3(16.0f, 20.0f, 16.0f));
    SettleOnGround(south, world);

    for (int i = 0; i < 30; ++i)
    {
        east.Step(world, Walking(glm::vec2(0.0f, 1.0f), 0.0f), Step);
        south.Step(world, Walking(glm::vec2(0.0f, 1.0f), 90.0f), Step);
    }

    //Yaw 0 walks +x; yaw 90 walks +z.
    CHECK(east.Position().x > 16.5f);
    CHECK(east.Position().z == doctest::Approx(16.0f).epsilon(0.01));
    CHECK(south.Position().z > 16.5f);
    CHECK(south.Position().x == doctest::Approx(16.0f).epsilon(0.01));
}

TEST_CASE("Two keys at once do not walk faster than one")
{
    //The cap. Without it, holding forward and strafe walks sqrt(2) times
    //faster, which is the oldest speed exploit there is.
    World world = FlatWorld();

    CharacterController straight;
    straight.Teleport(glm::vec3(8.0f, 20.0f, 16.0f));
    SettleOnGround(straight, world);

    CharacterController diagonal;
    diagonal.Teleport(glm::vec3(8.0f, 20.0f, 16.0f));
    SettleOnGround(diagonal, world);

    const glm::vec3 start = straight.Position();

    for (int i = 0; i < 30; ++i)
    {
        straight.Step(world, Walking(glm::vec2(0.0f, 1.0f)), Step);
        diagonal.Step(world, Walking(glm::vec2(1.0f, 1.0f)), Step);
    }

    const float straightDistance =
        glm::length(straight.Position() - start);
    const float diagonalDistance =
        glm::length(diagonal.Position() - start);

    CHECK(diagonalDistance == doctest::Approx(straightDistance).epsilon(0.01));
}

TEST_CASE("Half deflection walks at half speed")
{
    //The reason movement is capped rather than normalised: an analog stick
    //pushed halfway should walk slowly, not snap to full speed.
    World world = FlatWorld();

    CharacterController full;
    full.Teleport(glm::vec3(8.0f, 20.0f, 16.0f));
    SettleOnGround(full, world);

    CharacterController half;
    half.Teleport(glm::vec3(8.0f, 20.0f, 16.0f));
    SettleOnGround(half, world);

    const glm::vec3 start = full.Position();

    for (int i = 0; i < 30; ++i)
    {
        full.Step(world, Walking(glm::vec2(0.0f, 1.0f)), Step);
        half.Step(world, Walking(glm::vec2(0.0f, 0.5f)), Step);
    }

    const float fullDistance = glm::length(full.Position() - start);
    const float halfDistance = glm::length(half.Position() - start);

    CHECK(halfDistance == doctest::Approx(fullDistance * 0.5f).epsilon(0.02));
}
