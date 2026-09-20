#pragma once

#include <vespera/assets/asset_catalog.hpp>
#include <vespera/audio/audio.hpp>
#include <vespera/input/input.hpp>
#include <vespera/project/project.hpp>
#include <vespera/scene/scene.hpp>
#include <vespera/scene/scene_triggers.hpp>
#include <vespera/scripting/managed_script_host.hpp>
#ifdef VESPERA_HAS_LUA
#include <vespera/scripting/lua_script_host.hpp>
#endif
#include <vespera/ui/ui.hpp>
#include <vespera/ui/ui_surface.hpp>
#include <vespera/ui/rmlui_surface.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <vector>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace vespera::editor {

struct PlayRuntimeStatus {
    bool active = false;
    bool audio_ready = false;
    bool managed_ready = false;
    std::size_t managed_script_count = 0;
    bool lua_ready = false;
    std::string message;
};

// Editor-owned runtime services for an isolated play-scene copy. 0.8.7 keeps
// these services outside authored Scene data so Stop can tear them down without
// touching the edit scene. This is also a reusable boundary for later full
// in-editor runtime/game-host work.
struct RuntimePerformanceCounters;

class PlayRuntime {
public:
    PlayRuntime() = default;
    ~PlayRuntime();

    PlayRuntime(const PlayRuntime&) = delete;
    PlayRuntime& operator=(const PlayRuntime&) = delete;

    bool start(
        vespera::Scene& scene,
        const vespera::VesperaProject& project,
        vespera::AssetCatalog& assets,
        const std::filesystem::path& scene_path,
        const std::filesystem::path& managed_directory,
        vespera::UiDocument* ui_document = nullptr,
        vespera::RmlUiSurface* rml_ui_surface = nullptr,
        vespera::RuntimePerformanceCounters* performance = nullptr);
    void stop();

    // Host input is sampled by the editor only while the Game view owns input.
    void begin_input_frame();
    void set_key(vespera::Key key, bool down);
    void add_mouse_delta(float x, float y);
    void set_mouse_position(float x, float y);
    void set_mouse_button(vespera::MouseButton button, bool down);
    void add_text_input(std::string_view text);
    // Deterministic runtime-UI injection used by VAP/MCP. These operations are
    // implemented through the same InputSystem -> RmlUi path as normal Game-view input.
    [[nodiscard]] bool inject_rml_pointer(float x, float y, std::string_view phase);
    [[nodiscard]] bool inject_rml_navigation(std::string_view action);
    [[nodiscard]] bool inject_rml_text(std::string_view text, bool backspace, bool clear);
    void set_paused(bool paused);
    // Automation input is queued so it is applied *after* the editor copies the
    // previous/current input snapshots for the next runtime frame. This preserves
    // ActionPressed/ActionReleased semantics even when the MCP request arrives
    // between editor frames.
    void queue_action_override(std::string action, float value);
    void queue_clear_action_override(std::string action);

    // Advances native play services, managed scripts, the built-in reference
    // camera/player controller, collision, triggers and audio listener state.
    void update(vespera::Scene& scene, double delta_seconds);

    [[nodiscard]] const PlayRuntimeStatus& status() const { return status_; }
    [[nodiscard]] std::size_t current_sector() const { return current_sector_; }
    [[nodiscard]] vespera::SceneObjectId player_proxy_id() const { return player_proxy_id_; }
    [[nodiscard]] vespera::UiRuntimeState& ui_runtime_state() { return ui_runtime_state_; }
    [[nodiscard]] const vespera::UiRuntimeState& ui_runtime_state() const { return ui_runtime_state_; }
    [[nodiscard]] vespera::UiSurface* ui_surface() const { return ui_surface_; }
    [[nodiscard]] std::string_view ui_backend_name() const { return ui_surface_ ? ui_surface_->backend_name() : std::string_view{"none"}; }
    [[nodiscard]] std::vector<vespera::ManagedRuntimeEvent> runtime_events_since(std::uint64_t after_sequence = 0) const {
        return managed_.runtime_events_since(after_sequence);
    }
    [[nodiscard]] std::uint64_t latest_runtime_event_sequence() const { return managed_.latest_runtime_event_sequence(); }
    [[nodiscard]] std::uint32_t assembly_generation() const { return managed_.assembly_generation(); }
    void clear_runtime_events() { managed_.clear_runtime_events(); }

private:
    void configure_input_map();
    bool initialize_managed(vespera::Scene& scene);
#ifdef VESPERA_HAS_LUA
    bool initialize_lua(vespera::Scene& scene);
#endif
    void initialize_scripts(vespera::Scene& scene);
    void shutdown_scripts();
    bool load_requested_scene(vespera::Scene& scene, const std::filesystem::path& requested, bool force_reload = false);
    void create_player_proxy(vespera::Scene& scene);
    void update_reference_camera_controller(vespera::Scene& scene, float dt);
    void update_triggers(vespera::Scene& scene);
    std::filesystem::path resolve_scene_request(const std::filesystem::path& requested) const;

    vespera::InputSystem input_;
    vespera::AudioSystem audio_;
    vespera::ManagedScriptHost managed_;
#ifdef VESPERA_HAS_LUA
    vespera::LuaScriptHost lua_;
#endif
    vespera::TriggerTracker trigger_tracker_;
    std::unordered_map<std::string, std::optional<float>> pending_action_overrides_;
    vespera::AssetCatalog* assets_ = nullptr;
    const vespera::VesperaProject* project_ = nullptr;
    vespera::UiDocument* ui_document_ = nullptr;
    vespera::UiRuntimeState ui_runtime_state_{};
    std::optional<vespera::LegacyUiSurface> legacy_ui_surface_;
    vespera::UiSurface* ui_surface_ = nullptr;
    vespera::RmlUiSurface* rml_ui_surface_ = nullptr;
    vespera::RuntimePerformanceCounters* performance_ = nullptr;
    std::filesystem::path current_scene_path_;
    std::filesystem::path managed_directory_;
    vespera::ManagedScriptHostConfig managed_config_;
    vespera::SceneObjectId player_proxy_id_ = vespera::kInvalidSceneObjectId;
    std::size_t current_sector_ = 0;
    std::chrono::steady_clock::time_point same_scene_reload_window_{};
    int same_scene_reload_burst_ = 0;
    float eye_height_ = 1.65f;
    float body_height_ = 1.80f;
    float player_radius_ = 0.28f;
    float max_step_height_ = 0.35f;
    PlayRuntimeStatus status_;
};

} // namespace vespera::editor
