# Sandbox Map Reload (F9) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bind `F9` in the Sandbox to reloading `saved.vox`, so `F5` and `F9` form a checkpoint pair usable inside one session.

**Architecture:** Extract the constructor's world setup into a private `LoadWorld(path)` method, then call it from both the constructor and a new `ReloadWorld()` bound to `F9`. A helper lifts the player clear of terrain that may have been restored underneath them. One file changes.

**Tech Stack:** C++20, MSVC, GLFW, premake5.

Full design: [`docs/superpowers/specs/2026-07-31-sandbox-map-reload-design.md`](../specs/2026-07-31-sandbox-map-reload-design.md).

## Global Constraints

- **Only one source file changes:** `Sandbox/src/Sandbox.cpp`. No engine (`Cubit/`) changes, no test changes, no premake regeneration.
- **Build command:**
  ```bash
  "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Sandbox/Sandbox.vcxproj -p:Configuration=Debug -p:Platform=x64
  ```
  Use FORWARD slashes in the project path — backslashes fail in Git Bash with MSB1009.
- **Comment style:** `//` with no space for comments above a declaration; `// ` with a space for explanatory comments inside a function body. Match the surrounding file.
- **No automated tests.** `Tests` links `Cubit`, not `Sandbox`, so this code is unreachable from the suite. Verification is a build plus a manual in-window check. This is deliberate and matches how `HudLayer` and the player controller are treated.
- **Never** add Claude co-author trailers or attribution to commits.

---

### Task 1: F9 reloads the saved map

**Files:**
- Modify: `Sandbox/src/Sandbox.cpp` (anonymous namespace ~line 54-61; constructor ~line 80-87; private methods ~line 288-305; `OnKeyPressed` ~line 308-317)

**Interfaces:**
- Consumes, all existing: `BuildWorld(const VoxModel&) -> World`, `VoxLoader::LoadFile(const std::string&) -> VoxModel`, `SkyLight::PropagateAll(World&)`, `VoxelCollision::Overlaps(const World&, const glm::vec3& position, const glm::vec3& halfExtents) -> bool`, `KeyCode::F9`, `CB_INFO`, `CB_ERROR`.
- Produces: nothing outside this file.

- [ ] **Step 1: Add the `MapPath` constant**

In the anonymous `namespace { ... }`, immediately **above** the existing `SavePath` comment block and its `constexpr const char* SavePath = "assets/maps/saved.vox";`, add:

```cpp
    //The map the sandbox starts on, resolved against the working directory like
    //SavePath below.
    constexpr const char* MapPath = "assets/maps/battlefield.vox";

```

Leave `SavePath` and its comment exactly as they are.

- [ ] **Step 2: Replace the constructor's inline world setup with a call to `LoadWorld`**

In the `SandboxLayer` constructor body, find these five lines:

```cpp
        m_World = BuildWorld(VoxLoader::LoadFile("assets/maps/battlefield.vox"));
        //The world starts with every chunk dirty, so the first render meshes it.

        // Light has to exist before anything meshes, or the first frames bake
        // a fully dark world into their vertex colours.
        SkyLight::PropagateAll(m_World);

        UpdateCameraPosition();
```

Replace all of them with:

```cpp
        //The world starts with every chunk dirty, so the first render meshes it.
        //A sandbox that cannot load its map has nothing to do, so unlike F9 this
        //does not catch — the failure propagates out of the constructor.
        LoadWorld(MapPath);
```

- [ ] **Step 3: Add `LoadWorld` and `LiftPlayerClearOfTerrain`**

In the `private:` section, immediately **above** the existing `//Writes the edited world beside the executable and logs where it went.` comment on `SaveWorld`, add both methods:

```cpp
    //Replaces the world with the map at this path and settles the player into it.
    //Throws when the file cannot be read or parsed.
    void LoadWorld(const char* path)
    {
        // Assigning only after LoadFile returns means a bad file leaves the
        // current world untouched, rather than half-replaced.
        m_World = BuildWorld(VoxLoader::LoadFile(path));

        // Light has to exist before anything meshes, or the first frames bake
        // a fully dark world into their vertex colours.
        SkyLight::PropagateAll(m_World);

        LiftPlayerClearOfTerrain();
        m_VerticalVelocity = 0.0f;
        UpdateCameraPosition();
    }

    //Steps the player up until their box is clear of solid blocks.
    //
    //A reload can restore terrain where the player was standing, and
    //VoxelCollision only pushes a box out of a block on a move it detects, so a
    //player who starts embedded stays embedded with no escape but falling out of
    //the world. Keeping x and z preserves the part of the map being worked on,
    //which is the point of reloading quickly.
    void LiftPlayerClearOfTerrain()
    {
        const float top = static_cast<float>(m_World.GetHeight());

        while (m_PlayerPosition.y < top &&
            VoxelCollision::Overlaps(m_World, m_PlayerPosition, PlayerHalfExtents))
            m_PlayerPosition.y += 1.0f;

        // A column solid to the sky has nowhere to stand.
        if (VoxelCollision::Overlaps(m_World, m_PlayerPosition, PlayerHalfExtents))
            m_PlayerPosition = SpawnPosition;
    }

```

- [ ] **Step 4: Add `ReloadWorld`**

Immediately **below** the closing brace of `SaveWorld` and **above** the `//Selects the colour used when placing blocks, or logs an unhandled press.` comment on `OnKeyPressed`, add:

