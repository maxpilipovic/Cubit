# Audio — design (roadmap B7)

**Date:** 2026-09-27. **Status:** approved in conversation; this records it.

## Problem

There is no audio library in `vendor/` and no audio code. Scope doc POL-01, "Audio layer",
wants gunshots, footsteps, impacts and UI sounds that "play correctly", and its notes
ask for "3D positional audio for gameplay-critical cues". POL-04 wants audio among the
basics a player can adjust. There are no sound files in the repository either.

## Decisions

**miniaudio**, one public-domain header. It does device output, mixing, WAV decoding and
3D spatialisation (distance attenuation and panning), and it can run with no device at
all and render the mix into a buffer, which is what lets the suite test real output
headlessly. OpenAL Soft was the alternative: LGPL, so a DLL, and a CMake build beside
premake, for nothing this needs. Raw XAudio2 or WASAPI would mean writing the mixer and
the spatialiser.

**No third-party type in the engine's API.** Nothing under `Cubit/include` names a
miniaudio type, header or identifier. `AudioEngine` holds a `std::unique_ptr<Impl>`
whose definition lives only in `Cubit/src/Audio/AudioEngine.cpp`, the single file that
includes `miniaudio.h`. That is stricter than `EnetTransport.h`, which forward-declares
`_ENetHost` in a public header: ENet's name leaks even though its header does not.
miniaudio's structs are meant to be embedded by value, so forward declaration would not
work for them anyway. The test of this rule is that swapping the backend touches only
`AudioEngine.cpp` and `vendor/premake5.lua`. The build enforces it: miniaudio's include
path is given to the Cubit project only, so the game and the tests cannot include it by
accident. `glm::vec3` does appear in the API, as it does in every public header; it is
the engine's maths vocabulary, not a backend.

**The engine plays sounds; the game supplies them.** This follows the font. The engine
takes PCM samples and ships no assets. The harness makes its test tone in code.

**The sounds are synthesised at startup, not loaded from files.** A seeded generator in
the game builds each clip in memory, so there are no files, no licences and nothing to
author. B8a found that generating the map is cheaper than reading it, and this follows
that precedent. They will sound functional rather than polished. File loading is left
out until a real sound exists to load; miniaudio decodes WAV, so it is a small addition
when that day comes.

**Client-side only.** Every cue is derived from state the client already has, so there
is no protocol change and nothing on the server.

- The server already sends every shot ruling to everybody (`ShotResolvedMessage`:
  shooter, victim, impact point).
