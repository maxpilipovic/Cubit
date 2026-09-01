# Networking Stage 2 (The Wire) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** An ENet server and two connected clients moving in one world and editing terrain that everyone sees, with the latency deliberately visible and no prediction anywhere.

**Architecture:** A `Transport` interface with three implementations — `LoopbackTransport` (in-process, no properties), `SimulatedTransport` (a decorator adding deterministic latency, jitter and loss), and `EnetTransport` (real sockets). Above it, `MatchServer` owns the authoritative `MatchState` and broadcasts snapshots; `MatchClient` owns a `MatchState` it **never steps**, writing snapshots straight into it. `Server.exe` is a headless `ConsoleApp` on the `MapGen` precedent. `Sandbox` gains `--connect` and holds an optional `MatchClient`.

**Tech Stack:** C++20, MSVC (Visual Studio 18 / vs2026), premake5, doctest, GLM, and one new vendored dependency: ENet.

**Spec:** `docs/superpowers/specs/2026-08-31-networking-stage-2-design.md` (read it; the arc-level spec it builds on is `docs/superpowers/specs/2026-08-27-networking-design.md`)

## Global Constraints

- **C++20**, `cppdialect "C++20"` in `premake5.lua`. `std::span` and `std::bit_cast` are available.
- **`Cubit/src/Voxel/`, `Cubit/include/Cubit/Voxel/`, `Cubit/src/Net/` and `Cubit/include/Cubit/Net/` must stay GL-free.** No `glad`, `GLFW`, or `gl*`. This is what lets the simulation and the server run headless.
- **Any exported class (`CB_API`) with a `std::` or `glm::` member needs the 4251 pragma guard**, matching `World.h`:
  ```cpp
  #ifdef _MSC_VER
  #pragma warning(push)
  #pragma warning(disable: 4251)
  #endif
  // ... declarations ...
  #ifdef _MSC_VER
  #pragma warning(pop)
  #endif
  ```
- **`CB_API` is `dllexport`. An exported class must define every member it declares.** Never declare a member and leave its body for a later commit — that is LNK2019, not a TODO.
- **Every new `.cpp` under `Cubit/src/` must `#include "cub.h"` as its first line.** The project uses a precompiled header; the build fails otherwise.
- **Premake globs expand at generation time.** After adding any new file, run `/c/dev/premake/premake5 vs2026` from the repo root. Do **not** run `GenerateProjects.bat` — it deletes `bin/` and ends in `pause`, which hangs a non-interactive shell.
- **Tests include `<doctest.h>`**, not `<doctest/doctest.h>`.
- **Build command** (repo root):
  ```bash
  MSB="/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe"
  "$MSB" Cubit.slnx -p:Configuration=Debug -p:Platform=x64 -m -v:minimal -nologo
  ```
  The suite runs as a post-build step, so a failing test fails the build.
- **`NetworkSim::Latency` is ONE-WAY seconds. The `--latency` command-line flag is ROUND-TRIP milliseconds** and is halved on the way in. Two units for one idea invites exactly one bug; this is where it is pinned.
- **All test latencies must be whole tick multiples.** One tick is `1.0 / 60.0` s. The canonical test network is 50 ms one-way (exactly 3 ticks), 100 ms RTT, 5% loss, seed 1.
- **Wire fields are fixed-width little-endian.** No varints, no bit packing, no compression.
- **Never add Claude co-author trailers or attribution to commits.**
- **Single-player must not change.** The acceptance check is `Sandbox.exe` with no flags reporting `POS 240.500000 26.900099 300.500000` and `FACES 1927774`, identical to before this plan.

---

## File Structure

| File | Responsibility |
|---|---|
| `Cubit/include/Cubit/Voxel/MatchState.h` `Cubit/src/Voxel/MatchState.cpp` | **Modify.** Chosen-id `AddPlayer`, roster enumeration, `SetTick`. |
| `Cubit/include/Cubit/Voxel/CharacterController.h` `.cpp` | **Modify.** `SetState`. |
| `Tests/src/MatchStateTests.cpp` | **Modify.** Cover the four new members. |
| `vendor/ENet/**` | **Create.** Vendored C library. |
| `premake5.lua` | **Modify.** ENet StaticLib project; Cubit links it; new `Server` project. |
| `Cubit/include/Cubit/Net/ByteWriter.h` `ByteReader.h` | **Create.** Header-only little-endian codec. Header-only so nothing is exported and `Ok()` can inline. |
| `Tests/src/ByteCodecTests.cpp` | **Create.** Round-trip and fail-safe truncation. |
| `Cubit/include/Cubit/Net/Protocol.h` `Cubit/src/Net/Protocol.cpp` | **Create.** Six message structs, `Encode`, `Decode`, `PeekMessageId`. |
| `Tests/src/ProtocolTests.cpp` | **Create.** Round-trip every message; truncate every message at every length. |
| `Cubit/include/Cubit/Net/Transport.h` | **Create.** `PeerId`, `Channel`, `NetEvent`, the pure interface. No `CB_API`. |
| `Cubit/include/Cubit/Net/LoopbackTransport.h` `.cpp` | **Create.** In-process delivery with no latency and no loss. |
| `Tests/src/LoopbackTransportTests.cpp` | **Create.** Connect, send, broadcast, disconnect. |
| `Cubit/include/Cubit/Net/SimulatedTransport.h` `.cpp` | **Create.** Outbound-only latency/jitter/loss decorator. |
| `Tests/src/SimulatedTransportTests.cpp` | **Create.** Delay, loss, reliable re-queue, determinism. |
| `Cubit/include/Cubit/Net/MapHash.h` `.cpp` | **Create.** FNV-1a 64 over bytes and over a file. |
| `Tests/src/MapHashTests.cpp` | **Create.** |
| `Cubit/include/Cubit/Net/MatchServer.h` `.cpp` | **Create.** Authoritative match; join, input, snapshot, then edits. |
| `Cubit/include/Cubit/Net/MatchClient.h` `.cpp` | **Create.** Never steps. |
| `Tests/src/MatchServerTests.cpp` | **Create.** Join, roster, input staleness. |
| `Tests/src/WireOracleTests.cpp` | **Create.** The stage's oracle, plus edits, late join, convergence. |
| `Cubit/include/Cubit/Net/EnetTransport.h` `.cpp` | **Create.** Real sockets. |
| `Tests/src/EnetTransportTests.cpp` | **Create.** One localhost test. |
| `Server/src/Server.cpp` | **Create.** Headless `main()`. |
| `Sandbox/src/Sandbox.cpp` | **Modify.** `--connect`, three branch points, remote boxes. |
| `Sandbox/src/HudLayer.h` | **Modify.** RTT, tick skew, packets/sec. |
| `Cubit/include/Cubit/Cubit.h` | **Modify.** Export the new headers. |
| `docs/superpowers/specs/2026-08-31-networking-stage-2-design.md`, `docs/engine-roadmap.md` | **Modify.** Record shipped. |

---

## Task Order and Why

ENet is vendored **second**, before any code needs it. It is the one step in this plan that cannot be unit-tested into submission — it either builds and links on this toolchain or it does not — so it is discovered on day one rather than after eleven tasks of work that assume it.

Tasks 1 and 3–7 have no networking in them at all and are individually provable. `MatchServer` and `MatchClient` come only once everything they stand on is tested.

---

### Task 1: The four `MatchState` gaps

Recorded at the end of Stage 1. Items 1–2 bite on the server's first day, 3–4 on reconciliation's. They land before any socket exists because retrofitting them around a live transport is strictly worse.

**Files:**
- Modify: `Cubit/include/Cubit/Voxel/MatchState.h`, `Cubit/src/Voxel/MatchState.cpp`
- Modify: `Cubit/include/Cubit/Voxel/CharacterController.h`, `Cubit/src/Voxel/CharacterController.cpp`
- Modify: `Tests/src/MatchStateTests.cpp`, `Tests/src/CharacterControllerTests.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `PlayerId MatchState::AddPlayer(PlayerId id, const glm::vec3& spawn)` — throws `std::invalid_argument` on `InvalidPlayer` or an id already present; returns `id`.
  - `const std::map<PlayerId, CharacterController>& MatchState::Players() const`
  - `void MatchState::SetTick(std::uint64_t tick)`
  - `void CharacterController::SetState(const glm::vec3& position, const glm::vec3& previousPosition, float verticalVelocity, bool grounded)`

- [ ] **Step 1: Write the failing tests**

Append to `Tests/src/MatchStateTests.cpp`:

```cpp
TEST_CASE("A player can be added under a chosen id")
{
    MatchState match(FlatWorld());

    const PlayerId chosen = match.AddPlayer(PlayerId{ 7 }, glm::vec3(4.0f, 10.0f, 5.0f));

    CHECK(chosen == PlayerId{ 7 });
    CHECK(match.HasPlayer(PlayerId{ 7 }));
    CHECK(match.Player(PlayerId{ 7 }).Position() == glm::vec3(4.0f, 10.0f, 5.0f));
}

TEST_CASE("A chosen id is refused when it is taken or invalid")
{
    MatchState match(FlatWorld());
    match.AddPlayer(PlayerId{ 7 }, glm::vec3(0.0f));

    CHECK_THROWS_AS(match.AddPlayer(PlayerId{ 7 }, glm::vec3(0.0f)), std::invalid_argument);
    CHECK_THROWS_AS(match.AddPlayer(InvalidPlayer, glm::vec3(0.0f)), std::invalid_argument);
}

TEST_CASE("Auto-minted ids never collide with a chosen one")
{
    //The bug this prevents: a server hands a client id 7, then later mints its
    //own and hands out 7 again. Two players, one id, and the roster silently
    //loses one of them.
    MatchState match(FlatWorld());

    match.AddPlayer(PlayerId{ 7 }, glm::vec3(0.0f));
    const PlayerId minted = match.AddPlayer(glm::vec3(0.0f));

    CHECK(minted > PlayerId{ 7 });
    CHECK(match.Players().size() == 2);
}

TEST_CASE("The roster can be enumerated in id order")
{
    MatchState match(FlatWorld());
    match.AddPlayer(PlayerId{ 9 }, glm::vec3(0.0f));
    match.AddPlayer(PlayerId{ 2 }, glm::vec3(0.0f));

    std::vector<PlayerId> ids;
    for (const auto& [id, character] : match.Players())
    {
        (void)character;
        ids.push_back(id);
    }

    CHECK(ids == std::vector<PlayerId>{ PlayerId{ 2 }, PlayerId{ 9 } });
}

TEST_CASE("The tick can be set, so a client can align to a server")
{
    MatchState match(FlatWorld());

    match.SetTick(1234);

    CHECK(match.Tick() == 1234);
}
```

Add `#include <stdexcept>` and `#include <vector>` to the test file's includes.

Append to `Tests/src/CharacterControllerTests.cpp`:

```cpp
TEST_CASE("SetState restores every field a step depends on")
{
    //Teleport is wrong for this job twice over: it flattens PreviousPosition,
    //destroying the interpolation a correction is supposed to hide, and it
    //cannot restore Grounded, which Step reads from the previous step when
    //deciding whether a jump fires.
    CharacterController character;

    character.SetState(
        glm::vec3(1.0f, 2.0f, 3.0f), glm::vec3(1.0f, 1.0f, 3.0f), -4.5f, true);

    CHECK(character.Position() == glm::vec3(1.0f, 2.0f, 3.0f));
    CHECK(character.PreviousPosition() == glm::vec3(1.0f, 1.0f, 3.0f));
    CHECK(character.VerticalVelocity() == doctest::Approx(-4.5f));
    CHECK(character.Grounded());
}

TEST_CASE("SetState preserves the gap Teleport destroys")
{
    CharacterController character;

    character.SetState(glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(0.0f, 4.0f, 0.0f), 0.0f, false);
    const glm::vec3 halfway = character.InterpolatedPosition(0.5f);

    CHECK(halfway.y == doctest::Approx(4.5f));
}
```

- [ ] **Step 2: Run the tests and watch them fail**

```bash
MSB="/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe"
"$MSB" Cubit.slnx -p:Configuration=Debug -p:Platform=x64 -m -v:minimal -nologo
```
Expected: compile errors — no such member `AddPlayer(PlayerId, ...)`, `Players`, `SetTick`, `SetState`.

- [ ] **Step 3: Add `SetState` to `CharacterController`**

In `Cubit/include/Cubit/Voxel/CharacterController.h`, directly below the `Teleport` declaration:

```cpp
    //Overwrites everything a step reads from the step before it.
    //
    //Unlike Teleport this keeps the two positions independent, because a
    //correction that collapsed them would erase exactly the interpolation it
    //is meant to hide. Grounded is included because Step consults the previous
    //step's value when deciding whether a jump fires, so a replayed jump on
    //the first tick after a correction diverges without it.
    void SetState(const glm::vec3& position, const glm::vec3& previousPosition,
        float verticalVelocity, bool grounded);
```

In `Cubit/src/Voxel/CharacterController.cpp`:

```cpp
void CharacterController::SetState(const glm::vec3& position,
    const glm::vec3& previousPosition, float verticalVelocity, bool grounded)
{
    m_Position = position;
    m_PreviousPosition = previousPosition;
    m_VerticalVelocity = verticalVelocity;
    m_Grounded = grounded;
}
```

`m_BodyInFluid` and `m_EyeInFluid` are deliberately not restored: they are recomputed by the next `Step` from the world, and a snapshot that carried them would be sending derivable state over the wire.

- [ ] **Step 4: Add the three `MatchState` members**

In `Cubit/include/Cubit/Voxel/MatchState.h`, below the existing `AddPlayer`:

```cpp
    //Adds a player under an id chosen by someone else - the server, over a
    //wire. Returns the id it was given.
    //
    //Throws when the id is InvalidPlayer or already present: silently
    //replacing a player would drop whoever held that id, and silently
    //ignoring the call would leave the caller believing in a player that does
    //not exist.
    PlayerId AddPlayer(PlayerId id, const glm::vec3& spawn);
```

Below `Player(PlayerId)`:

```cpp
    //Everyone in the match, in id order.
    //
    //Returns the ordered container itself rather than a copied list of ids.
    //MatchState's reproducibility depends on iteration order, and handing back
    //the ordered map makes that promise visible to callers instead of hiding
    //it behind a copy.
    const std::map<PlayerId, CharacterController>& Players() const { return m_Players; }
```

Below `Tick()`:

```cpp
    //Aligns this match's clock to somebody else's - a client adopting the
    //server's tick from a snapshot.
    void SetTick(std::uint64_t tick) { m_Tick = tick; }
```

In `Cubit/src/Voxel/MatchState.cpp`:

```cpp
PlayerId MatchState::AddPlayer(PlayerId id, const glm::vec3& spawn)
{
    if (id == InvalidPlayer)
        throw std::invalid_argument("AddPlayer: InvalidPlayer names nobody");

    if (m_Players.contains(id))
        throw std::invalid_argument("AddPlayer: id already present");

    CharacterController character;
    character.Teleport(spawn);
    m_Players.emplace(id, character);

    //Keeps the auto-minting overload from ever handing out an id that arrived
    //from outside. Without this a server that accepts id 7 and later mints one
    //itself produces two players sharing an id, and the map keeps one.
    if (id >= m_NextPlayer)
        m_NextPlayer = static_cast<PlayerId>(id + 1);

    return id;
}
```

Confirm `<stdexcept>` is included — it already is, for `Player`'s throw.

- [ ] **Step 5: Run the tests and watch them pass**

```bash
"$MSB" Cubit.slnx -p:Configuration=Debug -p:Platform=x64 -m -v:minimal -nologo
```
Expected: PASS, with seven new test cases green and the pre-existing count unchanged. Record the suite's reported totals before and after — Task 13 needs the real figure, and the spec's "about 360" is an estimate to correct rather than a target to hit.

- [ ] **Step 6: Prove the collision test is not tautological**

Temporarily delete the `if (id >= m_NextPlayer)` block, rebuild, and confirm **"Auto-minted ids never collide with a chosen one"** goes red. Restore it. Stage 1's first determinism test was near-tautological and only caught because someone tried to falsify it; every new invariant here gets the same treatment.

- [ ] **Step 7: Commit**

```bash
git add Cubit/include/Cubit/Voxel/MatchState.h Cubit/src/Voxel/MatchState.cpp \
        Cubit/include/Cubit/Voxel/CharacterController.h Cubit/src/Voxel/CharacterController.cpp \
        Tests/src/MatchStateTests.cpp Tests/src/CharacterControllerTests.cpp
git commit -m "Give a match the API a wire needs"
```

---

### Task 2: Vendor ENet

Done now, not when `EnetTransport` needs it. This is the one step that cannot be unit-tested — it links or it does not — so it fails early or not at all. Nothing uses ENet at the end of this task; the point is a green build with it present.

**Files:**
- Create: `vendor/ENet/**` (upstream source)
- Modify: `premake5.lua`

**Interfaces:**
- Consumes: nothing.
- Produces: an `ENet` StaticLib project that `Cubit` links, and `#include <enet/enet.h>` resolving from `Cubit/src/`.

- [ ] **Step 1: Fetch ENet 1.3.18 into `vendor/ENet`**

```bash
cd /c/dev/Cubit
git clone --depth 1 --branch v1.3.18 https://github.com/lsalzman/enet.git vendor/ENet
rm -rf vendor/ENet/.git
ls vendor/ENet/include/enet/enet.h vendor/ENet/callbacks.c
```
Expected: both paths exist. ENet is plain C99 with no build-system dependency — the `.c` files at the repo root are the entire library.

- [ ] **Step 2: Add the premake project**

In `premake5.lua`, inside `group "Dependencies"`, after the `GLFW` project:

```lua
project "ENet"
    location "vendor/ENet"
    kind "StaticLib"
    language "C"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

    files
    {
        "vendor/ENet/include/**.h",
        "vendor/ENet/callbacks.c",
        "vendor/ENet/compress.c",
        "vendor/ENet/host.c",
        "vendor/ENet/list.c",
        "vendor/ENet/packet.c",
        "vendor/ENet/peer.c",
        "vendor/ENet/protocol.c",
        "vendor/ENet/win32.c"
    }

    includedirs
    {
        "vendor/ENet/include"
    }

    defines
    {
        --ENet's Windows backend. Without it the unix.c path is selected and
        --nothing links.
        "WIN32",
        "_CRT_SECURE_NO_WARNINGS",
        "_WINSOCK_DEPRECATED_NO_WARNINGS"
    }

    filter "system:windows"
        systemversion "latest"

    filter "configurations:Debug"
        runtime "Debug"
        symbols "On"

    filter "configurations:Release"
        runtime "Release"
        optimize "On"

    filter "configurations:Dist"
        runtime "Release"
        optimize "On"
```

The file list is explicit rather than a `**.c` glob on purpose: `unix.c` sits beside `win32.c` in the same directory and compiling both is a duplicate-symbol error.

- [ ] **Step 3: Link ENet into Cubit**

In the `Cubit` project, extend `includedirs` with `"vendor/ENet/include"` and `links` with `"ENet"`, `"ws2_32"`, `"winmm"`. ENet's Windows backend needs both system libraries; omitting them is an unresolved-external at link time, not a compile error.

- [ ] **Step 4: Prove it builds and links**

Create a throwaway `Cubit/src/Net/EnetSmoke.cpp`:

```cpp
#include "cub.h"

#include <enet/enet.h>

//Temporary: proves the vendored library compiles and links before anything
//depends on it. Deleted at the end of this task.
namespace
{
    const int g_EnetInitialised = enet_initialize();
}
```

Then:
```bash
/c/dev/premake/premake5 vs2026
MSB="/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe"
"$MSB" Cubit.slnx -p:Configuration=Debug -p:Platform=x64 -m -v:minimal -nologo
```
Expected: a clean build. A failure here is the whole point of doing this task second — resolve it now.

- [ ] **Step 5: Delete the smoke file and regenerate**

```bash
rm Cubit/src/Net/EnetSmoke.cpp
/c/dev/premake/premake5 vs2026
"$MSB" Cubit.slnx -p:Configuration=Debug -p:Platform=x64 -m -v:minimal -nologo
```
Expected: still clean, and the suite still green at its pre-existing count.

- [ ] **Step 6: Commit**

```bash
git add vendor/ENet premake5.lua
git commit -m "Vendor ENet"
```

---

### Task 3: The byte codec

Header-only and unexported. It is small, it is on the hot path, and keeping it out of the DLL boundary sidesteps the "an exported class must define every member" trap entirely.

**Files:**
- Create: `Cubit/include/Cubit/Net/ByteWriter.h`, `Cubit/include/Cubit/Net/ByteReader.h`
- Create: `Tests/src/ByteCodecTests.cpp`
- Modify: `Cubit/include/Cubit/Cubit.h`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `ByteWriter` with `U8/U16/U32/U64/F32/Vec3/IVec3/String/Bool` and `const std::vector<std::uint8_t>& Bytes() const`
  - `ByteReader(std::span<const std::uint8_t>)` with the same readers, plus `bool Ok() const` and `std::size_t Remaining() const`

- [ ] **Step 1: Write the failing test**

Create `Tests/src/ByteCodecTests.cpp`:

