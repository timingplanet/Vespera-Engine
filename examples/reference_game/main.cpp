#include "reference_textures.hpp"
#include "automation_server.hpp"
#include <vespera/core/application.hpp>
#include <vespera/core/game.hpp>
#include <vespera/core/log.hpp>
#include <vespera/core/version.hpp>
#include <vespera/assets/asset_catalog.hpp>
#include <vespera/assets/audio_clip_asset.hpp>
#include <vespera/assets/build_manifest.hpp>
#include <vespera/assets/material_asset.hpp>
#include <vespera/assets/runtime_asset_monitor.hpp>
#include <vespera/assets/sprite_clip_asset.hpp>
#include <vespera/assets/sprite_sheet_asset.hpp>
#include <vespera/input/input.hpp>
#include <vespera/project/project.hpp>
#include <vespera/scene/scene.hpp>
#include <vespera/scene/scene_io.hpp>
#include <vespera/scene/scene_stats.hpp>
#include <vespera/scene/prefab.hpp>
#include <vespera/scene/scene_collision.hpp>
#include <vespera/scene/scene_triggers.hpp>
#include <vespera/scene/scene_raycast.hpp>
#include <vespera/scripting/managed_script_host.hpp>
#ifdef VESPERA_HAS_LUA
#include <vespera/scripting/lua_script_host.hpp>
#endif
#include <vespera/world/sector_world.hpp>
#include <vespera/ui/ui_io.hpp>
#include <vespera/ui/ui_render.hpp>
#include <vespera/ui/ui_surface.hpp>
#include <vespera/render/render_backend.hpp>
#include <vespera/assets/texture_importer.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <memory>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace {

constexpr std::string_view kMoveForward = "move_forward";
constexpr std::string_view kMoveRight = "move_right";
constexpr std::string_view kLookX = "look_x";
constexpr std::string_view kLookY = "look_y";
constexpr std::string_view kSprint = "sprint";
constexpr std::string_view kProbe = "probe";

constexpr std::uint16_t kRuntimeAutomationPort = 46788;

const std::string* runtime_automation_arg(const vespera::editor::AutomationRequest& request, std::string_view name) {
    const auto it = request.args.find(std::string(name));
    return it == request.args.end() ? nullptr : &it->second;
}

std::optional<bool> runtime_automation_bool(std::string_view value) {
    if (value == "true" || value == "1") return true;
    if (value == "false" || value == "0") return false;
    return std::nullopt;
}

std::optional<float> runtime_automation_float(std::string_view value) {
    float parsed = 0.0f;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) return std::nullopt;
    return parsed;
}

std::optional<std::uint64_t> runtime_automation_u64(std::string_view value) {
    std::uint64_t parsed = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) return std::nullopt;
    return parsed;
}

std::string runtime_json_escape(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (const unsigned char c : value) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) out += std::format("\\u{:04x}", static_cast<unsigned int>(c));
                else out.push_back(static_cast<char>(c));
                break;
        }
    }
    return out;
}

vespera::editor::AutomationResponse runtime_automation_response(
    std::uint64_t id, bool ok, std::string text) {
    vespera::editor::AutomationResponse response;
    response.id = id;
    response.ok = ok;
    response.fields["text"] = std::move(text);
    return response;
}

class ReferenceGame final : public vespera::Game {
public:
    explicit ReferenceGame(bool automation_enabled = false) : runtime_automation_enabled_(automation_enabled) {}

