#pragma once

#include <vespera/assets/asset_reference.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vespera {

using UiNodeId = std::uint64_t;
constexpr UiNodeId kInvalidUiNodeId = 0;

struct UiVec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct UiRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct UiInsets {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

enum class UiNodeType : std::uint8_t {
    Canvas,
    Panel,
    Text,
    Image,
    Button,
    ProgressBar,
    ScrollView,
    List,
    Grid,
    Tabs,
    Modal,
    Tooltip,
    TextInput,
};

[[nodiscard]] constexpr std::string_view ui_node_type_name(UiNodeType type) {
    switch (type) {
        case UiNodeType::Canvas: return "Canvas";
        case UiNodeType::Panel: return "Panel";
        case UiNodeType::Text: return "Text";
        case UiNodeType::Image: return "Image";
        case UiNodeType::Button: return "Button";
        case UiNodeType::ProgressBar: return "Progress Bar";
        case UiNodeType::ScrollView: return "Scroll View";
        case UiNodeType::List: return "List";
        case UiNodeType::Grid: return "Grid";
        case UiNodeType::Tabs: return "Tabs";
        case UiNodeType::Modal: return "Modal";
        case UiNodeType::Tooltip: return "Tooltip";
        case UiNodeType::TextInput: return "Text Input";
    }
    return "Panel";
}

enum class UiScaleMode : std::uint8_t {
    ConstantPixelSize,
    ScaleWithScreenSize,
};

enum class UiHorizontalAlignment : std::uint8_t {
    Left,
    Center,
    Right,
};

enum class UiVerticalAlignment : std::uint8_t {
    Top,
    Middle,
    Bottom,
};

enum class UiLayoutMode : std::uint8_t {
    None,
    Horizontal,
    Vertical,
    Grid,
};

enum class UiImageFit : std::uint8_t {
    Stretch,
    Contain,
    Cover,
};

// Renderer-independent RectTransform-like authoring data. Anchors are normalized
// inside the parent rectangle. offset_min/offset_max are authored pixels in the
// Canvas reference-resolution space. A fixed-size element uses equal anchors.
struct UiRectTransform {
    UiVec2 anchor_min{0.0f, 0.0f};
    UiVec2 anchor_max{0.0f, 0.0f};
    UiVec2 offset_min{0.0f, 0.0f};
    UiVec2 offset_max{100.0f, 40.0f};
    UiVec2 pivot{0.5f, 0.5f};
};

struct UiCanvasProperties {
    UiVec2 reference_resolution{1280.0f, 720.0f};
    UiScaleMode scale_mode = UiScaleMode::ScaleWithScreenSize;
    float match_width_or_height = 0.5f;
};

struct UiVisualProperties {
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
    AssetReference image;
};

// Common authored surface styling shared by panels, buttons, inputs and images.
// Keeping appearance separate from layout lets one widget hierarchy be reskinned
// without rewriting RectTransforms or gameplay bindings.
struct UiSurfaceProperties {
    float opacity = 1.0f;
    float corner_radius = 0.0f;
    float border_width = 0.0f;
    std::array<float, 4> border_color{0.0f, 0.0f, 0.0f, 0.0f};
    UiVec2 shadow_offset{0.0f, 0.0f};
    float shadow_softness = 0.0f;
    std::array<float, 4> shadow_color{0.0f, 0.0f, 0.0f, 0.0f};
    UiImageFit image_fit = UiImageFit::Stretch;
    UiInsets nine_slice{};
};

struct UiTextProperties {
    std::string text = "Text";
    AssetReference font;
    std::string font_family = "Segoe UI";
    float font_size = 18.0f;
    std::uint16_t font_weight = 400;
    bool italic = false;
    bool wrap = false;
    float line_spacing = 1.0f;
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
    UiHorizontalAlignment horizontal_alignment = UiHorizontalAlignment::Left;
    UiVerticalAlignment vertical_alignment = UiVerticalAlignment::Top;
    UiVec2 shadow_offset{0.0f, 0.0f};
    std::array<float, 4> shadow_color{0.0f, 0.0f, 0.0f, 0.0f};
    bool rich_text = true;
};

struct UiButtonProperties {
    bool interactable = true;
    std::array<float, 4> normal_color{0.18f, 0.20f, 0.25f, 1.0f};
    std::array<float, 4> hovered_color{0.25f, 0.28f, 0.36f, 1.0f};
    std::array<float, 4> pressed_color{0.12f, 0.14f, 0.18f, 1.0f};
    std::array<float, 4> disabled_color{0.10f, 0.11f, 0.13f, 0.72f};
};

struct UiLayoutProperties {
    UiLayoutMode mode = UiLayoutMode::None;
    UiVec2 spacing{8.0f, 8.0f};
    UiVec2 cell_size{0.0f, 0.0f};
    std::uint32_t columns = 2;
    bool clip_children = false;
};

struct UiProgressProperties {
    float value = 0.5f;
    float minimum = 0.0f;
    float maximum = 1.0f;
    std::array<float, 4> background_color{0.10f, 0.11f, 0.14f, 1.0f};
    std::array<float, 4> fill_color{0.35f, 0.66f, 0.95f, 1.0f};
};

struct UiScrollProperties {
    UiVec2 offset{};
};

struct UiTabsProperties {
    std::uint32_t active_index = 0;
};

struct UiInputProperties {
    std::string placeholder = "Enter text...";
    std::uint32_t max_length = 256;
    bool read_only = false;
};

struct UiNode {
    UiNodeId id = kInvalidUiNodeId;
    UiNodeId parent_id = kInvalidUiNodeId;
    std::string name = "UI Node";
    UiNodeType type = UiNodeType::Panel;
    bool enabled = true;
    int z_order = 0;
    UiRectTransform rect{};
    UiInsets margin{};
    UiInsets padding{};
    UiCanvasProperties canvas{};
    UiVisualProperties visual{};
    UiSurfaceProperties surface{};
    UiTextProperties text{};
    UiButtonProperties button{};
    UiLayoutProperties layout{};
    UiProgressProperties progress{};
    UiScrollProperties scroll{};
    UiTabsProperties tabs{};
    UiInputProperties input{};
};

struct UiResolvedNode {
    UiNodeId id = kInvalidUiNodeId;
    UiNodeId parent_id = kInvalidUiNodeId;
    UiNodeType type = UiNodeType::Panel;
    UiRect rect{};
    UiRect clip_rect{};
    bool clip_enabled = false;
    float scale = 1.0f;
    int z_order = 0;
    bool enabled = true;
};

struct UiLayoutResult {
    std::vector<UiResolvedNode> nodes;
    float canvas_scale = 1.0f;
    UiVec2 viewport{};
    std::vector<std::string> warnings;

