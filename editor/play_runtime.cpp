#include "play_runtime.hpp"

#include <vespera/core/log.hpp>
#include <vespera/assets/material_asset.hpp>
#include <vespera/scene/scene_collision.hpp>
#include <vespera/scene/scene_io.hpp>
#include <vespera/world/sector_world.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>

namespace vespera::editor {
namespace {

constexpr std::string_view kMoveForward = "move_forward";
constexpr std::string_view kMoveRight = "move_right";
constexpr std::string_view kLookX = "look_x";
constexpr std::string_view kLookY = "look_y";
constexpr std::string_view kSprint = "sprint";
constexpr std::string_view kProbe = "probe";
constexpr std::string_view kSceneNext = "scene_next";
constexpr std::string_view kScenePrevious = "scene_previous";
constexpr std::string_view kAudioTest = "audio_test";
constexpr std::string_view kAudioSpatial = "audio_spatial";
constexpr std::string_view kLifecycleTest = "lifecycle_test";

} // namespace

PlayRuntime::~PlayRuntime() {
    stop();
}

void PlayRuntime::configure_input_map() {
    auto& map = input_.map();
    map.clear();
    if (project_ && !project_->input_bindings.empty()) {
        std::string error;
        if (vespera::configure_project_input_map(*project_, map, &error)) return;
        vespera::log::warn("Editor Play input map: " + error + "; using compatibility bindings.");
    }
    map.bind_key(std::string(kMoveForward), vespera::Key::W, 1.0f);
    map.bind_key(std::string(kMoveForward), vespera::Key::S, -1.0f);
    map.bind_key(std::string(kMoveRight), vespera::Key::D, 1.0f);
    map.bind_key(std::string(kMoveRight), vespera::Key::A, -1.0f);
    map.bind_key(std::string(kSprint), vespera::Key::LeftShift);
    map.bind_key(std::string(kProbe), vespera::Key::Enter);
    map.bind_key(std::string(kSceneNext), vespera::Key::Up);
    map.bind_key(std::string(kScenePrevious), vespera::Key::Down);
    map.bind_key(std::string(kAudioTest), vespera::Key::Left);
    map.bind_key(std::string(kAudioSpatial), vespera::Key::Right);
    map.bind_key(std::string(kLifecycleTest), vespera::Key::L);
    // Look actions remain available for future editor gamepad feeding. Mouse look
    // is delivered directly through InputSystem mouse deltas today.
    map.bind_gamepad_axis(std::string(kLookX), vespera::GamepadAxis::RightX, 1.0f, 0.16f);
    map.bind_gamepad_axis(std::string(kLookY), vespera::GamepadAxis::RightY, 1.0f, 0.16f);
}

bool PlayRuntime::initialize_managed(vespera::Scene& scene) {
    if (!project_ || !assets_) return false;
    if (project_->managed_assembly.empty()) {
        status_.managed_ready = false;
        status_.managed_script_count = 0;
        return false;
    }

    const std::filesystem::path managed_root = managed_directory_.empty()
        ? std::filesystem::path("managed")
        : managed_directory_;
    managed_config_.runtime_config = managed_root / "Vespera.Managed.runtimeconfig.json";
    managed_config_.bridge_assembly = managed_root / "Vespera.NET.dll";
    managed_config_.game_assembly = managed_root / (project_->managed_assembly + ".dll");
    managed_config_.auto_reload = true;

    if (!managed_.initialize(scene, input_, audio_, *assets_, managed_config_, ui_surface_, performance_)) {
        status_.managed_ready = false;
        status_.managed_script_count = 0;
        status_.message = "embedded play is running without C#: " + managed_.status().message;
        return false;
    }

    managed_.set_current_scene_path(current_scene_path_);
    managed_.start();
    status_.managed_ready = true;
    status_.managed_script_count = managed_.status().script_count;
    return true;
}

#ifdef VESPERA_HAS_LUA
bool PlayRuntime::initialize_lua(vespera::Scene& scene) {
    status_.lua_ready = false;
    if (!project_ || !assets_ || (project_->lua_entry.empty() && project_->lua_entry_asset_id.empty())) return false;
    const auto resolved = assets_->resolve_reference(project_->lua_entry_reference());
    if (!resolved || !resolved.record || resolved.record->kind != vespera::AssetKind::LuaScript) {
        status_.message = "embedded play Lua entry could not be resolved";
        return false;
    }
    vespera::LuaScriptHostConfig config;
    config.entry_script = resolved.record->absolute_path;
    if (!lua_.initialize(scene, input_, audio_, *assets_, config, ui_surface_)) {
        status_.message = "embedded play is running without Lua: " + lua_.status().message;
        return false;
    }
    lua_.set_current_scene_path(current_scene_path_);
    status_.lua_ready = lua_.start();
    return status_.lua_ready;
}
#endif

void PlayRuntime::initialize_scripts(vespera::Scene& scene) {
    initialize_managed(scene);
#ifdef VESPERA_HAS_LUA
    initialize_lua(scene);
#endif
}

void PlayRuntime::shutdown_scripts() {
    managed_.shutdown();
#ifdef VESPERA_HAS_LUA
    lua_.shutdown();
#endif
}

void PlayRuntime::create_player_proxy(vespera::Scene& scene) {
    vespera::Entity& proxy = scene.create_entity("Editor Play Player Proxy");
    proxy.tag = "player";
    proxy.layer = "Gameplay";
    proxy.transform.position = scene.camera.position;
    player_proxy_id_ = proxy.id;
    current_sector_ = scene.world.find_sector_index({scene.camera.position.x, scene.camera.position.z}).value_or(0);
}

bool PlayRuntime::start(
    vespera::Scene& scene,
    const vespera::VesperaProject& project,
    vespera::AssetCatalog& assets,
    const std::filesystem::path& scene_path,
    const std::filesystem::path& managed_directory,
    vespera::UiDocument* ui_document,
    vespera::RmlUiSurface* rml_ui_surface,
    vespera::RuntimePerformanceCounters* performance) {
    stop();
    project_ = &project;
    assets_ = &assets;
    ui_document_ = ui_document;
    rml_ui_surface_ = rml_ui_surface;
    performance_ = performance;
    ui_runtime_state_ = {};
    legacy_ui_surface_.reset();
    ui_surface_ = nullptr;
    if (rml_ui_surface_) {
        ui_surface_ = rml_ui_surface_;
    } else if (ui_document_) {
        legacy_ui_surface_.emplace(*ui_document_, &ui_runtime_state_);
        ui_surface_ = &*legacy_ui_surface_;
    }
    current_scene_path_ = scene_path;
    managed_directory_ = managed_directory.lexically_normal();
    configure_input_map();
    trigger_tracker_.reset();
    create_player_proxy(scene);

    status_ = {};
    status_.active = true;
    status_.audio_ready = audio_.initialize();
    initialize_scripts(scene);
    if (status_.message.empty()) {
        if (status_.managed_ready && status_.lua_ready) status_.message = std::format("Play Mode ready: {} C# component(s) + Lua", status_.managed_script_count);
        else if (status_.managed_ready) status_.message = std::format("Play Mode ready: {} C# component(s)", status_.managed_script_count);
        else if (status_.lua_ready) status_.message = "Play Mode ready: Lua";
        else status_.message = "Play Mode ready";
    }
    return true;
}

void PlayRuntime::stop() {
    shutdown_scripts();
    audio_.stop_all();
    audio_.shutdown();
    trigger_tracker_.reset();
    assets_ = nullptr;
    project_ = nullptr;
    ui_document_ = nullptr;
    rml_ui_surface_ = nullptr;
    performance_ = nullptr;
    ui_surface_ = nullptr;
    legacy_ui_surface_.reset();
    ui_runtime_state_ = {};
    current_scene_path_.clear();
    managed_directory_.clear();
    player_proxy_id_ = vespera::kInvalidSceneObjectId;
    current_sector_ = 0;
    status_ = {};
}

void PlayRuntime::begin_input_frame() {
    input_.host_begin_frame();
    for (const auto& [action, value] : pending_action_overrides_) {
        if (value) input_.host_set_action_override(action, *value);
        else input_.host_clear_action_override(action);
    }
    pending_action_overrides_.clear();
}

void PlayRuntime::set_key(vespera::Key key, bool down) {
    input_.host_set_key(key, down);
}

void PlayRuntime::add_mouse_delta(float x, float y) {
    input_.host_add_mouse_delta(x, y);
}

void PlayRuntime::set_mouse_position(float x, float y) {
    input_.host_set_mouse_position(x, y);
}

void PlayRuntime::set_mouse_button(vespera::MouseButton button, bool down) {
    input_.host_set_mouse_button(button, down);
}

void PlayRuntime::add_text_input(std::string_view text) {
    input_.host_add_text_input(text);
}

bool PlayRuntime::inject_rml_pointer(float x, float y, std::string_view phase) {
    if (!rml_ui_surface_ || !rml_ui_surface_->initialized()) return false;
    const auto apply = [&](bool down) {
        input_.host_begin_frame();
        input_.host_set_mouse_position(x, y);
        input_.host_set_mouse_button(vespera::MouseButton::Left, down);
        rml_ui_surface_->process_input(input_);
        (void)rml_ui_surface_->build_packet();
    };
    if (phase == "move") {
        input_.host_begin_frame();
        input_.host_set_mouse_position(x, y);
        rml_ui_surface_->process_input(input_);
        (void)rml_ui_surface_->build_packet();
        return true;
    }
    if (phase == "down") { apply(true); return true; }
    if (phase == "up") { apply(false); return true; }
    if (phase == "click") { apply(true); apply(false); return true; }
    return false;
}

bool PlayRuntime::inject_rml_navigation(std::string_view action) {
    if (!rml_ui_surface_ || !rml_ui_surface_->initialized()) return false;
    vespera::Key key = vespera::Key::Tab;
    bool shift = false;
    if (action == "next") key = vespera::Key::Tab;
    else if (action == "previous") { key = vespera::Key::Tab; shift = true; }
    else if (action == "activate") key = vespera::Key::Enter;
    else return false;

    input_.host_begin_frame();
    if (shift) input_.host_set_key(vespera::Key::LeftShift, true);
    input_.host_set_key(key, true);
    rml_ui_surface_->process_input(input_);
    (void)rml_ui_surface_->build_packet();

    input_.host_begin_frame();
    input_.host_set_key(key, false);
    if (shift) input_.host_set_key(vespera::Key::LeftShift, false);
    rml_ui_surface_->process_input(input_);
    (void)rml_ui_surface_->build_packet();
    return true;
}

bool PlayRuntime::inject_rml_text(std::string_view text, bool backspace, bool clear) {
    if (!rml_ui_surface_ || !rml_ui_surface_->initialized()) return false;
    const std::string focused = rml_ui_surface_->focused_id();
    if (focused.empty()) return false;
    if (clear && !rml_ui_surface_->set_value(focused, "")) return false;

    if (backspace) {
        input_.host_begin_frame();
        input_.host_set_key(vespera::Key::Backspace, true);
        rml_ui_surface_->process_input(input_);
        input_.host_begin_frame();
        input_.host_set_key(vespera::Key::Backspace, false);
        rml_ui_surface_->process_input(input_);
    }
    if (!text.empty()) {
        input_.host_begin_frame();
        input_.host_add_text_input(text);
        rml_ui_surface_->process_input(input_);
    }
    (void)rml_ui_surface_->build_packet();
    return true;
}

void PlayRuntime::set_paused(bool paused) {
    audio_.set_all_paused(paused);
}

void PlayRuntime::queue_action_override(std::string action, float value) {
    if (action.empty()) return;
    pending_action_overrides_[std::move(action)] = std::clamp(value, -1.0f, 1.0f);
}

void PlayRuntime::queue_clear_action_override(std::string action) {
    if (action.empty()) return;
    pending_action_overrides_[std::move(action)] = std::nullopt;
}

std::filesystem::path PlayRuntime::resolve_scene_request(const std::filesystem::path& requested) const {
    if (requested.empty()) return {};
    if (requested.is_absolute() && std::filesystem::exists(requested)) return requested;
    if (std::filesystem::exists(requested)) return requested;
    if (!project_) return requested;

    const auto in_project = project_->root_directory / requested;
    if (std::filesystem::exists(in_project)) return in_project;
    const auto in_assets = project_->assets_root() / requested;
    if (std::filesystem::exists(in_assets)) return in_assets;

    auto relative = requested.generic_string();
    if (relative.starts_with("assets/")) {
        const auto stripped = project_->assets_root() / std::filesystem::path(relative.substr(7));
        if (std::filesystem::exists(stripped)) return stripped;
    }
    return requested;
}

bool PlayRuntime::load_requested_scene(vespera::Scene& scene, const std::filesystem::path& requested, bool force_reload) {
    const auto resolved = resolve_scene_request(requested);
    bool same = false;
    if (!current_scene_path_.empty()) {
        std::error_code ec;
        same = (std::filesystem::exists(resolved, ec) && !ec
                && std::filesystem::exists(current_scene_path_, ec) && !ec
                && std::filesystem::equivalent(resolved, current_scene_path_, ec) && !ec)
            || std::filesystem::absolute(resolved).lexically_normal()
                == std::filesystem::absolute(current_scene_path_).lexically_normal();
    }
    if (same && !force_reload) {
        status_.message = "Play Mode coalesced a same-scene Scene.Load request; use Scene.Reload() for an intentional reload.";
        return false;
    }
    if (same && force_reload) {
        const auto now = std::chrono::steady_clock::now();
        if (same_scene_reload_window_.time_since_epoch().count() == 0
            || now - same_scene_reload_window_ > std::chrono::seconds(2)) {
            same_scene_reload_window_ = now;
            same_scene_reload_burst_ = 1;
        } else {
            ++same_scene_reload_burst_;
        }
        if (same_scene_reload_burst_ > 4) {
            status_.message = "Play Mode blocked a rapid same-scene reload loop (>4 reloads in 2s).";
            return false;
        }
    } else if (!same) {
        same_scene_reload_burst_ = 0;
        same_scene_reload_window_ = {};
    }
    shutdown_scripts();
    trigger_tracker_.reset();

    const auto result = vespera::load_scene_text(scene, resolved);
    if (!result) {
        status_.message = "Play Mode scene switch failed: " + result.message;
        initialize_scripts(scene);
        return false;
    }

    current_scene_path_ = resolved;
    if (assets_) (void)hydrate_scene_materials(scene, *assets_);
    create_player_proxy(scene);
    initialize_scripts(scene);
    status_.message = "Play Mode scene switched: " + resolved.filename().string();
    return true;
}

void PlayRuntime::update_reference_camera_controller(vespera::Scene& scene, float dt) {
    if (scene.world.sectors().empty()) return;

    vespera::Camera& camera = scene.camera;
    constexpr float mouse_sensitivity = 0.0022f;
    constexpr float gamepad_look_speed = 2.45f;
    camera.yaw += input_.mouse_delta_x() * mouse_sensitivity;
    camera.yaw += input_.action_value(kLookX) * gamepad_look_speed * dt;
    camera.pitch -= input_.mouse_delta_y() * mouse_sensitivity;
    camera.pitch -= input_.action_value(kLookY) * gamepad_look_speed * dt;
    camera.pitch = std::clamp(camera.pitch, -1.45f, 1.45f);

    float forward = input_.action_value(kMoveForward);
    float right = input_.action_value(kMoveRight);
    const float length = std::sqrt(forward * forward + right * right);
    if (length > 1.0f) {
        forward /= length;
        right /= length;
    }

    const float forward_x = std::sin(camera.yaw);
    const float forward_z = std::cos(camera.yaw);
    const float right_x = std::cos(camera.yaw);
    const float right_z = -std::sin(camera.yaw);
    const float speed = input_.action_down(kSprint) ? 6.5f : 4.0f;
    const vespera::Vec3 delta{
        (forward_x * forward + right_x * right) * speed * dt,
        0.0f,
        (forward_z * forward + right_z * right) * speed * dt,
    };

    const auto movement = vespera::move_circle_through_world(
        scene.world,
        current_sector_,
        camera.position,
        delta,
        player_radius_,
        body_height_,
        max_step_height_);

    const vespera::Vec3 sector_position = movement.position;
    camera.position = vespera::resolve_circle_motion_against_scene_colliders(
        scene, camera.position, sector_position, player_radius_, player_proxy_id_);

    std::size_t resolved_sector = movement.sector_index;
    const bool collider_changed =
        std::abs(camera.position.x - sector_position.x) > 0.0001f
        || std::abs(camera.position.z - sector_position.z) > 0.0001f;
    if (collider_changed) {
        resolved_sector = scene.world.find_sector_index({camera.position.x, camera.position.z}).value_or(0);
    }
    if (resolved_sector < scene.world.sectors().size()) {
        current_sector_ = resolved_sector;
        camera.position.y = scene.world.sectors()[current_sector_].floor_height + eye_height_;
    }

    if (auto* proxy = scene.find_entity(player_proxy_id_)) {
        proxy->transform.position = camera.position;
    }
}

void PlayRuntime::update_triggers(vespera::Scene& scene) {
    for (const auto& event : trigger_tracker_.update_circle(
             scene, scene.camera.position, player_radius_, player_proxy_id_)) {
        if (event.type == vespera::TriggerEventType::Enter) {
            managed_.trigger_enter(event.trigger_entity, player_proxy_id_);
        } else {
            managed_.trigger_exit(event.trigger_entity, player_proxy_id_);
        }
    }
    for (const auto trigger_id : trigger_tracker_.active_trigger_ids()) {
        managed_.trigger_stay(trigger_id, player_proxy_id_);
    }
}

void PlayRuntime::update(vespera::Scene& scene, double delta_seconds) {
    if (!status_.active) return;
    const float dt = static_cast<float>(std::clamp(delta_seconds, 0.0, 0.05));

    if (rml_ui_surface_ && rml_ui_surface_->initialized()) {
        rml_ui_surface_->process_input(input_);
    }

    if (status_.managed_ready) {
        if (input_.pressed(vespera::Key::Space)) {
            managed_.reload();
#ifdef VESPERA_HAS_LUA
            if (status_.lua_ready) lua_.reload();
#endif
            status_.managed_script_count = managed_.status().script_count;
        }
        managed_.update(dt);
        status_.managed_script_count = managed_.status().script_count;
        if (const auto requested = managed_.take_scene_load_request()) {
            if (load_requested_scene(scene, requested->path, requested->force_reload)) return;
        }
    }
#ifdef VESPERA_HAS_LUA
    if (status_.lua_ready) {
        lua_.update(dt);
        if (const auto requested = lua_.take_scene_load_request()) {
            if (load_requested_scene(scene, requested->path, requested->force_reload)) return;
        }
    }
#endif

    const bool managed_camera = status_.managed_ready && managed_.camera_was_written_last_update();
    if (!managed_camera) {
        update_reference_camera_controller(scene, dt);
    } else if (auto* proxy = scene.find_entity(player_proxy_id_)) {
        // Scripted cameras intentionally bypass the reference FPS controller, but
        // keep the hidden runtime proxy aligned for trigger/telemetry semantics.
        proxy->transform.position = scene.camera.position;
    }
    if (status_.managed_ready) update_triggers(scene);
    audio_.set_camera_listener_position(scene.camera.position);
    audio_.update();
}

} // namespace vespera::editor
