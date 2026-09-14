#pragma once

#include <vespera/assets/asset_reference.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace vespera {

class UiDocument;
struct UiRuntimeState;

enum class UiColorSlot : std::uint8_t {
    Visual = 1,
    Text = 2,
    ProgressFill = 3,
    ProgressBackground = 4,
};

enum class UiAssetSlot : std::uint8_t {
    Image = 1,
    Font = 2,
};

// Engine-owned game-facing UI element surface.
//
// This deliberately does not abstract layout, serialization, renderer packets,
// or authoring models. Legacy .slui and RmlUi have fundamentally different
// layout systems; the shared contract is only the semantic operations gameplay,
// scripting and automation need to perform on named elements.
class UiSurface {
public:
    virtual ~UiSurface() = default;

    [[nodiscard]] virtual std::string_view backend_name() const = 0;
    [[nodiscard]] virtual bool exists(std::string_view element_key) const = 0;

    [[nodiscard]] virtual bool set_text(std::string_view element_key, std::string_view text) = 0;
    [[nodiscard]] virtual std::string text(std::string_view element_key) const = 0;
    [[nodiscard]] virtual bool set_value(std::string_view element_key, std::string_view value) = 0;
    [[nodiscard]] virtual std::string value(std::string_view element_key) const = 0;

    // Generic style hooks are intentionally optional. RmlUi supports both;
    // legacy .slui does not have a CSS class model and returns false instead of
    // pretending it does.
    [[nodiscard]] virtual bool set_property(std::string_view element_key, std::string_view property, std::string_view value) = 0;
    [[nodiscard]] virtual bool set_class(std::string_view element_key, std::string_view class_name, bool enabled) = 0;

    [[nodiscard]] virtual bool set_disabled(std::string_view element_key, bool disabled) = 0;
    [[nodiscard]] virtual bool disabled(std::string_view element_key, bool& disabled) const = 0;
    [[nodiscard]] virtual bool set_visible(std::string_view element_key, bool visible) = 0;
    [[nodiscard]] virtual bool visible(std::string_view element_key, bool& visible) const = 0;
    [[nodiscard]] virtual bool set_interactable(std::string_view element_key, bool interactable) = 0;
    [[nodiscard]] virtual bool interactable(std::string_view element_key, bool& interactable) const = 0;
    [[nodiscard]] virtual bool set_read_only(std::string_view element_key, bool read_only) = 0;
    [[nodiscard]] virtual bool read_only(std::string_view element_key, bool& read_only) const = 0;
    [[nodiscard]] virtual bool set_numeric_value(std::string_view element_key, float value) = 0;
    [[nodiscard]] virtual bool numeric_value(std::string_view element_key, float& value) const = 0;
    [[nodiscard]] virtual bool set_color(std::string_view element_key, UiColorSlot slot, const std::array<float, 4>& color) = 0;
    [[nodiscard]] virtual bool set_asset(std::string_view element_key, UiAssetSlot slot, const AssetReference& reference, std::string_view resolved_path) = 0;
    [[nodiscard]] virtual bool focus(std::string_view element_key, bool focus_visible = true) = 0;
    [[nodiscard]] virtual bool blur(std::string_view element_key) = 0;
    [[nodiscard]] virtual bool click(std::string_view element_key) = 0;
    [[nodiscard]] virtual int consume_clicks(std::string_view element_key) = 0;

    [[nodiscard]] virtual std::string hovered_id() const = 0;
    [[nodiscard]] virtual std::string focused_id() const = 0;
};

// Managed/script-facing UI elements keep an opaque numeric handle so public APIs
// do not leak either legacy .slui node ids or RmlUi string ids. Handles are
// stable for the lifetime of this table; existence is always revalidated against
// the currently bound UiSurface so a reloaded/changed document cannot resurrect
// a missing element accidentally.
class UiHandleTable {
public:
    void bind(UiSurface* surface);
    void clear();

    [[nodiscard]] std::uint64_t find(std::string_view element_key);
    [[nodiscard]] bool exists(std::uint64_t handle) const;
    [[nodiscard]] std::string_view key(std::uint64_t handle) const;
    [[nodiscard]] UiSurface* surface() const { return surface_; }

private:
    UiSurface* surface_ = nullptr;
    std::uint64_t next_handle_ = 1;
    std::unordered_map<std::string, std::uint64_t> handle_by_key_;
    std::unordered_map<std::uint64_t, std::string> key_by_handle_;
};

// Compatibility adapter for the serialized .slui UiDocument model. Elements
// are addressed by their existing authored `name`, matching the current C#
// UI.Find(name) behavior. The adapter borrows both objects and never owns them.
class LegacyUiSurface final : public UiSurface {
public:
    LegacyUiSurface(UiDocument& document, UiRuntimeState* runtime_state = nullptr);

    void bind_runtime_state(UiRuntimeState* runtime_state);

    [[nodiscard]] std::string_view backend_name() const override;
    [[nodiscard]] bool exists(std::string_view element_key) const override;
    [[nodiscard]] bool set_text(std::string_view element_key, std::string_view text) override;
    [[nodiscard]] std::string text(std::string_view element_key) const override;
    [[nodiscard]] bool set_value(std::string_view element_key, std::string_view value) override;
    [[nodiscard]] std::string value(std::string_view element_key) const override;
    [[nodiscard]] bool set_property(std::string_view element_key, std::string_view property, std::string_view value) override;
    [[nodiscard]] bool set_class(std::string_view element_key, std::string_view class_name, bool enabled) override;
    [[nodiscard]] bool set_disabled(std::string_view element_key, bool disabled) override;
    [[nodiscard]] bool disabled(std::string_view element_key, bool& disabled) const override;
    [[nodiscard]] bool set_visible(std::string_view element_key, bool visible) override;
    [[nodiscard]] bool visible(std::string_view element_key, bool& visible) const override;
    [[nodiscard]] bool set_interactable(std::string_view element_key, bool interactable) override;
    [[nodiscard]] bool interactable(std::string_view element_key, bool& interactable) const override;
    [[nodiscard]] bool set_read_only(std::string_view element_key, bool read_only) override;
    [[nodiscard]] bool read_only(std::string_view element_key, bool& read_only) const override;
    [[nodiscard]] bool set_numeric_value(std::string_view element_key, float value) override;
    [[nodiscard]] bool numeric_value(std::string_view element_key, float& value) const override;
    [[nodiscard]] bool set_color(std::string_view element_key, UiColorSlot slot, const std::array<float, 4>& color) override;
    [[nodiscard]] bool set_asset(std::string_view element_key, UiAssetSlot slot, const AssetReference& reference, std::string_view resolved_path) override;
    [[nodiscard]] bool focus(std::string_view element_key, bool focus_visible = true) override;
    [[nodiscard]] bool blur(std::string_view element_key) override;
    [[nodiscard]] bool click(std::string_view element_key) override;
    [[nodiscard]] int consume_clicks(std::string_view element_key) override;
    [[nodiscard]] std::string hovered_id() const override;
    [[nodiscard]] std::string focused_id() const override;

private:
    UiDocument* document_ = nullptr;
    UiRuntimeState* runtime_state_ = nullptr;
};

} // namespace vespera
