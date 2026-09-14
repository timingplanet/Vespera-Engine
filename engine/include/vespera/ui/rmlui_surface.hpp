#pragma once

#include <vespera/ui/ui_render.hpp>
#include <vespera/ui/ui_surface.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace vespera {

class InputSystem;

// Vespera-owned RmlUi surface used by the production UI foundation.
//
// The public type deliberately exposes no RmlUi headers. Vespera keeps ownership
// of input, renderer packets, game-facing element access, assets, automation and
// export semantics; RmlUi remains an implementation detail for DOM/layout/text/
// style/control behavior. Legacy .slui stays available for compatibility/QA.
class RmlUiSurface final : public UiSurface {
public:
    struct Result {
        bool ok = false;
        std::string message;
        explicit operator bool() const { return ok; }
    };

    RmlUiSurface();
    ~RmlUiSurface() override;

    RmlUiSurface(const RmlUiSurface&) = delete;
    RmlUiSurface& operator=(const RmlUiSurface&) = delete;
    RmlUiSurface(RmlUiSurface&&) noexcept;
    RmlUiSurface& operator=(RmlUiSurface&&) noexcept;

    [[nodiscard]] Result initialize(
        const std::filesystem::path& document_path,
        int viewport_width,
        int viewport_height,
        const std::vector<std::filesystem::path>& font_paths = {}
    );
    void shutdown();
    [[nodiscard]] bool initialized() const;
    [[nodiscard]] bool reload();

    void resize(int viewport_width, int viewport_height);
    void process_input(const InputSystem& input);

    // Calls RmlUi Update/Render and captures its renderer-independent geometry
    // into Vespera's existing UI packet. This lets the spike exercise the same
    // D3D12/Vulkan-facing RenderBackend::render_ui path as .slui.
    [[nodiscard]] UiRenderPacket build_packet();

    [[nodiscard]] std::string_view backend_name() const override { return "rmlui"; }
    [[nodiscard]] bool exists(std::string_view element_id) const override;
    [[nodiscard]] bool set_text(std::string_view element_id, std::string_view text) override;
    [[nodiscard]] std::string text(std::string_view element_id) const override;
    [[nodiscard]] bool set_value(std::string_view element_id, std::string_view value) override;
    [[nodiscard]] std::string value(std::string_view element_id) const override;
    [[nodiscard]] bool set_property(std::string_view element_id, std::string_view property, std::string_view value) override;
    [[nodiscard]] bool set_class(std::string_view element_id, std::string_view class_name, bool enabled) override;
    [[nodiscard]] bool set_disabled(std::string_view element_id, bool disabled) override;
    [[nodiscard]] bool disabled(std::string_view element_id, bool& disabled) const override;
    [[nodiscard]] bool set_visible(std::string_view element_id, bool visible) override;
    [[nodiscard]] bool visible(std::string_view element_id, bool& visible) const override;
    [[nodiscard]] bool set_interactable(std::string_view element_id, bool interactable) override;
    [[nodiscard]] bool interactable(std::string_view element_id, bool& interactable) const override;
    [[nodiscard]] bool set_read_only(std::string_view element_id, bool read_only) override;
    [[nodiscard]] bool read_only(std::string_view element_id, bool& read_only) const override;
    [[nodiscard]] bool set_numeric_value(std::string_view element_id, float value) override;
    [[nodiscard]] bool numeric_value(std::string_view element_id, float& value) const override;
    [[nodiscard]] bool set_color(std::string_view element_id, UiColorSlot slot, const std::array<float, 4>& color) override;
    [[nodiscard]] bool set_asset(std::string_view element_id, UiAssetSlot slot, const AssetReference& reference, std::string_view resolved_path) override;
    [[nodiscard]] bool focus(std::string_view element_id, bool focus_visible = true) override;
    [[nodiscard]] bool blur(std::string_view element_id) override;
    [[nodiscard]] bool click(std::string_view element_id) override;
    [[nodiscard]] int consume_clicks(std::string_view element_id) override;
    [[nodiscard]] std::string hovered_id() const override;
    [[nodiscard]] std::string focused_id() const override;

    [[nodiscard]] const std::vector<std::string>& warnings() const;
    [[nodiscard]] std::string renderer_mode() const;
    [[nodiscard]] std::filesystem::path document_path() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vespera