    void on_start(vespera::GameContext& context) override {
        using namespace vespera;

        if (runtime_automation_enabled_) {
            runtime_automation_server_ = std::make_unique<vespera::editor::AutomationServer>();
            std::string automation_error;
            if (!runtime_automation_server_->start(kRuntimeAutomationPort, &automation_error)) {
                vespera::log::error("Runtime automation server failed: " + automation_error);
                runtime_automation_server_.reset();
            } else {
                vespera::log::info(std::format(
                    "Runtime automation server listening on 127.0.0.1:{} (test builds only).",
                    kRuntimeAutomationPort));
            }
        }

        auto& input_map = context.input.map();
        input_map.clear();

        const auto project_result = load_vespera_project(project_, "VesperaReference.vesperaproject");
        if (!project_result) {
            vespera::log::error("Project load failed: " + project_result.message);
            load_failed_ = true;
            return;
        }
        vespera::log::info("Project: " + project_result.message);
        bool project_has_errors = false;
        ProjectValidationOptions runtime_validation;
        runtime_validation.require_managed_source = false;
        for (const auto& issue : validate_vespera_project(project_, runtime_validation)) {
            if (issue.severity == ProjectValidationSeverity::Error) {
                vespera::log::error("Project validation: " + issue.message);
                project_has_errors = true;
            } else {
                vespera::log::warn("Project validation: " + issue.message);
            }
        }
        if (project_has_errors) {
            load_failed_ = true;
            return;
        }
        vespera::log::info("Project validation: passed");

        std::string input_error;
        if (!configure_project_input_map(project_, input_map, &input_error)) {
            vespera::log::error("Project input map failed: " + input_error);
            load_failed_ = true;
            return;
        }
        if (project_.input_bindings.empty()) {
            vespera::log::warn("Project has no authored input bindings; gameplay actions will remain unbound.");
        }

        AssetCatalogRefreshReport catalog_report;
        AssetCatalogRefreshOptions runtime_catalog_options;
        runtime_catalog_options.write_metadata = false;
        std::string catalog_error;
        if (!asset_catalog_.refresh(project_.assets_root(), runtime_catalog_options, &catalog_report, &catalog_error)) {
            vespera::log::error("Asset catalog refresh failed: " + catalog_error);
            load_failed_ = true;
            return;
        }
        vespera::log::info(std::format(
            "Asset catalog: {} scenes | {} prefabs | {} clips | {} sheets | {} textures | {} audio | {} audio clips | {} materials | {} ui | {} stable IDs | {} dependency edges | {} stable refs | {} broken | {} stale paths",
            asset_catalog_.records_of_kind(AssetKind::Scene).size(),
            asset_catalog_.records_of_kind(AssetKind::EntityPrefab).size(),
            asset_catalog_.records_of_kind(AssetKind::SpriteClip).size(),
            asset_catalog_.records_of_kind(AssetKind::SpriteSheet).size(),
            asset_catalog_.records_of_kind(AssetKind::Texture).size(),
            asset_catalog_.records_of_kind(AssetKind::Audio).size(),
            asset_catalog_.records_of_kind(AssetKind::AudioClip).size(),
            asset_catalog_.records_of_kind(AssetKind::Material).size(),
            asset_catalog_.records_of_kind(AssetKind::UiDocument).size(),
            asset_catalog_.records().size() - catalog_report.metadata_missing,
            catalog_report.dependency_edges, catalog_report.stable_reference_edges,
            catalog_report.broken_dependencies, catalog_report.stale_fallback_paths
        ));

        // Runtime UI reference path. The .slui document remains
        // renderer-independent; a CPU draw packet is built from layout + source
        // images and the active renderer consumes only vertices/atlas pixels.
        if (const auto* ui_asset = asset_catalog_.find("ui/reference_hud.slui")) {
            const auto ui_loaded = load_ui_document(runtime_ui_document_, ui_asset->absolute_path);
            if (!ui_loaded) {
                vespera::log::warn("Runtime UI document failed to load: " + ui_loaded.message);
            } else {
                runtime_ui_cache_.prepare_images(runtime_ui_document_, [&](const AssetReference& reference) -> std::optional<TextureData> {
                    const auto resolved = asset_catalog_.resolve_reference(reference);
                    if (!resolved || resolved.record->kind != AssetKind::Texture) return std::nullopt;
                    const auto imported = import_texture(resolved.record->absolute_path, resolved.record->display_name);
                    if (!imported) return std::nullopt;
                    return imported.texture;
                }, [&](const AssetReference& reference) -> std::optional<std::filesystem::path> {
                    const auto resolved = asset_catalog_.resolve_reference(reference);
                    if (!resolved || resolved.record->kind != AssetKind::Font) return std::nullopt;
                    return resolved.record->absolute_path;
                });
                const auto ui_layout = resolve_ui_layout(runtime_ui_document_,
                    static_cast<float>(project_.window_width), static_cast<float>(project_.window_height));
                runtime_ui_loaded_ = true;
                vespera::log::info(std::format(
                    "Runtime UI document: {} nodes | {} resolved | {} layout warnings | {} image warnings | visual renderer ready",
                    runtime_ui_document_.nodes().size(), ui_layout.nodes.size(), ui_layout.warnings.size(),
                    runtime_ui_cache_.image_warnings().size()));
            }
        }

        auto& world = context.scene.world;
        std::string texture_error;
        if (!vespera::reference_content::register_textures(world, project_.assets_root(), &texture_error)) {
            vespera::log::error("Texture import failed: " + texture_error);
            load_failed_ = true;
            return;
        }
        vespera::log::info(std::format(
            "Texture import: {} authored BMP textures loaded from {}",
            world.textures().size(),
            project_.assets_root().generic_string()
        ));

        const auto startup_resolution = asset_catalog_.resolve_reference(project_.startup_scene_reference());
        if (!startup_resolution || startup_resolution.record->kind != AssetKind::Scene) {
            vespera::log::error("Startup scene stable reference could not be resolved.");
            load_failed_ = true;
            return;
        }
        current_scene_path_ = startup_resolution.record->absolute_path;
        vespera::log::info(std::format(
            "Startup scene reference: {} | {}{}",
            startup_resolution.resolved_by_id ? "stable ID" : "fallback path",
            startup_resolution.record->relative_path.generic_string(),
            startup_resolution.stale_fallback_path ? " | fallback path stale" : ""));
        const auto load_result = load_scene_text(context.scene, current_scene_path_);
        if (!load_result) {
            vespera::log::error("Scene load failed: " + load_result.message);
            load_failed_ = true;
            return;
        }

        const auto material_report = vespera::hydrate_scene_materials(context.scene, asset_catalog_);
        if (!material_report.message.empty()) vespera::log::warn("Material hydration: " + material_report.message);
        vespera::log::info("Scene: " + load_result.message);
        vespera::log::info(std::format(
            "Gameplay queries: {} enemies | {} triggers | {} colliders | {} point lights",
            context.scene.find_entities_with_tag("enemy").size(),
            context.scene.find_entities_on_layer("Triggers").size(),
            context.scene.find_entities_with_component(BuiltinComponentType::CylinderCollider).size(),
            context.scene.find_entities_with_component(BuiltinComponentType::PointLight).size()
        ));

        // Exercise the write path every launch. This is intentionally a runtime
        // copy, not the authored source asset, so the test never mutates source.
        const auto save_result = save_scene_text(
            context.scene,
            "runtime/connected_sectors.roundtrip.slscene"
        );
        if (save_result) {
            vespera::log::info("Scene round-trip save: " + save_result.message);
        } else {
            vespera::log::warn("Scene round-trip save failed: " + save_result.message);
        }

        const auto build_manifest = build_project_asset_manifest(project_, asset_catalog_);
        vespera::log::info(std::format(
            "Build asset manifest: {} source asset(s) | {} missing roots | {} broken references | {} stale root paths",
            build_manifest.assets.size(), build_manifest.missing_roots.size(),
            build_manifest.broken_dependencies.size(), build_manifest.stale_root_paths.size()));
        std::string index_error;
        if (write_build_asset_index(build_manifest, "runtime/Vespera.BuildAssetIndex.txt", &index_error)) {
            vespera::log::info(std::format("Build asset index: wrote {} stable source entries", build_manifest.assets.size()));
        } else {
            vespera::log::warn("Build asset index: " + index_error);
        }

        const auto authored_audio = load_audio_clip_asset(
            project_.assets_root() / "audio/test_chime.slaudio", asset_catalog_);
        if (authored_audio) {
            vespera::log::info(std::format(
                "Audio clip asset: {} | source={} | volume={:.2f} | {} | spatial={} | ref={}",
                authored_audio.clip.name, authored_audio.clip.source_path.generic_string(), authored_audio.clip.volume,
                authored_audio.clip.loop ? "loop" : "one-shot", authored_audio.clip.spatial ? "yes" : "no",
                authored_audio.clip.source_resolved_by_id ? "stable-id" : "path"));
        } else {
            vespera::log::warn("Audio clip asset load failed: " + authored_audio.message);
        }

        const auto sheet_clip = load_sprite_sheet_asset(
            project_.assets_root() / "animations/watcher_walk.slspritesheet", asset_catalog_, context.scene.world);
        if (sheet_clip) {
            bool replaced = false;
            for (auto& clip : context.scene.sprite_clips) {
                if (clip.name == sheet_clip.clip.name) {
                    clip = sheet_clip.clip;
                    replaced = true;
                    break;
                }
            }
            if (!replaced) context.scene.sprite_clips.push_back(sheet_clip.clip);
            vespera::log::info(std::format(
                "Sprite sheet asset: {} | sheet {}x{} | {} directions x {} frames | derived textures={} | ref={}",
                sheet_clip.clip.name, sheet_clip.sheet_width, sheet_clip.sheet_height,
                sheet_clip.clip.direction_count, sheet_clip.clip.frame_count, sheet_clip.clip.textures.size(),
                sheet_clip.source_resolved_by_id ? "stable-id" : "path"));
        } else {
            vespera::log::warn("Sprite sheet asset load failed: " + sheet_clip.message);
        }

        const auto external_clip = load_sprite_clip_asset(
            project_.assets_root() / "animations/watcher_walk.slspriteclip", asset_catalog_, context.scene.world);
        if (external_clip) {
            vespera::log::info(std::format(
                "Legacy frame-list clip validated: {} | {} directions | {} frames | {:.1f} fps",
                external_clip.clip.name, external_clip.clip.direction_count,
                external_clip.clip.frame_count, external_clip.clip.frames_per_second));
        } else {
            vespera::log::warn("Sprite clip asset load failed: " + external_clip.message);
        }

        EntityPrefab watcher_prefab;
        const auto prefab_result = load_entity_prefab(
            context.scene, watcher_prefab, project_.assets_directory / "prefabs/watcher.slprefab"
        );
        if (prefab_result) {
            AssetReference watcher_source;
            watcher_source.path = "prefabs/watcher.slprefab";
            if (const auto* watcher_record = asset_catalog_.find("prefabs/watcher.slprefab")) {
                watcher_source.asset_id = watcher_record->asset_id;
            }
            if (Entity* runtime_watcher = instantiate_entity_prefab(
                    context.scene, watcher_prefab, "Runtime Prefab Watcher", std::move(watcher_source))) {
                runtime_watcher->transform.position = {-1.65f, 0.60f, 8.30f};
                (void)vespera::hydrate_scene_materials(context.scene, asset_catalog_);
                vespera::log::info(std::format(
                    "Prefab spawn: {} (entity id {})", runtime_watcher->name, runtime_watcher->id
                ));
            }
        } else {
            vespera::log::warn("Prefab load failed: " + prefab_result.message);
        }

        Entity& player_proxy = context.scene.create_entity("Runtime Player Proxy");
        player_proxy.tag = "player";
        player_proxy.layer = "Gameplay";
        player_proxy.transform.position = context.scene.camera.position;
        player_proxy_id_ = player_proxy.id;

        managed_config_.runtime_config = "managed/Vespera.Managed.runtimeconfig.json";
        managed_config_.bridge_assembly = "managed/Vespera.NET.dll";
        managed_config_.game_assembly = std::filesystem::path("managed") / (project_.managed_assembly + ".dll");
        managed_config_.auto_reload = true;
        initialize_managed(context);
#ifdef VESPERA_HAS_LUA
        initialize_lua(context);
#endif

        current_sector_ = context.scene.world.find_sector_index({context.scene.camera.position.x, context.scene.camera.position.z}).value_or(0);

        vespera::log::info(std::format("Vespera Engine {} reference runtime started.", vespera::kEngineVersion));
        vespera::log::info("The connected world, entity identity/tags, components, sprite clips, colliders, triggers, and point lights load from assets/scenes/connected_sectors.slscene.");
        vespera::log::info("Controls: WASD move | Mouse/right stick look | Shift sprint | Enter probe | Left Arrow one-shot audio | Right Arrow spatial loop | L lifecycle toggle | Up/Down Arrow switch scenes | Space manual script reload | Escape quit");
        vespera::log::info(std::format(
            "Input map: {} actions | {} bindings",
            input_map.action_count(),
            input_map.binding_count()
        ));
        if (context.input.gamepad_connected()) {
            vespera::log::info(std::format("Active gamepad: {}", context.input.gamepad_name()));
        } else {
            vespera::log::info("Active gamepad: none (hot-plug supported)");
        }
        log_current_sector(context.scene.world);
    }