```cpp
#include <doctest.h>

#include "Cubit/Net/ByteReader.h"
#include "Cubit/Net/ByteWriter.h"

#include <glm/glm.hpp>
#include <string>

TEST_CASE("Every field type survives a round trip")
{
    ByteWriter writer;
    writer.U8(0xAB);
    writer.U16(0x1234);
    writer.U32(0xDEADBEEF);
    writer.U64(0x0123456789ABCDEFull);
    writer.F32(-4.25f);
    writer.Vec3(glm::vec3(1.5f, -2.5f, 3.5f));
    writer.IVec3(glm::ivec3(-7, 8, -9));
    writer.String("battlefield512.vox");
    writer.Bool(true);

    ByteReader reader(writer.Bytes());
    CHECK(reader.U8() == 0xAB);
    CHECK(reader.U16() == 0x1234);
    CHECK(reader.U32() == 0xDEADBEEF);
    CHECK(reader.U64() == 0x0123456789ABCDEFull);
    CHECK(reader.F32() == doctest::Approx(-4.25f));
    CHECK(reader.Vec3() == glm::vec3(1.5f, -2.5f, 3.5f));
    CHECK(reader.IVec3() == glm::ivec3(-7, 8, -9));
    CHECK(reader.String() == "battlefield512.vox");
    CHECK(reader.Bool());
    CHECK(reader.Ok());
    CHECK(reader.Remaining() == 0);
}

TEST_CASE("Bytes are little-endian regardless of the host")
{
    //Pinned rather than assumed. A codec that happens to match the host today
    //is a codec that breaks the day anything reads it elsewhere, and the bug
    //looks like corrupt positions rather than a byte-order fault.
    ByteWriter writer;
    writer.U32(0x01020304);

    const std::vector<std::uint8_t>& bytes = writer.Bytes();
    REQUIRE(bytes.size() == 4);
    CHECK(bytes[0] == 0x04);
    CHECK(bytes[1] == 0x03);
    CHECK(bytes[2] == 0x02);
    CHECK(bytes[3] == 0x01);
}

TEST_CASE("Reading past the end fails safe instead of reading rubbish")
{
    const std::uint8_t bytes[] = { 0x01, 0x02 };
    ByteReader reader(bytes);

    CHECK(reader.U8() == 0x01);
    CHECK(reader.Ok());

    //Two bytes remain-1; a U32 cannot be satisfied.
    CHECK(reader.U32() == 0);
    CHECK_FALSE(reader.Ok());

    //Once broken it stays broken, so a caller that checks Ok() once at the end
    //cannot be fooled by a later read that happens to fit.
    CHECK(reader.U8() == 0);
    CHECK_FALSE(reader.Ok());
}

TEST_CASE("A string length longer than the buffer is refused")
{
    //The classic hostile packet: a small buffer claiming a huge payload. It
    //must not allocate and must not read.
    ByteWriter writer;
    writer.U16(60000);
    writer.U8('x');

    ByteReader reader(writer.Bytes());
    CHECK(reader.String().empty());
    CHECK_FALSE(reader.Ok());
}
```

- [ ] **Step 2: Run and watch it fail**

```bash
"$MSB" Cubit.slnx -p:Configuration=Debug -p:Platform=x64 -m -v:minimal -nologo
```
Expected: `Cannot open include file: 'Cubit/Net/ByteWriter.h'`.

- [ ] **Step 3: Write `ByteWriter.h`**

Create `Cubit/include/Cubit/Net/ByteWriter.h`:

```cpp
#pragma once

#include <glm/glm.hpp>

#include <bit>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

//Builds a little-endian byte buffer for the wire.
//
//Header-only and unexported on purpose: it is small enough to inline, it is on
//the per-tick path, and keeping it off the DLL boundary avoids the rule that an
//exported class must define every member it declares.
//
//Fixed-width, little-endian, no varints and no bit packing. There is no
//bandwidth problem to solve here, and a hand-rolled codec's bugs should be as
//boring as possible to find.
class ByteWriter
{
public:
    void U8(std::uint8_t value) { m_Bytes.push_back(value); }

    void U16(std::uint16_t value)
    {
        m_Bytes.push_back(static_cast<std::uint8_t>(value));
        m_Bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    }

    void U32(std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
            m_Bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }

    void U64(std::uint64_t value)
    {
        for (int shift = 0; shift < 64; shift += 8)
            m_Bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }

    void I32(std::int32_t value) { U32(static_cast<std::uint32_t>(value)); }

    //Written as its IEEE-754 bit pattern rather than memcpy'd, so the byte
    //order is the one this class promises rather than the one the host uses.
    void F32(float value) { U32(std::bit_cast<std::uint32_t>(value)); }

    void Bool(bool value) { U8(value ? 1u : 0u); }

    void Vec3(const glm::vec3& value)
    {
        F32(value.x);
        F32(value.y);
        F32(value.z);
    }

    void IVec3(const glm::ivec3& value)
    {
        I32(value.x);
        I32(value.y);
        I32(value.z);
    }

    //A u16 length followed by the bytes. Longer than 65535 is a programming
    //error rather than a wire condition, so it truncates rather than throwing
    //into the middle of a send.
    void String(std::string_view value)
    {
        const std::size_t length =
            value.size() > 0xFFFFu ? std::size_t{ 0xFFFFu } : value.size();

        U16(static_cast<std::uint16_t>(length));
        m_Bytes.insert(m_Bytes.end(), value.begin(), value.begin() + length);
    }

    const std::vector<std::uint8_t>& Bytes() const { return m_Bytes; }

    std::span<const std::uint8_t> Span() const { return m_Bytes; }

private:
    std::vector<std::uint8_t> m_Bytes;
};
```

- [ ] **Step 4: Write `ByteReader.h`**

Create `Cubit/include/Cubit/Net/ByteReader.h`:

```cpp
#pragma once

#include <glm/glm.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

//Reads a little-endian byte buffer produced by ByteWriter.
//
//Fails safe rather than throwing or reading past the end. This is the first
//data in the project that arrives from a socket: a truncated or hostile packet
//is a routine condition, not a bug in the caller, so every reader returns a
//zero value and latches Ok() to false.
//
//Ok() is sticky. Once a read has failed it stays failed, so a caller that
//checks once after decoding a whole message cannot be fooled by a later small
//read that happens to fit inside the remaining bytes.
class ByteReader
{
public:
    explicit ByteReader(std::span<const std::uint8_t> bytes)
        : m_Bytes(bytes) {}

    std::uint8_t U8()
    {
        if (!Take(1)) return 0;
        return m_Bytes[m_Offset - 1];
    }

    std::uint16_t U16()
    {
        if (!Take(2)) return 0;
        return static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(m_Bytes[m_Offset - 2]) |
            static_cast<std::uint16_t>(m_Bytes[m_Offset - 1]) << 8);
    }

    std::uint32_t U32()
    {
        if (!Take(4)) return 0;
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i)
            value |= static_cast<std::uint32_t>(m_Bytes[m_Offset - 4 + i]) << (i * 8);
        return value;
    }

    std::uint64_t U64()
    {
        if (!Take(8)) return 0;
        std::uint64_t value = 0;
        for (int i = 0; i < 8; ++i)
            value |= static_cast<std::uint64_t>(m_Bytes[m_Offset - 8 + i]) << (i * 8);
        return value;
    }

    std::int32_t I32() { return static_cast<std::int32_t>(U32()); }

    float F32() { return std::bit_cast<float>(U32()); }

    bool Bool() { return U8() != 0; }

    glm::vec3 Vec3()
    {
        const float x = F32();
        const float y = F32();
        const float z = F32();
        return glm::vec3(x, y, z);
    }

    glm::ivec3 IVec3()
    {
        const std::int32_t x = I32();
        const std::int32_t y = I32();
        const std::int32_t z = I32();
        return glm::ivec3(x, y, z);
    }

    //Refuses a length the buffer cannot satisfy without allocating for it.
    //A small packet claiming a huge string is the most obvious hostile input
    //there is, and reserving on its word is how that becomes a denial of
    //service rather than a rejected packet.
    std::string String()
    {
        const std::uint16_t length = U16();
        if (!m_Ok || length > Remaining())
        {
            m_Ok = false;
            return {};
        }

        const char* begin = reinterpret_cast<const char*>(m_Bytes.data() + m_Offset);
        m_Offset += length;
        return std::string(begin, begin + length);
    }

    bool Ok() const { return m_Ok; }

    std::size_t Remaining() const
    {
        return m_Ok ? m_Bytes.size() - m_Offset : 0;
    }

private:
    //Advances by `count` when the buffer allows it, latching failure when not.
    bool Take(std::size_t count)
    {
        if (!m_Ok || m_Bytes.size() - m_Offset < count)
        {
            m_Ok = false;
            return false;
        }

        m_Offset += count;
        return true;
    }

    std::span<const std::uint8_t> m_Bytes;
    std::size_t m_Offset = 0;
    bool m_Ok = true;
};
```

- [ ] **Step 5: Export the headers**

In `Cubit/include/Cubit/Cubit.h`, after the `Voxel/` block:

```cpp
#include "Cubit/Net/ByteReader.h"
#include "Cubit/Net/ByteWriter.h"
```

- [ ] **Step 6: Regenerate, build, and watch the tests pass**

```bash
/c/dev/premake/premake5 vs2026
"$MSB" Cubit.slnx -p:Configuration=Debug -p:Platform=x64 -m -v:minimal -nologo
```
Expected: PASS.

- [ ] **Step 7: Prove the fail-safe test is not tautological**

Temporarily change `Take` to `m_Offset += count; return true;` with no bounds check. Rebuild. **"Reading past the end fails safe"** must go red. Restore.

- [ ] **Step 8: Commit**

```bash
git add Cubit/include/Cubit/Net/ByteWriter.h Cubit/include/Cubit/Net/ByteReader.h \
        Tests/src/ByteCodecTests.cpp Cubit/include/Cubit/Cubit.h
git commit -m "Read and write bytes that arrive from a socket"
```

---

### Task 4: The protocol

Six messages and no more. There are deliberately no join or leave messages: the snapshot carries the full roster every tick and ids are never reused, so a client derives both by diffing.

**Files:**
- Create: `Cubit/include/Cubit/Net/Protocol.h`, `Cubit/src/Net/Protocol.cpp`
- Create: `Tests/src/ProtocolTests.cpp`
- Modify: `Cubit/include/Cubit/Cubit.h`

**Interfaces:**
- Consumes: `ByteWriter`, `ByteReader` (Task 3); `PlayerId`, `InvalidPlayer`, `CharacterInput`, `BlockEdit`.
- Produces:
  - `enum class MessageId : std::uint8_t { Hello = 1, Welcome, Input, Snapshot, EditRequest, EditApplied }`
  - `constexpr std::uint32_t ProtocolVersion = 1`
  - `struct HelloMessage`, `WelcomeMessage`, `InputMessage`, `PlayerSnapshot`, `SnapshotMessage`, `EditMessage`
  - `std::vector<std::uint8_t> Encode(const T&)` for each of the five sent types
  - `bool Decode(std::span<const std::uint8_t>, T& out)` for each
  - `bool PeekMessageId(std::span<const std::uint8_t>, MessageId& out)`

- [ ] **Step 1: Write the failing test**

Create `Tests/src/ProtocolTests.cpp`:

```cpp
#include <doctest.h>

#include "Cubit/Net/Protocol.h"

#include <glm/glm.hpp>
#include <vector>

namespace
{
    SnapshotMessage TwoPlayerSnapshot()
    {
        SnapshotMessage snapshot;
        snapshot.Tick = 4242;

        PlayerSnapshot first;
        first.Player = PlayerId{ 1 };
        first.Position = glm::vec3(240.5f, 26.9f, 300.5f);
        first.Yaw = -135.0f;
        first.Pitch = 12.5f;
        first.VerticalVelocity = -3.25f;
        first.Grounded = true;

        PlayerSnapshot second;
        second.Player = PlayerId{ 2 };
        second.Position = glm::vec3(-1.0f, 0.0f, 7.5f);
        second.Yaw = 90.0f;
        second.Pitch = -45.0f;
        second.VerticalVelocity = 0.0f;
        second.Grounded = false;

        snapshot.Players = { first, second };
        return snapshot;
    }
}

TEST_CASE("Hello round-trips and carries the protocol version")
{
    HelloMessage sent;
    HelloMessage received;

    REQUIRE(Decode(Encode(sent), received));
    CHECK(received.Version == ProtocolVersion);
}

TEST_CASE("Welcome round-trips, edit log and all")
{
    WelcomeMessage sent;
    sent.You = PlayerId{ 7 };
    sent.MapName = "battlefield512.vox";
    sent.MapHash = 0x0123456789ABCDEFull;
    sent.Tick = 900;
    sent.Edits = {
        BlockEdit{ glm::ivec3(1, 2, 3), BlockId{ 0 } },
        BlockEdit{ glm::ivec3(-4, 5, -6), BlockId{ 9 } }
    };

    WelcomeMessage received;
    REQUIRE(Decode(Encode(sent), received));

    CHECK(received.You == PlayerId{ 7 });
    CHECK(received.MapName == "battlefield512.vox");
    CHECK(received.MapHash == 0x0123456789ABCDEFull);
    CHECK(received.Tick == 900);
    REQUIRE(received.Edits.size() == 2);
    CHECK(received.Edits[0].Position == glm::ivec3(1, 2, 3));
    CHECK(received.Edits[1].Block == BlockId{ 9 });
}

TEST_CASE("Input round-trips a sequence and the character's intent")
{
    InputMessage sent;
    sent.Sequence = 123456;
    sent.Input.Move = glm::vec2(-1.0f, 1.0f);
    sent.Input.Yaw = -135.0f;
    sent.Input.Pitch = 30.0f;
    sent.Input.Jump = true;

    InputMessage received;
    REQUIRE(Decode(Encode(sent), received));

    CHECK(received.Sequence == 123456);
    CHECK(received.Input.Move == glm::vec2(-1.0f, 1.0f));
    CHECK(received.Input.Yaw == doctest::Approx(-135.0f));
    CHECK(received.Input.Pitch == doctest::Approx(30.0f));
    CHECK(received.Input.Jump);
}

TEST_CASE("Snapshot round-trips every player")
{
    const SnapshotMessage sent = TwoPlayerSnapshot();

    SnapshotMessage received;
    REQUIRE(Decode(Encode(sent), received));

    CHECK(received.Tick == 4242);
    REQUIRE(received.Players.size() == 2);
    CHECK(received.Players[0].Player == PlayerId{ 1 });
    CHECK(received.Players[0].Position == glm::vec3(240.5f, 26.9f, 300.5f));
    CHECK(received.Players[0].Grounded);
    CHECK(received.Players[1].Player == PlayerId{ 2 });
    CHECK(received.Players[1].Yaw == doctest::Approx(90.0f));
    CHECK_FALSE(received.Players[1].Grounded);
}

TEST_CASE("An empty roster is a legal snapshot")
{
    //A server with no clients still ticks and still broadcasts. A decoder that
    //assumes at least one player turns an idle server into a parse failure.
    SnapshotMessage sent;
    sent.Tick = 5;

    SnapshotMessage received;
    REQUIRE(Decode(Encode(sent), received));

    CHECK(received.Tick == 5);
    CHECK(received.Players.empty());
}

TEST_CASE("Edit messages round-trip and keep their own identity")
{
    EditMessage sent;
    sent.Edit = BlockEdit{ glm::ivec3(300, 40, -12), BlockId{ 3 } };

    std::vector<std::uint8_t> request = EncodeEditRequest(sent);
    std::vector<std::uint8_t> applied = EncodeEditApplied(sent);

    MessageId id = MessageId::Hello;
    REQUIRE(PeekMessageId(request, id));
    CHECK(id == MessageId::EditRequest);
    REQUIRE(PeekMessageId(applied, id));
    CHECK(id == MessageId::EditApplied);

    EditMessage received;
    REQUIRE(Decode(request, received));
    CHECK(received.Edit.Position == glm::ivec3(300, 40, -12));
    CHECK(received.Edit.Block == BlockId{ 3 });
}

TEST_CASE("A message of the wrong type is refused")
{
    HelloMessage hello;
    SnapshotMessage snapshot;

    CHECK_FALSE(Decode(Encode(hello), snapshot));
}

TEST_CASE("Every message truncated at every length is refused without crashing")
{
    //The stage's hostile-input sweep. This is the first data in the project's
    //history that arrives from a socket, so a short read is a routine wire
    //condition and must produce `false`, never a crash and never a half-filled
    //output the caller might act on.
    std::vector<std::vector<std::uint8_t>> messages;
    {
        HelloMessage hello;
        messages.push_back(Encode(hello));

        WelcomeMessage welcome;
        welcome.You = PlayerId{ 3 };
        welcome.MapName = "map.vox";
        welcome.Edits = { BlockEdit{ glm::ivec3(1, 1, 1), BlockId{ 2 } } };
        messages.push_back(Encode(welcome));

        InputMessage input;
        input.Sequence = 9;
        messages.push_back(Encode(input));

        messages.push_back(Encode(TwoPlayerSnapshot()));

        EditMessage edit;
        edit.Edit = BlockEdit{ glm::ivec3(2, 2, 2), BlockId{ 1 } };
        messages.push_back(EncodeEditRequest(edit));
    }

    for (const std::vector<std::uint8_t>& whole : messages)
    {
        for (std::size_t length = 0; length < whole.size(); ++length)
        {
            const std::span<const std::uint8_t> truncated(whole.data(), length);

            MessageId id = MessageId::Hello;
            if (!PeekMessageId(truncated, id))
                continue;

            HelloMessage hello;
            WelcomeMessage welcome;
            InputMessage input;
            SnapshotMessage snapshot;
            EditMessage edit;

            //Whichever decoder matches the id must refuse; the rest refuse on
            //the id alone. Either way nothing throws and nothing is trusted.
            switch (id)
            {
            case MessageId::Hello:       CHECK_FALSE(Decode(truncated, hello)); break;
            case MessageId::Welcome:     CHECK_FALSE(Decode(truncated, welcome)); break;
            case MessageId::Input:       CHECK_FALSE(Decode(truncated, input)); break;
            case MessageId::Snapshot:    CHECK_FALSE(Decode(truncated, snapshot)); break;
            case MessageId::EditRequest:
            case MessageId::EditApplied: CHECK_FALSE(Decode(truncated, edit)); break;
            }
        }
    }
}

TEST_CASE("A snapshot claiming more players than it carries is refused")
{
    //The counterpart to the string-length attack: a tiny packet claiming a
    //huge roster. Refusing on the count alone, before reserving for it, is
    //what stops this being a denial of service.
    ByteWriter writer;
    writer.U8(static_cast<std::uint8_t>(MessageId::Snapshot));
    writer.U64(1);
    writer.U16(60000);

    SnapshotMessage received;
    CHECK_FALSE(Decode(writer.Span(), received));
}
```

- [ ] **Step 2: Run and watch it fail**

```bash
"$MSB" Cubit.slnx -p:Configuration=Debug -p:Platform=x64 -m -v:minimal -nologo
```
Expected: `Cannot open include file: 'Cubit/Net/Protocol.h'`.

- [ ] **Step 3: Write `Protocol.h`**

Create `Cubit/include/Cubit/Net/Protocol.h`:

```cpp
#pragma once

#include "Cubit/Core.h"
#include "Cubit/Net/ByteReader.h"
#include "Cubit/Net/ByteWriter.h"
#include "Cubit/Voxel/BlockEdit.h"
#include "Cubit/Voxel/CharacterController.h"
#include "Cubit/Voxel/MatchState.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

//Every message the wire carries. Six, and deliberately not seven: there are no
//join or leave messages, because a snapshot carries the whole roster every tick
//and ids are never reused, so a client derives both by diffing what it held
//last.
enum class MessageId : std::uint8_t
{
    Hello = 1,
    Welcome = 2,
    Input = 3,
    Snapshot = 4,
    EditRequest = 5,
    EditApplied = 6
};

//Bumped whenever any message's layout changes. A mismatch is a disconnect with
//a logged reason: two builds of a hand-rolled wire format disagreeing about
//field widths produce garbage positions, which read as a physics bug and cost
//a day.
constexpr std::uint32_t ProtocolVersion = 1;

struct HelloMessage
{
    std::uint32_t Version = ProtocolVersion;
};

struct WelcomeMessage
{
    PlayerId You = InvalidPlayer;

    //A name, never the data - the shipped map is 23.8 MB. The client loads it
    //from its own assets and checks the hash.
    std::string MapName;
    std::uint64_t MapHash = 0;

    std::uint64_t Tick = 0;

    //Every edit applied since the map loaded, in application order. This is
    //what makes joining late correct: without it a client arriving after
    //somebody dug a hole would get a pristine world.
    std::vector<BlockEdit> Edits;
};

struct InputMessage
{
    //A counter, not a tick. In Stage 2 the client never steps, so it has no
    //simulation tick to name; the server uses this only to drop stale and
    //duplicate packets on an unordered channel. Stage 3 is where an input
    //acquires a real tick, because that is when replay needs to know where to
    //reinsert it.
    std::uint32_t Sequence = 0;

    CharacterInput Input;
};

struct PlayerSnapshot
{
    PlayerId Player = InvalidPlayer;
    glm::vec3 Position{ 0.0f };

    //Carried because CharacterController does not store them - they live in
    //CharacterInput - and a client needs them to draw a remote player facing
    //the right way.
    float Yaw = 0.0f;
    float Pitch = 0.0f;

    float VerticalVelocity = 0.0f;
    bool Grounded = false;
};

struct SnapshotMessage
{
    std::uint64_t Tick = 0;
    std::vector<PlayerSnapshot> Players;
};

//One edit, in either direction. The two directions share a payload but not a
//MessageId, because a client must never mistake its own request coming back
//for the server's authoritative answer.
struct EditMessage
{
    BlockEdit Edit;
};

CB_API std::vector<std::uint8_t> Encode(const HelloMessage& message);
CB_API std::vector<std::uint8_t> Encode(const WelcomeMessage& message);
CB_API std::vector<std::uint8_t> Encode(const InputMessage& message);
CB_API std::vector<std::uint8_t> Encode(const SnapshotMessage& message);
CB_API std::vector<std::uint8_t> EncodeEditRequest(const EditMessage& message);
CB_API std::vector<std::uint8_t> EncodeEditApplied(const EditMessage& message);

//Each returns false and leaves `out` untouched when the bytes are truncated,
//malformed, or of the wrong type. Malformed input is a routine wire condition
//rather than a caller's bug, so nothing here throws.
CB_API bool Decode(std::span<const std::uint8_t> bytes, HelloMessage& out);
CB_API bool Decode(std::span<const std::uint8_t> bytes, WelcomeMessage& out);
CB_API bool Decode(std::span<const std::uint8_t> bytes, InputMessage& out);
CB_API bool Decode(std::span<const std::uint8_t> bytes, SnapshotMessage& out);
CB_API bool Decode(std::span<const std::uint8_t> bytes, EditMessage& out);

//Reads the leading id without consuming anything, so a receiver can pick a
//decoder. False when the buffer is empty or the id is not one of the six.
CB_API bool PeekMessageId(std::span<const std::uint8_t> bytes, MessageId& out);

#ifdef _MSC_VER
#pragma warning(pop)
#endif
```

