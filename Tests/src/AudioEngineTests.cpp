#include <doctest.h>

//Every public audio header, included by a project that has no include path to
//the library underneath. If one of them ever names it, this file stops
//compiling - the build is the assertion that the facade holds.
#include "Cubit/Audio/AudioEngine.h"
#include "Cubit/Audio/SoundClip.h"

#include <glm/glm.hpp>

#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace
{
    constexpr std::uint32_t Rate = 48000;

    //A steady tone, so a window of the mix has a level worth comparing.
    SoundClip Tone(float seconds, float amplitude = 0.5f)
    {
        SoundClip clip;
        clip.SampleRate = Rate;
        clip.Samples.resize(static_cast<std::size_t>(seconds * Rate));

        for (std::size_t i = 0; i < clip.Samples.size(); ++i)
            clip.Samples[i] = amplitude * std::sin(2.0f * 3.14159265f * 440.0f * i / Rate);

        return clip;
    }

    struct Levels
    {
        float Left = 0.0f;
        float Right = 0.0f;
    };

    //Root mean square of each channel of interleaved stereo.
    Levels Measure(const std::vector<float>& stereo)
    {
        double left = 0.0;
        double right = 0.0;
        const std::size_t frames = stereo.size() / 2;

        for (std::size_t i = 0; i < frames; ++i)
        {
            left += stereo[2 * i] * stereo[2 * i];
            right += stereo[2 * i + 1] * stereo[2 * i + 1];
        }

        if (frames == 0)
            return {};

        return { static_cast<float>(std::sqrt(left / frames)),
                 static_cast<float>(std::sqrt(right / frames)) };
    }

    //Plays one tone at `position` on a fresh engine, listening from the origin
    //facing -Z, and measures a tenth of a second of it.
    Levels Hear(const glm::vec3& position)
    {
        AudioEngine engine = AudioEngine::Offline(Rate);
        engine.SetListener(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f));

        const ClipId tone = engine.Load(Tone(1.0f));
        engine.Play(tone, position);

        return Measure(engine.Render(Rate / 10));
    }
}

TEST_CASE("An offline engine has output and starts silent")
{
    AudioEngine engine = AudioEngine::Offline(Rate);

    CHECK(engine.HasOutput());
    CHECK(engine.ActiveVoices() == 0);

    const std::vector<float> mix = engine.Render(480);
    REQUIRE(mix.size() == 960);

    const Levels levels = Measure(mix);
    CHECK(levels.Left == 0.0f);
    CHECK(levels.Right == 0.0f);
}

TEST_CASE("A sound to the listener's right is louder on the right")
{
    const Levels right = Hear(glm::vec3(4.0f, 0.0f, 0.0f));
    CHECK(right.Right > 0.0f);
    CHECK(right.Right > right.Left * 1.5f);

    const Levels left = Hear(glm::vec3(-4.0f, 0.0f, 0.0f));
    CHECK(left.Left > 0.0f);
    CHECK(left.Left > left.Right * 1.5f);
}