    void on_update(vespera::GameContext& context, double delta_seconds) override {
        using namespace vespera;

        poll_runtime_automation(context);
        if (load_failed_ || context.scene.world.sectors().empty()) {
            return;
        }

        const float dt = static_cast<float>(std::clamp(delta_seconds, 0.0, 0.05));
        const auto asset_poll = runtime_asset_monitor_.update(asset_catalog_, project_.assets_root(), delta_seconds);
        if (asset_poll.polled && !asset_poll.ok) {
            vespera::log::warn("Runtime asset catalog refresh failed: " + asset_poll.message);
        } else if (asset_poll.polled && !asset_poll.report.changes.empty()) {
            vespera::log::info(std::format(
                "Runtime asset invalidation: {} added | {} changed | {} moved | {} removed; resource replacement deferred to consumers",
                asset_poll.report.assets_added, asset_poll.report.assets_changed, asset_poll.report.assets_moved, asset_poll.report.assets_removed));
        }
        if (context.input.pressed(Key::Space)) {
            if (managed_host_.reload()) {
                vespera::log::info("Managed scripting: " + managed_host_.status().message);
            } else {
                vespera::log::warn("Managed scripting reload failed: " + managed_host_.status().message);
            }
#ifdef VESPERA_HAS_LUA
            if (lua_ready_) {
                if (lua_host_.reload()) vespera::log::info("Lua scripting: " + lua_host_.status().message);
                else vespera::log::warn("Lua scripting reload failed: " + lua_host_.status().message);
            }
#endif
        }
        managed_host_.update(dt);
        if (const auto requested_scene = managed_host_.take_scene_load_request()) {
            if (load_runtime_scene(context, requested_scene->path, requested_scene->force_reload)) return;
        }
#ifdef VESPERA_HAS_LUA
        if (lua_ready_) {
            lua_host_.update(dt);
            if (const auto requested_scene = lua_host_.take_scene_load_request()) {
                if (load_runtime_scene(context, requested_scene->path, requested_scene->force_reload)) return;
            }
        }
#endif
        Camera& camera = context.scene.camera;

        constexpr float mouse_sensitivity = 0.0022f;
        constexpr float gamepad_look_speed = 2.45f;
        camera.yaw += context.input.mouse_delta_x() * mouse_sensitivity;
        camera.yaw += context.input.action_value(kLookX) * gamepad_look_speed * dt;
        camera.pitch -= context.input.mouse_delta_y() * mouse_sensitivity;
        camera.pitch -= context.input.action_value(kLookY) * gamepad_look_speed * dt;
        camera.pitch = std::clamp(camera.pitch, -1.45f, 1.45f);

        float move_forward = context.input.action_value(kMoveForward);
        float move_right = context.input.action_value(kMoveRight);

        const float input_length = std::sqrt(move_forward * move_forward + move_right * move_right);
        if (input_length > 1.0f) {
            move_forward /= input_length;
            move_right /= input_length;
        }

        const float forward_x = std::sin(camera.yaw);
        const float forward_z = std::cos(camera.yaw);
        const float right_x = std::cos(camera.yaw);
        const float right_z = -std::sin(camera.yaw);
        const float speed = context.input.action_down(kSprint) ? 6.5f : 4.0f;

        Vec3 delta{
            (forward_x * move_forward + right_x * move_right) * speed * dt,
            0.0f,
            (forward_z * move_forward + right_z * move_right) * speed * dt,
        };

        const SectorMoveResult movement = move_circle_through_world(
            context.scene.world,
            current_sector_,
            camera.position,
            delta,
            player_radius_,
            body_height_,
            max_step_height_
        );

        const Vec3 sector_resolved_position = movement.position;
        camera.position = resolve_circle_motion_against_scene_colliders(
            context.scene,
            camera.position,
            sector_resolved_position,
            player_radius_
        );

        std::size_t resolved_sector = movement.sector_index;
        const bool collider_changed_position =
            std::abs(camera.position.x - sector_resolved_position.x) > 0.0001f
            || std::abs(camera.position.z - sector_resolved_position.z) > 0.0001f;
        if (collider_changed_position) {
            resolved_sector = context.scene.world.find_sector_index({camera.position.x, camera.position.z}).value_or(0);
        }

        if (resolved_sector < context.scene.world.sectors().size()) {
            const bool changed_sector = resolved_sector != current_sector_;
            current_sector_ = resolved_sector;
            camera.position.y = context.scene.world.sectors()[current_sector_].floor_height + eye_height_;
            if (changed_sector) {
                log_current_sector(context.scene.world);
            }
        }

        if (context.input.action_pressed(kProbe)) {
            const Vec3 probe_direction{
                std::sin(camera.yaw) * std::cos(camera.pitch),
                0.0f,
                std::cos(camera.yaw) * std::cos(camera.pitch),
            };
            SceneRaycastOptions options;
            options.max_distance = 12.0f;
            options.include_trigger_colliders = true;
            if (const auto hit = raycast_scene_2d(context.scene, camera.position, probe_direction, options)) {
                if (hit->type == SceneRaycastHitType::EntityCollider) {
                    const Entity* entity = context.scene.find_entity(hit->entity_id);
                    vespera::log::info(std::format(
                        "Probe hit entity '{}' at {:.2f}m{}",
                        entity ? entity->name : std::string("<missing>"),
                        hit->distance,
                        entity ? std::format(" [tag={}]", entity->tag) : std::string{}
                    ));
                } else {
                    const auto& sectors = context.scene.world.sectors();
                    const std::string sector_name = hit->sector_index < sectors.size()
                        ? sectors[hit->sector_index].name
                        : std::string("<unknown sector>");
                    vespera::log::info(std::format(
                        "Probe hit wall in '{}' side {} at {:.2f}m",
                        sector_name,
                        hit->side_index,
                        hit->distance
                    ));
                }
            } else {
                vespera::log::info("Probe found no hit within 12m.");
            }
        }

        if (Entity* player_proxy = context.scene.find_entity(player_proxy_id_)) {
            player_proxy->transform.position = camera.position;
        }

        for (const TriggerEvent& event : trigger_tracker_.update_circle(
                 context.scene,
                 camera.position,
                 player_radius_,
                 player_proxy_id_)) {
            const Entity* trigger = context.scene.find_entity(event.trigger_entity);
            const std::string_view trigger_name = trigger ? std::string_view(trigger->name) : std::string_view("<missing trigger>");
            const std::string_view trigger_tag = trigger ? std::string_view(trigger->tag) : std::string_view("Untagged");
            vespera::log::info(std::format(
                "Trigger {}: {} [tag={}]",
                event.type == TriggerEventType::Enter ? "enter" : "exit",
                trigger_name,
                trigger_tag
            ));
            if (event.type == TriggerEventType::Enter) {
                managed_host_.trigger_enter(event.trigger_entity, player_proxy_id_);
            } else {
                managed_host_.trigger_exit(event.trigger_entity, player_proxy_id_);
            }
        }
        // Stay is dispatched from the same native TriggerTracker snapshot rather
        // than recomputing overlap state in managed code.
        for (const SceneObjectId trigger_id : trigger_tracker_.active_trigger_ids()) {
            managed_host_.trigger_stay(trigger_id, player_proxy_id_);
        }

        report_timer_ += delta_seconds;
        if (report_timer_ >= 10.0) {
            vespera::log::info(std::format(
                "Camera: ({:.2f}, {:.2f}, {:.2f}) | sector {} | yaw {:.1f} deg",
                camera.position.x,
                camera.position.y,
                camera.position.z,
                current_sector_,
                camera.yaw * 57.2957795f
            ));
            report_timer_ = 0.0;
        }
    }

