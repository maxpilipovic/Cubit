# B8: Engine, Harness and Game as Separate Projects — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split Cubit into an engine that knows no game rules, a sandbox that harnesses
the engine, and a game built on top — laid out so the game can later move to its own
repository with one `git subtree split`.

**Architecture:** The engine (`Cubit`) keeps every system and none of the game's numbers:
health, damage, shot range, fire rate and dig reach become a `MatchRules` value the game
supplies. Game code — the player, tools, shooting, HUD, the network client wiring, the
dedicated server and the map generator — moves under one top-level `game/` directory as a
static library plus thin executables, so the future repository split is a directory move.
The Sandbox keeps only what exercises the engine: map load, a free camera, world rendering,
debug draw, save and reload, the batch blast, and the timing readouts.

**Tech Stack:** C++20, premake5 (vs2026 action), OpenGL 3.3/4.3 via GLAD and GLFW, ENet,
glm, doctest.

**Spec:** `docs/engine-roadmap.md`, item B8, plus the Design section below. The two
decisions the user made on 2026-09-17: the Sandbox becomes an engine-only harness, and the
game rules come out of the engine as part of this work.

## Global Constraints

- Build: `MSBuild.exe C:\dev\Cubit\Cubit.slnx /p:Configuration=Debug /p:Platform=x64`
  (find MSBuild with `vswhere.exe -latest -find MSBuild\**\Bin\MSBuild.exe`). The build runs
  the test executables as a post-build step, so a failing test fails the build.
- Regenerate projects after adding, moving or removing any source file or premake file:
  `C:\dev\premake\premake5 vs2026` from the repo root. Never run `GenerateProjects.bat` — it
  deletes `bin/`.
- Every task ends green in Debug **and** Release, then commits and pushes to `master`
  (standing authorization). No Claude co-author trailers or attribution in commits.
- Comments explain why, in the voice of the surrounding code. Match the existing style:
  `//` comments, four-space indent, `m_` members, PascalCase functions.
- No behaviour changes in this plan. Every number that moves keeps its value: reach 12.0,
  shot range 128.0, starting health 100, shot damage 34, ten ticks between shots.
- Tests move with the code they cover. A moved test keeps its name so the suite's history
  stays readable.
- The engine must not include a header from `game/` or `Sandbox/`. After Task 6 the engine's
  project file has no include path into either.

## Design

**Directory layout when the plan is done.** Engine side, unchanged homes: `Cubit/`,
`Sandbox/`, `Tests/`, `vendor/`, `docs/`. Game side, all new, all under one directory so
`git subtree split -P game` can lift it with its history:

```
game/
  Game/src/         static library: player, tools, shooting, HUD, client wiring, rules
  GameApp/src/      thin executable around Game
  Server/src/       the dedicated server (moved from Server/)
  MapGen/src/       the map writer (moved from MapGen/)
  GameTests/src/    the game's own suite
  assets/           maps (moved from Sandbox/assets)
  premake5.lua      the game's projects, included by the root file
```

**Projects afterwards:** `GLAD`, `GLFW`, `ENet` (dependencies), `Cubit` (DLL), `Sandbox`
(exe), `Tests` (exe), `Game` (static lib), `GameApp` (exe), `Server` (exe), `MapGen` (exe),
`GameTests` (exe).

**What stays in the engine and why.** `MatchServer`, `MatchClient`, `EditRules`,
`ResolveShot`, `HitboxHistory`, `CharacterController`, `SpawnFinder` and `TerrainGen` are
engine systems: they implement mechanisms, and after Task 1 they hold no game numbers.
`TerrainGen` stays engine-side even though the map it draws is the game's content, because
`SpawnFinderTests`, `MapHashTests` and `VoxWriterTests` all build worlds with it; moving it
is its own job, recorded in the roadmap rather than done here.

**Rules.** One aggregate in the engine, passed in by the game:

```cpp
struct MatchRules
{
    float ReachDistance = 12.0f;
    float ShotRange = 128.0f;
    std::uint8_t StartingHealth = 100;
    std::uint8_t ShotDamage = 34;
    int TicksBetweenShots = 10;
};
```

The member values are placeholders so nothing reads uninitialised memory; the game states
its own explicitly in `game/Game/src/GameRules.h`. Reach is checked by the server **and** by
the client's prediction, so both ends must hold the same number. They do because a client
and a server come from one game build. Sending the rules in `Welcome` is the recorded answer
if mismatched builds ever become possible, and is deliberately not done here — it is a
protocol change for a case that cannot happen yet.

