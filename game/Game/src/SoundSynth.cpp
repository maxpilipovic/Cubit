#include "SoundSynth.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace CubitGame
{
    namespace
    {
        constexpr float Pi = 3.14159265f;

        //A fixed generator rather than <random>'s distributions, whose output
        //the standard leaves to each library - the same seed must make the
        //same sound on every build.
        class Noise
        {
        public:
            explicit Noise(std::uint32_t seed) : m_State(seed * 2654435761u + 1u) {}

            //Uniform in -1..1.
            float Next()
            {
                m_State ^= m_State << 13;
                m_State ^= m_State >> 17;
                m_State ^= m_State << 5;
                return static_cast<float>(m_State) / 2147483648.0f - 1.0f;
            }

        private:
            std::uint32_t m_State;
        };

        //Smooths a signal: `amount` near 0 keeps only the rumble, 1 keeps all.
        class LowPass
        {
        public:
            explicit LowPass(float amount) : m_Amount(amount) {}

            float operator()(float input)
            {
                m_Value += m_Amount * (input - m_Value);
                return m_Value;
            }

        private:
            float m_Amount;
            float m_Value = 0.0f;
        };

        std::size_t Frames(float seconds)
        {
            return static_cast<std::size_t>(seconds * SynthRate);
        }

        float Time(std::size_t frame)
        {
            return static_cast<float>(frame) / SynthRate;
        }

        //Rises over `attack` seconds, then dies away with time constant `decay`.
        float Envelope(float t, float attack, float decay)
        {
            const float rise = attack > 0.0f ? std::min(t / attack, 1.0f) : 1.0f;
            return rise * std::exp(-t / decay);
        }

        //Scales to `peak`, and fades the last few milliseconds so no clip ends
        //on a step, which is heard as a click.
        SoundClip Finish(std::vector<float> samples, float peak)
        {
            float loudest = 0.0f;
            for (float sample : samples)
                loudest = std::max(loudest, std::abs(sample));

            const float scale = loudest > 0.0f ? peak / loudest : 0.0f;
            const std::size_t fade = std::min(samples.size(), Frames(0.004f));

            for (std::size_t i = 0; i < samples.size(); ++i)
            {
                samples[i] *= scale;

                const std::size_t fromEnd = samples.size() - 1 - i;
                if (fromEnd < fade)
                    samples[i] *= static_cast<float>(fromEnd) / fade;
            }

            SoundClip clip;
            clip.Samples = std::move(samples);
            clip.SampleRate = SynthRate;
            return clip;
        }

        //Filtered noise under an envelope: the shape of every non-UI sound here.
        std::vector<float> Burst(std::uint32_t seed, float seconds, float attack, float decay,
            float brightness)
        {
            Noise noise(seed);
            LowPass filter(brightness);

            std::vector<float> samples(Frames(seconds));
            for (std::size_t i = 0; i < samples.size(); ++i)
                samples[i] = filter(noise.Next()) * Envelope(Time(i), attack, decay);

            return samples;
        }

        //A sine whose pitch slides from `from` to `to` Hz.
        void AddTone(std::vector<float>& samples, float from, float to, float level,
            float decay, std::size_t start = 0)
        {
            float phase = 0.0f;
            const std::size_t length = samples.size() - std::min(start, samples.size());

            for (std::size_t i = 0; i < length; ++i)
            {
                const float t = Time(i);
                const float progress = static_cast<float>(i) / std::max<std::size_t>(length, 1);
                phase += 2.0f * Pi * (from + (to - from) * progress) / SynthRate;
                samples[start + i] += level * std::sin(phase) * Envelope(t, 0.001f, decay);
            }
        }

        SoundClip Gunshot(std::uint32_t seed)
        {
            //A bright crack over a low thump.
            std::vector<float> samples = Burst(seed, 0.3f, 0.0005f, 0.045f, 0.6f);
            AddTone(samples, 110.0f, 45.0f, 0.8f, 0.07f);
            return Finish(std::move(samples), 0.95f);
        }

        SoundClip Impact(std::uint32_t seed)
        {
            //Short and sharp: noise with its low end taken out.
            Noise noise(seed);
            LowPass low(0.1f);

            std::vector<float> samples(Frames(0.08f));
            for (std::size_t i = 0; i < samples.size(); ++i)
            {
                const float raw = noise.Next();
                samples[i] = (raw - low(raw)) * Envelope(Time(i), 0.0003f, 0.012f);
            }

            return Finish(std::move(samples), 0.6f);
        }

        SoundClip Dig(std::uint32_t seed)
        {
            //A dull crunch, made gritty by chopping the noise into grains.
            std::vector<float> samples = Burst(seed, 0.14f, 0.002f, 0.04f, 0.15f);
            Noise grains(seed + 101);
            float grain = 1.0f;

            for (std::size_t i = 0; i < samples.size(); ++i)
            {
                if (i % Frames(0.006f) == 0)
                    grain = 0.5f + 0.5f * std::abs(grains.Next());
                samples[i] *= grain;
            }

            return Finish(std::move(samples), 0.7f);
        }

        SoundClip Place(std::uint32_t seed)
        {
            //A soft knock: a falling low tone and a little noise.
            std::vector<float> samples = Burst(seed, 0.1f, 0.001f, 0.02f, 0.2f);
            for (float& sample : samples)
                sample *= 0.4f;
            AddTone(samples, 220.0f, 120.0f, 0.8f, 0.03f);
            return Finish(std::move(samples), 0.65f);
        }

        SoundClip Crumble(std::uint32_t seed)
        {
            //A rumble with a scatter of little crunches through it.
            std::vector<float> samples = Burst(seed, 0.55f, 0.01f, 0.18f, 0.04f);
            Noise scatter(seed + 7);

            for (int k = 0; k < 9; ++k)
            {
                const float at = 0.03f + 0.35f * (0.5f + 0.5f * scatter.Next());
                std::vector<float> crunch = Burst(seed + 13 + k, 0.06f, 0.001f, 0.012f, 0.3f);
                const std::size_t start = Frames(at);

                for (std::size_t i = 0; i < crunch.size() && start + i < samples.size(); ++i)
                    samples[start + i] += 0.5f * crunch[i];
            }

            return Finish(std::move(samples), 0.8f);
        }

        SoundClip Footstep(std::uint32_t seed)
        {
            //A soft tap. The seed moves the filter a little, so four steps are
            //four sounds.
            Noise vary(seed + 31);
            const float brightness = 0.12f + 0.06f * vary.Next();
            std::vector<float> samples = Burst(seed, 0.06f, 0.001f, 0.012f, brightness);
            return Finish(std::move(samples), 0.35f);
        }

        SoundClip HitConfirm()
        {
            std::vector<float> samples(Frames(0.06f), 0.0f);
            AddTone(samples, 1800.0f, 1800.0f, 1.0f, 0.02f);
            return Finish(std::move(samples), 0.35f);
        }

        SoundClip KillConfirm()
        {
            //Two rising blips.
            std::vector<float> samples(Frames(0.16f), 0.0f);
            AddTone(samples, 1200.0f, 1200.0f, 1.0f, 0.025f);
            AddTone(samples, 1800.0f, 1800.0f, 1.0f, 0.025f, Frames(0.08f));
            return Finish(std::move(samples), 0.4f);
        }
    }

    const char* CueName(Cue cue)
    {
        switch (cue)
        {
        case Cue::Gunshot:     return "gunshot";
        case Cue::Impact:      return "impact";
        case Cue::Dig:         return "dig";
        case Cue::Place:       return "place";
        case Cue::Crumble:     return "crumble";
        case Cue::Footstep:    return "footstep";
        case Cue::HitConfirm:  return "hit confirm";
        case Cue::KillConfirm: return "kill confirm";
        }

        return "unknown";
    }

    SoundClip Synthesise(Cue cue, std::uint32_t seed)
    {
        switch (cue)
        {
        case Cue::Gunshot:     return Gunshot(seed);
        case Cue::Impact:      return Impact(seed);
        case Cue::Dig:         return Dig(seed);
        case Cue::Place:       return Place(seed);
        case Cue::Crumble:     return Crumble(seed);
        case Cue::Footstep:    return Footstep(seed);
        case Cue::HitConfirm:  return HitConfirm();
        case Cue::KillConfirm: return KillConfirm();
        }

        return {};
    }
}
