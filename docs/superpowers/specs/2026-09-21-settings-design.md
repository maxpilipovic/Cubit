# Settings — design (roadmap B6)

**Date:** 2026-09-21. **Status:** approved in conversation; this records it.

## Problem

Mouse sensitivity (`PerspectiveCameraController.h:62`), field of view (`:54`), window size
(`WindowProperties` in `Window.h`), the single-player map path (`GameOptions.h`) and the
spawn hint are compile-time constants. Changing any of them means a rebuild. Two are also
duplicated across the client and the server, which is a correctness bug rather than an
inconvenience: `GameApp.cpp:72` and `Server.cpp:42` each define the spawn hint as
`{ 240, 300 }`, and `GameOptions.h` and `Server.cpp:38` each define the default map path.
Change one copy and the client and server disagree.

The five are not one kind of thing:

- **Player settings** — sensitivity, field of view, window size. A player sets them once
  and expects them to stay set. These go in a file.
- **Launch choices** — which map single-player loads. Chosen per run. These are flags.
- **Map data** — the spawn hint belongs to the map. It needs exactly one definition, not
  configuration: there is one map today, and a per-map manifest is not wanted until there
  is a second.

## Decision

A plain `key = value` settings file beside the executable, with command-line flags
overriding it for one run. Chosen over flags only (no persistence, so not really
settings) and over vendored JSON (a large dependency for four numbers; nothing else in the
engine needs JSON). An in-game settings menu waits for B4.

## Engine — mechanism, no values

**`SettingsFile`** (`Cubit/include/Cubit/SettingsFile.h`, `Cubit/src/SettingsFile.cpp`).

- `static SettingsFile Parse(std::string_view text)` and
  `static std::optional<SettingsFile> Load(const std::string& path)` — `Load` returns empty
  when the file does not exist, and throws only when it exists and cannot be read.
- Grammar: one `key = value` per line. Whitespace around the key and value is trimmed.
  `#` starts a comment anywhere on a line. Blank lines are ignored. Keys are
  case-sensitive. **A repeated key: the last one wins**, because a player appending a line
  to the end of the file expects that line to take effect.
- A line with no `=`, or with an empty key, is **not an error**. It is recorded with its
  line number in `Problems()`, and the rest of the file still loads. A typo in a player's
  file must not stop the game starting.
- `std::optional<float> GetFloat(key)`, `std::optional<int> GetInt(key)`,
  `std::optional<std::string> GetString(key)`. A key that is present but does not parse
  as the requested type returns empty and is recorded in `Problems()`. `GetInt("x")` on
  `x = 12abc` is empty, not 12: a partial parse would hide the typo.
- `Keys()` lists every key present, so a caller can report the ones it does not know.

**`Application(const WindowProperties&)`** — a new constructor. The default constructor
keeps today's 1280×720 by delegating to it.

**`PerspectiveCameraController::SetFieldOfView(float)` and `SetMouseSensitivity(float)`.**
`SetFieldOfView` rebuilds the projection immediately. Neither validates: ranges are the
game's policy, not the engine's.

## Game — values and policy

**`GameSettings`** (`game/Game/src/GameSettings.h`, header-only). It lives in `Game/`
rather than `GameApp/` because `GameTests` compiles `Game/src` and not `GameApp/src`.

| Key | Default | Range |
|---|---|---|
| `mouse_sensitivity` | 0.12 | 0.01 – 2.0 |
| `field_of_view` | 60 | 30 – 120 |
| `window_width` | 1280 | 640 – 7680 |
| `window_height` | 720 | 360 – 4320 |

- `GameSettings Defaults()` and `std::string DefaultFileText()`. The text is the table
  above written as a commented file, and is what first run writes.
- `void Apply(const SettingsFile&, GameSettings&, std::vector<std::string>& warnings)`:
  a value that does not parse keeps the default and warns; an out-of-range value is
  **clamped** and warns; an unknown key is ignored and warns, naming the key. Clamping
  rather than rejecting, because `field_of_view = 500` plainly means "as wide as allowed".
- Command-line flags go through the same clamping, so no path reaches the engine with a
  value outside the table.

**`settings.cfg`** sits beside `GameApp.exe`, resolved against the working directory like
the maps. If it is missing it is written from `DefaultFileText()`. The **effective**
settings are logged at startup, one line, whatever their source. That matters for
verification: scripted screenshot runs depend on a no-argument launch behaving exactly as
it always has, and an edited `settings.cfg` left in `bin/` would change the field of view
in them silently otherwise.

**Flags:** `--fov`, `--sensitivity`, `--width`, `--height`, and `--map <path>` for the
single-player map. `--map` is ignored with a warning when `--connect` is given: a
connected client loads the map the server names.

**`Maps.h`** (`game/Game/src/Maps.h`) holds the default map path and the spawn hint.
`GameApp` and `Server` both include it and neither keeps its own copy.

## Harness

A `--map <path>` flag. `EnsureMap` still runs only for the default path: a file named on the
command line that does not exist is an error, not something to generate. The harness's
camera start stays its own constant — it is a camera position for the harness, and the
harness cannot depend on `game/`.

## Out of scope

An in-game settings menu (needs B4), key rebinding, fullscreen, vsync, and harness
settings beyond `--map`. The server has no player settings.

## Testing

- **Engine suite, `SettingsFileTests`:** a key and value with surrounding whitespace;
  comments, whole-line and trailing; blank lines; a repeated key (last wins); a line with
  no `=` recorded as a problem with its line number while the rest still loads; `GetInt`
  on a value with trailing garbage is empty and recorded; `GetFloat` on an integer
  literal succeeds; `Load` of a missing file is empty rather than throwing.
- **Game suite, `GameSettingsTests`:** `Apply` on an empty file leaves the defaults; each
  key overrides; a bad value keeps the default and warns; an out-of-range value clamps and
  warns; an unknown key warns and changes nothing; and `DefaultFileText()` parsed back
  through `Apply` yields exactly `Defaults()` with no warnings — which is what stops the
  written file and the code drifting apart.
- **By running:** `GameApp` with `field_of_view = 100` and a 1600×900 window in
  `settings.cfg`, screenshotted against a default run from the same position; then
  `--map` pointing at another `.vox`. `Server` and `GameApp` spawn on the same column,
  from the one definition.