    void on_render(vespera::GameContext& context, vespera::RenderBackend& renderer, double) override {
        if (!runtime_ui_loaded_) return;
        const int width = renderer.target_width();
        const int height = renderer.target_height();
        if (width <= 0 || height <= 0) return;
        last_render_width_ = width;
        last_render_height_ = height;

        vespera::UiPointerState pointer{};
        if (!project_.relative_mouse) {
            pointer.available = true;
            pointer.position = {context.input.mouse_x(), context.input.mouse_y()};
            pointer.primary_down = context.input.mouse_down(vespera::MouseButton::Left);
            pointer.primary_pressed = context.input.mouse_pressed(vespera::MouseButton::Left);
            pointer.primary_released = context.input.mouse_released(vespera::MouseButton::Left);
        }
        const bool reverse_focus = context.input.down(vespera::Key::LeftShift);
        const bool keyboard_focus = context.input.pressed(vespera::Key::Tab);
        vespera::UiNavigationState navigation{};
        navigation.focus_next = (keyboard_focus && !reverse_focus)
            || context.input.gamepad_pressed(vespera::GamepadButton::DpadDown);
        navigation.focus_previous = (keyboard_focus && reverse_focus)
            || context.input.gamepad_pressed(vespera::GamepadButton::DpadUp);
        navigation.activate_pressed = context.input.pressed(vespera::Key::Enter)
            || context.input.gamepad_pressed(vespera::GamepadButton::South);

        vespera::UiTextInputState text_input{};
        text_input.text = std::string(context.input.text_input());
        text_input.backspace = context.input.pressed(vespera::Key::Backspace);
        const auto packet = runtime_ui_cache_.build_packet(
            runtime_ui_document_, static_cast<float>(width), static_cast<float>(height), pointer,
            &runtime_ui_state_, navigation, text_input);
        renderer.render_ui(packet, nullptr);
    }