**Shared debug text.** Both apps need a readout, and the 5x7 font plus the string-drawing
loop are engine debug tooling, not game UI. They move into the engine; each app keeps its
own HUD layer, with its own labels and its own camera. `CursorCapture` moves into the engine
for the same reason: both apps capture the mouse, and the rules are window-free.

**Harness HUD** (Sandbox): `POS`, `FACES`, `DRAWN`, `PENDING`, `FPS`.
**Game HUD**: `POS`, `GND`, `OCEAN`, `HEALTH`, `UNDO`, `STEPS`, `FPS`, and the
not-connected notice.

**Verification at the end:** both executables run, screenshot as expected, and a two-process
connected run works — `game/Server` plus `GameApp --connect` — with an edit made on the
client appearing on the server.

---

### Task 1: Game rules out of the engine

**Done 2026-09-17, with one deviation.** Step 6 planned a `TestRules` in every test file and
a rules argument at every construction site. There are 105 of those, all in engine tests that
have nothing to say about tuning, so the rules parameter is **defaulted** on both
constructors and on `IsEditLegal` instead: engine tests construct as before, and a game
states its rules explicitly. `TestRules` still went into the five test files that named the
old constants. The weaker boundary this leaves — a caller can forget to pass rules and
silently get the placeholders — is worth the 100 untouched call sites, and Task 3's test pins
that the game passes its own.

**Files:**
- Create: `Cubit/include/Cubit/MatchRules.h`
- Modify: `Cubit/include/Cubit/Voxel/EditRules.h` (drop `ReachDistance`, take reach as an
  argument), `Cubit/src/Voxel/EditRules.cpp`
- Modify: `Cubit/include/Cubit/Net/MatchServer.h:27-44` (drop `TicksBetweenShots`,
  `ShotRange`, `StartingHealth`, `ShotDamage`; constructor takes `MatchRules`),
  `Cubit/src/Net/MatchServer.cpp:29-35, 459, 608, 662, 683-694`
- Modify: `Cubit/include/Cubit/Net/MatchClient.h` (constructor takes `MatchRules`),
  `Cubit/src/Net/MatchClient.cpp:143, 595`
- Modify: `Cubit/include/Cubit/Cubit.h` (include the new header)
- Modify: `Sandbox/src/Sandbox.cpp` (its own `MatchRules` instance for the aim ray, the
  tracer and the client)
