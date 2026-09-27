#include "cub.h"

#include "Cubit/Audio/AudioEngine.h"

#include "Cubit/Logger.h"

//The only file in the project that sees the library. See AudioEngine.h.
#include "miniaudio_config.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>

namespace
{
    constexpr std::uint32_t Channels = 2;

    //Inverse-distance attenuation: full volume inside ReferenceDistance, half
    //at twice it, a tenth at ten times it. Constants rather than settings until
    //play-testing asks for one.
    constexpr float ReferenceDistance = 2.0f;
    constexpr float MaxDistance = 1000.0f;
}

//One slot a sound can play in. The data source reads the clip's samples in
//place, so a voice copies nothing when it starts.
struct Voice
{
    ma_audio_buffer_ref Source{};
    ma_sound Sound{};

    //Whether Sound has been initialised and so needs uninitialising.
    bool Initialised = false;

    //When it started, in Play calls, so the oldest can be found to steal.
    std::uint64_t Started = 0;
};

struct AudioEngine::Impl
{
    ma_engine Engine{};
    bool Ready = false;
    bool Offline = false;

    //A deque so a clip's samples never move when another is loaded: a voice
    //may be reading them on the mixing thread.
    std::deque<SoundClip> Clips;

    std::array<Voice, AudioEngine::MaxVoices> Voices{};
    std::uint64_t Plays = 0;
    float Volume = 1.0f;

    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    ~Impl()
    {
        if (!Ready)
            return;

        for (Voice& voice : Voices)
            Release(voice);

        ma_engine_uninit(&Engine);
    }

    static void Release(Voice& voice)
    {
        if (!voice.Initialised)
            return;

        ma_sound_uninit(&voice.Sound);
        ma_audio_buffer_ref_uninit(&voice.Source);
        voice.Initialised = false;
    }

    static bool Sounding(const Voice& voice)
    {
        return voice.Initialised && !ma_sound_at_end(&voice.Sound);
    }

    //A free voice if there is one, otherwise the one that started first.
    Voice& Claim()
    {
        Voice* oldest = &Voices[0];

        for (Voice& voice : Voices)
        {
            if (!Sounding(voice))
                return voice;

            if (voice.Started < oldest->Started)
                oldest = &voice;
        }

        return *oldest;
    }

    const SoundClip* Find(ClipId clip) const
    {
        if (clip == InvalidClip || clip > Clips.size())
            return nullptr;

        return &Clips[clip - 1];
    }

    //Starts a clip on a voice. `spatial` false plays it centred and unattenuated.
    void Start(ClipId clip, bool spatial, const glm::vec3& position, float volume)
    {
        if (!Ready)
            return;

        const SoundClip* found = Find(clip);
        if (found == nullptr || found->Samples.empty())
            return;

        Voice& voice = Claim();
        Release(voice);

        if (ma_audio_buffer_ref_init(ma_format_f32, 1, found->Samples.data(),
                found->Samples.size(), &voice.Source) != MA_SUCCESS)
            return;

        //Set after init, which leaves it zero; the data source reports it, and
        //the engine resamples from it.
        voice.Source.sampleRate = found->SampleRate;

        const ma_uint32 flags = spatial ? 0 : MA_SOUND_FLAG_NO_SPATIALIZATION;
        if (ma_sound_init_from_data_source(&Engine, &voice.Source, flags, nullptr,
                &voice.Sound) != MA_SUCCESS)
        {
            ma_audio_buffer_ref_uninit(&voice.Source);
            return;
        }

        voice.Initialised = true;
        voice.Started = ++Plays;

        ma_sound_set_volume(&voice.Sound, std::max(volume, 0.0f));

        if (spatial)
        {
            ma_sound_set_attenuation_model(&voice.Sound, ma_attenuation_model_inverse);
            ma_sound_set_min_distance(&voice.Sound, ReferenceDistance);
            ma_sound_set_max_distance(&voice.Sound, MaxDistance);
            ma_sound_set_position(&voice.Sound, position.x, position.y, position.z);
        }

        ma_sound_start(&voice.Sound);
    }
};