- [ ] **Step 4: Write `Protocol.cpp`**

Create `Cubit/src/Net/Protocol.cpp`:

```cpp
#include "cub.h"

#include "Cubit/Net/Protocol.h"

namespace
{
    //Bytes each entry costs on the wire. Used to reject an absurd count before
    //reserving for it, which is what stops a tiny hostile packet claiming a
    //huge collection from becoming a denial of service.
    constexpr std::size_t PlayerSnapshotBytes = 2 + 12 + 4 + 4 + 4 + 1;
    constexpr std::size_t BlockEditBytes = 12 + 2;

    void WriteEdit(ByteWriter& writer, const BlockEdit& edit)
    {
        writer.IVec3(edit.Position);
        writer.U16(static_cast<std::uint16_t>(edit.Block));
    }

    BlockEdit ReadEdit(ByteReader& reader)
    {
        BlockEdit edit;
        edit.Position = reader.IVec3();
        edit.Block = static_cast<BlockId>(reader.U16());
        return edit;
    }

    //Confirms the buffer opens with the expected id and leaves the reader
    //positioned just past it.
    bool OpenAs(ByteReader& reader, MessageId expected)
    {
        return reader.U8() == static_cast<std::uint8_t>(expected) && reader.Ok();
    }
}

std::vector<std::uint8_t> Encode(const HelloMessage& message)
{
    ByteWriter writer;
    writer.U8(static_cast<std::uint8_t>(MessageId::Hello));
    writer.U32(message.Version);
    return writer.Bytes();
}

std::vector<std::uint8_t> Encode(const WelcomeMessage& message)
{
    ByteWriter writer;
    writer.U8(static_cast<std::uint8_t>(MessageId::Welcome));
    writer.U16(message.You);
    writer.String(message.MapName);
    writer.U64(message.MapHash);
    writer.U64(message.Tick);
    writer.U32(static_cast<std::uint32_t>(message.Edits.size()));

    for (const BlockEdit& edit : message.Edits)
        WriteEdit(writer, edit);

    return writer.Bytes();
}

std::vector<std::uint8_t> Encode(const InputMessage& message)
{
    ByteWriter writer;
    writer.U8(static_cast<std::uint8_t>(MessageId::Input));
    writer.U32(message.Sequence);
    writer.F32(message.Input.Move.x);
    writer.F32(message.Input.Move.y);
    writer.F32(message.Input.Yaw);
    writer.F32(message.Input.Pitch);
    writer.Bool(message.Input.Jump);
    return writer.Bytes();
}

std::vector<std::uint8_t> Encode(const SnapshotMessage& message)
{
    ByteWriter writer;
    writer.U8(static_cast<std::uint8_t>(MessageId::Snapshot));
    writer.U64(message.Tick);
    writer.U16(static_cast<std::uint16_t>(message.Players.size()));

    for (const PlayerSnapshot& player : message.Players)
    {
        writer.U16(player.Player);
        writer.Vec3(player.Position);
        writer.F32(player.Yaw);
        writer.F32(player.Pitch);
        writer.F32(player.VerticalVelocity);
        writer.Bool(player.Grounded);
    }

    return writer.Bytes();
}

std::vector<std::uint8_t> EncodeEditRequest(const EditMessage& message)
{
    ByteWriter writer;
    writer.U8(static_cast<std::uint8_t>(MessageId::EditRequest));
    WriteEdit(writer, message.Edit);
    return writer.Bytes();
}

std::vector<std::uint8_t> EncodeEditApplied(const EditMessage& message)
{
    ByteWriter writer;
    writer.U8(static_cast<std::uint8_t>(MessageId::EditApplied));
    WriteEdit(writer, message.Edit);
    return writer.Bytes();
}

bool Decode(std::span<const std::uint8_t> bytes, HelloMessage& out)
{
    ByteReader reader(bytes);
    if (!OpenAs(reader, MessageId::Hello))
        return false;

    HelloMessage message;
    message.Version = reader.U32();

    if (!reader.Ok())
        return false;

    out = message;
    return true;
}

bool Decode(std::span<const std::uint8_t> bytes, WelcomeMessage& out)
{
    ByteReader reader(bytes);
    if (!OpenAs(reader, MessageId::Welcome))
        return false;

    WelcomeMessage message;
    message.You = static_cast<PlayerId>(reader.U16());
    message.MapName = reader.String();
    message.MapHash = reader.U64();
    message.Tick = reader.U64();

    const std::uint32_t count = reader.U32();
    if (!reader.Ok() || count > reader.Remaining() / BlockEditBytes)
        return false;

    message.Edits.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i)
        message.Edits.push_back(ReadEdit(reader));

    if (!reader.Ok())
        return false;

    out = std::move(message);
    return true;
}

bool Decode(std::span<const std::uint8_t> bytes, InputMessage& out)
{
    ByteReader reader(bytes);
    if (!OpenAs(reader, MessageId::Input))
        return false;

    InputMessage message;
    message.Sequence = reader.U32();
    message.Input.Move.x = reader.F32();
    message.Input.Move.y = reader.F32();
    message.Input.Yaw = reader.F32();
    message.Input.Pitch = reader.F32();
    message.Input.Jump = reader.Bool();

    if (!reader.Ok())
        return false;

    out = message;
    return true;
}

bool Decode(std::span<const std::uint8_t> bytes, SnapshotMessage& out)
{
    ByteReader reader(bytes);
    if (!OpenAs(reader, MessageId::Snapshot))
        return false;

    SnapshotMessage message;
    message.Tick = reader.U64();

    const std::uint16_t count = reader.U16();

    //Checked against what the buffer can actually hold, before reserving.
    //Trusting the count and reserving on its word is how a 13-byte packet
    //becomes a 1.7 MB allocation.
    if (!reader.Ok() || count > reader.Remaining() / PlayerSnapshotBytes)
        return false;

    message.Players.reserve(count);
    for (std::uint16_t i = 0; i < count; ++i)
    {
        PlayerSnapshot player;
        player.Player = static_cast<PlayerId>(reader.U16());
        player.Position = reader.Vec3();
        player.Yaw = reader.F32();
        player.Pitch = reader.F32();
        player.VerticalVelocity = reader.F32();
        player.Grounded = reader.Bool();
        message.Players.push_back(player);
    }

    if (!reader.Ok())
        return false;

    out = std::move(message);
    return true;
}

bool Decode(std::span<const std::uint8_t> bytes, EditMessage& out)
{
    ByteReader reader(bytes);

    const std::uint8_t id = reader.U8();
    if (!reader.Ok())
        return false;

    if (id != static_cast<std::uint8_t>(MessageId::EditRequest) &&
        id != static_cast<std::uint8_t>(MessageId::EditApplied))
        return false;

    EditMessage message;
    message.Edit = ReadEdit(reader);

    if (!reader.Ok())
        return false;

    out = message;
    return true;
}

bool PeekMessageId(std::span<const std::uint8_t> bytes, MessageId& out)
{
    if (bytes.empty())
        return false;

    const std::uint8_t id = bytes[0];
    if (id < static_cast<std::uint8_t>(MessageId::Hello) ||
        id > static_cast<std::uint8_t>(MessageId::EditApplied))
        return false;

    out = static_cast<MessageId>(id);
    return true;
}
```

- [ ] **Step 5: Export the header, regenerate, build**

Add `#include "Cubit/Net/Protocol.h"` to `Cubit/include/Cubit/Cubit.h`, then:
```bash
/c/dev/premake/premake5 vs2026
"$MSB" Cubit.slnx -p:Configuration=Debug -p:Platform=x64 -m -v:minimal -nologo
```
Expected: PASS.

- [ ] **Step 6: Prove the hostile-count test is not tautological**

Temporarily delete `count > reader.Remaining() / PlayerSnapshotBytes` from the snapshot decoder. Rebuild. **"A snapshot claiming more players than it carries is refused"** must go red — and note that the *truncation* sweep will likely still pass, which is exactly why the count test exists separately. Restore.

- [ ] **Step 7: Commit**

```bash
git add Cubit/include/Cubit/Net/Protocol.h Cubit/src/Net/Protocol.cpp \
        Tests/src/ProtocolTests.cpp Cubit/include/Cubit/Cubit.h
git commit -m "Give the wire six messages"
```

---

### Task 5: `Transport` and `LoopbackTransport`

`LoopbackTransport` delivers **instantly, with no loss**. Every network property lives in `SimulatedTransport` (Task 6). That split is what keeps both testable: the loopback is the wire with no behaviour, the decorator is the behaviour with no wire.

**Files:**
- Create: `Cubit/include/Cubit/Net/Transport.h`
- Create: `Cubit/include/Cubit/Net/LoopbackTransport.h`, `Cubit/src/Net/LoopbackTransport.cpp`
- Create: `Tests/src/LoopbackTransportTests.cpp`
- Modify: `Cubit/include/Cubit/Cubit.h`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `using PeerId = std::uint32_t`, `constexpr PeerId InvalidPeer = 0`
  - `enum class Channel : std::uint8_t { Unreliable = 0, Reliable = 1 }`
  - `enum class NetEventType { None, Connected, Disconnected, Message }`
  - `struct NetEvent { NetEventType Type; PeerId Peer; std::vector<std::uint8_t> Data; }`
  - `class Transport` with `Send`, `Broadcast`, `Disconnect`, `Poll`, `Advance`, `RoundTripTime`
  - `class LoopbackNetwork` with `Transport& Server()`, `Transport& AddClient(PeerId& peer)`, `void RemoveClient(PeerId)`, and `static constexpr PeerId ServerPeer = 1`

- [ ] **Step 1: Write the failing test**

Create `Tests/src/LoopbackTransportTests.cpp`:

```cpp
#include <doctest.h>

#include "Cubit/Net/LoopbackTransport.h"

#include <cstdint>
#include <vector>

namespace
{
    std::vector<std::uint8_t> Bytes(std::initializer_list<std::uint8_t> values)
    {
        return std::vector<std::uint8_t>(values);
    }

    //Drains a transport into a list, which is what every test here wants and
    //what nothing in the interface gives you.
    std::vector<NetEvent> Drain(Transport& transport)
    {
        std::vector<NetEvent> events;
        NetEvent event;
        while (transport.Poll(event))
            events.push_back(event);
        return events;
    }
}

TEST_CASE("Adding a client connects both sides")
{
    LoopbackNetwork network;
    PeerId peer = InvalidPeer;
    Transport& client = network.AddClient(peer);

    CHECK(peer != InvalidPeer);

    const std::vector<NetEvent> serverEvents = Drain(network.Server());
    REQUIRE(serverEvents.size() == 1);
    CHECK(serverEvents[0].Type == NetEventType::Connected);
    CHECK(serverEvents[0].Peer == peer);

    const std::vector<NetEvent> clientEvents = Drain(client);
    REQUIRE(clientEvents.size() == 1);
    CHECK(clientEvents[0].Type == NetEventType::Connected);
    CHECK(clientEvents[0].Peer == LoopbackNetwork::ServerPeer);
}

TEST_CASE("A client's message reaches the server tagged with the sender")
{
    LoopbackNetwork network;
    PeerId peer = InvalidPeer;
    Transport& client = network.AddClient(peer);
    Drain(network.Server());
    Drain(client);

    client.Send(LoopbackNetwork::ServerPeer, Bytes({ 1, 2, 3 }), Channel::Reliable);

    const std::vector<NetEvent> events = Drain(network.Server());
    REQUIRE(events.size() == 1);
    CHECK(events[0].Type == NetEventType::Message);
    CHECK(events[0].Peer == peer);
    CHECK(events[0].Data == Bytes({ 1, 2, 3 }));
}

TEST_CASE("A broadcast reaches every client and nobody else")
{
    LoopbackNetwork network;
    PeerId first = InvalidPeer;
    PeerId second = InvalidPeer;
    Transport& clientA = network.AddClient(first);
    Transport& clientB = network.AddClient(second);
    Drain(network.Server());
    Drain(clientA);
    Drain(clientB);

    network.Server().Broadcast(Bytes({ 9 }), Channel::Unreliable);

    const std::vector<NetEvent> a = Drain(clientA);
    const std::vector<NetEvent> b = Drain(clientB);
    REQUIRE(a.size() == 1);
    REQUIRE(b.size() == 1);
    CHECK(a[0].Data == Bytes({ 9 }));
    CHECK(b[0].Data == Bytes({ 9 }));

    //And the server did not deliver its own broadcast to itself.
    CHECK(Drain(network.Server()).empty());
}

TEST_CASE("A targeted send reaches only its addressee")
{
    LoopbackNetwork network;
    PeerId first = InvalidPeer;
    PeerId second = InvalidPeer;
    Transport& clientA = network.AddClient(first);
    Transport& clientB = network.AddClient(second);
    Drain(network.Server());
    Drain(clientA);
    Drain(clientB);

    network.Server().Send(first, Bytes({ 4 }), Channel::Reliable);

    CHECK(Drain(clientA).size() == 1);
    CHECK(Drain(clientB).empty());
}

TEST_CASE("Removing a client tells both sides and stops delivery")
{
    LoopbackNetwork network;
    PeerId peer = InvalidPeer;
    Transport& client = network.AddClient(peer);
    Drain(network.Server());
    Drain(client);

    network.RemoveClient(peer);

    const std::vector<NetEvent> serverEvents = Drain(network.Server());
    REQUIRE(serverEvents.size() == 1);
    CHECK(serverEvents[0].Type == NetEventType::Disconnected);
    CHECK(serverEvents[0].Peer == peer);

    //A broadcast after the departure must not reach the departed.
    network.Server().Broadcast(Bytes({ 7 }), Channel::Unreliable);
    CHECK(Drain(client).empty());
}

TEST_CASE("Disconnecting from either end removes the client")
{
    //Both directions, because both are used: a server ejects a client that
    //fails the handshake, and a client drops a server whose map it does not
    //have.
    LoopbackNetwork network;

    PeerId fromServer = InvalidPeer;
    Transport& ejected = network.AddClient(fromServer);
    network.Server().Disconnect(fromServer);
    CHECK(Drain(ejected).size() == 2);  //Connected, then Disconnected

    PeerId fromClient = InvalidPeer;
    Transport& leaving = network.AddClient(fromClient);
    Drain(network.Server());
    leaving.Disconnect(LoopbackNetwork::ServerPeer);

    const std::vector<NetEvent> serverEvents = Drain(network.Server());
    REQUIRE(serverEvents.size() == 1);
    CHECK(serverEvents[0].Type == NetEventType::Disconnected);
    CHECK(serverEvents[0].Peer == fromClient);
}

TEST_CASE("Loopback has no latency of its own")
{
    //The whole reason latency lives in SimulatedTransport: a wire with no
    //properties is a wire whose tests never argue about timing.
    LoopbackNetwork network;
    PeerId peer = InvalidPeer;
    Transport& client = network.AddClient(peer);
    Drain(network.Server());
    Drain(client);

    client.Send(LoopbackNetwork::ServerPeer, Bytes({ 1 }), Channel::Reliable);

    //No Advance call at all, and it has already arrived.
    CHECK(Drain(network.Server()).size() == 1);
    CHECK(client.RoundTripTime(LoopbackNetwork::ServerPeer) == doctest::Approx(0.0));
}
```

- [ ] **Step 2: Run and watch it fail**

Expected: `Cannot open include file: 'Cubit/Net/LoopbackTransport.h'`.

- [ ] **Step 3: Write `Transport.h`**

Create `Cubit/include/Cubit/Net/Transport.h`:

```cpp
#pragma once

#include <cstdint>
#include <span>
#include <vector>

//Identifies the other end of a connection, for the machine holding this
//transport. A server's peer ids name its clients; a client has exactly one,
//naming the server. Not a PlayerId: a peer exists from the moment a socket
//connects, before anybody has joined a match.
using PeerId = std::uint32_t;
constexpr PeerId InvalidPeer = 0;

//Two, and no more.
//
//Unreliable carries snapshots: a stale snapshot is worthless, so resending one
//spends bandwidth delivering something already superseded. Reliable carries
//joins and edits: an edit that arrives late is still correct, but one that
//vanishes is a permanent world desync.
enum class Channel : std::uint8_t
{
    Unreliable = 0,
    Reliable = 1
};

enum class NetEventType
{
    None,
    Connected,
    Disconnected,
    Message
};

struct NetEvent
{
    NetEventType Type = NetEventType::None;
    PeerId Peer = InvalidPeer;
    std::vector<std::uint8_t> Data;
};

//Packet-level, mirroring ENet rather than inventing a vocabulary over it.
//
//The seam exists for TESTING, not portability. With it, a doctest runs a server
//and two clients under deterministic latency, jitter and loss with no socket
//and no flakiness. Without it, netcode gets verified by somebody saying it
//feels fine.
//
//No CB_API: a pure interface exports nothing, and an exported class must define
//every member it declares.
class Transport
{
public:
    virtual ~Transport() = default;

    virtual void Send(PeerId peer, std::span<const std::uint8_t> data, Channel channel) = 0;
    virtual void Broadcast(std::span<const std::uint8_t> data, Channel channel) = 0;

    //Ends a connection. Used to refuse a client at the handshake - a wrong
    //protocol version, or a map the client does not have - because a rejection
    //that merely goes quiet leaves the other end waiting on a Welcome that will
    //never come, which is the silent failure the loud one exists to prevent.
    virtual void Disconnect(PeerId peer) = 0;

    //Drains one event. False when there is nothing left this step.
    virtual bool Poll(NetEvent& out) = 0;

    //Services the transport for one step. The caller says how much time
    //passed rather than the transport reading a clock, which is what lets a
    //test run a hundred simulated seconds instantly and identically every run.
    virtual void Advance(double seconds) = 0;

    //Round-trip time in seconds, for the HUD. Zero when unknown.
    virtual double RoundTripTime(PeerId peer) const = 0;
};
```

- [ ] **Step 4: Write `LoopbackTransport.h`**

Create `Cubit/include/Cubit/Net/LoopbackTransport.h`:

```cpp
#pragma once

#include "Cubit/Core.h"
#include "Cubit/Net/Transport.h"

#include <deque>
#include <memory>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

class LoopbackNetwork;

//One endpoint of an in-process network. Created only by LoopbackNetwork.
class CB_API LoopbackTransport final : public Transport
{
public:
    LoopbackTransport(LoopbackNetwork& network, PeerId self);

    void Send(PeerId peer, std::span<const std::uint8_t> data, Channel channel) override;
    void Broadcast(std::span<const std::uint8_t> data, Channel channel) override;
    void Disconnect(PeerId peer) override;
    bool Poll(NetEvent& out) override;
    void Advance(double seconds) override;
    double RoundTripTime(PeerId peer) const override;

    //Queues an event for this endpoint to Poll. Called by the network.
    void Deliver(NetEvent event);

private:
    LoopbackNetwork& m_Network;

    //Who this endpoint is, from everyone else's point of view.
    PeerId m_Self = InvalidPeer;

    std::deque<NetEvent> m_Inbox;
};

//A server endpoint and any number of client endpoints, delivering to each other
//with no socket.
//
//Delivery is INSTANT and lossless, deliberately. Every network property -
//latency, jitter, loss - lives in SimulatedTransport, which wraps this. Keeping
//the two apart is what makes both testable: this is the wire with no behaviour,
//that is the behaviour with no wire.
class CB_API LoopbackNetwork
{
public:
    LoopbackNetwork();
    ~LoopbackNetwork();

    LoopbackNetwork(const LoopbackNetwork&) = delete;
    LoopbackNetwork& operator=(const LoopbackNetwork&) = delete;

    //The id every client uses to address the server.
    static constexpr PeerId ServerPeer = 1;

    Transport& Server();

    //Adds a client already connected to the server. Both sides see a Connected
    //event. `peer` receives the id the server will know this client by.
    Transport& AddClient(PeerId& peer);

    //Disconnects a client. Both sides see a Disconnected event, and nothing
    //further is delivered to it.
    void RemoveClient(PeerId peer);

    //Routes one message. Called by LoopbackTransport.
    void Route(PeerId from, PeerId to, std::span<const std::uint8_t> data);

    //Routes one message to every client. Called by the server's endpoint.
    void RouteBroadcast(std::span<const std::uint8_t> data);

private:
    //Returns the endpoint for an id, or nullptr once it has been removed.
    LoopbackTransport* Find(PeerId peer);

    std::unique_ptr<LoopbackTransport> m_Server;

    struct ClientSlot
    {
        PeerId Peer = InvalidPeer;
        std::unique_ptr<LoopbackTransport> Endpoint;
    };

    std::vector<ClientSlot> m_Clients;

    //Never reused, matching PlayerId's rule and for the same reason: a stale
    //message naming somebody who left must not be applied to whoever arrived
    //next. Starts past ServerPeer.
    PeerId m_NextPeer = ServerPeer + 1;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
```

- [ ] **Step 5: Write `LoopbackTransport.cpp`**

Create `Cubit/src/Net/LoopbackTransport.cpp`:

