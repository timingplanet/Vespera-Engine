#pragma once

#include <algorithm>
#include <cstdint>
#include <vespera/render/render_stats.hpp>

namespace vespera {

class AudioSystem;
class InputSystem;
class Scene;
class RenderBackend;

struct RuntimePerformanceCounters {
    std::uint64_t frame_index = 0;
    std::uint64_t live_resize_redraws = 0;
    double last_frame_ms = 0.0;
    double smoothed_frame_ms = 0.0;
    double max_frame_ms = 0.0;
    double update_ms = 0.0;
    double render_ms = 0.0;
    RenderFrameStats render{};

    void record_frame(double frame_ms, double update_duration_ms, double render_duration_ms) {
        last_frame_ms = std::max(0.0, frame_ms);
        update_ms = std::max(0.0, update_duration_ms);
        render_ms = std::max(0.0, render_duration_ms);
        max_frame_ms = std::max(max_frame_ms, last_frame_ms);
        smoothed_frame_ms = frame_index == 0
            ? last_frame_ms
            : (smoothed_frame_ms * 0.9 + last_frame_ms * 0.1);
        ++frame_index;
    }
};

struct GameContext {
    Scene& scene;
    InputSystem& input;
    AudioSystem& audio;
    RuntimePerformanceCounters& performance;
};

class Game {
public:
    virtual ~Game() = default;

    virtual void on_start(GameContext&) {}
    virtual void on_update(GameContext&, double delta_seconds) = 0;
    virtual void on_render(GameContext&, RenderBackend&, double) {}
    virtual void on_stop(GameContext&) {}
    // Lightweight host-exit hook used by launcher/tooling games such as the
    // Project Hub. Normal gameplay can ignore it; closing the SDL window still
    // works as before.
    [[nodiscard]] virtual bool wants_quit() const { return false; }
    // Process exit status for hosts that reject an invalid project after the
    // SDL window is created. Normal games remain success (0).
    [[nodiscard]] virtual int exit_code() const { return 0; }
};

} // namespace vespera