    void on_stop(vespera::GameContext&) override {
        managed_host_.shutdown();
#ifdef VESPERA_HAS_LUA
        lua_host_.shutdown();
        lua_ready_ = false;
#endif
        if (runtime_automation_server_) {
            runtime_automation_server_->stop();
            runtime_automation_server_.reset();
        }
        vespera::log::info("Reference game stopped.");
    }

private:
    vespera::editor::AutomationResponse dispatch_runtime_automation(
        vespera::GameContext& context,
        const vespera::editor::AutomationRequest& request) {
        const auto fail = [&](std::string message) {
            return runtime_automation_response(request.id, false, std::move(message));
        };
        const auto ok = [&](std::string message) {
            return runtime_automation_response(request.id, true, std::move(message));
        };

        if (request.command == "vespera_runtime_get_state") {
            const auto& camera = context.scene.camera;
            const auto scene_stats = vespera::collect_scene_stats(context.scene);
            const auto& perf = context.performance;
            const bool watcher_clip_available = asset_catalog_.find("animations/watcher_walk.slspriteclip") != nullptr;
            return ok(std::format(
                "{{\"engine_version\":\"{}\",\"automation_port\":{},\"scene\":\"{}\",\"entities\":{},"
                "\"load_failed\":{},\"ui_loaded\":{},\"watcher_clip_available\":{},"
                "\"lua_ready\":{},\"managed_assembly_generation\":{},\"runtime_event_sequence\":{},\"camera\":[{},{},{}],"
                "\"scene_stats\":{{\"sectors\":{},\"enabled_entities\":{},\"sprites\":{},\"meshes\":{},\"colliders\":{},\"triggers\":{},\"point_lights\":{},\"managed_scripts\":{}}},"
                "\"asset_stats\":{{\"records\":{},\"dependency_edges\":{},\"broken_dependencies\":{}}},"
                "\"performance\":{{\"frame_index\":{},\"last_frame_ms\":{:.3f},\"smoothed_frame_ms\":{:.3f},\"max_frame_ms\":{:.3f},\"update_ms\":{:.3f},\"render_ms\":{:.3f},\"live_resize_redraws\":{}}}}}",
                vespera::kEngineVersion, kRuntimeAutomationPort,
                runtime_json_escape(current_scene_path_.generic_string()), scene_stats.entities,
                load_failed_ ? "true" : "false", runtime_ui_loaded_ ? "true" : "false",
                watcher_clip_available ? "true" : "false",
#ifdef VESPERA_HAS_LUA
                lua_ready_ ? "true" : "false",
#else
                "false",
#endif
                managed_host_.assembly_generation(),
                managed_host_.latest_runtime_event_sequence(), camera.position.x, camera.position.y, camera.position.z,
                scene_stats.sectors, scene_stats.enabled_entities, scene_stats.sprite_renderers, scene_stats.mesh_renderers,
                scene_stats.colliders, scene_stats.triggers, scene_stats.point_lights, scene_stats.managed_scripts,
                asset_catalog_.records().size(), asset_catalog_.dependencies().size(), asset_catalog_.broken_dependencies().size(),
                perf.frame_index, perf.last_frame_ms, perf.smoothed_frame_ms, perf.max_frame_ms,
                perf.update_ms, perf.render_ms, perf.live_resize_redraws));
        }

        if (request.command == "vespera_runtime_inject_input_action") {
            const auto* action = runtime_automation_arg(request, "action");
            if (!action || action->empty()) return fail("action is required");
            const std::string phase = runtime_automation_arg(request, "phase")
                ? *runtime_automation_arg(request, "phase") : "set";
            float value = 1.0f;
            if (const auto* raw = runtime_automation_arg(request, "value")) {
                const auto parsed = runtime_automation_float(*raw);
                if (!parsed) return fail("value must be numeric");
                value = std::clamp(*parsed, -1.0f, 1.0f);
            }
            if (phase == "press") context.input.host_set_action_override(*action, std::max(0.5f, value));
            else if (phase == "set") context.input.host_set_action_override(*action, value);
            else if (phase == "release") context.input.host_set_action_override(*action, 0.0f);
            else if (phase == "clear") context.input.host_clear_action_override(*action);
            else return fail("phase must be set, press, release or clear");
            return ok(std::format(
                "{{\"action\":\"{}\",\"phase\":\"{}\",\"value\":{},\"effective_value\":{}}}",
                runtime_json_escape(*action), runtime_json_escape(phase), value,
                context.input.action_value(*action)));
        }

        if (request.command == "vespera_runtime_pointer_event") {
            if (!runtime_ui_loaded_) return fail("runtime UI document is not loaded");
            const auto* raw_x = runtime_automation_arg(request, "x");
            const auto* raw_y = runtime_automation_arg(request, "y");
            const auto x = raw_x ? runtime_automation_float(*raw_x) : std::nullopt;
            const auto y = raw_y ? runtime_automation_float(*raw_y) : std::nullopt;
            if (!x || !y) return fail("x and y are required numeric coordinates");
            int width = std::max(1, last_render_width_ > 0 ? last_render_width_ : project_.window_width);
            int height = std::max(1, last_render_height_ > 0 ? last_render_height_ : project_.window_height);
            if (const auto* raw = runtime_automation_arg(request, "width")) {
                const auto parsed = runtime_automation_u64(*raw);
                if (!parsed || *parsed == 0 || *parsed > 16384) return fail("width must be 1..16384");
                width = static_cast<int>(*parsed);
            }
            if (const auto* raw = runtime_automation_arg(request, "height")) {
                const auto parsed = runtime_automation_u64(*raw);
                if (!parsed || *parsed == 0 || *parsed > 16384) return fail("height must be 1..16384");
                height = static_cast<int>(*parsed);
            }
            const bool normalized = runtime_automation_arg(request, "normalized")
                ? runtime_automation_bool(*runtime_automation_arg(request, "normalized")).value_or(false) : false;
            const vespera::UiVec2 point{
                normalized ? *x * static_cast<float>(width) : *x,
                normalized ? *y * static_cast<float>(height) : *y};
            const std::string phase = runtime_automation_arg(request, "phase")
                ? *runtime_automation_arg(request, "phase") : "move";
            auto apply = [&](bool down, bool pressed, bool released) {
                vespera::UiPointerState pointer{};
                pointer.available = true;
                pointer.position = point;
                pointer.primary_down = down;
                pointer.primary_pressed = pressed;
                pointer.primary_released = released;
                (void)runtime_ui_cache_.build_packet(
                    runtime_ui_document_, static_cast<float>(width), static_cast<float>(height),
                    pointer, &runtime_ui_state_);
            };
            if (phase == "move") apply(false, false, false);
            else if (phase == "down") apply(true, true, false);
            else if (phase == "up") apply(false, false, true);
            else if (phase == "click") { apply(true, true, false); apply(false, false, true); }
            else return fail("phase must be move, down, up or click");
            return ok(std::format(
                "{{\"phase\":\"{}\",\"hovered\":{},\"pressed\":{},\"focused\":{},\"pending_clicks\":{}}}",
                runtime_json_escape(phase), runtime_ui_state_.hovered.value_or(0),
                runtime_ui_state_.pressed.value_or(0), runtime_ui_state_.focused.value_or(0),
                runtime_ui_state_.pending_clicks.size()));
        }

        if (request.command == "vespera_runtime_ui_navigation") {
            if (!runtime_ui_loaded_) return fail("runtime UI document is not loaded");
            const auto* action = runtime_automation_arg(request, "action");
            if (!action) return fail("action is required");
            vespera::UiNavigationState navigation{};
            if (*action == "next") navigation.focus_next = true;
            else if (*action == "previous") navigation.focus_previous = true;
            else if (*action == "activate") navigation.activate_pressed = true;
            else return fail("action must be next, previous or activate");
            const float width = static_cast<float>(std::max(1, last_render_width_ > 0 ? last_render_width_ : project_.window_width));
            const float height = static_cast<float>(std::max(1, last_render_height_ > 0 ? last_render_height_ : project_.window_height));
            (void)runtime_ui_cache_.build_packet(
                runtime_ui_document_, width, height, {}, &runtime_ui_state_, navigation);
            return ok(std::format(
                "{{\"action\":\"{}\",\"focused\":{},\"pending_clicks\":{}}}",
                runtime_json_escape(*action), runtime_ui_state_.focused.value_or(0),
                runtime_ui_state_.pending_clicks.size()));
        }

        if (request.command == "vespera_runtime_text_input") {
            if (!runtime_ui_loaded_) return fail("runtime UI document is not loaded");
            vespera::UiTextInputState input{};
            if (const auto* text = runtime_automation_arg(request, "text")) input.text = *text;
            input.backspace = runtime_automation_arg(request, "backspace")
                ? runtime_automation_bool(*runtime_automation_arg(request, "backspace")).value_or(false) : false;
            input.clear = runtime_automation_arg(request, "clear")
                ? runtime_automation_bool(*runtime_automation_arg(request, "clear")).value_or(false) : false;
            const float width = static_cast<float>(std::max(1, last_render_width_ > 0 ? last_render_width_ : project_.window_width));
            const float height = static_cast<float>(std::max(1, last_render_height_ > 0 ? last_render_height_ : project_.window_height));
            (void)runtime_ui_cache_.build_packet(
                runtime_ui_document_, width, height, {}, &runtime_ui_state_, {}, input);
            const auto focused = runtime_ui_state_.focused;
            const auto* node = focused ? runtime_ui_document_.find(*focused) : nullptr;
            if (!node || node->type != vespera::UiNodeType::TextInput) {
                return fail("no TextInput node is currently focused");
            }
            return ok(std::format(
                "{{\"focused\":{},\"text\":\"{}\",\"read_only\":{},\"max_length\":{}}}",
                node->id, runtime_json_escape(node->text.text), node->input.read_only ? "true" : "false",
                node->input.max_length));
        }

        if (request.command == "vespera_runtime_get_ui_state") {
            if (!runtime_ui_loaded_) return fail("runtime UI document is not loaded");
            std::string clicks = "[";
            for (std::size_t i = 0; i < runtime_ui_state_.pending_clicks.size(); ++i) {
                if (i != 0) clicks += ',';
                clicks += std::to_string(runtime_ui_state_.pending_clicks[i]);
            }
            clicks += ']';
            std::string nodes = "[";
            bool first = true;
            for (const auto& node : runtime_ui_document_.nodes()) {
                if (!first) nodes += ',';
                first = false;
                nodes += std::format(
                    "{{\"id\":{},\"name\":\"{}\",\"type\":\"{}\",\"enabled\":{},"
                    "\"text\":\"{}\",\"progress\":{}}}",
                    node.id, runtime_json_escape(node.name),
                    runtime_json_escape(vespera::ui_node_type_name(node.type)),
                    node.enabled ? "true" : "false", runtime_json_escape(node.text.text), node.progress.value);
            }
            nodes += ']';
            return ok(std::format(
                "{{\"hovered\":{},\"pressed\":{},\"focused\":{},\"pointer_capture\":{},"
                "\"pending_clicks\":{},\"nodes\":{}}}",
                runtime_ui_state_.hovered.value_or(0), runtime_ui_state_.pressed.value_or(0),
                runtime_ui_state_.focused.value_or(0), runtime_ui_state_.pointer_capture, clicks, nodes));
        }

        if (request.command == "vespera_runtime_get_events") {
            std::uint64_t after = 0;
            std::size_t limit = 100;
            if (const auto* raw = runtime_automation_arg(request, "after_sequence")) {
                const auto parsed = runtime_automation_u64(*raw);
                if (!parsed) return fail("after_sequence must be an unsigned integer");
                after = *parsed;
            }
            if (const auto* raw = runtime_automation_arg(request, "limit")) {
                const auto parsed = runtime_automation_u64(*raw);
                if (!parsed || *parsed == 0) return fail("limit must be positive");
                limit = static_cast<std::size_t>(std::min<std::uint64_t>(*parsed, 500));
            }
            auto events = managed_host_.runtime_events_since(after);
            if (events.size() > limit) {
                events.erase(events.begin(), events.end() - static_cast<std::ptrdiff_t>(limit));
            }
            std::string json = "[";
            bool first = true;
            for (const auto& event : events) {
                if (!first) json += ',';
                first = false;
                json += std::format(
                    "{{\"sequence\":{},\"frame\":{},\"generation\":{},\"entity_id\":{},"
                    "\"component\":\"{}\",\"callback\":\"{}\"}}",
                    event.sequence, event.frame, event.assembly_generation, event.entity_id,
                    runtime_json_escape(event.component), runtime_json_escape(event.callback));
            }
            json += ']';
            return ok(std::format(
                "{{\"assembly_generation\":{},\"latest_sequence\":{},\"events\":{}}}",
                managed_host_.assembly_generation(), managed_host_.latest_runtime_event_sequence(), json));
        }

        if (request.command == "vespera_runtime_clear_events") {
            managed_host_.clear_runtime_events();
            return ok("{\"cleared\":true}");
        }

        return fail("unknown runtime automation command: " + request.command);
    }