AudioEngine::AudioEngine()
    : m_Impl(std::make_unique<Impl>())
{
    ma_engine_config config = ma_engine_config_init();
    config.channels = Channels;

    if (ma_engine_init(&config, &m_Impl->Engine) != MA_SUCCESS)
    {
        CB_WARN("audio: no output device could be opened; running silent");
        return;
    }

    m_Impl->Ready = true;
    ma_engine_listener_set_world_up(&m_Impl->Engine, 0, 0.0f, 1.0f, 0.0f);
}

AudioEngine AudioEngine::Offline(std::uint32_t sampleRate)
{
    auto impl = std::make_unique<Impl>();

    ma_engine_config config = ma_engine_config_init();
    config.noDevice = MA_TRUE;
    config.channels = Channels;
    config.sampleRate = sampleRate;

    if (ma_engine_init(&config, &impl->Engine) == MA_SUCCESS)
    {
        impl->Ready = true;
        impl->Offline = true;
        ma_engine_listener_set_world_up(&impl->Engine, 0, 0.0f, 1.0f, 0.0f);
    }
    else
    {
        CB_WARN("audio: the offline mixer could not be created; running silent");
    }

    return AudioEngine(std::move(impl));
}

AudioEngine::AudioEngine(std::unique_ptr<Impl> impl)
    : m_Impl(std::move(impl))
{
}

AudioEngine::~AudioEngine() = default;
AudioEngine::AudioEngine(AudioEngine&& other) noexcept = default;
AudioEngine& AudioEngine::operator=(AudioEngine&& other) noexcept = default;

bool AudioEngine::HasOutput() const
{
    return m_Impl && m_Impl->Ready;
}

ClipId AudioEngine::Load(const SoundClip& clip)
{
    if (!m_Impl)
        return InvalidClip;

    //Kept even when silent, so a ClipId means the same thing either way.
    m_Impl->Clips.push_back(clip);
    return static_cast<ClipId>(m_Impl->Clips.size());
}

void AudioEngine::Play(ClipId clip, const glm::vec3& position, float volume)
{
    if (m_Impl)
        m_Impl->Start(clip, true, position, volume);
}

void AudioEngine::Play2D(ClipId clip, float volume)
{
    if (m_Impl)
        m_Impl->Start(clip, false, glm::vec3(0.0f), volume);
}

void AudioEngine::SetListener(const glm::vec3& position, const glm::vec3& forward)
{
    if (!HasOutput())
        return;

    ma_engine_listener_set_position(&m_Impl->Engine, 0, position.x, position.y, position.z);
    ma_engine_listener_set_direction(&m_Impl->Engine, 0, forward.x, forward.y, forward.z);
}

void AudioEngine::SetMasterVolume(float volume)
{
    if (!m_Impl)
        return;

    //NaN is unordered, so clamp would hand it straight through.
    m_Impl->Volume = std::isnan(volume) ? 0.0f : std::clamp(volume, 0.0f, 1.0f);

    if (m_Impl->Ready)
        ma_engine_set_volume(&m_Impl->Engine, m_Impl->Volume);
}

float AudioEngine::MasterVolume() const
{
    return m_Impl ? m_Impl->Volume : 0.0f;
}

std::size_t AudioEngine::ActiveVoices() const
{
    if (!HasOutput())
        return 0;

    return static_cast<std::size_t>(std::count_if(m_Impl->Voices.begin(), m_Impl->Voices.end(),
        [](const Voice& voice) { return Impl::Sounding(voice); }));
}

std::vector<float> AudioEngine::Render(std::size_t frames)
{
    std::vector<float> mix(frames * Channels, 0.0f);

    if (!HasOutput() || !m_Impl->Offline || frames == 0)
        return mix;

    ma_uint64 read = 0;
    ma_engine_read_pcm_frames(&m_Impl->Engine, mix.data(), frames, &read);
    return mix;
}