```cpp
#include "cub.h"

#include "Cubit/Net/LoopbackTransport.h"

LoopbackTransport::LoopbackTransport(LoopbackNetwork& network, PeerId self)
    : m_Network(network), m_Self(self)
{
}

void LoopbackTransport::Send(PeerId peer, std::span<const std::uint8_t> data, Channel channel)
{
    //Loopback loses nothing, so the two channels behave identically here. The
    //distinction is real in EnetTransport and modelled in SimulatedTransport;
    //pretending it mattered at this layer would be theatre.
    (void)channel;
    m_Network.Route(m_Self, peer, data);
}

void LoopbackTransport::Broadcast(std::span<const std::uint8_t> data, Channel channel)
{
    (void)channel;
    m_Network.RouteBroadcast(data);
}

bool LoopbackTransport::Poll(NetEvent& out)
{
    if (m_Inbox.empty())
        return false;

    out = std::move(m_Inbox.front());
    m_Inbox.pop_front();
    return true;
}

void LoopbackTransport::Disconnect(PeerId peer)
{
    //A client addresses the server, so "disconnect the server" means "remove
    //me". A server names the client it is ejecting.
    m_Network.RemoveClient(peer == LoopbackNetwork::ServerPeer ? m_Self : peer);
}

void LoopbackTransport::Advance(double seconds)
{
    //Nothing to service: delivery already happened inside Send.
    (void)seconds;
}

double LoopbackTransport::RoundTripTime(PeerId peer) const
{
    (void)peer;
    return 0.0;
}

void LoopbackTransport::Deliver(NetEvent event)
{
    m_Inbox.push_back(std::move(event));
}

LoopbackNetwork::LoopbackNetwork()
    : m_Server(std::make_unique<LoopbackTransport>(*this, ServerPeer))
{
}

LoopbackNetwork::~LoopbackNetwork() = default;

Transport& LoopbackNetwork::Server()
{
    return *m_Server;
}

Transport& LoopbackNetwork::AddClient(PeerId& peer)
{
    const PeerId assigned = m_NextPeer++;

    ClientSlot slot;
    slot.Peer = assigned;
    slot.Endpoint = std::make_unique<LoopbackTransport>(*this, assigned);
    LoopbackTransport& endpoint = *slot.Endpoint;
    m_Clients.push_back(std::move(slot));

    NetEvent toServer;
    toServer.Type = NetEventType::Connected;
    toServer.Peer = assigned;
    m_Server->Deliver(std::move(toServer));

    NetEvent toClient;
    toClient.Type = NetEventType::Connected;
    toClient.Peer = ServerPeer;
    endpoint.Deliver(std::move(toClient));

    peer = assigned;
    return endpoint;
}

void LoopbackNetwork::RemoveClient(PeerId peer)
{
    const auto slot = std::find_if(m_Clients.begin(), m_Clients.end(),
        [peer](const ClientSlot& candidate) { return candidate.Peer == peer; });

    if (slot == m_Clients.end())
        return;

    NetEvent toClient;
    toClient.Type = NetEventType::Disconnected;
    toClient.Peer = ServerPeer;
    slot->Endpoint->Deliver(std::move(toClient));

    //Erased before the server is told, so a handler that reacts by broadcasting
    //cannot reach the endpoint that just left.
    m_Clients.erase(slot);

    NetEvent toServer;
    toServer.Type = NetEventType::Disconnected;
    toServer.Peer = peer;
    m_Server->Deliver(std::move(toServer));
}

void LoopbackNetwork::Route(PeerId from, PeerId to, std::span<const std::uint8_t> data)
{
    LoopbackTransport* target = Find(to);
    if (target == nullptr)
        return;

    NetEvent event;
    event.Type = NetEventType::Message;
    event.Peer = from;
    event.Data.assign(data.begin(), data.end());
    target->Deliver(std::move(event));
}

void LoopbackNetwork::RouteBroadcast(std::span<const std::uint8_t> data)
{
    for (const ClientSlot& slot : m_Clients)
    {
        NetEvent event;
        event.Type = NetEventType::Message;
        event.Peer = ServerPeer;
        event.Data.assign(data.begin(), data.end());
        slot.Endpoint->Deliver(std::move(event));
    }
}

LoopbackTransport* LoopbackNetwork::Find(PeerId peer)
{
    if (peer == ServerPeer)
        return m_Server.get();

    for (const ClientSlot& slot : m_Clients)
    {
        if (slot.Peer == peer)
            return slot.Endpoint.get();
    }

    return nullptr;
}
```

`<algorithm>` comes in through `cub.h`'s transitive includes; if `std::find_if` does not resolve, add `#include <algorithm>` below the `cub.h` include.

- [ ] **Step 6: Export, regenerate, build, watch the tests pass**

Add `#include "Cubit/Net/Transport.h"` and `#include "Cubit/Net/LoopbackTransport.h"` to `Cubit/include/Cubit/Cubit.h`, then regenerate and build.
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add Cubit/include/Cubit/Net/Transport.h Cubit/include/Cubit/Net/LoopbackTransport.h \
        Cubit/src/Net/LoopbackTransport.cpp Tests/src/LoopbackTransportTests.cpp \
        Cubit/include/Cubit/Cubit.h
git commit -m "Deliver packets in process, with no socket and no properties"
```

---

### Task 6: `SimulatedTransport`

The decorator this whole stage leans on. It wraps **any** `Transport` and adds deterministic latency, jitter and loss — which gives one mechanism two payoffs: the test suite gets a bad network with no socket, and the demo gets a latency knob without which running two clients on `127.0.0.1` would prove nothing.

**Delay is applied to outbound traffic only.** Both endpoints wrap themselves, so a client-to-server packet is delayed by the client's outbound latency and a server-to-client packet by the server's. Delaying both directions at each end would double every number.

**Files:**
- Create: `Cubit/include/Cubit/Net/SimulatedTransport.h`, `Cubit/src/Net/SimulatedTransport.cpp`
- Create: `Tests/src/SimulatedTransportTests.cpp`
- Modify: `Cubit/include/Cubit/Cubit.h`

**Interfaces:**
- Consumes: `Transport`, `LoopbackNetwork` (Task 5).
- Produces:
  - `struct NetworkSim { double Latency; double Jitter; float Loss; std::uint64_t Seed; }` — `Latency` and `Jitter` are **one-way seconds**
  - `class SimulatedTransport final : public Transport` with `SimulatedTransport(Transport& inner, const NetworkSim& sim)`

- [ ] **Step 1: Write the failing test**

Create `Tests/src/SimulatedTransportTests.cpp`:

```cpp
#include <doctest.h>

#include "Cubit/FrameClock.h"
#include "Cubit/Net/LoopbackTransport.h"
#include "Cubit/Net/SimulatedTransport.h"

#include <cstdint>
#include <vector>

namespace
{
    //50 ms one-way at 60 Hz is exactly 3 ticks. Every latency in the suite is a
    //whole tick multiple so assertions can be exact rather than approximate.
    constexpr double OneWayLatency = 3.0 * FrameClock::FixedStepSeconds;

    std::vector<std::uint8_t> Bytes(std::uint8_t value)
    {
        return std::vector<std::uint8_t>{ value };
    }

    int Drain(Transport& transport)
    {
        int count = 0;
        NetEvent event;
        while (transport.Poll(event))
        {
            if (event.Type == NetEventType::Message)
                ++count;
        }
        return count;
    }
}

TEST_CASE("A packet arrives after the one-way latency, not before")
{
    LoopbackNetwork network;
    PeerId peer = InvalidPeer;
    Transport& rawClient = network.AddClient(peer);

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    SimulatedTransport client(rawClient, sim);
    Drain(network.Server());
    Drain(client);

    client.Send(LoopbackNetwork::ServerPeer, Bytes(1), Channel::Reliable);

    //Two ticks is not enough.
    client.Advance(FrameClock::FixedStepSeconds);
    CHECK(Drain(network.Server()) == 0);
    client.Advance(FrameClock::FixedStepSeconds);
    CHECK(Drain(network.Server()) == 0);

    //The third delivers it.
    client.Advance(FrameClock::FixedStepSeconds);
    CHECK(Drain(network.Server()) == 1);
}

TEST_CASE("Loss drops unreliable packets and never drops reliable ones")
{
    LoopbackNetwork network;
    PeerId peer = InvalidPeer;
    Transport& rawClient = network.AddClient(peer);

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    sim.Loss = 0.5f;
    SimulatedTransport client(rawClient, sim);
    Drain(network.Server());
    Drain(client);

    for (int i = 0; i < 200; ++i)
        client.Send(LoopbackNetwork::ServerPeer, Bytes(0), Channel::Unreliable);
    for (int i = 0; i < 200; ++i)
        client.Send(LoopbackNetwork::ServerPeer, Bytes(1), Channel::Reliable);

    //Long enough for every retransmit to land too.
    for (int i = 0; i < 60; ++i)
        client.Advance(FrameClock::FixedStepSeconds);

    const int arrived = Drain(network.Server());

    //200 reliable always, plus roughly half the unreliable. The band is wide
    //because this asserts the mechanism, not the RNG's exact draw.
    CHECK(arrived >= 200 + 60);
    CHECK(arrived <= 200 + 140);
}

TEST_CASE("A reliable packet selected for loss arrives late rather than never")
{
    //Modelling reliability as never-delayed would make every test lie about
    //the one property the reliable channel actually costs.
    LoopbackNetwork network;
    PeerId peer = InvalidPeer;
    Transport& rawClient = network.AddClient(peer);

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    sim.Loss = 1.0f;
    SimulatedTransport client(rawClient, sim);
    Drain(network.Server());
    Drain(client);

    client.Send(LoopbackNetwork::ServerPeer, Bytes(1), Channel::Unreliable);
    client.Send(LoopbackNetwork::ServerPeer, Bytes(2), Channel::Reliable);

    for (int i = 0; i < 3; ++i)
        client.Advance(FrameClock::FixedStepSeconds);

    //Neither has arrived: the unreliable one never will, and the reliable one
    //is still waiting out its retransmit.
    CHECK(Drain(network.Server()) == 0);

    for (int i = 0; i < 30; ++i)
        client.Advance(FrameClock::FixedStepSeconds);

    CHECK(Drain(network.Server()) == 1);
}

TEST_CASE("The same seed produces the same delivery schedule")
{
    //Everything else in this suite is worthless without this. A network that
    //differs run to run turns every downstream failure into a coin flip.
    const auto Run = [](std::uint64_t seed)
    {
        LoopbackNetwork network;
        PeerId peer = InvalidPeer;
        Transport& rawClient = network.AddClient(peer);

        NetworkSim sim;
        sim.Latency = OneWayLatency;
        sim.Jitter = FrameClock::FixedStepSeconds;
        sim.Loss = 0.3f;
        sim.Seed = seed;
        SimulatedTransport client(rawClient, sim);
        Drain(network.Server());
        Drain(client);

        std::vector<int> arrivals;
        for (int tick = 0; tick < 300; ++tick)
        {
            client.Send(LoopbackNetwork::ServerPeer, Bytes(1), Channel::Unreliable);
            client.Advance(FrameClock::FixedStepSeconds);
            arrivals.push_back(Drain(network.Server()));
        }
        return arrivals;
    };

    CHECK(Run(1) == Run(1));
    CHECK(Run(1) != Run(2));
}

TEST_CASE("Round-trip time is twice the one-way latency")
{
    LoopbackNetwork network;
    PeerId peer = InvalidPeer;
    Transport& rawClient = network.AddClient(peer);

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    SimulatedTransport client(rawClient, sim);

    CHECK(client.RoundTripTime(LoopbackNetwork::ServerPeer)
        == doctest::Approx(2.0 * OneWayLatency));
}
```

- [ ] **Step 2: Run and watch it fail**

Expected: `Cannot open include file: 'Cubit/Net/SimulatedTransport.h'`.

- [ ] **Step 3: Write `SimulatedTransport.h`**

Create `Cubit/include/Cubit/Net/SimulatedTransport.h`:

```cpp
#pragma once

#include "Cubit/Core.h"
#include "Cubit/Net/Transport.h"

#include <cstdint>
#include <deque>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

//How bad the network is. Latency and Jitter are ONE-WAY seconds.
//
//The --latency command-line flag is round-trip milliseconds and is halved on
//the way in. Two units for one idea invites exactly one bug, so the split is
//stated everywhere it appears.
struct NetworkSim
{
    double Latency = 0.0;
    double Jitter = 0.0;

    //Fraction of unreliable packets discarded. Reliable packets selected by
    //this are delayed instead, modelling retransmission.
    float Loss = 0.0f;

    std::uint64_t Seed = 1;
};

//Adds deterministic latency, jitter and loss to any Transport.
//
//This is the piece that makes netcode testable to this project's standard. With
//it, a doctest runs a server and two clients under 100 ms RTT and 5% loss in one
//process with no socket, identically every run. Without it, netcode gets
//"verified" by somebody saying it feels fine.
//
//It earns its keep twice: the same class wraps EnetTransport in the Sandbox, so
//`--latency 150` makes a localhost demo show real latency. Loopback has none of
//its own, so without this the demo would prove nothing.
//
//DELAY IS OUTBOUND ONLY. Both endpoints wrap themselves, so each direction is
//delayed exactly once. Delaying inbound as well would double every number.
class CB_API SimulatedTransport final : public Transport
{
public:
    //Keeps a reference to `inner`, which must outlive this.
    SimulatedTransport(Transport& inner, const NetworkSim& sim);

    void Send(PeerId peer, std::span<const std::uint8_t> data, Channel channel) override;
    void Broadcast(std::span<const std::uint8_t> data, Channel channel) override;
    void Disconnect(PeerId peer) override;
    bool Poll(NetEvent& out) override;
    void Advance(double seconds) override;
    double RoundTripTime(PeerId peer) const override;

private:
    //A packet waiting out its delay.
    struct Pending
    {
        double Due = 0.0;

        //Breaks ties in Due so ordering is total and therefore reproducible.
        //Without it two packets due on the same tick sort by whatever the sort
        //happens to do, and "deterministic" quietly stops being true.
        std::uint64_t Serial = 0;

        PeerId Peer = InvalidPeer;
        bool IsBroadcast = false;
        Channel Sent = Channel::Unreliable;
        std::vector<std::uint8_t> Data;
    };

    //Queues one packet, applying loss and jitter. `broadcast` chooses which of
    //the inner transport's two send paths it takes when it comes due.
    void Queue(PeerId peer, bool broadcast, std::span<const std::uint8_t> data, Channel channel);

    //xorshift64*, hand-rolled rather than <random>, because the standard
    //distributions are not required to draw identically across
    //implementations and this must be reproducible to be worth anything.
    std::uint64_t NextRandom();

    //A draw in [0, 1).
    double NextUnit();

    Transport& m_Inner;
    NetworkSim m_Sim;

    double m_Now = 0.0;
    std::uint64_t m_NextSerial = 0;
    std::uint64_t m_RandomState = 1;

    std::vector<Pending> m_Outbound;
    std::deque<NetEvent> m_Inbox;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
```

- [ ] **Step 4: Write `SimulatedTransport.cpp`**

Create `Cubit/src/Net/SimulatedTransport.cpp`:

```cpp
#include "cub.h"

#include "Cubit/Net/SimulatedTransport.h"

#include <algorithm>

SimulatedTransport::SimulatedTransport(Transport& inner, const NetworkSim& sim)
    : m_Inner(inner), m_Sim(sim)
{
    //A zero seed would leave xorshift stuck at zero forever, turning every
    //draw into the same value and every "random" loss into no loss at all.
    m_RandomState = sim.Seed == 0 ? 1 : sim.Seed;
}

void SimulatedTransport::Send(PeerId peer, std::span<const std::uint8_t> data, Channel channel)
{
    Queue(peer, false, data, channel);
}

void SimulatedTransport::Broadcast(std::span<const std::uint8_t> data, Channel channel)
{
    Queue(InvalidPeer, true, data, channel);
}

void SimulatedTransport::Queue(PeerId peer, bool broadcast,
    std::span<const std::uint8_t> data, Channel channel)
{
    double due = m_Now + m_Sim.Latency;

    if (m_Sim.Jitter > 0.0)
    {
        //Uniform across +/- Jitter. Packets can therefore arrive out of order,
        //which is realistic and is exactly why InputMessage carries a sequence.
        due += (NextUnit() * 2.0 - 1.0) * m_Sim.Jitter;
    }

    if (m_Sim.Loss > 0.0f && NextUnit() < static_cast<double>(m_Sim.Loss))
    {
        if (channel == Channel::Unreliable)
            return;

        //Reliable traffic is retransmitted rather than lost. One extra
        //round trip is the cheapest honest model of that, and it keeps the
        //cost of reliability visible in the tests instead of free.
        due += 2.0 * m_Sim.Latency;
    }

    //Never before now, even if jitter pushed it backwards past the send.
    due = std::max(due, m_Now);

    Pending pending;
    pending.Due = due;
    pending.Serial = m_NextSerial++;
    pending.Peer = peer;
    pending.IsBroadcast = broadcast;
    pending.Sent = channel;
    pending.Data.assign(data.begin(), data.end());
    m_Outbound.push_back(std::move(pending));
}

void SimulatedTransport::Disconnect(PeerId peer)
{
    //Not delayed, and deliberately so. A disconnect is a local decision about
    //a connection this end is abandoning, not a packet whose travel time is
    //being modelled. Queueing it would leave a rejected client being simulated
    //for another 50 ms.
    m_Inner.Disconnect(peer);
}

bool SimulatedTransport::Poll(NetEvent& out)
{
    if (m_Inbox.empty())
        return false;

    out = std::move(m_Inbox.front());
    m_Inbox.pop_front();
    return true;
}

void SimulatedTransport::Advance(double seconds)
{
    m_Now += seconds;

    //Everything now due, in a total order: by time, then by send order. The
    //partition keeps the not-yet-due entries without rebuilding the vector.
    std::vector<Pending> due;
    const auto split = std::stable_partition(m_Outbound.begin(), m_Outbound.end(),
        [this](const Pending& pending) { return pending.Due <= m_Now; });

    due.assign(std::make_move_iterator(m_Outbound.begin()), std::make_move_iterator(split));
    m_Outbound.erase(m_Outbound.begin(), split);

    std::sort(due.begin(), due.end(),
        [](const Pending& a, const Pending& b)
        {
            if (a.Due != b.Due)
                return a.Due < b.Due;
            return a.Serial < b.Serial;
        });

    for (const Pending& pending : due)
    {
        if (pending.IsBroadcast)
            m_Inner.Broadcast(pending.Data, pending.Sent);
        else
            m_Inner.Send(pending.Peer, pending.Data, pending.Sent);
    }

    m_Inner.Advance(seconds);

    //Inbound is taken straight through. The other endpoint's own
    //SimulatedTransport already delayed it on the way out; delaying it again
    //here would double the latency of every packet in the system.
    NetEvent event;
    while (m_Inner.Poll(event))
        m_Inbox.push_back(std::move(event));
}

double SimulatedTransport::RoundTripTime(PeerId peer) const
{
    (void)peer;

    //Both ends are assumed to be configured alike, which is true of every
    //caller: the tests build them from one NetworkSim and the Sandbox passes
    //one flag. Reported rather than measured because there is nothing to
    //measure against - no acknowledgement exists until Stage 3.
    return 2.0 * m_Sim.Latency;
}

std::uint64_t SimulatedTransport::NextRandom()
{
    //xorshift64*, Vigna. Small, fast, and identical everywhere, which is the
    //only property that matters here.
    m_RandomState ^= m_RandomState >> 12;
    m_RandomState ^= m_RandomState << 25;
    m_RandomState ^= m_RandomState >> 27;
    return m_RandomState * 0x2545F4914F6CDD1Dull;
}

double SimulatedTransport::NextUnit()
{
    //53 bits is the whole mantissa of a double, so this spans [0, 1) evenly
    //without the modulo bias a smaller shift would introduce.
    return static_cast<double>(NextRandom() >> 11) / 9007199254740992.0;
}
```

- [ ] **Step 5: Export, regenerate, build, watch the tests pass**

Add `#include "Cubit/Net/SimulatedTransport.h"` to `Cubit/include/Cubit/Cubit.h`, regenerate, build.
Expected: PASS.

- [ ] **Step 6: Prove the determinism test is not tautological**

Temporarily seed `m_RandomState` from `std::chrono::steady_clock::now().time_since_epoch().count()` instead of `sim.Seed`. Rebuild. **"The same seed produces the same delivery schedule"** must go red. Restore.

Then temporarily delete the `Serial` tie-break from the sort comparator and rerun the same test several times; it should be *unstable*, which is the point of the field. Restore.

- [ ] **Step 7: Commit**

```bash
git add Cubit/include/Cubit/Net/SimulatedTransport.h Cubit/src/Net/SimulatedTransport.cpp \
        Tests/src/SimulatedTransportTests.cpp Cubit/include/Cubit/Cubit.h
git commit -m "Make a bad network deterministic"
```

---

### Task 7: `MapHash`

A client with a different `battlefield512.vox` must fail at the handshake, loudly, rather than desync silently an hour later and look like a physics bug.

**Files:**
- Create: `Cubit/include/Cubit/Net/MapHash.h`, `Cubit/src/Net/MapHash.cpp`
- Create: `Tests/src/MapHashTests.cpp`
- Modify: `Cubit/include/Cubit/Cubit.h`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `CB_API std::uint64_t HashBytes(std::span<const std::uint8_t> bytes)`
  - `CB_API std::uint64_t HashMapFile(const std::string& path)` — returns 0 when unreadable

- [ ] **Step 1: Write the failing test**

Create `Tests/src/MapHashTests.cpp`:

```cpp
#include <doctest.h>

#include "Cubit/Net/MapHash.h"

#include <cstdint>
#include <vector>

TEST_CASE("Hashing is stable and order-sensitive")
{
    const std::vector<std::uint8_t> a{ 1, 2, 3 };
    const std::vector<std::uint8_t> b{ 3, 2, 1 };

    CHECK(HashBytes(a) == HashBytes(a));
    CHECK(HashBytes(a) != HashBytes(b));
}

TEST_CASE("A one-byte difference changes the hash")
{
    //The case this exists for: two maps that differ in a single block. A hash
    //that missed that would let the desync it is meant to catch straight
    //through.
    std::vector<std::uint8_t> original(4096, 7);
    std::vector<std::uint8_t> altered = original;
    altered[2048] = 8;

    CHECK(HashBytes(original) != HashBytes(altered));
}

TEST_CASE("The empty buffer hashes to the FNV-1a basis")
{
    CHECK(HashBytes(std::vector<std::uint8_t>{}) == 0xCBF29CE484222325ull);
}

TEST_CASE("An unreadable file hashes to zero rather than throwing")
{
    //A missing map is a startup condition to report, not an exception to
    //unwind a server through.
    CHECK(HashMapFile("no/such/map.vox") == 0);
}
```

- [ ] **Step 2: Run and watch it fail**

Expected: `Cannot open include file: 'Cubit/Net/MapHash.h'`.

- [ ] **Step 3: Write the header and implementation**

Create `Cubit/include/Cubit/Net/MapHash.h`:

```cpp
#pragma once

#include "Cubit/Core.h"

#include <cstdint>
#include <span>
#include <string>

//FNV-1a, 64-bit. Not cryptographic and does not need to be: this catches two
//machines running different files, not somebody forging one.
CB_API std::uint64_t HashBytes(std::span<const std::uint8_t> bytes);

//Hashes a map file's raw bytes. Returns 0 when the file cannot be read, which
//callers treat as "no map" rather than as a hash.
//
//The file, not the loaded World: it is the identity the server names in
//Welcome, and hashing the bytes means a client and server that read the same
//file agree without depending on the loader producing identical structures.
CB_API std::uint64_t HashMapFile(const std::string& path);
```

Create `Cubit/src/Net/MapHash.cpp`:

```cpp
#include "cub.h"

#include "Cubit/Net/MapHash.h"

#include <fstream>

namespace
{
    constexpr std::uint64_t FnvBasis = 0xCBF29CE484222325ull;
    constexpr std::uint64_t FnvPrime = 0x00000100000001B3ull;
}

std::uint64_t HashBytes(std::span<const std::uint8_t> bytes)
{
    std::uint64_t hash = FnvBasis;

    for (const std::uint8_t byte : bytes)
    {
        hash ^= byte;
        hash *= FnvPrime;
    }

    return hash;
}

std::uint64_t HashMapFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return 0;

    std::uint64_t hash = FnvBasis;

    //Streamed in blocks rather than read whole: the shipped map is 23.8 MB and
    //there is no reason to hold a second copy of it to hash it.
    std::array<char, 64 * 1024> buffer{};
    while (file.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || file.gcount() > 0)
    {
        const std::streamsize read = file.gcount();
        for (std::streamsize i = 0; i < read; ++i)
        {
            hash ^= static_cast<std::uint8_t>(buffer[static_cast<std::size_t>(i)]);
            hash *= FnvPrime;
        }
    }

    return hash;
}
```

- [ ] **Step 4: Export, regenerate, build, watch the tests pass**

Add `#include "Cubit/Net/MapHash.h"` to `Cubit/include/Cubit/Cubit.h`, regenerate, build.
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add Cubit/include/Cubit/Net/MapHash.h Cubit/src/Net/MapHash.cpp \
        Tests/src/MapHashTests.cpp Cubit/include/Cubit/Cubit.h
git commit -m "Name a map by the bytes it is made of"
```

---

### Task 8: `MatchServer`

The authority. It owns the only `MatchState` anyone is allowed to believe, and it is the only place in the codebase that calls `Step` during a networked match.

**Files:**
- Create: `Cubit/include/Cubit/Net/MatchServer.h`, `Cubit/src/Net/MatchServer.cpp`
- Create: `Tests/src/MatchServerTests.cpp`
- Modify: `Cubit/include/Cubit/Cubit.h`

**Interfaces:**
- Consumes: `Transport`, `Channel`, `PeerId` (Task 5); `Protocol` (Task 4); `MatchState::AddPlayer(PlayerId, vec3)` and `Players()` (Task 1).
- Produces:
  - `MatchServer(World world, std::string mapName, std::uint64_t mapHash, const glm::vec3& spawn, Transport& transport)`
  - `void Step(double seconds)`
  - `const MatchState& Match() const`
  - `const std::vector<BlockEdit>& EditLog() const`
  - `std::size_t ClientCount() const`

- [ ] **Step 1: Write the failing test**

Create `Tests/src/MatchServerTests.cpp`:

```cpp
#include <doctest.h>

#include "Cubit/FrameClock.h"
#include "Cubit/Net/LoopbackTransport.h"
#include "Cubit/Net/MatchServer.h"
#include "Cubit/Net/Protocol.h"

#include <glm/glm.hpp>
#include <optional>
#include <vector>

namespace
{
    World FlatWorld()
    {
        World world(2, 2, 2);

        for (int z = 0; z < world.GetDepth(); ++z)
            for (int x = 0; x < world.GetWidth(); ++x)
                world.SetBlock(x, 0, z, BlockId{ 1 });

        return world;
    }

