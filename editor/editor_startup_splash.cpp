#include "editor_startup_splash.hpp"
#include "editor_installation.hpp"
#include <vespera/assets/texture_importer.hpp>
#include <vespera/render/render_backend.hpp>
#include <vespera/ui/ui_render.hpp>
#include <algorithm>
#include <cstdint>
#include <filesystem>
namespace vespera::editor {
vespera::UiRenderPacket make_editor_startup_splash_packet(
    const vespera::TextureData& texture,
    int viewport_width,
    int viewport_height
) {
    vespera::UiRenderPacket packet;
    packet.viewport_width = std::max(viewport_width, 1);
    packet.viewport_height = std::max(viewport_height, 1);
    packet.atlas = &texture;
    packet.atlas_revision = 1;
    if (!texture.valid()) return packet;

    const float vw = static_cast<float>(packet.viewport_width);
    const float vh = static_cast<float>(packet.viewport_height);
    const float iw = static_cast<float>(texture.width);
    const float ih = static_cast<float>(texture.height);
    const float scale = std::min(vw / iw, vh / ih);
    const float width = iw * scale;
    const float height = ih * scale;
    const float x0 = (vw - width) * 0.5f;
    const float y0 = (vh - height) * 0.5f;
    const float x1 = x0 + width;
    const float y1 = y0 + height;
    constexpr std::uint32_t white = 0xFFFFFFFFu;
    packet.vertices = {
        {x0, y0, 0.0f, 0.0f, white}, {x1, y0, 1.0f, 0.0f, white}, {x1, y1, 1.0f, 1.0f, white},
        {x0, y0, 0.0f, 0.0f, white}, {x1, y1, 1.0f, 1.0f, white}, {x0, y1, 0.0f, 1.0f, white},
    };
    return packet;
}

void present_editor_startup_splash(vespera::RenderBackend& renderer) {
    const auto root = editor_installation_root();
    const auto splash_path = root.empty()
        ? std::filesystem::path("branding/vespera_splash.png")
        : root / "branding" / "vespera_splash.png";
    const auto splash = vespera::import_texture(splash_path, "Vespera editor startup splash");
    if (!splash || !splash.texture.valid() || !renderer.begin_frame()) return;
    auto packet = make_editor_startup_splash_packet(
        splash.texture,
        std::max(renderer.target_width(), 1),
        std::max(renderer.target_height(), 1));
    renderer.render_ui(packet);
    (void)renderer.end_frame();
}


} // namespace vespera::editor