    [[nodiscard]] const UiResolvedNode* find(UiNodeId id) const;
};

// Runtime-only UI interaction state. Nothing here is serialized into .slui.
// A host may keep one state per visible UI document and expose it to C#/Lua.
struct UiRuntimeState {
    std::optional<UiNodeId> hovered;
    std::optional<UiNodeId> pressed;
    std::optional<UiNodeId> focused;
    UiNodeId pointer_capture = kInvalidUiNodeId;
    std::vector<UiNodeId> pending_clicks;

    void clear_transient();
    [[nodiscard]] bool was_clicked(UiNodeId id) const;
    bool consume_click(UiNodeId id);
};

class UiDocument {
public:
    UiNode& create_node(UiNodeType type, std::string name = {});
    UiNode* create_node_with_id(UiNodeId id, UiNodeType type, std::string name = {}, std::string* error = nullptr);
    void clear();
    bool destroy_node(UiNodeId id);
    bool reparent(UiNodeId child_id, UiNodeId parent_id, std::string* error = nullptr);

    [[nodiscard]] UiNode* find(UiNodeId id);
    [[nodiscard]] const UiNode* find(UiNodeId id) const;
    [[nodiscard]] bool is_descendant(UiNodeId possible_descendant, UiNodeId ancestor) const;
    [[nodiscard]] const std::vector<UiNode>& nodes() const { return nodes_; }
    [[nodiscard]] std::vector<UiNode>& nodes() { return nodes_; }

private:
    UiNodeId next_id_ = 1;
    std::vector<UiNode> nodes_;
};

// Pure engine-side layout resolution. It has no dependency on ImGui, D3D12,
// SDL rendering or sector geometry, so UI-only games can use the same model.
[[nodiscard]] UiLayoutResult resolve_ui_layout(
    const UiDocument& document,
    float viewport_width,
    float viewport_height
);

// Returns the top-most enabled node containing the point. By default only
// interactable Buttons/Text Inputs are considered, which is the foundation for
// pointer routing without exposing renderer internals.
[[nodiscard]] std::optional<UiNodeId> ui_hit_test(
    const UiDocument& document,
    const UiLayoutResult& layout,
    UiVec2 point,
    bool interactables_only = true
);

} // namespace vespera
