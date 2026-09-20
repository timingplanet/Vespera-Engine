#include <vespera/render/render_backend.hpp>

namespace vespera {
namespace {

class NullRenderer final : public RenderBackend {
public:
    bool initialize(SDL_Window*) override { return true; }
    void resize(int width, int height) override { width_ = width; height_ = height; }
    bool begin_frame(const RenderFrameConfig&) override { frame_stats_ = {}; return true; }
    void render_scene(const Scene&, double, const Camera*, const RenderViewport*) override {}
    void render_ui(const UiRenderPacket&, const RenderViewport*) override {}
    bool end_frame() override { return true; }
    int target_width() const override { return width_; }
    int target_height() const override { return height_; }
    void shutdown() override {}
    std::string_view name() const override { return "Null Renderer"; }
    const RendererCapabilities& capabilities() const override { return capabilities_; }
    void set_vsync_enabled(bool enabled) override { vsync_enabled_ = enabled; }
    bool vsync_enabled() const override { return vsync_enabled_; }
    const RenderFrameStats& frame_stats() const override { return frame_stats_; }

private:
    int width_ = 0;
    int height_ = 0;
    RendererCapabilities capabilities_{};
    bool vsync_enabled_ = true;
    RenderFrameStats frame_stats_{};
};

} // namespace

std::unique_ptr<RenderBackend> create_null_render_backend() {
    return std::make_unique<NullRenderer>();
}

} // namespace vespera