    void poll_runtime_automation(vespera::GameContext& context) {
        if (!runtime_automation_server_ || !runtime_automation_server_->running()) return;
        for (const auto& incoming : runtime_automation_server_->poll()) {
            auto response = dispatch_runtime_automation(context, incoming.request);
            if (!runtime_automation_server_->respond(incoming.client_id, response)) {
                vespera::log::warn("Runtime automation response could not be delivered to local client.");
            }
        }
    }

    void initialize_managed(vespera::GameContext& context) {
        runtime_ui_surface_.reset();
        if (runtime_ui_loaded_) runtime_ui_surface_.emplace(runtime_ui_document_, &runtime_ui_state_);
        if (managed_host_.initialize(context.scene, context.input, context.audio, asset_catalog_, managed_config_,
                runtime_ui_surface_ ? static_cast<vespera::UiSurface*>(&*runtime_ui_surface_) : nullptr)) {
            managed_host_.set_current_scene_path(current_scene_path_);
            vespera::log::info("Managed scripting: " + managed_host_.status().message);
            managed_host_.start();
        } else {
            vespera::log::warn("Managed scripting unavailable: " + managed_host_.status().message);
        }
    }

#ifdef VESPERA_HAS_LUA
    void initialize_lua(vespera::GameContext& context) {
        lua_ready_ = false;
        if (project_.lua_entry.empty() && project_.lua_entry_asset_id.empty()) return;
        const auto resolved = asset_catalog_.resolve_reference(project_.lua_entry_reference());
        if (!resolved || !resolved.record || resolved.record->kind != vespera::AssetKind::LuaScript) {
            vespera::log::warn("Lua scripting unavailable: reference lua_entry could not be resolved.");
            return;
        }
        vespera::LuaScriptHostConfig config;
        config.entry_script = resolved.record->absolute_path;
        vespera::UiSurface* ui = runtime_ui_surface_ ? static_cast<vespera::UiSurface*>(&*runtime_ui_surface_) : nullptr;
        if (!lua_host_.initialize(context.scene, context.input, context.audio, asset_catalog_, config, ui)) {
            vespera::log::warn("Lua scripting unavailable: " + lua_host_.status().message);
            return;
        }
        lua_host_.set_current_scene_path(current_scene_path_);
        lua_ready_ = lua_host_.start();
        if (lua_ready_) vespera::log::info("Lua scripting: " + lua_host_.status().message);
    }
#endif