- Modify: `Server/src/Server.cpp` (pass rules to `MatchServer`)
- Test: `Tests/src/EditRulesTests.cpp`, `Tests/src/MatchServerTests.cpp`,
  `Tests/src/PredictionTests.cpp`, `Tests/src/PredictedEditTests.cpp`,
  `Tests/src/LagCompensationTests.cpp`, `Tests/src/WireOracleTests.cpp`,
  `Tests/src/ResolveShotTests.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `struct MatchRules` with the five fields above;
  `bool IsCellWithinReach(const glm::vec3& eye, const glm::ivec3& cell, float reach)`;
  `bool IsEditLegal(const MatchState& match, PlayerId editor, const BlockEdit& edit, OtherPlayers others, const MatchRules& rules)`;
  `MatchServer(World world, std::string mapName, std::uint64_t mapHash, const glm::vec3& spawn, Transport& transport, const MatchRules& rules)`;
  `MatchClient(Transport& transport, MapLoader loadMap, const MatchRules& rules)`;
  `const MatchRules& MatchServer::Rules() const`.

- [x] **Step 1: Write the failing test** — rules reach the behaviour, not just the struct.
  Append to `Tests/src/EditRulesTests.cpp`:

```cpp
TEST_CASE("Reach comes from the rules, not from the engine")
{
    const glm::vec3 eye(0.0f, 0.5f, 0.0f);

    MatchRules shortArms;
    shortArms.ReachDistance = 4.0f;

    CHECK(IsCellWithinReach(eye, glm::ivec3(3, 0, 0), shortArms.ReachDistance));
    CHECK_FALSE(IsCellWithinReach(eye, glm::ivec3(5, 0, 0), shortArms.ReachDistance));

    MatchRules longArms;
    longArms.ReachDistance = 20.0f;

    CHECK(IsCellWithinReach(eye, glm::ivec3(5, 0, 0), longArms.ReachDistance));
}
```

  And to `Tests/src/MatchServerTests.cpp`, where the rules must change what the server does.
  `Join`, `Settle`, `SendInputWithEdit` and `DrainEdits` already exist in that file:

```cpp
TEST_CASE("A weaker shot from the rules takes more hits to kill")
{
    LoopbackNetwork network;

    MatchRules rules;
    rules.ShotDamage = 10;

    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server(), rules);

    PeerId shooterPeer = InvalidPeer;
    Transport& shooter = network.AddClient(shooterPeer);
    const PlayerId shooterId = Join(server, shooter);
    REQUIRE(shooterId != InvalidPlayer);

    PeerId targetPeer = InvalidPeer;
    Transport& target = network.AddClient(targetPeer);
    const PlayerId targetId = Join(server, target);
    REQUIRE(targetId != InvalidPlayer);

    Settle(server, shooterId);

    //Both players stand on the spawn, so a shot straight ahead cannot miss.
    FireMessage fire;
    fire.ClientTick = 1;
    fire.RenderTick = server.Match().Tick();
    fire.Yaw = 0.0f;
    fire.Pitch = 0.0f;
    shooter.Send(LoopbackNetwork::ServerPeer, Encode(fire), Channel::Reliable);
    server.Step(FrameClock::FixedStepSeconds);

    CHECK(server.HealthOf(targetId) == 90);
}
```

- [x] **Step 2: Run the tests and watch them fail to compile**

Run: `MSBuild.exe C:\dev\Cubit\Cubit.slnx /p:Configuration=Debug /p:Platform=x64`
Expected: compile errors — `MatchRules` is undeclared, `IsCellWithinReach` takes two
arguments, `MatchServer`'s constructor takes five.

- [x] **Step 3: Add the rules header**

Create `Cubit/include/Cubit/MatchRules.h` with the struct from the Design section above.
Comment it: the numbers are placeholders so nothing reads uninitialised memory, and the game
states its own; the file exists so the engine can be told the rules rather than hold them.
Include it from `Cubit/include/Cubit/Cubit.h` beside the other top-level headers.

- [x] **Step 4: Thread reach through the edit rules**

In `EditRules.h`, delete `constexpr float ReachDistance` and add `float reach` as the last
parameter of `IsCellWithinReach`, and `const MatchRules& rules` as the last parameter of
`IsEditLegal`. In `EditRules.cpp`, use `reach` and `rules.ReachDistance`. Keep every comment
about why one number serves the aim ray, the prediction and the ruling — it is now about one
*value*, not one constant, so reword that sentence rather than deleting it.

- [x] **Step 5: Give the server and the client their rules**

`MatchServer`: drop the four constants, take `const MatchRules&` as the last constructor
parameter, store it as `m_Rules`, add `const MatchRules& Rules() const`, and read
`m_Rules.TicksBetweenShots`, `m_Rules.ShotRange`, `m_Rules.ShotDamage` and
`m_Rules.StartingHealth` at the five sites listed under Files. `Client::Health` cannot use
`StartingHealth` as a default member initialiser any more: set it from `m_Rules` where a
client is admitted.

`MatchClient`: take `const MatchRules&`, store it, and pass it to both `IsEditLegal` calls.

- [x] **Step 6: Update the callers and the tests**

`Sandbox.cpp`: one `const MatchRules SandboxRules{};` in its anonymous namespace, used for
the aim ray's reach, the tracer's range and the `MatchClient` constructor.
`Server.cpp`: the same, passed to `MatchServer`.
Tests: every `MatchServer`/`MatchClient` construction gains a rules argument. Add
`const MatchRules TestRules{};` in each test file's anonymous namespace and pass that, so a
later change to a rule does not touch fifty call sites.

- [x] **Step 7: Run the tests in both configurations**

Run the Debug build, then
`MSBuild.exe C:\dev\Cubit\Cubit.slnx /p:Configuration=Release /p:Platform=x64`.
Expected: both green, with two more cases than before.

- [x] **Step 8: Prove the new tests can fail**

Change `IsCellWithinReach` to ignore its `reach` argument and use `12.0f`; rebuild; expect
the reach test red. Restore. Change the damage site to a literal `34`; rebuild; expect the
damage test red. Restore, rebuild, green.

- [x] **Step 9: Commit**

```bash
git add -A
git commit -m "Tell the engine the rules instead of holding them"
git push origin master
```

---

### Task 2: Debug text and cursor capture into the engine

**Done 2026-09-17, with two deviations.** Step 1's new label test was redundant:
`DebugFontTests` already checks every HUD label against the font, `HEALTH` included, so
there was nothing to add and nothing that could fail. And Step 4 planned to move the text
loop into a free function, which cannot work — drawing a glyph needs the pixel-space camera,
the unit quad, the shader and the font atlas that `HudLayer` owned. So the whole of that
plumbing moved instead, as `ScreenOverlay` (`Begin`, `End`, `DrawText`, `DrawQuad`,
`DrawCrosshair`, `FillScreen`, `Resize`, `LineHeight`, `TopLine`, `FormatOneDecimal`).
`HudLayer` is now 210 lines of labels over the engine's drawing, which is the split Task 3
and Task 5 need. Verified by screenshot rather than by test, since it is all GL: the readout,
crosshair and block outline all draw as before.

**Files:**
- Create: `Cubit/include/Cubit/Renderer/DebugFont.h` (moved from `Sandbox/src/DebugFont.h`),
  `Cubit/include/Cubit/Renderer/DebugText.h`, `Cubit/src/Renderer/DebugText.cpp`
- Create: `Cubit/include/Cubit/CursorCapture.h` (moved from `Sandbox/src/CursorCapture.h`)
- Delete: `Sandbox/src/DebugFont.h`, `Sandbox/src/CursorCapture.h`
- Modify: `Sandbox/src/HudLayer.h` (draw through `DrawDebugText`), `Sandbox/src/Sandbox.cpp`
  (include path for `CursorCapture`)
- Modify: `Cubit/include/Cubit/Cubit.h`
- Test: `Tests/src/DebugFontTests.cpp`, `Tests/src/CursorCaptureTests.cpp` (include paths),
  new case in `Tests/src/DebugFontTests.cpp`

**Interfaces:**
- Consumes: Task 1's headers only for the include ordering.
- Produces: `DebugFont` (unchanged API, new header path);
  `void DrawDebugText(std::string_view text, float x, float y, float scale, const glm::vec4& colour)`,
  which draws with `Renderer` quads and requires a scene to be open;
  `CursorCapture` (unchanged API, new header path).

- [x] **Step 1: Write the failing test**

Append to `Tests/src/DebugFontTests.cpp`:

```cpp
TEST_CASE("The font covers every label both apps draw")
{
    //Each app owns its labels; the font has to carry all of them, and an
    //unsupported character draws as a blank rather than failing.
    for (const std::string_view label : {
            "POS", "FACES", "DRAWN", "PENDING", "FPS",
            "GND", "OCEAN", "HEALTH", "UNDO", "STEPS", "NOT CONNECTED" })
    {
        for (const char character : label)
        {
            CAPTURE(label);
            CAPTURE(character);
            CHECK(character == ' ' ? true : DebugFont::IndexOf(character) != DebugFont::IndexOf(' '));
        }
    }
}
```

- [x] **Step 2: Run it and watch it fail**

Run: the Debug build.
Expected: FAIL — `HEALTH` is not among today's HUD labels, so this is the first test that
asks for it, and any letter the font lacks shows up here.

- [x] **Step 3: Move the font and the cursor rules**

`git mv Sandbox/src/DebugFont.h Cubit/include/Cubit/Renderer/DebugFont.h` and
`git mv Sandbox/src/CursorCapture.h Cubit/include/Cubit/CursorCapture.h`. Fix the include in
`HudLayer.h`, `Sandbox.cpp`, `DebugFontTests.cpp` and `CursorCaptureTests.cpp` to the
`Cubit/...` paths. Add both to `Cubit/include/Cubit/Cubit.h`. Remove `"Sandbox/src"` from the
`Tests` project's `includedirs` in `premake5.lua:414-423`, with its comment.

- [x] **Step 4: Lift the text drawing out of the HUD**

`Sandbox/src/HudLayer.h` draws strings by walking `DebugFont` and emitting quads. Move that
loop verbatim into `DrawDebugText` in `Cubit/src/Renderer/DebugText.cpp`, taking the text,
pixel position, scale and colour, and have `HudLayer::DrawText` call it. Document in
`DebugText.h` that a scene must already be open, because the function draws quads and does
not manage the camera.

- [x] **Step 5: Run the tests**

Run: the Debug build, then Release.
Expected: green, including the new label case.

- [x] **Step 6: Commit**

```bash
git add -A
git commit -m "Move the debug font, its text drawing and the cursor rules into the engine"
git push origin master
```

---

### Task 3: The game library and its application

**Done 2026-09-17, with four deviations.**
1. **`WorldScene` went into the engine first**, committed on its own. Both apps draw the same
   world, and the app that did it held the chunk shader as a string literal; duplicating that
   in two apps would have let it drift. The engine owns the chunk renderer, its shader and
   the fog now.
2. **`PlayerLayer` stayed inside `GameApp.cpp`** rather than becoming a library header.
   Nothing outside the app constructs it and it needs a GL context; the library holds the
   rules, the options and the HUD, which is what `GameTests` links. A library with only
   headers also produces nothing to link, which is why `GameRules.cpp` exists.
3. **The method was a move, not a rewrite.** `Sandbox.cpp` became `GameApp.cpp` with its
   authoring tools removed, and the harness was written fresh — the game keeps the code that
   already worked, and the new code is the small half.
4. **Undo, the blast, `F5` and `F9` are the harness's**, not the game's, so the game HUD lost
   its `UNDO` line. Those are map-authoring tools, and the harness is where a map is authored.

**Also worth recording:** `rm -rf game/game`, aimed at a stray generated directory, deleted
`game/Game` as well — Windows paths are case-insensitive. `GameHudLayer.h` came back with
`git checkout-index` because the move was staged; the three new files were not, and were
rewritten. Stage or commit before any recursive delete near a path that differs only in case.

**Files:**
- Create: `game/Game/src/GameRules.h`, `game/Game/src/GameOptions.h`,
  `game/Game/src/PlayerLayer.h`, `game/Game/src/PlayerLayer.cpp`,
  `game/Game/src/GameHudLayer.h`, `game/Game/src/GameHudState.h`
- Create: `game/GameApp/src/GameApp.cpp`
- Create: `game/premake5.lua`
- Modify: `premake5.lua` (include the game's file, see Task 6 for the full split)
- Modify: `Sandbox/src/Sandbox.cpp`, `Sandbox/src/HudLayer.h` (what is left behind)
- Move: `Sandbox/assets` to `game/assets`, and keep a copy of `battlefield512.vox` reachable
  by the Sandbox (see Step 6)

**Interfaces:**
- Consumes: `MatchRules` and `MatchServer`/`MatchClient` constructors from Task 1;
  `DrawDebugText`, `DebugFont`, `CursorCapture` from Task 2.
- Produces: `CubitGame::Rules()` returning the game's `MatchRules`;
  `struct GameOptions { bool Connect; std::string Host; std::uint16_t Port; double LatencyRtt; float Loss; };`
  `PlayerLayer(EventBus&, std::shared_ptr<GameHudState>, const GameOptions&)`;
  `GameHudLayer(std::shared_ptr<GameHudState>, std::uint32_t width, std::uint32_t height)`.

- [x] **Step 1: Write the failing test for the game's own rules**

Create `game/GameTests/src/GameRulesTests.cpp`:

```cpp
#include <doctest.h>

