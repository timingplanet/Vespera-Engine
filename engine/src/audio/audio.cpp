#include <vespera/audio/audio.hpp>

#include <vespera/core/log.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <vector>

namespace vespera {
namespace {

float distance(Vec3 a, Vec3 b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace

struct AudioSystem::Impl {
    struct Voice {
        AudioVoiceHandle handle = kInvalidAudioVoice;
        SDL_AudioStream* stream = nullptr;
        std::filesystem::path source;
        std::vector<Uint8> source_bytes;
        float volume = 1.0f;
        bool loop = false;
        bool paused = false;
        bool spatial = false;
        Vec3 position{};
        float min_distance = 1.0f;
        float max_distance = 20.0f;
    };

    bool initialized = false;
    float master_volume = 1.0f;
    std::string status_message = "audio system not initialized";
    std::vector<Voice> voices;
    AudioVoiceHandle next_handle = 1;
    Vec3 camera_listener_position{};
    Vec3 explicit_listener_position{};
    bool camera_listener = true;
};

AudioSystem::AudioSystem() : impl_(std::make_unique<Impl>()) {}
AudioSystem::~AudioSystem() { shutdown(); }

bool AudioSystem::initialize() {
    if (impl_->initialized) return true;
    if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            impl_->status_message = std::format("SDL audio initialization failed: {}", SDL_GetError());
            return false;
        }
    }
    impl_->initialized = true;
    impl_->status_message = "SDL3 audio playback ready (voice handles + spatial attenuation)";
    return true;
}

void AudioSystem::update() {
    if (!impl_->initialized) return;
    const Vec3 listener = listener_position();

    auto it = impl_->voices.begin();
    while (it != impl_->voices.end()) {
        if (!it->stream) {
            it = impl_->voices.erase(it);
            continue;
        }

        float attenuation = 1.0f;
        if (it->spatial) {
            const float min_distance = (std::max)(0.0f, it->min_distance);
            const float max_distance = (std::max)(min_distance + 0.001f, it->max_distance);
            const float d = distance(listener, it->position);
            if (d >= max_distance) attenuation = 0.0f;
            else if (d > min_distance) attenuation = 1.0f - ((d - min_distance) / (max_distance - min_distance));
        }
        const float gain = std::clamp(it->volume, 0.0f, 4.0f) * impl_->master_volume * attenuation;
        SDL_SetAudioStreamGain(it->stream, gain);

        const bool drained = SDL_GetAudioStreamQueued(it->stream) <= 0
            && SDL_GetAudioStreamAvailable(it->stream) <= 0;
        if (!it->paused && drained) {
            if (it->loop && !it->source_bytes.empty()) {
                if (!SDL_PutAudioStreamData(it->stream, it->source_bytes.data(), static_cast<int>(it->source_bytes.size()))
                    || !SDL_FlushAudioStream(it->stream)
                    || !SDL_ResumeAudioStreamDevice(it->stream)) {
                    log::warn(std::format("Audio: looping voice {} failed: {}", it->handle, SDL_GetError()));
                    SDL_DestroyAudioStream(it->stream);
                    it = impl_->voices.erase(it);
                    continue;
                }
            } else {
                SDL_DestroyAudioStream(it->stream);
                it = impl_->voices.erase(it);
                continue;
            }
        }
        ++it;
    }
}

void AudioSystem::shutdown() {
    if (!impl_) return;
    stop_all();
    impl_->initialized = false;
}

bool AudioSystem::initialized() const { return impl_->initialized; }
const std::string& AudioSystem::status_message() const { return impl_->status_message; }

AudioVoiceHandle AudioSystem::play(const std::filesystem::path& wav_path, float volume, bool loop) {
    if (!impl_->initialized && !initialize()) return kInvalidAudioVoice;

    SDL_AudioSpec source_spec{};
    Uint8* audio_buffer = nullptr;
    Uint32 audio_length = 0;
    const std::string path = wav_path.string();
    if (!SDL_LoadWAV(path.c_str(), &source_spec, &audio_buffer, &audio_length)) {
        impl_->status_message = std::format("could not load WAV '{}': {}", path, SDL_GetError());
        log::warn("Audio: " + impl_->status_message);
        return kInvalidAudioVoice;
    }

    SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &source_spec, nullptr, nullptr);
    if (!stream) {
        SDL_free(audio_buffer);
        impl_->status_message = std::format("could not open playback stream: {}", SDL_GetError());
        log::warn("Audio: " + impl_->status_message);
        return kInvalidAudioVoice;
    }

    std::vector<Uint8> source_bytes(audio_buffer, audio_buffer + audio_length);
    SDL_free(audio_buffer);

