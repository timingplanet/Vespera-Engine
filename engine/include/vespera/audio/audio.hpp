#pragma once

#include <vespera/math/types.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace vespera {

using AudioVoiceHandle = std::uint64_t;
constexpr AudioVoiceHandle kInvalidAudioVoice = 0;

// SDL3-backed runtime audio service. 0.6.3 expands the original one-shot
// foundation into handle-based voices that can loop, pause, move through the
// 2.5D world, and attenuate against a listener. Scene data still does not own
// device/stream resources; those remain process/runtime state in AudioSystem.
class AudioSystem {
public:
    AudioSystem();
    ~AudioSystem();

    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    bool initialize();
    void update();
    void shutdown();

    [[nodiscard]] bool initialized() const;
    [[nodiscard]] const std::string& status_message() const;

    AudioVoiceHandle play(const std::filesystem::path& wav_path, float volume = 1.0f, bool loop = false);
    bool play_one_shot(const std::filesystem::path& wav_path, float volume = 1.0f);
    bool stop(AudioVoiceHandle handle);
    void stop_all();

    bool set_voice_volume(AudioVoiceHandle handle, float volume);
    bool set_voice_loop(AudioVoiceHandle handle, bool loop);
    bool set_voice_paused(AudioVoiceHandle handle, bool paused);
    // Runtime-host convenience used by editor Pause/Resume. Existing per-voice
    // paused state is replaced deliberately; gameplay can restore individual
    // voice policy after resume if it needs a bespoke paused mix.
    void set_all_paused(bool paused);
    bool set_voice_position(AudioVoiceHandle handle, Vec3 position, bool spatial = true,
        float min_distance = 1.0f, float max_distance = 20.0f);
    [[nodiscard]] bool voice_playing(AudioVoiceHandle handle) const;

    void set_master_volume(float volume);
    [[nodiscard]] float master_volume() const;
    [[nodiscard]] std::size_t active_voice_count() const;

    // By default the Application feeds the active Scene camera here each frame.
    // Gameplay can override the listener explicitly (for example, attaching it
    // to a player entity) and return to camera-following with use_camera_listener().
    void set_camera_listener_position(Vec3 position);
    void set_listener_position(Vec3 position);
    void use_camera_listener();
    [[nodiscard]] Vec3 listener_position() const;
    [[nodiscard]] bool using_camera_listener() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vespera
