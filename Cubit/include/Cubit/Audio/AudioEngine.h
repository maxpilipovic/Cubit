#pragma once

#include "Cubit/Core.h"
#include "Cubit/Audio/SoundClip.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

//The engine's own name for a loaded sound. Zero is never a clip.
using ClipId = std::uint32_t;
constexpr ClipId InvalidClip = 0;

//Plays sounds, positioned in the world or not.
//
//A facade: nothing in this header names the library underneath, which lives
//entirely in AudioEngine.cpp. Swapping it would touch that file and the vendor
//build, and nothing that includes this.
//
//Every call is made from the main thread. The mixing happens on the library's
//own thread, which is not the caller's concern.
//
//An engine with no sound device is SILENT, not broken: every call still
//succeeds and nothing is heard. Missing sound is never a reason not to play.
class CB_API AudioEngine
{
public:
    //Voices play at once at most. A Play beyond this steals the oldest.
    static constexpr std::size_t MaxVoices = 32;

    //Opens the default output device, or logs once and runs silent.
    AudioEngine();

    //No device: the mix is pulled with Render instead. For tests.
    static AudioEngine Offline(std::uint32_t sampleRate = 48000);

    ~AudioEngine();

    //Owns a device and the voices playing on it.
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;
    AudioEngine(AudioEngine&& other) noexcept;
    AudioEngine& operator=(AudioEngine&& other) noexcept;

    //False when there is no device and this is not offline: nothing will be
    //heard. Everything still works.
    bool HasOutput() const;

    //Copies the samples in. The clip can be dropped afterwards.
    ClipId Load(const SoundClip& clip);

    //Plays a clip at a point in the world, heard from the listener.
    void Play(ClipId clip, const glm::vec3& position, float volume = 1.0f);

    //Plays a clip from nowhere in particular: centred, at full volume
    //wherever the listener is. For UI.
    void Play2D(ClipId clip, float volume = 1.0f);

    //Where the ears are. Up is +Y.
    void SetListener(const glm::vec3& position, const glm::vec3& forward);

    //Held to 0..1.
    void SetMasterVolume(float volume);
    float MasterVolume() const;

    //Voices still sounding.
    std::size_t ActiveVoices() const;

    //The next `frames` of the mix, as interleaved stereo. Offline only; an
    //engine with a device returns silence, because the device is the one
    //reading the mix.
    std::vector<float> Render(std::size_t frames);

private:
    struct Impl;

    explicit AudioEngine(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_Impl;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
