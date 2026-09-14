#pragma once

#include <array>
#include <vespera/render/render_stats.hpp>
#include <memory>
#include <string>
#include <string_view>

struct SDL_Window;

namespace vespera {

class Scene;
struct Camera;
struct UiRenderPacket;

struct RendererCapabilities {
    std::string adapter_name;
    unsigned long long dedicated_video_memory_bytes = 0;
    int feature_level_major = 0;
    int feature_level_minor = 0;
    bool hardware_ray_tracing = false;
    int ray_tracing_tier_major = 0;
    int ray_tracing_tier_minor = 0;
    bool variable_rate_shading = false;
    bool mesh_shaders = false;
};

// Pixel-space viewport inside the active render target. Width/height <= 0 means
// "use the whole target". Keeping this renderer-independent lets tools such as
// the editor render the same scene into a sub-region today and later map the
// same concept to Vulkan/offscreen targets without changing scene code.
struct RenderViewport {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct RenderFrameConfig {
    std::array<float, 4> clear_color{0.018f, 0.024f, 0.035f, 1.0f};
};

class RenderBackend {
public:
    virtual ~RenderBackend() = default;

    virtual bool initialize(SDL_Window* window) = 0;
    virtual void resize(int pixel_width, int pixel_height) = 0;
    virtual void set_vsync_enabled(bool enabled) = 0;
    [[nodiscard]] virtual bool vsync_enabled() const = 0;
    [[nodiscard]] virtual const RenderFrameStats& frame_stats() const = 0;

    // Staged frame API used by tools. begin_frame() owns back-buffer/depth
    // transitions and clearing. One or more render_scene() calls may follow,
    // then external tooling may record backend-native overlay commands before
    // end_frame() transitions/presents the frame.
    virtual bool begin_frame(const RenderFrameConfig& config = {}) = 0;
    virtual void render_scene(
        const Scene& scene,
        double total_seconds,
        const Camera* camera_override = nullptr,
        const RenderViewport* viewport = nullptr
    ) = 0;
    // Engine-native runtime UI overlay. Coordinates in UiRenderPacket are local
    // to the supplied viewport. Backends render it after 3D without exposing UI
    // documents to renderer-specific code.
    virtual void render_ui(
        const UiRenderPacket& packet,
        const RenderViewport* viewport = nullptr
    ) = 0;
    virtual bool end_frame() = 0;
    [[nodiscard]] virtual int target_width() const = 0;
    [[nodiscard]] virtual int target_height() const = 0;

    // Runtime convenience path retained for normal games.
    virtual void render(const Scene& scene, double total_seconds) {
        if (!begin_frame()) return;
        render_scene(scene, total_seconds, nullptr, nullptr);
        end_frame();
    }

    virtual void shutdown() = 0;
    [[nodiscard]] virtual std::string_view name() const = 0;
    [[nodiscard]] virtual const RendererCapabilities& capabilities() const = 0;
};

std::unique_ptr<RenderBackend> create_default_render_backend();

} // namespace vespera