    bool load_runtime_scene(vespera::GameContext& context, const std::filesystem::path& path, bool force_reload = false) {
        bool same = false;
        if (!current_scene_path_.empty()) {
            std::error_code ec;
            same = (std::filesystem::exists(path, ec) && !ec
                    && std::filesystem::exists(current_scene_path_, ec) && !ec
                    && std::filesystem::equivalent(path, current_scene_path_, ec) && !ec)
                || std::filesystem::absolute(path).lexically_normal()
                    == std::filesystem::absolute(current_scene_path_).lexically_normal();
        }
        if (same && !force_reload) {
            vespera::log::warn("Coalesced same-scene Scene.Load request; use Scene.Reload() for an intentional reload.");
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
                vespera::log::warn("Blocked a rapid same-scene reload loop (>4 explicit reloads in 2s).");
                return false;
            }
        } else if (!same) {
            same_scene_reload_burst_ = 0;
            same_scene_reload_window_ = {};
        }
        const auto previous_path = current_scene_path_;
        managed_host_.shutdown();
#ifdef VESPERA_HAS_LUA
        lua_host_.shutdown();
        lua_ready_ = false;
#endif
        trigger_tracker_.reset();

        const auto result = vespera::load_scene_text(context.scene, path);
        if (!result) {
            vespera::log::error("Scene switch failed: " + result.message);
            current_scene_path_ = previous_path;
            initialize_managed(context);
#ifdef VESPERA_HAS_LUA
            initialize_lua(context);
#endif
            return false;
        }