TEST_CASE("Turning the listener around swaps the sides")
{
    AudioEngine engine = AudioEngine::Offline(Rate);

    //Facing +Z, +X is on the left.
    engine.SetListener(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    engine.Play(engine.Load(Tone(1.0f)), glm::vec3(4.0f, 0.0f, 0.0f));

    const Levels levels = Measure(engine.Render(Rate / 10));
    CHECK(levels.Left > levels.Right * 1.5f);
}

TEST_CASE("A distant sound is quieter than a near one")
{
    const Levels near = Hear(glm::vec3(0.0f, 0.0f, -3.0f));
    const Levels far = Hear(glm::vec3(0.0f, 0.0f, -30.0f));

    CHECK(near.Left > 0.0f);
    CHECK(far.Left > 0.0f);
    CHECK(far.Left < near.Left * 0.5f);
}

TEST_CASE("A 2D sound is centred wherever the listener is")
{
    AudioEngine engine = AudioEngine::Offline(Rate);
    engine.SetListener(glm::vec3(100.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    engine.Play2D(engine.Load(Tone(1.0f)));

    const Levels levels = Measure(engine.Render(Rate / 10));
    CHECK(levels.Left > 0.0f);
    CHECK(levels.Left == doctest::Approx(levels.Right).epsilon(0.01));
}

TEST_CASE("Master volume scales the mix and zero silences it")
{
    auto level = [](float volume)
    {
        AudioEngine engine = AudioEngine::Offline(Rate);
        engine.SetMasterVolume(volume);
        engine.Play2D(engine.Load(Tone(1.0f)));
        return Measure(engine.Render(Rate / 10)).Left;
    };

    const float full = level(1.0f);
    CHECK(full > 0.0f);
    CHECK(level(0.5f) == doctest::Approx(full * 0.5f).epsilon(0.02));
    CHECK(level(0.0f) == 0.0f);
}

TEST_CASE("Master volume is held to 0..1")
{
    AudioEngine engine = AudioEngine::Offline(Rate);

    engine.SetMasterVolume(3.0f);
    CHECK(engine.MasterVolume() == 1.0f);

    engine.SetMasterVolume(-1.0f);
    CHECK(engine.MasterVolume() == 0.0f);

    engine.SetMasterVolume(std::nanf(""));
    CHECK(engine.MasterVolume() == 0.0f);
}

TEST_CASE("More sounds than voices steals the oldest rather than growing")
{
    AudioEngine engine = AudioEngine::Offline(Rate);
    const ClipId tone = engine.Load(Tone(1.0f));

    for (std::size_t i = 0; i < AudioEngine::MaxVoices + 5; ++i)
        engine.Play2D(tone);

    CHECK(engine.ActiveVoices() == AudioEngine::MaxVoices);
}

TEST_CASE("A voice is free again once its clip has played out")
{
    AudioEngine engine = AudioEngine::Offline(Rate);
    engine.Play2D(engine.Load(Tone(0.05f)));
    CHECK(engine.ActiveVoices() == 1);

    engine.Render(Rate / 10);
    CHECK(engine.ActiveVoices() == 0);
}

TEST_CASE("A clip at another sample rate plays for its own length")
{
    AudioEngine engine = AudioEngine::Offline(Rate);

    SoundClip clip = Tone(0.1f);
    clip.SampleRate = Rate / 2;   //the same samples, now 0.2 s long
    engine.Play2D(engine.Load(clip));

    engine.Render(Rate * 15 / 100);
    CHECK(engine.ActiveVoices() == 1);

    engine.Render(Rate * 10 / 100);
    CHECK(engine.ActiveVoices() == 0);
}

TEST_CASE("Playing no clip, or a clip that was never loaded, does nothing")
{
    AudioEngine engine = AudioEngine::Offline(Rate);

    engine.Play2D(InvalidClip);
    engine.Play(12345, glm::vec3(0.0f));

    CHECK(engine.ActiveVoices() == 0);
    CHECK(Measure(engine.Render(480)).Left == 0.0f);
}

TEST_CASE("An empty clip loads and plays without sounding")
{
    AudioEngine engine = AudioEngine::Offline(Rate);
    const ClipId empty = engine.Load(SoundClip{});

    CHECK(empty != InvalidClip);
    engine.Play2D(empty);
    CHECK(Measure(engine.Render(480)).Left == 0.0f);
}

TEST_CASE("A moved-from engine is inert and the moved-to one keeps playing")
{
    AudioEngine first = AudioEngine::Offline(Rate);
    first.Play2D(first.Load(Tone(1.0f)));

    AudioEngine second = std::move(first);
    CHECK(second.ActiveVoices() == 1);
    CHECK(Measure(second.Render(480)).Left > 0.0f);

    //Every call on the husk is safe and does nothing.
    CHECK(!first.HasOutput());
    CHECK(first.Load(Tone(0.1f)) == InvalidClip);
    first.Play2D(1);
    first.SetMasterVolume(0.5f);
    CHECK(first.ActiveVoices() == 0);
    CHECK(first.Render(4).size() == 8);
}

TEST_CASE("A device engine constructs whether or not this machine has a device")
{
    //A build machine may have no sound card; this must not throw either way.
    AudioEngine engine;
    engine.SetMasterVolume(0.0f);

    //Silent or not, an engine with a device never hands its mix to Render.
    const std::vector<float> mix = engine.Render(64);
    CHECK(mix.size() == 128);
    CHECK(Measure(mix).Left == 0.0f);
}