- The server sends `EditApplied` to everybody **except the editor**
  (`MatchServer::Broadcast`'s `except`), and B2's collapses arrive through it as
  multi-edit batches.

**The application owns the audio, not `Application`.** `GameApplication` and the Sandbox
each create an `AudioEngine`. `Application` does not, because tests construct
applications and layers, and none of them should open a sound device. The server is not
an `Application` and has no audio at all.

## Engine

### `SoundClip` — `Cubit/include/Cubit/Audio/SoundClip.h`

```cpp
struct SoundClip
{
    std::vector<float> Samples;   // mono, -1..1
    std::uint32_t SampleRate = 48000;
};
```

A value with no methods beyond a duration helper. Mono because every clip is either
positioned, where the spatialiser makes the stereo, or 2D, where mono centred is right.

### `AudioEngine` — `Cubit/include/Cubit/Audio/AudioEngine.h`, `Cubit/src/Audio/AudioEngine.cpp`

- `AudioEngine()` opens the default output device. **When there is no device, or it fails
  to open, it logs one warning and runs silent**: every call below becomes a no-op.
  Missing sound is never a reason not to play.
- `static AudioEngine Offline(std::uint32_t sampleRate)` makes an engine with no device,
  whose mix is pulled by `Render`. This exists for tests.
- `std::vector<float> Render(std::size_t frames)` returns the next `frames` of the mix as
  interleaved stereo. It works in offline mode only and returns silence otherwise.
- `ClipId Load(const SoundClip&)` copies the samples into the engine. `ClipId` is the
  engine's own integer type.
- `void Play(ClipId, glm::vec3 position, float volume = 1.0f)` plays a sound positioned
  in the world.
- `void Play2D(ClipId, float volume = 1.0f)` plays a sound unpositioned and centred. This
  is for UI.
- `void SetListener(glm::vec3 position, glm::vec3 forward)` sets the listener; up is +Y.
- `void SetMasterVolume(float)`, clamped to 0..1.
- `std::size_t ActiveVoices() const`, for tests and the HUD.

**Voices.** A fixed pool of 32 voices. When it is full, the oldest voice is stolen, so
the pool cannot grow without limit in a firefight. A finished voice returns to the pool.
miniaudio mixes on its own thread. The facade's calls are made from the main thread
only; miniaudio does its own locking between them and the mixing thread.

**Attenuation.** Inverse distance with a reference distance of 2 m. Sounds are audible
across the 128-block map, but a gunshot at 60 m is plainly distant. These are constants
in `AudioEngine.cpp`, not settings, until play-testing asks for one.

**Movable, not copyable.** It owns a device.

### Harness

In the Sandbox, **N** plays a code-generated 440 Hz tone, 0.3 s long, at a point 4 m to
the camera's right. This lets the engine be heard without the game, and it is the
check that panning points the right way round.

### Build

`vendor/miniaudio/miniaudio.h` at a pinned release, and a `miniaudio` static-lib project
in `vendor/premake5.lua` whose only source is `miniaudio.c`
(`#define MINIAUDIO_IMPLEMENTATION` then the include), with the decoders, encoders and
generation module the engine does not use compiled out. The Cubit project links it and
alone gets `../vendor/miniaudio` on its include path.

## `MatchClient` changes

**Shot rulings get a serial and a history.** `LastShot()` holds only the most recent
ruling, so two rulings drained in one `Step` would give one gunshot. The same flaw exists
latently in `DeathAnnouncer`: it deduplicates by `ReceivedAtTick`, so two kills arriving
in one tick announce one death.

- `ShotReport` gains `std::uint64_t Serial`, starting at 1 and counting up, one per
  ruling this client has received.
- `const std::deque<ShotReport>& RecentShots() const` holds the last 32 rulings, oldest
  first. `LastShot()` stays and is still the back of the deque.
- `DeathAnnouncer` deduplicates by serial and walks the new rulings, so every kill is
  announced once.

**Edits the client shows get the same treatment.** This is new: until now the client
applied remote edits without telling anyone.

```cpp
struct ShownEdit
{
    std::uint64_t Serial;
    bool Local;                    // this client's own predicted edit
    std::vector<BlockEdit> Edits;  // one for a player's edit, many for a collapse
};
const std::deque<ShownEdit>& RecentEdits() const;   // last 32
```

A local edit is recorded when `Step` accepts the prediction, not when `RequestEdit`
queues it, so an edit the rules refuse is never heard. Recording at the point it is
first *shown* also means a later refusal does not replay the sound. An `EditApplied`
message is recorded as one entry, whatever its size.

## Game

### `SoundSynth` — `game/Game/src/SoundSynth.h`

`SoundClip Make(Cue, std::uint32_t seed)` for each cue below. It uses noise from a seeded
generator, shaped by exponential envelopes and one-pole filters, plus short sine blips
for the UI cues. The output is deterministic, so a test can pin it down.

| Cue | Sound | Positioned at |
|---|---|---|
| `Gunshot` | Loud 0.25 s noise burst, fast attack | the shooter |
| `Impact` | Short 0.08 s filtered click | the ruling's `Impact` |
| `Dig` | Low 0.12 s crunch | the block |
| `Place` | Dull 0.1 s thud | the block |
| `Crumble` | Longer 0.5 s low rumble | centre of the batch |
| `Footstep` | Very short 0.05 s soft tap, pitch varied per step | the walker's feet |
| `HitConfirm` | 2D, 0.06 s high blip | — |
| `KillConfirm` | 2D, two rising blips | — |

### `SoundCues` — `game/Game/src/SoundCues.h`

This is pure logic: it takes client state in and gives a list of `{Cue, position, is2D,
volume}` out, and never touches an `AudioEngine`. That is why GameTests can cover it.

- **Shots.** Every ruling with a serial newer than the last one seen produces two cues.
  The first is a `Gunshot` at the shooter: the camera for the local player, or
  `PoseOf(shooter)` for anyone else, or the impact point if that player is not known.
  The second is an `Impact` at `Impact`. When the local player is the shooter, a
  `HitConfirm` plays on a hit, or a `KillConfirm` on a kill. The local gunshot plays when
  the ruling arrives, not when the button is pressed. This matches the existing rule
  that a shot does nothing locally until the server rules on it, and it is one round
  trip late. Playing it on the press is a feel change for play-testing to ask for.
- **Edits.** Every `ShownEdit` newer than the last one seen produces a cue. A single edit
  plays `Dig` if it empties the cell, or `Place` otherwise. A batch plays one `Crumble`.
- **Footsteps.** For each player, horizontal distance is accumulated while they are
  walking, and a `Footstep` plays every 1.6 m. A local player is walking when
  `CharacterController::Grounded()` is true and they are not in water. A remote player
  has no grounded bit on the wire, so they count as walking when the pose's height
  changed by less than 0.05 m that frame. A remote player swimming at a steady depth
  therefore makes footsteps. This is recorded as a limitation; the fix would be a
  movement-state bit in the snapshot.
- **First observation.** On the first frame after connecting, the serials seen are set
  to the newest ones present, so joining a match does not replay its recent history.

Single-player, which is the no-client path in GameApp, feeds its own edits and its own
footsteps through the same `SoundCues` entry points.

### Wiring — `GameApp.cpp`

`GameApplication` owns the `AudioEngine`, loads the synthesised clips once, and hands
both to the player layer. Each rendered frame the layer does three things. It sets the
listener from the camera. It asks `SoundCues` for this frame's cues. It plays them.

### Settings

`master_volume`, from 0 to 1 with a default of 0.8, goes in `settings.cfg`, and
`--volume` overrides it. Both go through the existing `Apply`, so they get the same clamp
and the same NaN handling as every other setting, and the default file text gains the
line.

## Error handling

- **No device, or the device fails:** one warning, and the game runs silent.
- **Too many sounds:** the oldest voice is stolen.
- **`Play` with an unknown `ClipId`:** ignored, with no assert: a sound that fails to play is
  never worth stopping the game for, which is the rule the whole facade follows.
- **Offline `Render` on a device engine:** returns silence.

## Tests

**Engine (`Tests`)**, all on `AudioEngine::Offline`, with no device:

- A clip played 4 m to the listener's right is louder in the right channel than the
  left, and the reverse for the left.
- The same clip at 30 m is quieter than at 3 m.
- Master volume 0 renders silence; 0.5 renders half the amplitude of 1.
- `Play2D` renders equal left and right.
- The 33rd simultaneous `Play` still leaves 32 active voices.
- A clip finishes: after its length has been rendered, `ActiveVoices()` is back to 0.
- A default-constructed engine does not throw whether or not the machine has a device.
  CI machines may have none.
- **The headers include no miniaudio.** A test source includes every header under
  `Cubit/include/Cubit/Audio/` and, with no include path to `vendor/miniaudio`, compiles.
  The build is the assertion.

**MatchClient (`Tests`)**, on the loopback transport:

- Two rulings delivered in one `Step` give two entries with consecutive serials.
- A remote `EditApplied` appears in `RecentEdits` as not local. A local predicted edit
  appears as local. A locally refused edit does not appear.

**Game (`GameTests`)**:

- Each cue synthesises a clip that is non-empty, within -1..1, and identical for the
  same seed.
- `SoundCues`: one ruling gives one gunshot and one impact; three in one frame give three
  of each. A hit by the local player adds a `HitConfirm`, and a hit by someone else does
  not. A single dig gives `Dig`, and a batch gives one `Crumble`. Footsteps follow
  distance: 1.6 m in one frame and 1.6 m over a hundred frames each give exactly one step.
  First observation replays nothing.
- `DeathAnnouncer`: two kills in one frame announce two deaths.
- Settings: `master_volume` reads, clamps and rejects NaN, and `--volume` overrides it.

**By running.** Launch GameApp, walk, dig, place and fire, with a second client
connected so remote shots and edits are heard positioned. Also press N in the Sandbox and
confirm the tone is on the right. What something sounds like cannot be screenshotted, so
this check is by ear, and the user does it.

## Out of scope

- Real authored sounds and file loading. Adding them later is additive.
- Music, reverb, occlusion through blocks, and Doppler.
- Water sounds, and footsteps that vary with the surface.
- Separate volume buses such as effects, UI and music; one master volume only.
- Playing the local gunshot at the button press rather than at the ruling.