#include "GameRules.h"

TEST_CASE("The game states its rules rather than taking the engine's placeholders")
{
    const MatchRules rules = CubitGame::Rules();

    //The numbers the game has shipped with since shooting landed.
    CHECK(rules.StartingHealth == 100);
    CHECK(rules.ShotDamage == 34);
    CHECK(rules.ShotRange == doctest::Approx(128.0f));
    CHECK(rules.ReachDistance == doctest::Approx(12.0f));
    CHECK(rules.TicksBetweenShots == 10);

    //Three shots kill, and the third overshoots rather than wrapping.
    CHECK(rules.ShotDamage * 3 > rules.StartingHealth);
    CHECK(rules.ShotDamage * 2 < rules.StartingHealth);
}
```

- [x] **Step 2: Run it and watch it fail**

Run: the Debug build.
Expected: the file is not in any project yet, so it does not even compile. Add the projects
in Step 3 and it fails on the missing `GameRules.h` instead.

- [x] **Step 3: Add the game's projects**

Write `game/premake5.lua` with four projects, following the existing file's shape exactly
(`targetdir`, `objdir`, `cppdialect "C++20"`, the three configuration filters, the
`CB_PLATFORM_WINDOWS` define, and the Cubit DLL copy in `postbuildcommands`):

- `Game`, `kind "StaticLib"`, files `game/Game/src/**`, includedirs `Cubit/include`,
  `vendor/GLM`, `game/Game/src`.
- `GameApp`, `kind "ConsoleApp"`, files `game/GameApp/src/**`, links `Game`, `Cubit`,
  same includedirs plus `game/Game/src`; `debugdir` its target directory, and a
  `{COPYDIR} "../game/assets"` postbuild so it runs beside the maps.
- `GameTests`, `kind "ConsoleApp"`, files `game/GameTests/src/**`, links `Game`, `Cubit`,
  includedirs as `GameApp` plus `vendor/doctest/doctest`; postbuild runs the executable, the
  way `Tests` does.
- `Server`, moved as it stands, with its files under `game/Server/src/**` and `links` gaining
  `Game` so it can call `CubitGame::Rules()`.

Add `include "game"` to the root `premake5.lua`, then run
`C:\dev\premake\premake5 vs2026`.

- [x] **Step 4: Write the game's rules and options**

`game/Game/src/GameRules.h`: `namespace CubitGame { MatchRules Rules(); }` — or an
`inline` function in the header, which suits five assignments. Comment each number with what
it means for play (three shots to kill, six shots a second, twelve blocks of reach), because
this file is where a designer will come to change them.

`game/Game/src/GameOptions.h`: the struct from `SandboxOptions` in `Sandbox.cpp:32-41`,
renamed, with its comment about single-player being the default.

- [x] **Step 5: Move the player, tools, shooting and client wiring**

From `Sandbox/src/Sandbox.cpp` into `PlayerLayer`, verbatim except for names: the
`Match_`/`World_`/`Player_`/`HaveLocalPlayer` accessors and the branch-point comments,
`Connect`, `ReadInput`, `ReadWalkInput`, `UpdateCameraPosition`, `AimAtMapCentre`,
`DrawTargetedBlockOutline`, `DrawRemotePlayers`, `FireShot`, the tracer and impact drawing,
`OnMouseButtonPressed`, `UndoLastEdit`, `PushUndo`, `CollapseAfter`, `ResolveSpawn`,
`LiftPlayerClearOfTerrain`, `OnPlayerDied`, `m_DeathSubscription`, and the constants
`SpawnHintXZ`, `PlaceableBlocks`, `FallResetHeight`, `RemotePlayerColor`, `TracerColor`,
`ImpactHitColor`, `ImpactMissColor`, `ImpactHalfSize`, `TracerTicks`, `ShotMarkerTicks`,
`TracerMuzzleRight`, `TracerMuzzleDown`, `OutlineColor`.

`PlayerLayer` gets its own copy of the world load path (`LoadWorld`) because a game session
loads a map too, and the map path moves with it as `game/Game/src/GameRules.h`'s neighbour:
put `MapPath` in `GameOptions.h` with its comment about working directories.

`GameHudLayer` and `GameHudState`: `Sandbox/src/HudLayer.h` minus the mesh counters, plus
`HEALTH`. Keep the crosshair, the underwater wash and the not-connected notice.

`game/GameApp/src/GameApp.cpp`: the `SandboxApplication` class and `main` from
`Sandbox.cpp:1177-1228`, renamed, parsing the same flags (`--connect`, `--host`, `--port`,
`--latency`, `--loss`), pushing `PlayerLayer` and `GameHudLayer`, and calling
`CrashHandler::Install("Game")` and `Logger::OpenFile("Game")`.

- [x] **Step 6: Cut the Sandbox back to a harness**

What stays in `Sandbox.cpp`: the world load, `SaveWorld`, `ReloadWorld`, `BlastAtAim` and
its undo stack, `DrawTargetedBlockOutline`'s outline of the block under the crosshair,
`ApplyCursor` and the cursor rules, `LoadWorld`'s profiler session, the fog and water
uniforms, the shader, `WorldRenderer` drawing, and the `F5`/`F9`/`B`/`U`/Escape bindings.

What replaces the player: a free camera. `PerspectiveCameraController` already moves with
`W`/`A`/`S`/`D` and the mouse when nothing overrides it, so the harness reads input straight
into the controller and stops resolving a spawn or stepping a character. It keeps a `POS`
readout from the camera's position so the screenshot scripts still have one.

The harness keeps `assets/` beside its executable: change its `postbuildcommands` copy to
`{COPYDIR} "../game/assets" "%{cfg.targetdir}/assets"` so one copy of the maps serves both
apps.

- [x] **Step 7: Run everything**

Run: `C:\dev\premake\premake5 vs2026`, then the Debug build, then Release.
Expected: both suites green — `Tests` (engine) and `GameTests` (the rules case above).

- [x] **Step 8: Prove the game's rules test can fail**

Change `CubitGame::Rules()` to leave `ShotDamage` at the engine's placeholder minus one
(33); rebuild; expect the three-shots-to-kill check red. Restore, rebuild, green.

- [x] **Step 9: Commit**

```bash
git add -A
git commit -m "Build the game as its own library and application"
git push origin master
```

---

### Task 4: Move the map generator game-side

**Done 2026-09-18.** Step 1 was already in the ground: the move and its premake block rode
along with Task 3's commit, because the root file could not name a project the game
directory now owned. So this task was the checking, not the moving. The generator, run from
its own directory, wrote a battlefield byte-identical to the committed
`game/assets/maps/battlefield.vox` — the same hash — which is the evidence that a move of a
tool changed nothing about what it makes.

**Files:**
- Move: `MapGen/src/MapGen.cpp` to `game/MapGen/src/MapGen.cpp`
- Modify: `game/premake5.lua` (add the `MapGen` project, moved verbatim from
  `premake5.lua:286-335`), root `premake5.lua` (drop it)
- Modify: `docs/engine-roadmap.md` (record that `TerrainGen` stays engine-side for now)

**Interfaces:**
- Consumes: nothing new.
- Produces: nothing new. `MapGen` keeps its command line and its output path.

- [x] **Step 1: Move the project**

`git mv MapGen game/MapGen`, move its premake block into `game/premake5.lua`, and update the
paths inside it to `game/MapGen/src/**`. Run `C:\dev\premake\premake5 vs2026`.

- [x] **Step 2: Build, then generate a map and check it loads**

Run: the Debug build, then
`bin\Debug-windows-x86_64\MapGen\MapGen.exe` in its own directory.
Expected: it writes its `.vox` as before. Then run the engine suite, which loads
`battlefield512.vox` in `SpawnFinderTests` and the two measurement cases, so a broken asset
path fails the build.

- [x] **Step 3: Record the TerrainGen decision**

In `docs/engine-roadmap.md` under B8, note that `TerrainGen` stays in the engine because
three engine test files build worlds with it, and that moving it is its own item if the
engine is ever shipped without the game's terrain.

- [x] **Step 4: Commit**

```bash
git add -A
git commit -m "Move the map generator to the game side"
git push origin master
```

---

### Task 5: Split the test suites

**Files:**
- Move to `game/GameTests/src/`: `Tests/src/DebugFontTests.cpp` label case only (the font's
  own table stays engine-side — split the file), `Tests/src/HeadingTests.cpp` stays,
  see Step 1 for the exact division
- Create: `game/GameTests/src/GameHudTests.cpp`
- Modify: `Tests/src/DebugFontTests.cpp`

**Interfaces:**
- Consumes: `GameHudState` and `GameHudLayer` from Task 3.
- Produces: nothing other tasks read.

- [ ] **Step 1: Decide the division by what a test would break on**

Engine suite keeps everything that fails when the engine changes: the font's glyph table,
`CursorCapture`, `EditRules`, the netcode, the voxel systems, `Support`, `BlockEdit`.
Game suite takes what fails when the game changes: the game's rules (Task 3), and the
labels the game's HUD draws.

- [ ] **Step 2: Write the game HUD's test**

Create `game/GameTests/src/GameHudTests.cpp`:

```cpp
#include <doctest.h>

#include "Cubit/Renderer/DebugFont.h"
#include "GameHudState.h"

TEST_CASE("Every label the game HUD draws is one the debug font can draw")
{
    //An unsupported character renders as a blank glyph rather than an error, so
    //a label that drifts out of the font hides the value it was added to show.
    for (const std::string_view label : GameHudState::Labels)
    {
        for (const char character : label)
        {
            CAPTURE(label);
            CHECK(character == ' ' ? true : DebugFont::IndexOf(character) != DebugFont::IndexOf(' '));
        }
    }
}
```

  Add `static constexpr std::string_view Labels[] = { "POS", "GND", "OCEAN", "HEALTH", "UNDO", "STEPS", "FPS", "NOT CONNECTED" };`
  to `GameHudState`, and have `GameHudLayer` draw from that list, so the test and the drawing
  cannot drift apart.

- [ ] **Step 3: Move the label case out of the engine suite**

Delete the label case added in Task 2 from `Tests/src/DebugFontTests.cpp`, leaving the glyph
table and `IndexOf` cases. The harness HUD's labels get the same treatment as the game's: add
`HudState::Labels` in `Sandbox/src/HudLayer.h` and a case in `Tests/src/DebugFontTests.cpp`
that checks that list.

- [ ] **Step 4: Run both suites in both configurations**

Run: the Debug build, then Release.
Expected: green. Note the two case counts; they sum to at least what the suite had before.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "Give the game its own test suite"
git push origin master
```

---

### Task 6: One premake file per project

**Files:**
- Create: `Cubit/premake5.lua`, `Sandbox/premake5.lua`, `Tests/premake5.lua`,
  `vendor/premake5.lua`
- Modify: `premake5.lua` (workspace, configurations, `outputdir`, and `include` lines only)
- Modify: `game/premake5.lua` (unchanged in shape, checked against the others)

**Interfaces:**
- Consumes: nothing.
- Produces: `outputdir` as a workspace-level variable the included files read.

- [ ] **Step 1: Move each project block into its own file**

Cut each `project "X"` block into `X/premake5.lua` verbatim, with paths unchanged — premake
resolves them from the root because `include` runs the file in place. The dependency blocks
(`GLAD`, `GLFW`, `ENet`) go to `vendor/premake5.lua` under their `group "Dependencies"`.

- [ ] **Step 2: Leave the root file with the workspace only**

The root keeps the `workspace`, `architecture`, `configurations`, `startproject`,
`outputdir`, and:

```lua
include "vendor"
include "Cubit"
include "Sandbox"
include "Tests"
include "game"
```

- [ ] **Step 3: Regenerate and build**

Run: `C:\dev\premake\premake5 vs2026`, then the Debug build, then Release.
Expected: the same projects, the same outputs, both suites green. `git diff` on the
generated `.vcxproj` files should show no meaningful change beyond ordering.

- [ ] **Step 4: Check the engine cannot see the game**

Run: `grep -rn "game/" Cubit/premake5.lua Sandbox/premake5.lua Tests/premake5.lua`
Expected: no matches. The engine, harness and engine tests name no game path.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "Give each project its own build file"
git push origin master
```

---

### Task 7: Verify both apps, then document

**Files:**
- Modify: `README.md` (projects, layout, how to run each app, test counts)
- Modify: `docs/engine-roadmap.md` (tick B8 with how, and the repo-split note)

**Interfaces:**
- Consumes: everything above.
- Produces: the documentation that says what the layout is and why.

- [ ] **Step 1: Run the harness and screenshot it**

Launch `bin\Debug-windows-x86_64\Sandbox\Sandbox.exe` from its own directory, move the GL
window to a known rect, and capture it (see
`.claude/projects/C--dev-Cubit/memory/screenshot-cubit-gl-window.md` for the pitfalls: grab
the `GLFW30` window, not `MainWindowHandle`; allow time for the budgeted mesher; close with
`WM_CLOSE`). Expected: the battlefield renders, the readout shows `FACES`, `DRAWN`,
`PENDING`, `FPS` and a camera `POS`.

- [ ] **Step 2: Run the game and screenshot it**

Launch `bin\Debug-windows-x86_64\GameApp\GameApp.exe` the same way. Expected: the player
stands on the map, the HUD shows `HEALTH 100`, and a left click breaks a block.

- [ ] **Step 3: Run a match across two processes**

Start `bin\Debug-windows-x86_64\Server\Server.exe`, then
`GameApp.exe --connect 127.0.0.1`. Expected: the client's log says it joined; an edit made on
the client appears in the server's log; both exit cleanly. Read both log files under each
executable's `logs/`.

- [ ] **Step 4: Update the README**

The project list, the directory layout, the two executables and how to run them, and the two
test counts. Say plainly that the game lives under `game/` so it can move to its own
repository, and that the engine holds no game rules.

- [ ] **Step 5: Tick B8 in the roadmap**

Record: the layout, the rules extraction, what stayed engine-side and why (`TerrainGen`),
the Sandbox's new job and what that cost (gameplay screenshots retarget to `GameApp`), the
`git subtree split -P game` path for the eventual repository split, and that sending rules in
`Welcome` is the recorded answer if client and server builds can ever differ.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "Say what the new layout is and why"
git push origin master
```

---

## Self-Review

**Spec coverage.** Rules extraction: Task 1. Game library and app: Task 3. Sandbox as
harness: Task 3, Step 6. Server game-side: Task 3, Step 3. MapGen game-side: Task 4. Test
split: Task 5. Per-project build files: Task 6. Screenshots and a connected run: Task 7.
Shared text and cursor rules, which the split forced into the open: Task 2.

**Known gap, deliberate.** `TerrainGen` stays in the engine, recorded in Task 4, Step 3.
A future `Welcome` carrying the rules is recorded in Task 7, Step 5 rather than built.

**Risk.** Task 3 is the big one: it moves about 900 lines and rewrites the Sandbox's input
path. If it cannot be finished green in one pass, stop and split it — the player and client
wiring first, the HUD second — rather than leaving the tree red.