    const glm::vec3 Spawn{ 8.0f, 2.0f, 8.0f };

    //Reads whatever the raw endpoint has waiting and returns the last snapshot
    //among it, which is what a test almost always wants to look at.
    std::optional<SnapshotMessage> LastSnapshot(Transport& transport)
    {
        std::optional<SnapshotMessage> latest;

        NetEvent event;
        while (transport.Poll(event))
        {
            if (event.Type != NetEventType::Message)
                continue;

            SnapshotMessage snapshot;
            if (Decode(event.Data, snapshot))
                latest = snapshot;
        }

        return latest;
    }

    std::optional<WelcomeMessage> FindWelcome(Transport& transport)
    {
        std::optional<WelcomeMessage> found;

        NetEvent event;
        while (transport.Poll(event))
        {
            if (event.Type != NetEventType::Message)
                continue;

            WelcomeMessage welcome;
            if (Decode(event.Data, welcome))
                found = welcome;
        }

        return found;
    }
}

TEST_CASE("A server with no clients still ticks and broadcasts")
{
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    server.Step(FrameClock::FixedStepSeconds);

    CHECK(server.Match().Tick() == 1);
    CHECK(server.ClientCount() == 0);
}

TEST_CASE("Saying hello gets a welcome, a player and a place to stand")
{
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& client = network.AddClient(peer);
    client.Send(LoopbackNetwork::ServerPeer, Encode(HelloMessage{}), Channel::Reliable);

    server.Step(FrameClock::FixedStepSeconds);

    const std::optional<WelcomeMessage> welcome = FindWelcome(client);
    REQUIRE(welcome.has_value());
    CHECK(welcome->You != InvalidPlayer);
    CHECK(welcome->MapName == "flat.vox");
    CHECK(welcome->MapHash == 0xABCD);
    CHECK(welcome->Edits.empty());

    REQUIRE(server.Match().HasPlayer(welcome->You));
    CHECK(server.Match().Player(welcome->You).Position() == Spawn);
}

TEST_CASE("A client speaking the wrong protocol version is disconnected, not tolerated")
{
    //Two builds disagreeing about field widths produce garbage positions, which
    //read as a physics bug and cost a day. Failing at the handshake is the
    //cheap version of that discovery.
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& client = network.AddClient(peer);

    HelloMessage wrong;
    wrong.Version = ProtocolVersion + 1;
    client.Send(LoopbackNetwork::ServerPeer, Encode(wrong), Channel::Reliable);

    server.Step(FrameClock::FixedStepSeconds);

    CHECK_FALSE(FindWelcome(client).has_value());
    CHECK(server.ClientCount() == 0);
    CHECK(server.Match().Players().empty());
}

TEST_CASE("The snapshot carries the whole roster, so joins and leaves need no message")
{
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId firstPeer = InvalidPeer;
    PeerId secondPeer = InvalidPeer;
    Transport& first = network.AddClient(firstPeer);
    Transport& second = network.AddClient(secondPeer);

    first.Send(LoopbackNetwork::ServerPeer, Encode(HelloMessage{}), Channel::Reliable);
    second.Send(LoopbackNetwork::ServerPeer, Encode(HelloMessage{}), Channel::Reliable);
    server.Step(FrameClock::FixedStepSeconds);

    //Drain the welcomes so only snapshots remain.
    FindWelcome(first);
    FindWelcome(second);
    server.Step(FrameClock::FixedStepSeconds);

    const std::optional<SnapshotMessage> snapshot = LastSnapshot(first);
    REQUIRE(snapshot.has_value());
    CHECK(snapshot->Players.size() == 2);
    CHECK(snapshot->Tick == server.Match().Tick());

    network.RemoveClient(secondPeer);
    server.Step(FrameClock::FixedStepSeconds);

    const std::optional<SnapshotMessage> afterLeaving = LastSnapshot(first);
    REQUIRE(afterLeaving.has_value());
    CHECK(afterLeaving->Players.size() == 1);
    CHECK(server.Match().Players().size() == 1);
}

TEST_CASE("Input moves the player it came from")
{
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& client = network.AddClient(peer);
    client.Send(LoopbackNetwork::ServerPeer, Encode(HelloMessage{}), Channel::Reliable);
    server.Step(FrameClock::FixedStepSeconds);

    const std::optional<WelcomeMessage> welcome = FindWelcome(client);
    REQUIRE(welcome.has_value());
    const PlayerId player = welcome->You;
    const glm::vec3 before = server.Match().Player(player).Position();

    for (std::uint32_t i = 1; i <= 30; ++i)
    {
        InputMessage input;
        input.Sequence = i;
        input.Input.Move = glm::vec2(0.0f, 1.0f);
        input.Input.Yaw = 0.0f;
        client.Send(LoopbackNetwork::ServerPeer, Encode(input), Channel::Unreliable);
        server.Step(FrameClock::FixedStepSeconds);
    }

    CHECK(server.Match().Player(player).Position() != before);
}

TEST_CASE("A stale or duplicated input is ignored")
{
    //The unreliable channel is unordered, so an old packet arriving after a
    //newer one is routine. Applying it would rewind the player by one step for
    //no visible reason.
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& client = network.AddClient(peer);
    client.Send(LoopbackNetwork::ServerPeer, Encode(HelloMessage{}), Channel::Reliable);
    server.Step(FrameClock::FixedStepSeconds);

    const std::optional<WelcomeMessage> welcome = FindWelcome(client);
    REQUIRE(welcome.has_value());
    const PlayerId player = welcome->You;

    InputMessage newer;
    newer.Sequence = 10;
    newer.Input.Move = glm::vec2(0.0f, 1.0f);
    client.Send(LoopbackNetwork::ServerPeer, Encode(newer), Channel::Unreliable);
    server.Step(FrameClock::FixedStepSeconds);

    const glm::vec3 afterNewer = server.Match().Player(player).Position();

    //Sequence 9 arrives late. It must be dropped, so this step applies no
    //input at all and the player stands still.
    InputMessage stale;
    stale.Sequence = 9;
    stale.Input.Move = glm::vec2(0.0f, -1.0f);
    client.Send(LoopbackNetwork::ServerPeer, Encode(stale), Channel::Unreliable);
    server.Step(FrameClock::FixedStepSeconds);

    CHECK(server.Match().Player(player).Position().x == doctest::Approx(afterNewer.x));
    CHECK(server.Match().Player(player).Position().z == doctest::Approx(afterNewer.z));
}

TEST_CASE("A garbage packet is ignored rather than fatal")
{
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", 0xABCD, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& client = network.AddClient(peer);

    const std::vector<std::uint8_t> nonsense{ 0xFF, 0x00, 0x42 };
    client.Send(LoopbackNetwork::ServerPeer, nonsense, Channel::Reliable);

    server.Step(FrameClock::FixedStepSeconds);

    CHECK(server.Match().Tick() == 1);
    CHECK(server.Match().Players().empty());
}
```

- [ ] **Step 2: Run and watch it fail**

Expected: `Cannot open include file: 'Cubit/Net/MatchServer.h'`.

- [ ] **Step 3: Write `MatchServer.h`**

Create `Cubit/include/Cubit/Net/MatchServer.h`:

```cpp
#pragma once

#include "Cubit/Core.h"
#include "Cubit/Net/Protocol.h"
#include "Cubit/Net/Transport.h"
#include "Cubit/Voxel/BlockEdit.h"
#include "Cubit/Voxel/MatchState.h"
#include "Cubit/Voxel/World.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

//The authority. Owns the only MatchState anybody is entitled to believe.
//
//Holds no window, no renderer and no GL context, so it runs anywhere a World
//does. Server.exe is a bare main() around this, on the MapGen precedent.
class CB_API MatchServer
{
public:
    //`mapName` and `mapHash` are what joining clients are told to load and
    //check against. `spawn` is where every joining player is placed - players
    //do not collide with each other in this stage, so one point is enough.
    //`transport` must outlive this.
    MatchServer(World world, std::string mapName, std::uint64_t mapHash,
        const glm::vec3& spawn, Transport& transport);

    //One authoritative tick: service the transport, admit joiners, collect this
    //tick's inputs and edits, apply the edits in player-id order, step the
    //match, broadcast a snapshot.
    void Step(double seconds);

    const MatchState& Match() const { return m_Match; }

    //Every edit applied since construction, in application order. Sent to
    //joiners so a client arriving after somebody dug a hole sees the hole.
    const std::vector<BlockEdit>& EditLog() const { return m_EditLog; }

    //Connected peers that have completed the handshake.
    std::size_t ClientCount() const { return m_Clients.size(); }

private:
    //One connected participant. A peer exists from the moment the socket
    //connects; a Player only once Hello has been accepted.
    struct Client
    {
        PeerId Peer = InvalidPeer;
        PlayerId Player = InvalidPlayer;

        //Highest sequence applied. The unreliable channel is unordered, so
        //anything not strictly greater is stale or duplicated and is dropped.
        std::uint32_t LastSequence = 0;

        //This tick's input, if one arrived. Deliberately not carried over from
        //the previous tick: a lost input should cost one step of movement and
        //be visible, because that is what motivates Stage 3 bundling inputs
        //redundantly. Papering over it here would hide the very thing this
        //stage exists to show.
        bool HasInput = false;
        CharacterInput Input;

        //Last reported view angles, resent in every snapshot so remote
        //characters are drawn facing the right way.
        float Yaw = 0.0f;
        float Pitch = 0.0f;
    };

    //An edit waiting for this tick's ordered application.
    struct PendingEdit
    {
        PlayerId Player = InvalidPlayer;
        BlockEdit Edit;
    };

    void HandleConnected(PeerId peer);
    void HandleDisconnected(PeerId peer);
    void HandleMessage(PeerId peer, std::span<const std::uint8_t> data);

    //Applies this tick's edits in player-id order and broadcasts each one that
    //actually changed the world.
    void ApplyPendingEdits();

    void BroadcastSnapshot();

    //Returns the client for a peer, or nullptr when it has gone.
    Client* Find(PeerId peer);

    MatchState m_Match;
    std::string m_MapName;
    std::uint64_t m_MapHash = 0;
    glm::vec3 m_Spawn{ 0.0f };
    Transport& m_Transport;