    const float gain = std::clamp(volume, 0.0f, 4.0f) * impl_->master_volume;
    if (!SDL_SetAudioStreamGain(stream, gain)
        || !SDL_PutAudioStreamData(stream, source_bytes.data(), static_cast<int>(source_bytes.size()))
        || !SDL_FlushAudioStream(stream)
        || !SDL_ResumeAudioStreamDevice(stream)) {
        impl_->status_message = std::format("could not queue/play WAV '{}': {}", path, SDL_GetError());
        SDL_DestroyAudioStream(stream);
        log::warn("Audio: " + impl_->status_message);
        return kInvalidAudioVoice;
    }

    AudioVoiceHandle handle = impl_->next_handle++;
    if (handle == kInvalidAudioVoice) handle = impl_->next_handle++;
    impl_->voices.push_back({handle, stream, wav_path, std::move(source_bytes),
        std::clamp(volume, 0.0f, 4.0f), loop, false, false, {}, 1.0f, 20.0f});
    impl_->status_message = std::format("playing '{}' (voice {})", path, handle);
    return handle;
}

bool AudioSystem::play_one_shot(const std::filesystem::path& wav_path, float volume) {
    return play(wav_path, volume, false) != kInvalidAudioVoice;
}

bool AudioSystem::stop(AudioVoiceHandle handle) {
    auto it = std::find_if(impl_->voices.begin(), impl_->voices.end(),
        [handle](const Impl::Voice& voice) { return voice.handle == handle; });
    if (it == impl_->voices.end()) return false;
    if (it->stream) SDL_DestroyAudioStream(it->stream);
    impl_->voices.erase(it);
    return true;
}

void AudioSystem::stop_all() {
    if (!impl_) return;
    for (auto& voice : impl_->voices) {
        if (voice.stream) SDL_DestroyAudioStream(voice.stream);
    }
    impl_->voices.clear();
}

bool AudioSystem::set_voice_volume(AudioVoiceHandle handle, float volume) {
    for (auto& voice : impl_->voices) {
        if (voice.handle != handle) continue;
        voice.volume = std::clamp(volume, 0.0f, 4.0f);
        return true;
    }
    return false;
}

bool AudioSystem::set_voice_loop(AudioVoiceHandle handle, bool loop) {
    for (auto& voice : impl_->voices) {
        if (voice.handle != handle) continue;
        voice.loop = loop;
        return true;
    }
    return false;
}

bool AudioSystem::set_voice_paused(AudioVoiceHandle handle, bool paused) {
    for (auto& voice : impl_->voices) {
        if (voice.handle != handle || !voice.stream) continue;
        const bool ok = paused ? SDL_PauseAudioStreamDevice(voice.stream) : SDL_ResumeAudioStreamDevice(voice.stream);
        if (!ok) return false;
        voice.paused = paused;
        return true;
    }
    return false;
}

void AudioSystem::set_all_paused(bool paused) {
    if (!impl_ || !impl_->initialized) return;
    for (auto& voice : impl_->voices) {
        if (!voice.stream) continue;
        const bool ok = paused
            ? SDL_PauseAudioStreamDevice(voice.stream)
            : SDL_ResumeAudioStreamDevice(voice.stream);
        if (ok) voice.paused = paused;
    }
}

bool AudioSystem::set_voice_position(AudioVoiceHandle handle, Vec3 position, bool spatial,
    float min_distance, float max_distance) {
    for (auto& voice : impl_->voices) {
        if (voice.handle != handle) continue;
        voice.position = position;
        voice.spatial = spatial;
        voice.min_distance = (std::max)(0.0f, min_distance);
        voice.max_distance = (std::max)(voice.min_distance + 0.001f, max_distance);
        return true;
    }
    return false;
}

bool AudioSystem::voice_playing(AudioVoiceHandle handle) const {
    return std::any_of(impl_->voices.begin(), impl_->voices.end(),
        [handle](const Impl::Voice& voice) { return voice.handle == handle; });
}

void AudioSystem::set_master_volume(float volume) {
    impl_->master_volume = std::clamp(volume, 0.0f, 1.0f);
}

float AudioSystem::master_volume() const { return impl_->master_volume; }
std::size_t AudioSystem::active_voice_count() const { return impl_->voices.size(); }

void AudioSystem::set_camera_listener_position(Vec3 position) {
    impl_->camera_listener_position = position;
}

void AudioSystem::set_listener_position(Vec3 position) {
    impl_->explicit_listener_position = position;
    impl_->camera_listener = false;
}

void AudioSystem::use_camera_listener() {
    impl_->camera_listener = true;
}

Vec3 AudioSystem::listener_position() const {
    return impl_->camera_listener ? impl_->camera_listener_position : impl_->explicit_listener_position;
}

bool AudioSystem::using_camera_listener() const {
    return impl_->camera_listener;
}

} // namespace vespera
