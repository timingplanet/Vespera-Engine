#pragma once

#include <vespera/render/texture_data.hpp>
#include <vespera/ui/ui.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace vespera {

struct UiPointerState {
    UiVec2 position{};
    bool available = false;
    bool primary_down = false;
    bool primary_pressed = false;
    bool primary_released = false;
};

struct UiNavigationState {
    bool focus_next = false;
    bool focus_previous = false;
    bool activate_pressed = false;
};

struct UiTextInputState {
    std::string text;
    bool backspace = false;
    bool clear = false;
};

struct UiDrawVertex {
    float x = 0.0f;
    float y = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    std::uint32_t color_rgba8 = 0xFFFFFFFFu;
};

struct UiRenderPacket {
    int viewport_width = 0;
    int viewport_height = 0;
    std::vector<UiDrawVertex> vertices;
    const TextureData* atlas = nullptr;
    std::uint64_t atlas_revision = 0;
    std::optional<UiNodeId> hovered_button;
    std::optional<UiNodeId> pressed_button;
    std::optional<UiNodeId> focused_node;
    std::vector<std::string> warnings;
};

using UiTextureResolver = std::function<std::optional<TextureData>(const AssetReference&)>;
using UiFontResolver = std::function<std::optional<std::filesystem::path>(const AssetReference&)>;

class UiRenderCache {
public:
    UiRenderCache() = default;
    ~UiRenderCache();

    void clear();
    // Rebuild the shared UI atlas from authored image and font references. On
    // Windows, TrueType/OpenType assets are loaded privately and rasterized with
    // the native font stack; other platforms retain the deterministic fallback.
    void prepare_images(const UiDocument& document, const UiTextureResolver& resolver, const UiFontResolver& font_resolver = {});

    // Runtime state is optional so read-only previews can still build packets.
    // When provided, pointer/focus/click state is updated before geometry is emitted.
    [[nodiscard]] UiRenderPacket build_packet(
        UiDocument& document,
        float viewport_width,
        float viewport_height,
        const UiPointerState& pointer = {},
        UiRuntimeState* runtime_state = nullptr,
        const UiNavigationState& navigation = {},
        const UiTextInputState& text_input = {}
    ) const;

    [[nodiscard]] const TextureData& atlas() const { return atlas_; }
    [[nodiscard]] std::uint64_t atlas_revision() const { return atlas_revision_; }
    [[nodiscard]] const std::vector<std::string>& image_warnings() const { return asset_warnings_; }
    [[nodiscard]] const std::vector<std::string>& asset_warnings() const { return asset_warnings_; }

public:
    // Internal atlas metadata is public for renderer-independent helper/testing code.
    struct AtlasUv {
        float u0 = 0.0f;
        float v0 = 0.0f;
        float u1 = 1.0f;
        float v1 = 1.0f;
        std::uint32_t pixel_width = 0;
        std::uint32_t pixel_height = 0;
    };

    struct FontGlyph {
        AtlasUv uv{};
        float advance = 0.0f;
        float offset_x = 0.0f;
        float offset_y = 0.0f;
        float line_height = 0.0f;
        float raster_size = 48.0f;
    };

private:
    TextureData atlas_;
    std::uint64_t atlas_revision_ = 1;
    std::unordered_map<std::string, AtlasUv> atlas_uvs_;
    std::unordered_map<std::string, FontGlyph> font_glyphs_;
    std::unordered_map<std::string, std::string> font_families_;
    std::vector<std::filesystem::path> loaded_font_paths_;
    std::vector<std::string> asset_warnings_;
};

} // namespace vespera