        const auto material_report = vespera::hydrate_scene_materials(context.scene, asset_catalog_);
        if (!material_report.message.empty()) vespera::log::warn("Material hydration: " + material_report.message);
        current_scene_path_ = path;
        vespera::Entity& player_proxy = context.scene.create_entity("Runtime Player Proxy");
        player_proxy.tag = "player";
        player_proxy.layer = "Gameplay";
        player_proxy.transform.position = context.scene.camera.position;
        player_proxy_id_ = player_proxy.id;
        current_sector_ = context.scene.world.find_sector_index({context.scene.camera.position.x, context.scene.camera.position.z}).value_or(0);
        initialize_managed(context);
#ifdef VESPERA_HAS_LUA
        initialize_lua(context);
#endif
        vespera::log::info("Scene switched: " + path.generic_string() + " | " + result.message);
        log_current_sector(context.scene.world);
        return true;
    }

    void log_current_sector(const vespera::SectorWorld& world) const {
        if (current_sector_ >= world.sectors().size()) {
            return;
        }

        const auto& sector = world.sectors()[current_sector_];
        vespera::log::info(std::format(
            "Entered sector {}: {} | floor {:.2f} | ceiling {:.2f}",
            current_sector_,
            sector.name,
            sector.floor_height,
            sector.ceiling_height
        ));
    }

    std::size_t current_sector_ = 0;
    std::chrono::steady_clock::time_point same_scene_reload_window_{};
    int same_scene_reload_burst_ = 0;
    double report_timer_ = 0.0;
    float eye_height_ = 1.65f;
    float body_height_ = 1.80f;
    float player_radius_ = 0.28f;
    float max_step_height_ = 0.35f;
    bool load_failed_ = false;
    vespera::SceneObjectId player_proxy_id_ = vespera::kInvalidSceneObjectId;
    vespera::VesperaProject project_;
    vespera::AssetCatalog asset_catalog_;
    vespera::RuntimeAssetMonitor runtime_asset_monitor_{1.0};
    std::filesystem::path current_scene_path_;
    vespera::ManagedScriptHostConfig managed_config_;
    vespera::ManagedScriptHost managed_host_;
#ifdef VESPERA_HAS_LUA
    vespera::LuaScriptHost lua_host_;
    bool lua_ready_ = false;
#endif
    vespera::UiDocument runtime_ui_document_;
    vespera::UiRenderCache runtime_ui_cache_;
    vespera::UiRuntimeState runtime_ui_state_{};
    std::optional<vespera::LegacyUiSurface> runtime_ui_surface_;
    bool runtime_ui_loaded_ = false;
    bool runtime_automation_enabled_ = false;
    std::unique_ptr<vespera::editor::AutomationServer> runtime_automation_server_;
    int last_render_width_ = 0;
    int last_render_height_ = 0;
    vespera::TriggerTracker trigger_tracker_;
};

} // namespace

int main(int argc, char** argv) {
    bool runtime_automation = false;
    vespera::RenderBackendType requested_renderer = vespera::RenderBackendType::Automatic;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i] ? std::string_view(argv[i]) : std::string_view{};
        if (arg == "--automation") {
            runtime_automation = true;
            continue;
        }
        if (arg == "--renderer" || arg.starts_with("--renderer=")) {
            std::string_view value;
            if (arg == "--renderer") {
                if (i + 1 >= argc) {
                    vespera::log::error("--renderer requires auto, d3d12, vulkan, or null");
                    return 2;
                }
                value = argv[++i] ? std::string_view(argv[i]) : std::string_view{};
            } else {
                value = arg.substr(std::string_view("--renderer=").size());
            }
            const auto parsed = vespera::parse_render_backend_type(value);
            if (!parsed) {
                vespera::log::error(std::format(
                    "Unknown renderer backend '{}' (expected auto, d3d12, vulkan, or null).", value));
                return 2;
            }
            requested_renderer = *parsed;
            continue;
        }
        vespera::log::error(std::format("Unknown Vespera reference-game argument: {}", arg));
        return 2;
    }
    ReferenceGame game(runtime_automation);
    vespera::Application app;

    vespera::ApplicationConfig config;
    config.renderer = requested_renderer;
    config.title = std::format("Vespera Engine {} - C# Gameplay", vespera::kEngineVersion);
    vespera::VesperaProject startup_project;
    const auto startup_project_result = vespera::load_vespera_project(
        startup_project, "VesperaReference.vesperaproject");
    if (startup_project_result) {
        config.title = startup_project.window_title.empty() ? startup_project.name : startup_project.window_title;
        config.width = startup_project.window_width;
        config.height = startup_project.window_height;
        config.resizable = startup_project.window_resizable;
        config.relative_mouse = startup_project.relative_mouse;
        config.escape_quits = startup_project.escape_quits;
    } else {
        vespera::log::warn("Could not load project window settings before startup: " + startup_project_result.message);
        config.width = 1280;
        config.height = 720;
        config.relative_mouse = true;
        config.escape_quits = true;
    }

    return app.run(game, config);
}