    std::vector<Client> m_Clients;
    std::vector<PendingEdit> m_PendingEdits;
    std::vector<BlockEdit> m_EditLog;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
```

- [ ] **Step 4: Write `MatchServer.cpp`**

Create `Cubit/src/Net/MatchServer.cpp`:

```cpp
#include "cub.h"

#include "Cubit/Net/MatchServer.h"

#include "Cubit/Logger.h"

#include <algorithm>

MatchServer::MatchServer(World world, std::string mapName, std::uint64_t mapHash,
    const glm::vec3& spawn, Transport& transport)
    : m_Match(std::move(world)),
      m_MapName(std::move(mapName)),
      m_MapHash(mapHash),
      m_Spawn(spawn),
      m_Transport(transport)
{
}

void MatchServer::Step(double seconds)
{
    m_Transport.Advance(seconds);

    NetEvent event;
    while (m_Transport.Poll(event))
    {
        switch (event.Type)
        {
        case NetEventType::Connected:    HandleConnected(event.Peer); break;
        case NetEventType::Disconnected: HandleDisconnected(event.Peer); break;
        case NetEventType::Message:      HandleMessage(event.Peer, event.Data); break;
        case NetEventType::None:         break;
        }
    }

    //Before the step, so an edit and the movement that follows it in the same
    //tick see the same world.
    ApplyPendingEdits();

    std::vector<PlayerCommand> commands;
    commands.reserve(m_Clients.size());

    for (Client& client : m_Clients)
    {
        if (client.Player == InvalidPlayer || !client.HasInput)
            continue;

        commands.push_back(PlayerCommand{ client.Player, client.Input });
        client.HasInput = false;
    }

    //Sorted by player id so the step order does not depend on connection order
    //or on how the transport happened to schedule this tick's packets.
    std::sort(commands.begin(), commands.end(),
        [](const PlayerCommand& a, const PlayerCommand& b) { return a.Player < b.Player; });

    m_Match.Step(commands, static_cast<float>(seconds));

    BroadcastSnapshot();
}

void MatchServer::HandleConnected(PeerId peer)
{
    //A socket, not yet a player. The player is minted when Hello is accepted,
    //so a peer that connects and says nothing costs a slot and no simulation.
    Client client;
    client.Peer = peer;
    m_Clients.push_back(client);
}

void MatchServer::HandleDisconnected(PeerId peer)
{
    const auto found = std::find_if(m_Clients.begin(), m_Clients.end(),
        [peer](const Client& candidate) { return candidate.Peer == peer; });

    if (found == m_Clients.end())
        return;

    if (found->Player != InvalidPlayer)
        m_Match.RemovePlayer(found->Player);

    m_Clients.erase(found);
}

void MatchServer::HandleMessage(PeerId peer, std::span<const std::uint8_t> data)
{
    Client* client = Find(peer);
    if (client == nullptr)
        return;

    MessageId id = MessageId::Hello;
    if (!PeekMessageId(data, id))
        return;

    switch (id)
    {
    case MessageId::Hello:
    {
        HelloMessage hello;
        if (!Decode(data, hello))
            return;

        if (hello.Version != ProtocolVersion)
        {
            //Loud, and terminal. Continuing on a best-effort basis with a build
            //that disagrees about field widths produces garbage positions,
            //which read as a physics bug rather than a handshake failure.
            CB_WARN("Rejecting a client speaking a different protocol version");
            m_Transport.Disconnect(peer);

            //Looks redundant over loopback, where Disconnect queues a
            //Disconnected event this same drain loop will pick up. It is not:
            //ENet's disconnect_now generates no local event, so without this
            //the ejected peer would linger in m_Clients for ever. The second
            //call, when it happens, finds nothing and does nothing.
            HandleDisconnected(peer);
            return;
        }

        //Already joined: a repeated Hello is ignored rather than minting a
        //second player for one socket.
        if (client->Player != InvalidPlayer)
            return;

        client->Player = m_Match.AddPlayer(m_Spawn);

        WelcomeMessage welcome;
        welcome.You = client->Player;
        welcome.MapName = m_MapName;
        welcome.MapHash = m_MapHash;
        welcome.Tick = m_Match.Tick();
        welcome.Edits = m_EditLog;
        m_Transport.Send(peer, Encode(welcome), Channel::Reliable);
        return;
    }

    case MessageId::Input:
    {
        InputMessage input;
        if (!Decode(data, input) || client->Player == InvalidPlayer)
            return;

        //Strictly greater, so a duplicate is dropped alongside a stale one.
        if (input.Sequence <= client->LastSequence)
            return;

        client->LastSequence = input.Sequence;
        client->HasInput = true;
        client->Input = input.Input;
        client->Yaw = input.Input.Yaw;
        client->Pitch = input.Input.Pitch;
        return;
    }

    case MessageId::EditRequest:
    {
        EditMessage edit;
        if (!Decode(data, edit) || client->Player == InvalidPlayer)
            return;

        m_PendingEdits.push_back(PendingEdit{ client->Player, edit.Edit });
        return;
    }

    //Server-to-client messages arriving at a server are malformed traffic, not
    //something to act on.
    case MessageId::Welcome:
    case MessageId::Snapshot:
    case MessageId::EditApplied:
        return;
    }
}

void MatchServer::ApplyPendingEdits()
{
    if (m_PendingEdits.empty())
        return;

    //Player-id order, not arrival order. Arrival order is socket scheduling,
    //which is not reproducible - two clients editing one block on one tick
    //would resolve differently run to run, and every test touching edits would
    //be a coin flip. stable_sort so two edits from one player keep the order
    //that player sent them in.
    std::stable_sort(m_PendingEdits.begin(), m_PendingEdits.end(),
        [](const PendingEdit& a, const PendingEdit& b) { return a.Player < b.Player; });

    for (const PendingEdit& pending : m_PendingEdits)
    {
        //Nothing came back: the position was out of range, or the block was
        //already what the client asked for. Either way the world did not
        //change, so there is nothing to log and nothing to tell anybody.
        const std::optional<BlockEdit> inverse = ApplyBlockEdit(m_Match.GetWorld(), pending.Edit);
        if (!inverse.has_value())
            continue;

        m_EditLog.push_back(pending.Edit);

        EditMessage applied;
        applied.Edit = pending.Edit;

        //Everyone, the requester included. A client's own world changes only
        //when this arrives, which is what makes the round trip visible.
        m_Transport.Broadcast(EncodeEditApplied(applied), Channel::Reliable);
    }

    m_PendingEdits.clear();
}

void MatchServer::BroadcastSnapshot()
{
    SnapshotMessage snapshot;
    snapshot.Tick = m_Match.Tick();
    snapshot.Players.reserve(m_Match.Players().size());

    for (const auto& [player, character] : m_Match.Players())
    {
        PlayerSnapshot entry;
        entry.Player = player;
        entry.Position = character.Position();
        entry.VerticalVelocity = character.VerticalVelocity();
        entry.Grounded = character.Grounded();

        //Angles live on the client record rather than on the character, because
        //CharacterController does not store them - they arrive in the input and
        //are consumed by the step.
        const auto owner = std::find_if(m_Clients.begin(), m_Clients.end(),
            [player](const Client& candidate) { return candidate.Player == player; });

        if (owner != m_Clients.end())
        {
            entry.Yaw = owner->Yaw;
            entry.Pitch = owner->Pitch;
        }

        snapshot.Players.push_back(entry);
    }

    //Unreliable: the next one supersedes this one, so resending a stale
    //snapshot spends bandwidth delivering something already out of date.
    m_Transport.Broadcast(Encode(snapshot), Channel::Unreliable);
}

MatchServer::Client* MatchServer::Find(PeerId peer)
{
    const auto found = std::find_if(m_Clients.begin(), m_Clients.end(),
        [peer](const Client& candidate) { return candidate.Peer == peer; });

    return found == m_Clients.end() ? nullptr : &*found;
}
```

`<optional>` and `<span>` arrive through `Protocol.h` and `BlockEdit.h`; add explicit includes if the compiler disagrees.

- [ ] **Step 5: Export, regenerate, build, watch the tests pass**

Add `#include "Cubit/Net/MatchServer.h"` to `Cubit/include/Cubit/Cubit.h`, regenerate, build.
Expected: PASS.

- [ ] **Step 6: Prove the staleness test is not tautological**

Temporarily change `if (input.Sequence <= client->LastSequence)` to `if (false)`. Rebuild. **"A stale or duplicated input is ignored"** must go red. Restore.

- [ ] **Step 7: Commit**

```bash
git add Cubit/include/Cubit/Net/MatchServer.h Cubit/src/Net/MatchServer.cpp \
        Tests/src/MatchServerTests.cpp Cubit/include/Cubit/Cubit.h
git commit -m "Make one machine the authority"
```

---

### Task 9: `MatchClient`, and the stage's oracle

**The client never calls `Step`.** It owns a `MatchState` purely as a receptacle: input goes up, snapshots come down and are written straight in. That is what makes Stage 3's diff one sentence — *start calling `Step`, and replay unacknowledged inputs after each snapshot* — and it is only one sentence because this task leaves the socket clean.

**Files:**
- Create: `Cubit/include/Cubit/Net/MatchClient.h`, `Cubit/src/Net/MatchClient.cpp`
- Create: `Tests/src/WireOracleTests.cpp`
- Modify: `Cubit/include/Cubit/Cubit.h`

**Interfaces:**
- Consumes: everything from Tasks 1 and 3–8.
- Produces:
  - `struct LoadedMap { World Map; std::uint64_t Hash; }`
  - `using MapLoader = std::function<std::optional<LoadedMap>(const std::string& name)>`
  - `MatchClient(Transport& transport, MapLoader loadMap)`
  - `void Step(double seconds)`, `void SetInput(const CharacterInput&)`, `void RequestEdit(const BlockEdit&)`
  - `bool Connected() const`, `bool Rejected() const`, `PlayerId LocalPlayer() const`
  - `const MatchState& Match() const`, `glm::vec2 ViewAngles(PlayerId) const`, `double RoundTripTime() const`

- [ ] **Step 1: Write the failing test**

Create `Tests/src/WireOracleTests.cpp`:

```cpp
#include <doctest.h>

#include "Cubit/FrameClock.h"
#include "Cubit/Net/LoopbackTransport.h"
#include "Cubit/Net/MatchClient.h"
#include "Cubit/Net/MatchServer.h"
#include "Cubit/Net/SimulatedTransport.h"

#include <glm/glm.hpp>
#include <map>
#include <optional>
#include <vector>

namespace
{
    //50 ms one-way at 60 Hz is exactly 3 ticks, so the oracle asserts exact
    //equality against an offset history rather than equality within a
    //tolerance. Every latency in this suite is a whole tick multiple.
    constexpr int LatencyTicks = 3;
    constexpr double OneWayLatency = LatencyTicks * FrameClock::FixedStepSeconds;

    constexpr std::uint64_t MapHash = 0xFEEDFACEull;
    const glm::vec3 Spawn{ 8.0f, 2.0f, 8.0f };

    World FlatWorld()
    {
        World world(2, 2, 2);

        for (int z = 0; z < world.GetDepth(); ++z)
            for (int x = 0; x < world.GetWidth(); ++x)
                world.SetBlock(x, 0, z, BlockId{ 1 });

        return world;
    }

    //A loader that always succeeds with the same world the server is running.
    MatchClient::MapLoader GoodLoader()
    {
        return [](const std::string&) -> std::optional<LoadedMap>
        {
            return LoadedMap{ FlatWorld(), MapHash };
        };
    }

    bool WorldsMatch(const World& a, const World& b)
    {
        if (a.GetWidth() != b.GetWidth() ||
            a.GetHeight() != b.GetHeight() ||
            a.GetDepth() != b.GetDepth())
            return false;

        for (int y = 0; y < a.GetHeight(); ++y)
            for (int z = 0; z < a.GetDepth(); ++z)
                for (int x = 0; x < a.GetWidth(); ++x)
                    if (a.GetBlock(x, y, z) != b.GetBlock(x, y, z))
                        return false;

        return true;
    }

    CharacterInput Walking(float yaw)
    {
        CharacterInput input;
        input.Move = glm::vec2(0.0f, 1.0f);
        input.Yaw = yaw;
        return input;
    }
}

TEST_CASE("A client's state is the server's state, delayed by exactly the one-way latency")
{
    //THE ORACLE FOR THIS STAGE.
    //
    //Stage 1's was "MatchState must agree with bare CharacterControllers fed
    //the same inputs". This is its successor: a client driven only by
    //snapshots must equal the server's own history, offset by how long a
    //snapshot takes to arrive.
    //
    //It is written as two separate exact assertions rather than one combined
    //one, because they fail for different reasons. The first catches a wire
    //that corrupts or reorders state. The second catches a wire that delivers
    //the right state at the wrong time.
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = OneWayLatency;

    SimulatedTransport serverNet(network.Server(), sim);

    PeerId peer = InvalidPeer;
    SimulatedTransport clientNet(network.AddClient(peer), sim);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, serverNet);
    MatchClient client(clientNet, GoodLoader());

    //Tick -> the server's own record of where everybody was at that tick.
    std::map<std::uint64_t, std::vector<std::pair<PlayerId, glm::vec3>>> history;

    std::vector<std::uint64_t> observedSkew;

    for (int i = 0; i < 400; ++i)
    {
        //ORDER MATTERS AND IS PART OF THE ASSERTION. The client sends and
        //applies first, then the server receives and steps. If the skew below
        //is not exactly LatencyTicks, do NOT simply change the constant -
        //confirm this loop order first, because an unexpected offset means a
        //packet is being serviced in the wrong phase, which is a real bug.
        client.SetInput(Walking(90.0f));
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);

        std::vector<std::pair<PlayerId, glm::vec3>> row;
        for (const auto& [player, character] : server.Match().Players())
            row.emplace_back(player, character.Position());
        history[server.Match().Tick()] = row;

        if (!client.Connected())
            continue;

        //ASSERTION ONE: whatever tick the client believes it is at, its state
        //must be the server's state at that exact tick. Exact equality, not
        //Approx: these are the same floats, round-tripped through the codec,
        //not two independent computations that might drift.
        const auto recorded = history.find(client.Match().Tick());
        if (recorded != history.end())
        {
            std::vector<std::pair<PlayerId, glm::vec3>> mine;
            for (const auto& [player, character] : client.Match().Players())
                mine.emplace_back(player, character.Position());

            CHECK(mine == recorded->second);
        }

        //Sampled after warm-up, once the pipeline is full.
        if (i > 60)
            observedSkew.push_back(server.Match().Tick() - client.Match().Tick());
    }

    REQUIRE_FALSE(observedSkew.empty());

    //ASSERTION TWO: the delay is constant and is the one-way latency. A
    //snapshot is LatencyTicks old when it arrives. Your own keypress takes
    //twice that to show up, because the input must go up before the snapshot
    //reflecting it can come down - that second number is what the stage makes
    //you feel, and it is deliberately not hidden.
    for (const std::uint64_t skew : observedSkew)
        CHECK(skew == LatencyTicks);
}

TEST_CASE("The client never steps the simulation itself")
{
    //The defining constraint of Stage 2. If this fails, prediction has grown
    //by accident, the latency is being hidden, and Stage 3 will begin from a
    //half-built reconciliation loop instead of a clean one.
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    SimulatedTransport serverNet(network.Server(), sim);

    PeerId peer = InvalidPeer;
    SimulatedTransport clientNet(network.AddClient(peer), sim);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, serverNet);
    MatchClient client(clientNet, GoodLoader());

    for (int i = 0; i < 200; ++i)
    {
        client.SetInput(Walking(90.0f));
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);

        //A client that stepped would run ahead of the last snapshot it was
        //told about, and its tick would exceed the server's.
        CHECK(client.Match().Tick() <= server.Match().Tick());
    }
}

TEST_CASE("Two clients see each other move")
{
    //Why two and not one: a snapshot format designed around exactly one
    //character is the same "abstraction over a single instance" mistake this
    //project has declined twice. Two forces the snapshot to be a collection.
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    SimulatedTransport serverNet(network.Server(), sim);

    PeerId firstPeer = InvalidPeer;
    PeerId secondPeer = InvalidPeer;
    SimulatedTransport firstNet(network.AddClient(firstPeer), sim);
    SimulatedTransport secondNet(network.AddClient(secondPeer), sim);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, serverNet);
    MatchClient first(firstNet, GoodLoader());
    MatchClient second(secondNet, GoodLoader());

    for (int i = 0; i < 200; ++i)
    {
        first.SetInput(Walking(90.0f));
        second.SetInput(Walking(-90.0f));
        first.Step(FrameClock::FixedStepSeconds);
        second.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }

    REQUIRE(first.Connected());
    REQUIRE(second.Connected());

    //Each holds both players.
    CHECK(first.Match().Players().size() == 2);
    CHECK(second.Match().Players().size() == 2);

    //And each sees the other somewhere other than the spawn.
    CHECK(first.Match().Player(second.LocalPlayer()).Position() != Spawn);
    CHECK(second.Match().Player(first.LocalPlayer()).Position() != Spawn);

    //The angles travelled too, which is what lets a remote character be drawn
    //facing the right way.
    CHECK(first.ViewAngles(second.LocalPlayer()).x == doctest::Approx(-90.0f));
}

TEST_CASE("An edit takes a round trip and is not applied locally first")
{
    //The visible round trip, asserted rather than felt. The requester's own
    //world changes only when the server's broadcast arrives.
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    SimulatedTransport serverNet(network.Server(), sim);

    PeerId peer = InvalidPeer;
    SimulatedTransport clientNet(network.AddClient(peer), sim);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, serverNet);
    MatchClient client(clientNet, GoodLoader());

    for (int i = 0; i < 30; ++i)
    {
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }
    REQUIRE(client.Connected());

    const glm::ivec3 target(4, 0, 4);
    REQUIRE(client.Match().GetWorld().GetBlock(4, 0, 4) == BlockId{ 1 });

    client.RequestEdit(BlockEdit{ target, BlockId{ 0 } });

    //One tick later nothing has happened locally: the request has not even
    //reached the server yet.
    client.Step(FrameClock::FixedStepSeconds);
    server.Step(FrameClock::FixedStepSeconds);
    CHECK(client.Match().GetWorld().GetBlock(4, 0, 4) == BlockId{ 1 });

    for (int i = 0; i < 30; ++i)
    {
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }

    CHECK(client.Match().GetWorld().GetBlock(4, 0, 4) == BlockId{ 0 });
    CHECK(server.Match().GetWorld().GetBlock(4, 0, 4) == BlockId{ 0 });
    CHECK(server.EditLog().size() == 1);
}

TEST_CASE("A client joining late gets a world matching everyone else's, block for block")
{
    //Without the edit log in Welcome, a client arriving after somebody dug a
    //hole would get a pristine world and then collide against terrain nobody
    //else has. The symptom would look like a prediction bug and is not.
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    SimulatedTransport serverNet(network.Server(), sim);

    PeerId firstPeer = InvalidPeer;
    SimulatedTransport firstNet(network.AddClient(firstPeer), sim);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, serverNet);
    MatchClient first(firstNet, GoodLoader());

    for (int i = 0; i < 30; ++i)
    {
        first.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }
    REQUIRE(first.Connected());

    //Dig a trench.
    for (int x = 0; x < 20; ++x)
    {
        first.RequestEdit(BlockEdit{ glm::ivec3(x, 0, 6), BlockId{ 0 } });
        first.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }

    for (int i = 0; i < 40; ++i)
    {
        first.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }
    REQUIRE(server.EditLog().size() == 20);

    //Now somebody arrives.
    PeerId secondPeer = InvalidPeer;
    SimulatedTransport secondNet(network.AddClient(secondPeer), sim);
    MatchClient second(secondNet, GoodLoader());

    for (int i = 0; i < 40; ++i)
    {
        first.Step(FrameClock::FixedStepSeconds);
        second.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }

    REQUIRE(second.Connected());
    CHECK(WorldsMatch(second.Match().GetWorld(), server.Match().GetWorld()));
    CHECK(WorldsMatch(second.Match().GetWorld(), first.Match().GetWorld()));
}

TEST_CASE("Two clients editing the same block on the same tick converge")
{
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    SimulatedTransport serverNet(network.Server(), sim);

    PeerId firstPeer = InvalidPeer;
    PeerId secondPeer = InvalidPeer;
    SimulatedTransport firstNet(network.AddClient(firstPeer), sim);
    SimulatedTransport secondNet(network.AddClient(secondPeer), sim);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, serverNet);
    MatchClient first(firstNet, GoodLoader());
    MatchClient second(secondNet, GoodLoader());

    for (int i = 0; i < 30; ++i)
    {
        first.Step(FrameClock::FixedStepSeconds);
        second.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }
    REQUIRE(first.Connected());
    REQUIRE(second.Connected());

    const glm::ivec3 contested(5, 0, 5);
    first.RequestEdit(BlockEdit{ contested, BlockId{ 0 } });
    second.RequestEdit(BlockEdit{ contested, BlockId{ 2 } });

    for (int i = 0; i < 40; ++i)
    {
        first.Step(FrameClock::FixedStepSeconds);
        second.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }

    //Both agree with the server, whichever won. Player-id order decides, and
    //it decides the same way every run.
    CHECK(WorldsMatch(first.Match().GetWorld(), server.Match().GetWorld()));
    CHECK(WorldsMatch(second.Match().GetWorld(), server.Match().GetWorld()));
}

TEST_CASE("A client with the wrong map is refused at the handshake")
{
    //Loudly, and now - not silently, an hour later, as movement that disagrees
    //with the server for reasons that look like a netcode bug.
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& clientNet = network.AddClient(peer);

    MatchClient client(clientNet,
        [](const std::string&) -> std::optional<LoadedMap>
        {
            return LoadedMap{ FlatWorld(), MapHash + 1 };
        });

    for (int i = 0; i < 20; ++i)
    {
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }

    CHECK(client.Rejected());
    CHECK_FALSE(client.Connected());
}

TEST_CASE("A client that cannot find the map at all is refused too")
{
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& clientNet = network.AddClient(peer);

    MatchClient client(clientNet,
        [](const std::string&) -> std::optional<LoadedMap> { return std::nullopt; });

    for (int i = 0; i < 20; ++i)
    {
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }

    CHECK(client.Rejected());
    CHECK_FALSE(client.Connected());
}

TEST_CASE("The wire survives 5% loss and 150 ms RTT with jitter")
{
    //The bad-network run. Snapshots are unreliable, so some are simply lost;
    //the next one supersedes them, and the client must end up where the server
    //says regardless.
    LoopbackNetwork network;

    NetworkSim sim;
    sim.Latency = OneWayLatency;
    sim.Jitter = FrameClock::FixedStepSeconds;
    sim.Loss = 0.05f;
    sim.Seed = 1;

    SimulatedTransport serverNet(network.Server(), sim);
    PeerId peer = InvalidPeer;
    SimulatedTransport clientNet(network.AddClient(peer), sim);

    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, serverNet);
    MatchClient client(clientNet, GoodLoader());

    for (int i = 0; i < 300; ++i)
    {
        client.SetInput(Walking(90.0f));
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }

    //Stop moving and let everything drain, so the two must agree exactly.
    for (int i = 0; i < 120; ++i)
    {
        client.SetInput(CharacterInput{});
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }

    REQUIRE(client.Connected());
    CHECK(client.Match().Player(client.LocalPlayer()).Position()
        == server.Match().Player(client.LocalPlayer()).Position());
}

TEST_CASE("A stale snapshot never overwrites a newer one")
{
    //Jitter reorders packets on the unreliable channel. Applying an older
    //snapshot after a newer one would yank the world backwards, which on screen
    //is indistinguishable from a physics fault.
    LoopbackNetwork network;
    MatchServer server(FlatWorld(), "flat.vox", MapHash, Spawn, network.Server());

    PeerId peer = InvalidPeer;
    Transport& raw = network.AddClient(peer);
    MatchClient client(raw, GoodLoader());

    for (int i = 0; i < 10; ++i)
    {
        client.Step(FrameClock::FixedStepSeconds);
        server.Step(FrameClock::FixedStepSeconds);
    }
    REQUIRE(client.Connected());

    const std::uint64_t reached = client.Match().Tick();

    //Hand-deliver a snapshot from the past, straight into the client's inbox.
    SnapshotMessage old;
    old.Tick = reached - 5;
    PlayerSnapshot entry;
    entry.Player = client.LocalPlayer();
    entry.Position = glm::vec3(999.0f, 999.0f, 999.0f);
    old.Players.push_back(entry);

    network.Server().Send(peer, Encode(old), Channel::Unreliable);
    client.Step(FrameClock::FixedStepSeconds);

    CHECK(client.Match().Tick() >= reached);
    CHECK(client.Match().Player(client.LocalPlayer()).Position()
        != glm::vec3(999.0f, 999.0f, 999.0f));
}
```

- [ ] **Step 2: Run and watch it fail**

Expected: `Cannot open include file: 'Cubit/Net/MatchClient.h'`.

- [ ] **Step 3: Write `MatchClient.h`**

Create `Cubit/include/Cubit/Net/MatchClient.h`:

```cpp
#pragma once

#include "Cubit/Core.h"
#include "Cubit/Net/Protocol.h"
#include "Cubit/Net/Transport.h"
#include "Cubit/Voxel/BlockEdit.h"
#include "Cubit/Voxel/MatchState.h"
#include "Cubit/Voxel/World.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

//A map the client found on its own disk, and the hash of the bytes it came
//from. The hash is checked against the server's before anything is trusted.
struct LoadedMap
{
    World Map;
    std::uint64_t Hash = 0;
};

//The client half of a match. It NEVER STEPS.
//
//That is the defining constraint of Stage 2, not an omission. Input goes up,
//snapshots come down and are written straight in, so the latency is plainly
//visible instead of hidden behind a guess. It makes Stage 3's diff one
//sentence: start calling Step, and replay unacknowledged inputs after each
//snapshot.
//
//It holds a MatchState anyway, for two reasons: it needs a World to render and
//a roster to draw, and Stage 3 needs somewhere to start stepping.
class CB_API MatchClient
{
public:
    //Called with the map name from Welcome. Returns the world to display and
    //the hash of the file it came from, or nothing when the map is missing.
    //
    //A callback rather than a path, so a test can hand over a trivial world
    //while the Sandbox reads 23.8 MB of .vox from disk.
    using MapLoader = std::function<std::optional<LoadedMap>(const std::string& mapName)>;

    //`transport` must outlive this.
    MatchClient(Transport& transport, MapLoader loadMap);

    //Services the transport, sends this frame's input, and applies whatever
    //arrived. Deliberately does not advance the simulation.
    void Step(double seconds);

    //What to send on the next Step. Held rather than sent immediately so the
    //caller can set it whenever it likes without deciding the send rate.
    void SetInput(const CharacterInput& input);

    //Asks the server to change a block. Nothing happens locally until the
    //server's answer arrives - that round trip is the point.
    void RequestEdit(const BlockEdit& edit);

    //True once Welcome has been accepted and the world is loaded.
    bool Connected() const { return m_Connected; }

    //True when the handshake failed: a protocol mismatch, a missing map, or a
    //map whose bytes differ from the server's. Terminal.
    bool Rejected() const { return m_Rejected; }

    PlayerId LocalPlayer() const { return m_LocalPlayer; }

    const MatchState& Match() const { return m_Match; }
    MatchState& MatchForWrite() { return m_Match; }

    //Yaw in x, pitch in y, as last reported for this player. Zero for anyone
    //not in the last snapshot.
    glm::vec2 ViewAngles(PlayerId player) const;

    double RoundTripTime() const;

private:
    void HandleWelcome(std::span<const std::uint8_t> data);
    void HandleSnapshot(std::span<const std::uint8_t> data);
    void HandleEditApplied(std::span<const std::uint8_t> data);

    //Ends the connection and latches Rejected.
    void Reject(const char* reason);

    Transport& m_Transport;
    MapLoader m_LoadMap;

    //A placeholder until Welcome arrives with the real map, matching what the
    //Sandbox already does. MatchState needs a World to exist at all.
    MatchState m_Match{ World(1, 1, 1) };

    PeerId m_ServerPeer = InvalidPeer;
    PlayerId m_LocalPlayer = InvalidPlayer;

    bool m_SaidHello = false;
    bool m_Connected = false;
    bool m_Rejected = false;

    //Monotonic, and NOT a tick. In this stage the client never steps, so it has
    //no simulation tick to name; the server uses this only to drop stale and
    //duplicated packets on an unordered channel.
    std::uint32_t m_Sequence = 0;
    CharacterInput m_Input;
    bool m_HasInput = false;

    //Highest snapshot tick applied. Jitter reorders packets, and applying an
    //older snapshot after a newer one yanks the world backwards.
    std::uint64_t m_LastSnapshotTick = 0;

    std::map<PlayerId, glm::vec2> m_ViewAngles;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
```

- [ ] **Step 4: Write `MatchClient.cpp`**

Create `Cubit/src/Net/MatchClient.cpp`:

```cpp
#include "cub.h"

#include "Cubit/Net/MatchClient.h"

#include "Cubit/Logger.h"

#include <vector>

MatchClient::MatchClient(Transport& transport, MapLoader loadMap)
    : m_Transport(transport), m_LoadMap(std::move(loadMap))
{
}

void MatchClient::Step(double seconds)
{
    m_Transport.Advance(seconds);

    NetEvent event;
    while (m_Transport.Poll(event))
    {
        switch (event.Type)
        {
        case NetEventType::Connected:
        {
            m_ServerPeer = event.Peer;

            if (!m_SaidHello)
            {
                m_Transport.Send(m_ServerPeer, Encode(HelloMessage{}), Channel::Reliable);
                m_SaidHello = true;
            }
            break;
        }

        case NetEventType::Disconnected:
            m_Connected = false;
            break;

        case NetEventType::Message:
        {
            MessageId id = MessageId::Hello;
            if (!PeekMessageId(event.Data, id))
                break;

            switch (id)
            {
            case MessageId::Welcome:     HandleWelcome(event.Data); break;
            case MessageId::Snapshot:    HandleSnapshot(event.Data); break;
            case MessageId::EditApplied: HandleEditApplied(event.Data); break;

            //Client-to-server messages arriving at a client are malformed
            //traffic, not something to act on.
            case MessageId::Hello:
            case MessageId::Input:
            case MessageId::EditRequest:
                break;
            }
            break;
        }

        case NetEventType::None:
            break;
        }
    }

    if (!m_Connected || !m_HasInput)
        return;

    InputMessage message;
    message.Sequence = ++m_Sequence;
    message.Input = m_Input;

    //Unreliable: a lost input costs one step of movement, which is a small
    //stutter and is honest. Resending it would deliver an intent the player
    //has already replaced.
    m_Transport.Send(m_ServerPeer, Encode(message), Channel::Unreliable);
    m_HasInput = false;
}

void MatchClient::SetInput(const CharacterInput& input)
{
    m_Input = input;
    m_HasInput = true;
}

void MatchClient::RequestEdit(const BlockEdit& edit)
{
    if (!m_Connected)
        return;

    EditMessage message;
    message.Edit = edit;

    //Reliable: an edit that arrives late is still correct, but one that
    //vanishes is a permanent world desync.
    m_Transport.Send(m_ServerPeer, EncodeEditRequest(message), Channel::Reliable);
}

void MatchClient::HandleWelcome(std::span<const std::uint8_t> data)
{
    WelcomeMessage welcome;
    if (!Decode(data, welcome))
        return;

    //A second Welcome is a server fault or a replayed packet; either way the
    //world has already been built and rebuilding it would discard live state.
    if (m_Connected)
        return;

    std::optional<LoadedMap> loaded = m_LoadMap(welcome.MapName);
    if (!loaded.has_value())
    {
        Reject("the server's map is not present on this machine");
        return;
    }

    if (loaded->Hash != welcome.MapHash)
    {
        //Now, loudly. A mismatched map desyncs silently an hour later as
        //movement that disagrees with the server, which reads as a netcode bug
        //and is not one.
        Reject("this machine's copy of the map differs from the server's");
        return;
    }

    m_Match.ReplaceWorld(std::move(loaded->Map));
    m_Match.SetTick(welcome.Tick);
    m_LastSnapshotTick = welcome.Tick;

    //Replay the edits applied since the map loaded. Without this, a client
    //joining after somebody dug a hole gets a pristine world.
    for (const BlockEdit& edit : welcome.Edits)
        ApplyBlockEdit(m_Match.GetWorld(), edit);

    m_LocalPlayer = welcome.You;
    m_Connected = true;
}

void MatchClient::HandleSnapshot(std::span<const std::uint8_t> data)
{
    if (!m_Connected)
        return;

    SnapshotMessage snapshot;
    if (!Decode(data, snapshot))
        return;

    //Jitter reorders the unreliable channel. An older snapshot arriving after
    //a newer one must be discarded, not applied: rewinding the world reads on
    //screen as a physics fault.
    if (snapshot.Tick < m_LastSnapshotTick)
        return;

    m_LastSnapshotTick = snapshot.Tick;
    m_Match.SetTick(snapshot.Tick);

    std::vector<PlayerId> present;
    present.reserve(snapshot.Players.size());

    for (const PlayerSnapshot& entry : snapshot.Players)
    {
        present.push_back(entry.Player);

        if (!m_Match.HasPlayer(entry.Player))
        {
            //Derived from the roster rather than announced. Ids are never
            //reused, so a player appearing in a snapshot is a join and one
            //disappearing is a leave - which is why the protocol has no
            //message for either.
            m_Match.AddPlayer(entry.Player, entry.Position);
        }

        //The previous position is the last one this client knew about, so the
        //renderer's existing alpha interpolation smooths between snapshots
        //rather than snapping. At 60 Hz that gap is exactly one fixed step,
        //which is what the interpolation already assumes.
        const glm::vec3 previous = m_Match.Player(entry.Player).Position();

        m_Match.PlayerForWrite(entry.Player).SetState(
            entry.Position, previous, entry.VerticalVelocity, entry.Grounded);

        m_ViewAngles[entry.Player] = glm::vec2(entry.Yaw, entry.Pitch);
    }

    //Anybody the snapshot did not mention has left.
    std::vector<PlayerId> departed;
    for (const auto& [player, character] : m_Match.Players())
    {
        (void)character;
        if (std::find(present.begin(), present.end(), player) == present.end())
            departed.push_back(player);
    }

    for (const PlayerId player : departed)
    {
        m_Match.RemovePlayer(player);
        m_ViewAngles.erase(player);
    }
}

void MatchClient::HandleEditApplied(std::span<const std::uint8_t> data)
{
    if (!m_Connected)
        return;

    EditMessage message;
    if (!Decode(data, message))
        return;

    ApplyBlockEdit(m_Match.GetWorld(), message.Edit);
}

void MatchClient::Reject(const char* reason)
{
    CB_ERROR(std::string("Refusing to join: ") + reason);

    m_Rejected = true;
    m_Connected = false;

    if (m_ServerPeer != InvalidPeer)
        m_Transport.Disconnect(m_ServerPeer);
}

glm::vec2 MatchClient::ViewAngles(PlayerId player) const
{
    const auto found = m_ViewAngles.find(player);
    return found == m_ViewAngles.end() ? glm::vec2(0.0f) : found->second;
}

double MatchClient::RoundTripTime() const
{
    return m_ServerPeer == InvalidPeer ? 0.0 : m_Transport.RoundTripTime(m_ServerPeer);
}
```

Add `#include <algorithm>` below the `cub.h` include if `std::find` does not resolve.

- [ ] **Step 5: Export, regenerate, build, watch the tests pass**

Add `#include "Cubit/Net/MatchClient.h"` to `Cubit/include/Cubit/Cubit.h`, regenerate, build.
Expected: PASS.

If the skew assertion reports a number other than 3, **read the note in the test before touching the constant.** Confirm the loop order in the test matches the one written there, and that `MatchClient::Step` services the transport before sending. An unexpected offset almost always means a packet is serviced in the wrong phase.

- [ ] **Step 6: Prove the two hardest invariants are not tautological**

Three deliberate breaks, each of which must turn exactly one test red:

1. Delete `if (snapshot.Tick < m_LastSnapshotTick) return;` → **"A stale snapshot never overwrites a newer one"** goes red.
2. In `HandleSnapshot`, pass `entry.Position` as the previous position too → **"A client's state is the server's state..."** stays green (positions still match) but this is worth seeing: it proves the oracle does *not* cover interpolation, which is why the `SetState` test in Task 1 exists separately.
3. Make `MatchClient::Step` call `m_Match.Step(...)` with the local input → **"The client never steps the simulation itself"** goes red, and so does the oracle. That is the whole stage's guard rail; confirm it fires.

Restore after each.

- [ ] **Step 7: Commit**

```bash
git add Cubit/include/Cubit/Net/MatchClient.h Cubit/src/Net/MatchClient.cpp \
        Tests/src/WireOracleTests.cpp Cubit/include/Cubit/Cubit.h
git commit -m "Show the server's world without guessing at it"
```

---

### Task 10: `EnetTransport`

Everything above is proved with no socket. This is the one place a socket appears, and it gets exactly one test — because without it, nothing automated proves ENet is wired up at all.

**Files:**
- Create: `Cubit/include/Cubit/Net/EnetTransport.h`, `Cubit/src/Net/EnetTransport.cpp`
- Create: `Tests/src/EnetTransportTests.cpp`
- Modify: `Cubit/include/Cubit/Cubit.h`

**Interfaces:**
- Consumes: `Transport` (Task 5), vendored ENet (Task 2).
- Produces:
  - `EnetTransport::Listen(std::uint16_t port, std::size_t maxClients)` → `std::unique_ptr<EnetTransport>`, null on failure
  - `EnetTransport::Connect(const std::string& host, std::uint16_t port)` → `std::unique_ptr<EnetTransport>`, null on failure
  - `static constexpr PeerId EnetServerPeer = 1`

- [ ] **Step 1: Write the failing test**

Create `Tests/src/EnetTransportTests.cpp`:

```cpp
#include <doctest.h>

#include "Cubit/FrameClock.h"
#include "Cubit/Net/EnetTransport.h"

#include <cstdint>
#include <memory>
#include <vector>

TEST_CASE("A server and two clients talk over a real socket")
{
    //THE ONLY TEST IN THE SUITE THAT TOUCHES THE OS NETWORK STACK.
    //
    //It may trip a Windows firewall prompt the first time it runs. It earns
    //that: every other net test runs over LoopbackTransport, so without this
    //one nothing would prove EnetTransport is wired up correctly, and the
    //failure would surface as a Sandbox that silently never connects.
    //
    //Port 27960 is high, unregistered, and not one anything else here uses.
    constexpr std::uint16_t Port = 27960;

    std::unique_ptr<EnetTransport> server = EnetTransport::Listen(Port, 4);
    REQUIRE(server != nullptr);

    std::unique_ptr<EnetTransport> first = EnetTransport::Connect("127.0.0.1", Port);
    std::unique_ptr<EnetTransport> second = EnetTransport::Connect("127.0.0.1", Port);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);

    const auto Pump = [&](int steps)
    {
        for (int i = 0; i < steps; ++i)
        {
            server->Advance(FrameClock::FixedStepSeconds);
            first->Advance(FrameClock::FixedStepSeconds);
            second->Advance(FrameClock::FixedStepSeconds);
        }
    };

    //Bounded rather than open-ended: a hang here must fail the suite, not
    //stall the build for ever.
    std::vector<PeerId> connected;
    for (int attempt = 0; attempt < 600 && connected.size() < 2; ++attempt)
    {
        Pump(1);

        NetEvent event;
        while (server->Poll(event))
        {
            if (event.Type == NetEventType::Connected)
                connected.push_back(event.Peer);
        }
    }

    REQUIRE(connected.size() == 2);
    CHECK(connected[0] != connected[1]);

    //Drain each client's own Connected event.
    NetEvent ignored;
    while (first->Poll(ignored)) {}
    while (second->Poll(ignored)) {}

    const std::vector<std::uint8_t> payload{ 1, 2, 3, 4 };
    server->Broadcast(payload, Channel::Reliable);
    Pump(60);

    const auto ReceivedPayload = [&payload](EnetTransport& transport)
    {
        NetEvent event;
        while (transport.Poll(event))
        {
            if (event.Type == NetEventType::Message && event.Data == payload)
                return true;
        }
        return false;
    };

    CHECK(ReceivedPayload(*first));
    CHECK(ReceivedPayload(*second));
}
```

- [ ] **Step 2: Run and watch it fail**

Expected: `Cannot open include file: 'Cubit/Net/EnetTransport.h'`.

- [ ] **Step 3: Write `EnetTransport.h`**

Create `Cubit/include/Cubit/Net/EnetTransport.h`:

```cpp
#pragma once

#include "Cubit/Core.h"
#include "Cubit/Net/Transport.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

struct _ENetHost;
struct _ENetPeer;

//A Transport over real UDP sockets, via ENet.
//
//ENet provides exactly the two channel types this design needs, plus handshake,
//timeout, MTU discovery, sequencing, fragmentation and acknowledgement. Hand
//rolling those would be a project before the game part starts, and unlike this
//codebase's other hand-rolled pieces their bugs are non-deterministic and
//awkward to unit test.
//
//The ENet types are forward-declared so no consumer of this header acquires a
//dependency on <enet/enet.h>.
class CB_API EnetTransport final : public Transport
{
public:
    ~EnetTransport() override;

    EnetTransport(const EnetTransport&) = delete;
    EnetTransport& operator=(const EnetTransport&) = delete;

    //The id a client uses to address the server. Clients hold exactly one peer.
    static constexpr PeerId EnetServerPeer = 1;

    //Opens a listening host. Null when the port cannot be bound.
    static std::unique_ptr<EnetTransport> Listen(std::uint16_t port, std::size_t maxClients);

    //Opens a host and begins connecting. Null when the address is unusable.
    //The Connected event arrives from Poll once the handshake completes, which
    //takes several Advance calls.
    static std::unique_ptr<EnetTransport> Connect(const std::string& host, std::uint16_t port);

    void Send(PeerId peer, std::span<const std::uint8_t> data, Channel channel) override;
    void Broadcast(std::span<const std::uint8_t> data, Channel channel) override;
    void Disconnect(PeerId peer) override;
    bool Poll(NetEvent& out) override;
    void Advance(double seconds) override;
    double RoundTripTime(PeerId peer) const override;

private:
    EnetTransport() = default;

    //Maps ENet's peer pointers to the stable ids this interface hands out.
    //Ids are never reused, matching PlayerId's rule and for the same reason: a
    //stale message naming somebody who left must not be applied to whoever
    //arrived next.
    struct PeerSlot
    {
        PeerId Id = InvalidPeer;
        _ENetPeer* Peer = nullptr;
    };

    PeerId IdFor(_ENetPeer* peer) const;
    _ENetPeer* PeerFor(PeerId id) const;

    _ENetHost* m_Host = nullptr;
    std::vector<PeerSlot> m_Peers;
    std::deque<NetEvent> m_Inbox;
    PeerId m_NextPeer = EnetServerPeer;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
```

- [ ] **Step 4: Write `EnetTransport.cpp`**

Create `Cubit/src/Net/EnetTransport.cpp`:

```cpp
#include "cub.h"

#include "Cubit/Net/EnetTransport.h"

#include "Cubit/Logger.h"

#include <enet/enet.h>

#include <algorithm>

namespace
{
    //Initialised once for the process and torn down at exit. ENet's own
    //counterpart to this is global state, so wrapping it in a static keeps the
    //ordering correct whether the first host is a server or a client.
    struct EnetLibrary
    {
        bool Ready = false;

        EnetLibrary() { Ready = enet_initialize() == 0; }
        ~EnetLibrary() { if (Ready) enet_deinitialize(); }
    };

    EnetLibrary& Library()
    {
        static EnetLibrary library;
        return library;
    }

    //Two channels, matching Channel. Unreliable snapshots are also unsequenced:
    //a snapshot that arrives after a newer one is worthless, and MatchClient
    //discards it by tick anyway.
    constexpr std::size_t ChannelCount = 2;

    enet_uint32 FlagsFor(Channel channel)
    {
        return channel == Channel::Reliable
            ? ENET_PACKET_FLAG_RELIABLE
            : ENET_PACKET_FLAG_UNSEQUENCED;
    }
}

std::unique_ptr<EnetTransport> EnetTransport::Listen(std::uint16_t port, std::size_t maxClients)
{
    if (!Library().Ready)
    {
        CB_ERROR("ENet failed to initialise");
        return nullptr;
    }

    ENetAddress address{};
    address.host = ENET_HOST_ANY;
    address.port = port;

    ENetHost* host = enet_host_create(&address, maxClients, ChannelCount, 0, 0);
    if (host == nullptr)
    {
        CB_ERROR("Could not bind the server port");
        return nullptr;
    }

    std::unique_ptr<EnetTransport> transport(new EnetTransport());
    transport->m_Host = host;
    return transport;
}

std::unique_ptr<EnetTransport> EnetTransport::Connect(const std::string& host, std::uint16_t port)
{
    if (!Library().Ready)
    {
        CB_ERROR("ENet failed to initialise");
        return nullptr;
    }

    //One outgoing connection, which is all a client ever has.
    ENetHost* client = enet_host_create(nullptr, 1, ChannelCount, 0, 0);
    if (client == nullptr)
    {
        CB_ERROR("Could not create the client host");
        return nullptr;
    }

    ENetAddress address{};
    address.port = port;
    if (enet_address_set_host(&address, host.c_str()) != 0)
    {
        CB_ERROR("Could not resolve the server address");
        enet_host_destroy(client);
        return nullptr;
    }

    ENetPeer* peer = enet_host_connect(client, &address, ChannelCount, 0);
    if (peer == nullptr)
    {
        CB_ERROR("No peer slot available for the connection");
        enet_host_destroy(client);
        return nullptr;
    }

    std::unique_ptr<EnetTransport> transport(new EnetTransport());
    transport->m_Host = client;

    //Registered now rather than on the Connected event, so Send has somewhere
    //to address before the handshake finishes. The Connected event still
    //arrives from Poll when ENet says so.
    transport->m_Peers.push_back(PeerSlot{ transport->m_NextPeer++, peer });
    return transport;
}

EnetTransport::~EnetTransport()
{
    if (m_Host != nullptr)
        enet_host_destroy(m_Host);
}

void EnetTransport::Send(PeerId peer, std::span<const std::uint8_t> data, Channel channel)
{
    ENetPeer* target = PeerFor(peer);
    if (target == nullptr)
        return;

    ENetPacket* packet = enet_packet_create(data.data(), data.size(), FlagsFor(channel));
    enet_peer_send(target, static_cast<enet_uint8>(channel), packet);
}

void EnetTransport::Broadcast(std::span<const std::uint8_t> data, Channel channel)
{
    if (m_Host == nullptr)
        return;

    ENetPacket* packet = enet_packet_create(data.data(), data.size(), FlagsFor(channel));
    enet_host_broadcast(m_Host, static_cast<enet_uint8>(channel), packet);
}

void EnetTransport::Disconnect(PeerId peer)
{
    ENetPeer* target = PeerFor(peer);
    if (target == nullptr)
        return;

    //Immediate rather than graceful: this is used to refuse a handshake, and a
    //client being refused has nothing left worth flushing.
    enet_peer_disconnect_now(target, 0);

    const auto slot = std::find_if(m_Peers.begin(), m_Peers.end(),
        [peer](const PeerSlot& candidate) { return candidate.Id == peer; });

    if (slot != m_Peers.end())
        m_Peers.erase(slot);
}

bool EnetTransport::Poll(NetEvent& out)
{
    if (m_Inbox.empty())
        return false;

    out = std::move(m_Inbox.front());
    m_Inbox.pop_front();
    return true;
}

void EnetTransport::Advance(double seconds)
{
    (void)seconds;

    if (m_Host == nullptr)
        return;

    //Zero timeout: this is called once per fixed step from a loop that has
    //other work to do, so it must drain what is ready and return rather than
    //block waiting for traffic.
    ENetEvent event;
    while (enet_host_service(m_Host, &event, 0) > 0)
    {
        switch (event.type)
        {
        case ENET_EVENT_TYPE_CONNECT:
        {
            PeerId id = IdFor(event.peer);
            if (id == InvalidPeer)
            {
                id = m_NextPeer++;
                m_Peers.push_back(PeerSlot{ id, event.peer });
            }

            NetEvent connected;
            connected.Type = NetEventType::Connected;
            connected.Peer = id;
            m_Inbox.push_back(std::move(connected));
            break;
        }

        case ENET_EVENT_TYPE_DISCONNECT:
        {
            const PeerId id = IdFor(event.peer);

            NetEvent disconnected;
            disconnected.Type = NetEventType::Disconnected;
            disconnected.Peer = id;
            m_Inbox.push_back(std::move(disconnected));

            const auto slot = std::find_if(m_Peers.begin(), m_Peers.end(),
                [id](const PeerSlot& candidate) { return candidate.Id == id; });

            if (slot != m_Peers.end())
                m_Peers.erase(slot);
            break;
        }

        case ENET_EVENT_TYPE_RECEIVE:
        {
            NetEvent message;
            message.Type = NetEventType::Message;
            message.Peer = IdFor(event.peer);
            message.Data.assign(
                event.packet->data, event.packet->data + event.packet->dataLength);
            m_Inbox.push_back(std::move(message));

            //ENet hands over ownership of the packet with the event.
            enet_packet_destroy(event.packet);
            break;
        }

        case ENET_EVENT_TYPE_NONE:
            break;
        }
    }
}

double EnetTransport::RoundTripTime(PeerId peer) const
{
    const ENetPeer* target = PeerFor(peer);

    //ENet reports milliseconds; this interface promises seconds.
    return target == nullptr ? 0.0 : static_cast<double>(target->roundTripTime) / 1000.0;
}

PeerId EnetTransport::IdFor(_ENetPeer* peer) const
{
    for (const PeerSlot& slot : m_Peers)
    {
        if (slot.Peer == peer)
            return slot.Id;
    }

    return InvalidPeer;
}

_ENetPeer* EnetTransport::PeerFor(PeerId id) const
{
    for (const PeerSlot& slot : m_Peers)
    {
        if (slot.Id == id)
            return slot.Peer;
    }

    return nullptr;
}
```

`PeerFor` is declared returning a non-const pointer from a const method, which is what `RoundTripTime` needs; keep the `const` on the method and the pointer non-const, matching how the peers are owned by ENet rather than by this class.

- [ ] **Step 5: Export, regenerate, build, watch the test pass**

Add `#include "Cubit/Net/EnetTransport.h"` to `Cubit/include/Cubit/Cubit.h`, regenerate, build.

Expected: PASS. **A Windows firewall prompt may appear the first time** — allow it. If the test times out at 600 attempts, the likely causes in order are: the firewall silently blocking loopback UDP, port 27960 already in use, or `ws2_32`/`winmm` missing from Cubit's `links`.

- [ ] **Step 6: Commit**

```bash
git add Cubit/include/Cubit/Net/EnetTransport.h Cubit/src/Net/EnetTransport.cpp \
        Tests/src/EnetTransportTests.cpp Cubit/include/Cubit/Cubit.h
git commit -m "Put the wire on a real socket"
```

---

### Task 11: `Server.exe`

The `MapGen` precedent, exactly: a `ConsoleApp` that links Cubit, runs a `main()`, and never touches `Application`, `Window` or `Renderer`. No engine surgery, because none was ever needed — that was one of the two "blockers" the arc spec records as false.

**Files:**
- Create: `Server/src/Server.cpp`
- Modify: `premake5.lua`

**Interfaces:**
- Consumes: `MatchServer` (Task 8), `EnetTransport` (Task 10), `MapHash` (Task 7), `BuildWorld`/`VoxLoader`, `SkyLight::PropagateAll`, `FindSpawn`, `FrameClock`.
- Produces: an executable. Nothing links against it.

- [ ] **Step 1: Add the premake project**

In `premake5.lua`, after the `MapGen` project, copying its shape exactly:

```lua
project "Server"
    location "Server"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

    --Run with the target dir as the working directory so the server resolves
    --"assets/..." next to the exe, exactly as the Sandbox does.
    debugdir ("bin/" .. outputdir .. "/%{prj.name}")

    files
    {
        "Server/src/**.h",
        "Server/src/**.cpp"
    }

    includedirs
    {
        "Cubit/include",
        "vendor/GLM"
    }

    links
    {
        "Cubit"
    }

    defines
    {
        "CB_PLATFORM_WINDOWS"
    }

    filter "system:windows"
        systemversion "latest"

        postbuildcommands
        {
            ("{COPY} ../bin/" .. outputdir .. "/Cubit/Cubit.dll ../bin/" .. outputdir .. "/Server"),
            ('{COPYDIR} "../Sandbox/assets" "%{cfg.targetdir}/assets"')
        }

    filter "configurations:Debug"
        defines "CB_DEBUG"
        symbols "On"

    filter "configurations:Release"
        defines "CB_RELEASE"
        optimize "On"

    filter "configurations:Dist"
        defines "CB_DIST"
        optimize "On"
```

The server copies the Sandbox's assets rather than owning a second copy of a 23.8 MB map. Both ends must read byte-identical files or the hash check rejects the join — which is the check working, but it would be a confusing first experience.

- [ ] **Step 2: Write `Server.cpp`**

Create `Server/src/Server.cpp`:

```cpp
#include "Cubit/FrameClock.h"
#include "Cubit/Logger.h"
#include "Cubit/Net/EnetTransport.h"
#include "Cubit/Net/MapHash.h"
#include "Cubit/Net/MatchServer.h"
#include "Cubit/Net/SimulatedTransport.h"
#include "Cubit/Voxel/SkyLight.h"
#include "Cubit/Voxel/SpawnFinder.h"
#include "Cubit/Voxel/VoxLoader.h"

#include <chrono>
#include <cstdlib>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace
{
    constexpr std::uint16_t DefaultPort = 27015;
    constexpr const char* DefaultMap = "assets/maps/battlefield512.vox";

    //Matches the Sandbox's own hint, so a player spawns where they would in
    //single-player rather than somewhere unrelated.
    const glm::ivec2 SpawnHint{ 240, 300 };
    const glm::vec3 PlayerHalfExtents{ 0.3f, 0.9f, 0.3f };
}

//The authoritative server. No window, no Application, no Renderer, no GL
//context - the MapGen precedent. The simulation core is already GL-free, which
//is what makes this eighty lines rather than an engine refactor.
int main(int argc, char** argv)
{
    std::string mapPath = DefaultMap;
    std::uint16_t port = DefaultPort;

    //Round-trip milliseconds, halved into the one-way latency NetworkSim wants.
    //Present on the server as well as the client so a demo can put the delay on
    //either side.
    double latencyRtt = 0.0;
    float loss = 0.0f;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];

        if (arg == "--port" && i + 1 < argc)
            port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        else if (arg == "--latency" && i + 1 < argc)
            latencyRtt = std::atof(argv[++i]);
        else if (arg == "--loss" && i + 1 < argc)
            loss = static_cast<float>(std::atof(argv[++i])) / 100.0f;
        else
            mapPath = arg;
    }

    try
    {
        CB_INFO("Loading " + mapPath);

        World world = BuildWorld(VoxLoader::LoadFile(mapPath));

        //Before anything reads light. The server never meshes, but a client
        //reloading from a snapshot would otherwise disagree about lighting.
        SkyLight::PropagateAll(world);

        const std::uint64_t mapHash = HashMapFile(mapPath);
        if (mapHash == 0)
        {
            CB_ERROR("Could not hash the map file");
            return 1;
        }

        const std::optional<glm::vec3> spawn =
            FindSpawn(world, SpawnHint, PlayerHalfExtents);

        if (!spawn.has_value())
        {
            CB_ERROR("No usable spawn near the hint");
            return 1;
        }

        std::unique_ptr<EnetTransport> socket = EnetTransport::Listen(port, 16);
        if (socket == nullptr)
            return 1;

        //The same decorator the tests use. Wrapping the real socket is what
        //lets a localhost demo show latency at all - loopback has none of its
        //own, so without this the round trip would be invisible and the stage
        //would prove nothing.
        NetworkSim sim;
        sim.Latency = latencyRtt / 2000.0;
        sim.Loss = loss;

        SimulatedTransport simulated(*socket, sim);
        Transport& transport = latencyRtt > 0.0 || loss > 0.0f
            ? static_cast<Transport&>(simulated)
            : static_cast<Transport&>(*socket);

        //The map name, not the path: the client resolves it against its own
        //assets directory, and the 23.8 MB of data is never sent.
        const std::string mapName =
            mapPath.substr(mapPath.find_last_of("/\\") + 1);

        MatchServer server(std::move(world), mapName, mapHash, *spawn, transport);

        CB_INFO("Listening on port " + std::to_string(port));

        //A fixed-step loop with no rendering. FrameClock turns a wall-clock
        //delta into whole simulation steps, exactly as it does in the Sandbox;
        //Alpha() is meaningless here because nothing interpolates.
        FrameClock clock;
        auto previous = std::chrono::steady_clock::now();

        for (;;)
        {
            const auto now = std::chrono::steady_clock::now();
            const double elapsed =
                std::chrono::duration<double>(now - previous).count();
            previous = now;

            const int steps = clock.Advance(elapsed);
            for (int i = 0; i < steps; ++i)
                server.Step(FrameClock::FixedStepSeconds);

            //Without this the loop spins a core flat out to do nothing. One
            //millisecond is far below the 16.7 ms step, so it costs no
            //simulation accuracy.
            if (steps == 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    catch (const std::exception& error)
    {
        CB_CRITICAL(std::string("Server failed: ") + error.what());
        return 1;
    }
}
```

- [ ] **Step 3: Regenerate, build, and run it**

```bash
/c/dev/premake/premake5 vs2026
MSB="/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe"
"$MSB" Cubit.slnx -p:Configuration=Debug -p:Platform=x64 -m -v:minimal -nologo
```

Then, in a terminal the user can close:
```
! bin/Debug-windows-x86_64/Server/Server.exe
```
Expected output: `Loading assets/maps/battlefield512.vox`, then `Listening on port 27015`, and it stays up. Ctrl-C to stop.

If the load takes noticeably longer than the Sandbox's ~1.4 s, that is the missing profiler session rather than a regression — the server deliberately does not open one.

- [ ] **Step 4: Commit**

```bash
git add Server/src/Server.cpp premake5.lua
git commit -m "Run a match on a machine with no screen"
```

---

### Task 12: Sandbox `--connect`

Three branch points, and no more. The file already reaches everything through the `World_()` and `LocalPlayer()` accessors added in Stage 1, which is what keeps this from spreading.

**Files:**
- Modify: `Sandbox/src/Sandbox.cpp`
- Modify: `Sandbox/src/HudLayer.h`

**Interfaces:**
- Consumes: `MatchClient`, `LoadedMap` (Task 9), `EnetTransport` (Task 10), `SimulatedTransport` (Task 6), `HashMapFile` (Task 7).
- Produces: nothing anything else links to.

- [ ] **Step 1: Add the connection options and the client**

At the top of `Sandbox.cpp`, add the includes:

```cpp
#include "Cubit/Net/EnetTransport.h"
#include "Cubit/Net/MapHash.h"
#include "Cubit/Net/MatchClient.h"
#include "Cubit/Net/SimulatedTransport.h"

//The file includes <exception> but not <stdexcept>, and the connect path
//throws std::runtime_error.
#include <stdexcept>
```

Add the options struct beside the other file-scope constants:

```cpp
//How this Sandbox was launched. Default-constructed means single-player, which
//must stay byte-for-byte the app it was before networking existed: the
//project's rendering verification is scripted screenshots and POS/FACES probes
//run against it, and none of that may start depending on a socket.
struct SandboxOptions
{
    bool Connect = false;
    std::string Host = "127.0.0.1";
    std::uint16_t Port = 27015;

    //Round-trip milliseconds. Halved into NetworkSim's one-way latency.
    double LatencyRtt = 0.0;
    float Loss = 0.0f;
};
```

Give `SandboxLayer` and `SandboxApplication` a `SandboxOptions` constructor parameter, store it as `m_Options`, and add the members:

```cpp
    SandboxOptions m_Options;

    //Present only when connected. Owns the client's MatchState; m_Match is the
    //single-player one and is left untouched while these are alive.
    std::unique_ptr<EnetTransport> m_Socket;
    std::unique_ptr<SimulatedTransport> m_Simulated;
    std::unique_ptr<MatchClient> m_Client;
```

- [ ] **Step 2: Add the one accessor that hides the mode**

Replace the existing shorthands with:

```cpp
    //BRANCH POINT 1 OF 3. Everything else in this file reads the match through
    //here, which is what keeps a second mode from spreading across 620 lines.
    //
    //If a fourth branch point appears while working in this file, that is the
    //signal to extract rather than to continue.
    MatchState& Match_()
    {
        return m_Client ? m_Client->MatchForWrite() : m_Match;
    }
    const MatchState& Match_() const
    {
        return m_Client ? m_Client->Match() : m_Match;
    }

    World& World_() { return Match_().GetWorld(); }
    const World& World_() const { return Match_().GetWorld(); }

    const CharacterController& Player_() const
    {
        return Match_().Player(m_LocalPlayer);
    }
```

- [ ] **Step 3: Connect during attach**

**First, do not load the map twice.** `OnAttach` currently calls `LoadWorld(MapPath)` unconditionally, and `MatchClient` loads the map again when `Welcome` names it — that is 23.8 MB and ~1.4 s of debug load spent twice, and it leaves a fully built world in `m_Match` that nothing connected ever reads. Guard it:

```cpp
        //Connected, the map arrives by name in Welcome and MatchClient's loader
        //builds it. Loading here as well would pay the whole load cost twice
        //and leave a second world nothing reads.
        if (!m_Options.Connect)
        {
            LoadWorld(MapPath);
        }
```

The profiler session around it stays where it is; a connected launch simply records a much shorter load, which is honest.

Then, in place of the unconditional `m_LocalPlayer = m_Match.AddPlayer(m_Spawn);`:

```cpp
        if (m_Options.Connect)
        {
            m_Socket = EnetTransport::Connect(m_Options.Host, m_Options.Port);
            if (m_Socket == nullptr)
                throw std::runtime_error("Could not reach the server");

            Transport* transport = m_Socket.get();

            if (m_Options.LatencyRtt > 0.0 || m_Options.Loss > 0.0f)
            {
                NetworkSim sim;
                sim.Latency = m_Options.LatencyRtt / 2000.0;
                sim.Loss = m_Options.Loss;
                m_Simulated = std::make_unique<SimulatedTransport>(*m_Socket, sim);
                transport = m_Simulated.get();
            }

            //The world is loaded here rather than inside MatchClient because
            //this is the only place that knows where the Sandbox keeps its
            //assets, and because a bad file must leave the app in a state that
            //can report why.
            m_Client = std::make_unique<MatchClient>(*transport,
                [](const std::string& mapName) -> std::optional<LoadedMap>
                {
                    const std::string path = "assets/maps/" + mapName;
                    if (!std::filesystem::exists(path))
                        return std::nullopt;

                    World world = BuildWorld(VoxLoader::LoadFile(path));
                    SkyLight::PropagateAll(world);
                    return LoadedMap{ std::move(world), HashMapFile(path) };
                });
        }
        else
        {
            m_LocalPlayer = m_Match.AddPlayer(m_Spawn);
        }
```

`m_LocalPlayer` is assigned from the client's `LocalPlayer()` once `Welcome` lands, in the next step.

**The rest of `OnAttach` must be guarded too, and this one crashes if it is not.** The lines immediately after aim the camera:

```cpp
        const glm::vec3 eye = Player_().InterpolatedEye(1.0f);
```

`Player_()` resolves to `Match_().Player(m_LocalPlayer)`, and `MatchState::Player` **throws** when the id is absent — which it is, for a connected client, until `Welcome` arrives a round trip later. Left alone this is an unhandled exception during startup, every time, on the networked path.

Wrap the camera-aiming block and the `UpdateCameraPosition(1.0f)` call that follows it in the same `if (!m_Options.Connect)`. Connected, the camera is placed by the first `OnRender` after `Welcome` instead. Add an early return to `OnRender` for the same reason:

```cpp
        //Nothing to draw from until the server has said who we are and where.
        //Player_() would throw, and the world is a 1x1x1 placeholder.
        if (m_Client && !m_Client->Connected())
            return;
```

This is the class of defect the Stage 1 pre-flight scan was written to catch — a task that compiles and cannot run. Scan the rest of `Sandbox.cpp` for any other unguarded `Player_()` call before moving on; `OnFrameUpdate` and the F9 reload path are the likely candidates.

- [ ] **Step 4: Branch the fixed update**

Replace the body of `OnFixedUpdate` with:

```cpp
    void OnFixedUpdate(Timestep timestep) override
    {
        ++m_StepsThisFrame;

        CharacterInput input;
        input.Move = ReadWalkInput();
        input.Yaw = m_CameraController.GetYaw();
        input.Pitch = m_CameraController.GetPitch();
        input.Jump = Input::IsKeyPressed(KeyCode::Space);

        //BRANCH POINT 2 OF 3.
        if (m_Client)
        {
            //Input goes up; the answer comes back. Nothing is simulated here,
            //deliberately: pressing W does not move the view until the server
            //has said so. On 127.0.0.1 that is imperceptible; at --latency 150
            //it is unpleasant, and that is the point of this whole stage.
            //If it feels fine, prediction has grown by accident.
            m_Client->SetInput(input);
            m_Client->Step(timestep.GetSeconds());

            m_LocalPlayer = m_Client->LocalPlayer();

            m_HudState->Connected = m_Client->Connected();
            m_HudState->Rejected = m_Client->Rejected();
            m_HudState->RoundTripMs = m_Client->RoundTripTime() * 1000.0;
            m_HudState->RemotePlayers = Match_().Players().size();

            if (!m_Client->Connected())
                return;
        }
        else
        {
            const PlayerCommand commands[] = { { m_LocalPlayer, input } };
            m_Match.Step(commands, static_cast<float>(timestep.GetSeconds()));
        }

        m_HudState->PlayerPosition = Player_().Position();
        m_HudState->Grounded = Player_().Grounded();
        m_HudState->BodyInFluid = Player_().BodyInFluid();
        m_HudState->EyeInFluid = Player_().EyeInFluid();

        //Falling off the edge is a Sandbox rule, and it belongs to whoever owns
        //the simulation. Connected, that is the server, so the client does not
        //get to teleport itself.
        if (!m_Client && Player_().Position().y < FallResetHeight)
        {
            m_Match.TeleportPlayer(m_LocalPlayer, m_Spawn);
            m_Match.PlayerForWrite(m_LocalPlayer).SetVerticalVelocity(0.0f);
        }
    }
```

- [ ] **Step 5: Branch the edit, and draw the other players**

In the edit path, replace the `ApplyBlockEdit` call with:

```cpp
        //BRANCH POINT 3 OF 3.
        if (m_Client)
        {
            //Nothing happens locally. The block disappears when the server says
            //so, one round trip later - which is the most legible demonstration
            //of latency this app has.
            m_Client->RequestEdit(edit);
            return true;
        }

        const std::optional<BlockEdit> inverse = ApplyBlockEdit(World_(), edit);
```

Undo (`U`) stays single-player only: the undo stack describes edits this machine applied, and connected it applied none. Guard its handler with `if (m_Client) return false;` and note that in a comment.

In `OnRender`, before `DebugDraw::Flush`, add:

```cpp
        //Remote players as wireframe boxes at their real half extents. Not a
        //character model - modelling is gameplay, and a model chosen now would
        //be a guess.
        if (m_Client)
        {
            for (const auto& [player, character] : Match_().Players())
            {
                if (player == m_LocalPlayer)
                    continue;

                const glm::vec3 centre = character.InterpolatedPosition(alpha);
                const glm::vec3 half = character.Config().HalfExtents;
                DebugDraw::Box(centre - half, centre + half, RemotePlayerColor);
            }
        }
```

with `const glm::vec4 RemotePlayerColor{ 0.9f, 0.3f, 0.2f, 1.0f };` beside the other colour constants.

- [ ] **Step 6: Extend the HUD**

In `Sandbox/src/HudLayer.h`, add to `HudState`:

```cpp
    //Networking. All zero and false in single-player, which is how the overlay
    //knows not to draw any of it.
    bool Connected = false;
    bool Rejected = false;
    double RoundTripMs = 0.0;
    std::size_t RemotePlayers = 0;
```

Draw them in the overlay beside the existing counters, following whatever formatting the neighbouring lines use. Show `NOT CONNECTED` when `Rejected` is set — a refused handshake must be visible on screen, not only in the log.

- [ ] **Step 7: Parse the command line**

Replace `int main()` with:

```cpp
//Starts the Sandbox. With no arguments this is the single-player app exactly as
//it has always been, with no socket anywhere in it.
int main(int argc, char** argv)
{
    SandboxOptions options;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];

        if (arg == "--connect" && i + 1 < argc)
        {
            options.Connect = true;
            options.Host = argv[++i];
        }
        else if (arg == "--port" && i + 1 < argc)
            options.Port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        else if (arg == "--latency" && i + 1 < argc)
            options.LatencyRtt = std::atof(argv[++i]);
        else if (arg == "--loss" && i + 1 < argc)
            options.Loss = static_cast<float>(std::atof(argv[++i])) / 100.0f;
    }

    SandboxApplication app(options);
    app.Run();

    return 0;
}
```

- [ ] **Step 8: Build and prove single-player is untouched**

```bash
"$MSB" Cubit.slnx -p:Configuration=Debug -p:Platform=x64 -m -v:minimal -nologo
```

Then run `bin/Debug-windows-x86_64/Sandbox/Sandbox.exe` with **no arguments** and confirm the HUD reads `POS 240.500000 26.900099 300.500000` and `FACES 1927774`.

This is the acceptance check for the whole plan. If either number moved, the networked path has leaked into the single-player one.

- [ ] **Step 9: Commit**

```bash
git add Sandbox/src/Sandbox.cpp Sandbox/src/HudLayer.h
git commit -m "Let the Sandbox be a client"
```

---

### Task 13: Verify the real thing, and record the stage

The suite proves the socket. This task proves the app, and writes down what the next stage needs.

**Files:**
- Modify: `docs/superpowers/specs/2026-08-31-networking-stage-2-design.md`
- Modify: `docs/engine-roadmap.md`

- [ ] **Step 1: Run a real two-client match**

Three terminals:
```
! bin/Debug-windows-x86_64/Server/Server.exe
! bin/Debug-windows-x86_64/Sandbox/Sandbox.exe --connect 127.0.0.1
! bin/Debug-windows-x86_64/Sandbox/Sandbox.exe --connect 127.0.0.1 --latency 150
```

Confirm by eye: each window shows a red wireframe box where the other player is; walking in one moves the box in the other; breaking a block in one makes it vanish in both; the second window's own movement is visibly delayed and the first's is not.

- [ ] **Step 2: Record the positions, since screen capture is not trustworthy**

Screen capture of the Cubit window fails intermittently — three consecutive blank frames with a healthy process, a DWM/OpenGL `CopyFromScreen` fault rather than a code one. Do not hang the proof on it.

Instead add a temporary probe in `OnFixedUpdate`, inside the `m_Client` branch:

```cpp
            //TEMPORARY probe. Removed with git checkout at the end of this step.
            for (const auto& [player, character] : Match_().Players())
            {
                if (player == m_LocalPlayer)
                    continue;
                CB_INFO("REMOTE " + std::to_string(player) + " " +
                    std::to_string(character.Position().x) + " " +
                    std::to_string(character.Position().z));
            }
```

Run both clients, walk one of them, and confirm each log shows the other moving. Then:
```bash
git checkout -- Sandbox/src/Sandbox.cpp
```

- [ ] **Step 3: Measure the bandwidth**

The spec promised a real number for Stage 3 to optimise against rather than an intuition. With two clients connected, log `snapshot bytes x 60` once a second from `MatchServer::BroadcastSnapshot` behind a temporary probe, record the figure, and `git checkout --` the probe.

Expected around 3.9 KB/s down per client and 1.3 KB/s up. A figure far above that means the snapshot is carrying something it should not.

- [ ] **Step 4: Update the spec's status line and record what was learned**

Change the spec's header to `Status: **SHIPPED <date>**`, and add a short section recording: the measured bandwidth, the actual test count, anything the implementation found that the design got wrong, and — most importantly — anything Stage 3 now inherits. The arc spec's Stage 3 section should be re-read at that point and corrected where Stage 2's reality differs from what it assumed.

Confirm these still stand, and correct them if not: the client's tick equals the last snapshot's tick; `SetState` restores everything replay needs; the edit log is the only unbounded structure.

- [ ] **Step 5: Update the roadmap and commit**

```bash
git add docs/superpowers/specs/2026-08-31-networking-stage-2-design.md docs/engine-roadmap.md
git commit -m "Record networking stage 2 as shipped"
git push origin master
```

---

## Self-Review

Run this after the last task, against the spec.

**Spec coverage.** Every section of `2026-08-31-networking-stage-2-design.md` maps to a task: launch shape → 11, 12; client never steps → 9; edits → 8, 9, 12; 60 Hz snapshots → 8; look instant / move delayed → 12; `Transport` seam → 5, 6; `MatchState` gaps → 1; six messages → 4; join and edit log → 8, 9; server → 11; Sandbox integration → 12; testing strategy → 3, 4, 5, 6, 8, 9, 10; out-of-scope items appear nowhere, which is correct.

**The three things most likely to go wrong**, in order:

1. **The skew constant in the oracle.** If it is not 3, the loop order or the send/service phase is wrong. Do not adjust the number to make the test pass.
2. **ENet linking.** Caught in Task 2 by design. If Task 2 was skipped or rushed, this reappears in Task 10 with ten tasks of work sitting on top of it.
3. **Single-player drift.** The `POS`/`FACES` check in Task 12 Step 8 is the only thing standing between this plan and a silently changed single-player app.