```cpp
    //Restores the world from the last F5 save, leaving the current one alone if
    //there isn't one.
    void ReloadWorld()
    {
        // Same reason SaveWorld catches: this runs inside a GLFW key callback,
        // which is C code, and throwing across a C frame is undefined.
        try
        {
            LoadWorld(SavePath);

            CB_INFO("Reloaded world from " +
                std::filesystem::absolute(SavePath).string());
        }
        catch (const std::exception& error)
        {
            CB_ERROR(std::string("Could not reload world: ") + error.what());
        }
    }

```

- [ ] **Step 5: Bind F9**

In `OnKeyPressed`, directly **below** the existing F5 block:

```cpp
        if (event.GetKeyCode() == KeyCode::F5)
        {
            SaveWorld();
            return true;
        }
```

add:

```cpp

        if (event.GetKeyCode() == KeyCode::F9)
        {
            ReloadWorld();
            return true;
        }
```

- [ ] **Step 6: Build**

Run:
```bash
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Sandbox/Sandbox.vcxproj -p:Configuration=Debug -p:Platform=x64
```

Expected: build succeeds, 0 errors, and no new warnings from `Sandbox.cpp`.

If the compiler reports that `LoadWorld` is not declared when the constructor calls it, that is not an error to work around — member functions of a class are visible throughout the class regardless of declaration order. Re-read the error; it will be something else.

- [ ] **Step 7: Commit**

```bash
git add Sandbox/src/Sandbox.cpp
git commit -m "Reload the saved map from the sandbox with F9"
```

- [ ] **Step 8: Update the README controls line**

In `README.md`, find the `**Controls:**` paragraph in the *What works* section:

```markdown
**Controls:** `W`/`A`/`S`/`D` to move, `Space` to jump, mouse to look. Left click breaks
a block, right click places one, and `1`–`8` pick the colour.
```

Replace it with:

```markdown
**Controls:** `W`/`A`/`S`/`D` to move, `Space` to jump, mouse to look. Left click breaks
a block, right click places one, and `1`–`8` pick the colour. `F5` saves the edited
world, `F9` restores it — a checkpoint pair for authoring a map by playing it.
```

Then, in the *Content pipeline* list, find this bullet:

```markdown
- `ToVoxModel` and `VoxWriter::WriteFile` save an edited world back out, so a map
  can be fixed by playing it — the sandbox binds this to `F5`, though promoting a
  save over the shipped map still takes a manual copy and rebuild
```

Replace the trailing clause so it reads:

```markdown
- `ToVoxModel` and `VoxWriter::WriteFile` save an edited world back out, so a map
  can be fixed by playing it — the sandbox binds this to `F5` and `F9`, though
  promoting a save over the shipped map still takes a manual copy and rebuild
```

If the exact wording of that bullet differs from what is quoted above, keep the existing wording and change only the `F5` mention to `F5` and `F9`.

```bash
git add README.md
git commit -m "Document the F9 reload key"
```

---

## Manual verification (human, after the plan is complete)

Not an agent step — an agent cannot drive the GUI. Run from the target directory so `assets/...` resolves:

```bash
cd bin/Debug-windows-x86_64/Sandbox && ./Sandbox.exe
```

1. Dig a visible pit, press `F5`, confirm the save log line.
2. Fill the pit back in, press `F9`.
3. Expect a frozen window for several seconds (debug build — `BuildWorld` plus the sky-light flood), then the HUD's `PENDING` counter draining as chunks re-mesh, then the pit is back and the player is standing on solid ground rather than inside it.
4. Close the window with its close button so the log survives.
5. Separately: delete `assets/maps/saved.vox`, relaunch, and press `F9` immediately. Expect one `Could not reload world:` error line and an otherwise untouched world.

---

## Self-Review

**Spec coverage:**

| Spec section | Step |
|---|---|
| `LoadWorld(path)` extracted, used by constructor and F9 | 2, 3 |
| `MapPath` constant beside `SavePath` | 1 |
| Sky light propagated before anything meshes | 3 |
| `LiftPlayerClearOfTerrain`, keeps x/z, falls back to spawn | 3 |
| `m_VerticalVelocity` reset on load | 3 |
| F9 catches `std::exception`, reports `CB_ERROR` | 4 |
| Constructor does NOT catch | 2 (stated in the comment it adds) |
| F9 bound in `OnKeyPressed`, returns true | 5 |
| Failed load leaves the world untouched | 3 (assignment ordering, with the comment explaining it) |
| No automated tests; manual verification | Global Constraints, Manual verification |
| Cost/stall accepted, no loading screen | Manual verification step 3 sets the expectation |

No gaps. The spec's *Out of scope* items (progress UI, async loading, revert key, save slots) appear in no step, which is correct.

**Placeholder scan:** every step contains the literal code or literal markdown to write, plus the exact surrounding text to locate it by. No "TBD", no "handle errors appropriately".

**Type consistency:** `LoadWorld(const char*)` is called with `MapPath` and `SavePath`, both `constexpr const char*`. `LiftPlayerClearOfTerrain()` and `ReloadWorld()` take no arguments and return `void`. `VoxelCollision::Overlaps` is called with `(m_World, m_PlayerPosition, PlayerHalfExtents)`, matching its declaration in `Cubit/include/Cubit/Voxel/VoxelCollision.h`. `m_PlayerPosition`, `m_VerticalVelocity`, `m_World`, `SpawnPosition`, and `PlayerHalfExtents` all already exist in the file.
