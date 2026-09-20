#include "editor_automation.hpp"

#include "editor_assets.hpp"
#include "editor_asset_interactions.hpp"
#include "editor_automation_values.hpp"
#include "editor_build_pipeline.hpp"
#include "editor_command_runtime.hpp"
#include "editor_console.hpp"
#include "editor_history.hpp"
#include "editor_installation.hpp"
#include "editor_managed.hpp"
#include "editor_play_controls.hpp"
#include "editor_prefabs.hpp"
#include "editor_project_session.hpp"
#include "editor_scene_commands.hpp"
#include "editor_selection.hpp"

#include <vespera/assets/asset_authoring.hpp>
#include <vespera/assets/project_package.hpp>
#include <vespera/core/version.hpp>
#include <vespera/scene/component_access.hpp>
#include <vespera/scene/scene_stats.hpp>
#include <vespera/scene/scene_validation.hpp>
#include <vespera/ui/ui_io.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace vespera::editor {

namespace {
constexpr float kDegreesToRadians = 0.017453292519943295f;
constexpr float kRadiansToDegrees = 57.29577951308232f;
}

const vespera::AssetRecord* automation_asset_record(
    const EditorState& state,
    const vespera::editor::AutomationRequest& request
) {
    if (const auto* id = automation_arg(request, "asset_id"); id && !id->empty()) {
        return state.asset_catalog.find_by_id(*id);
    }
    if (const auto* path = automation_arg(request, "path"); path && !path->empty()) {
        return state.asset_catalog.find(*path);
    }
    return nullptr;
}
std::optional<std::filesystem::path> automation_managed_source_path(
    const EditorState& state,
    std::string_view requested,
    std::string* error = nullptr
) {
    if (!state.project_loaded || state.project.managed_project.empty()) {
        if (error) *error = "the current project has no managed C# project";
        return std::nullopt;
    }
    if (requested.empty()) {
        if (error) *error = "path is required";
        return std::nullopt;
    }
    std::filesystem::path relative(requested);
    if (relative.is_absolute() || relative.has_root_name() || relative.has_root_directory()) {
        if (error) *error = "managed source path must be relative to the managed project directory";
        return std::nullopt;
    }
    relative = relative.lexically_normal();
    const auto text = relative.generic_string();
    if (text.empty() || text == "." || text.starts_with("../") || text == "..") {
        if (error) *error = "managed source path must stay inside the managed project directory";
        return std::nullopt;
    }
    if (relative.extension() != ".cs") {
        if (error) *error = "managed source path must end in .cs";
        return std::nullopt;
    }
    const auto base = state.project.managed_project_path().parent_path().lexically_normal();
    return (base / relative).lexically_normal();
}
bool automation_guarded_replace_text(
    const std::filesystem::path& path,
    std::string_view text,
    std::string* error = nullptr
) {
    if (text.size() > 1024 * 1024) {
        if (error) *error = "managed source is limited to 1 MiB through automation";
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        if (error) *error = "could not create managed source directory: " + ec.message();
        return false;
    }
    auto temp = path; temp += ".vespera_tmp";
    auto backup = path; backup += ".vespera_bak";
    std::filesystem::remove(temp, ec); ec.clear();
    std::filesystem::remove(backup, ec); ec.clear();
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) { if (error) *error = "could not open temporary managed source for writing"; return false; }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) { std::filesystem::remove(temp, ec); if (error) *error = "failed while writing temporary managed source"; return false; }
    }
    const bool existed = std::filesystem::exists(path, ec) && !ec; ec.clear();
    if (existed) {
        std::filesystem::rename(path, backup, ec);
        if (ec) { std::filesystem::remove(temp, ec); if (error) *error = "could not back up existing managed source before replacement"; return false; }
    }
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        const auto commit_error = ec.message();
        std::string restore_error;
        ec.clear();
        if (existed) {
            std::filesystem::rename(backup, path, ec);
            if (ec) restore_error = "; backup restore also failed: " + ec.message();
        }
        if (error) *error = "could not commit managed source replacement: " + commit_error + restore_error;
        return false;
    }
    if (existed) { ec.clear(); std::filesystem::remove(backup, ec); }
    return true;
}
vespera::editor::AutomationResponse automation_response(std::uint64_t id, bool ok, std::string text) {
    vespera::editor::AutomationResponse response;
    response.id = id;
    response.ok = ok;
    response.fields["text"] = std::move(text);
    return response;
}
vespera::editor::AutomationResponse dispatch_automation_request(
    EditorState& state,
    const vespera::editor::AutomationRequest& request) {
    using vespera::editor::AutomationResponse;
    const auto fail = [&](std::string message) { return automation_response(request.id, false, std::move(message)); };
    const auto ok = [&](std::string message) { return automation_response(request.id, true, std::move(message)); };

    if (request.command == "vespera_get_state") {
        const auto selected = state.selection.kind == SelectionKind::Entity
            ? state.selection.object_id : vespera::kInvalidSceneObjectId;
        const std::string play_state = state.play_state == EditorPlayState::Editing
            ? "editing" : (state.play_state == EditorPlayState::Paused ? "paused" : "playing");
        const std::uint32_t assembly_generation = state.play_runtime
            ? state.play_runtime->assembly_generation() : state.last_runtime_assembly_generation;
        const std::uint64_t runtime_event_sequence = state.play_runtime
            ? state.play_runtime->latest_runtime_event_sequence() : state.last_runtime_event_sequence;
        const auto& visible_scene = editor_is_playing(state) ? state.play_scene : state.scene;
        const auto scene_stats = vespera::collect_scene_stats(visible_scene);
        const auto& perf = state.performance;
        return ok(std::format(
            "{{\"engine_version\":\"{}\",\"project\":\"{}\",\"scene\":\"{}\",\"entities\":{},\"sectors\":{},\"dirty\":{},\"play_state\":\"{}\",\"selected_entity_id\":{},\"managed_assembly_generation\":{},\"runtime_event_sequence\":{},"
            "\"scene_stats\":{{\"enabled_entities\":{},\"sprites\":{},\"meshes\":{},\"colliders\":{},\"triggers\":{},\"point_lights\":{},\"managed_scripts\":{},\"sprite_clips\":{}}},"
            "\"asset_stats\":{{\"records\":{},\"dependency_edges\":{},\"broken_dependencies\":{}}},"
            "\"performance\":{{\"frame_index\":{},\"last_frame_ms\":{:.3f},\"smoothed_frame_ms\":{:.3f},\"max_frame_ms\":{:.3f},\"play_update_ms\":{:.3f},\"render_ms\":{:.3f},\"live_resize_redraws\":{}}}}}",
            vespera::kEngineVersion,
            automation_json_escape(state.project_loaded ? state.project.name : std::string{}),
            automation_json_escape(state.scene_path.generic_string()),
            scene_stats.entities, scene_stats.sectors,
            state.dirty ? "true" : "false", play_state, selected, assembly_generation, runtime_event_sequence,
            scene_stats.enabled_entities, scene_stats.sprite_renderers, scene_stats.mesh_renderers,
            scene_stats.colliders, scene_stats.triggers, scene_stats.point_lights, scene_stats.managed_scripts, scene_stats.sprite_clips,
            state.asset_catalog.records().size(), state.asset_catalog.dependencies().size(), state.asset_catalog.broken_dependencies().size(),
            perf.frame_index, perf.last_frame_ms, perf.smoothed_frame_ms, perf.max_frame_ms,
            state.play_update_ms, perf.render_ms, perf.live_resize_redraws));
    }

    if (request.command == "vespera_list_entities") {
        std::string json = "[";
        for (std::size_t i = 0; i < state.scene.entities.size(); ++i) {
            const auto& entity = state.scene.entities[i];
            if (i != 0) json += ',';
            json += std::format(
                "{{\"id\":{},\"name\":\"{}\",\"parent_id\":{},\"tag\":\"{}\",\"layer\":\"{}\",\"components\":[",
                entity.id, automation_json_escape(entity.name), entity.parent_id,
                automation_json_escape(entity.tag), automation_json_escape(entity.layer));
            bool first = true;
            for (const auto& component : vespera::kBuiltinComponentTypes) {
                if (!entity.has_component(component.type)) continue;
                if (!first) json += ',';
                first = false;
                json += '"'; json += component.key; json += '"';
            }
            for (const auto& script : entity.managed_scripts) {
                if (!first) json += ',';
                first = false;
                json += "\"csharp:" + automation_json_escape(script.class_name) + "\"";
            }
            json += "]}";
        }
        json += "]";
        return ok(std::move(json));
    }

    if (request.command == "vespera_get_entity") {
        const auto* raw_id = automation_arg(request, "entity_id");
        const auto id = raw_id ? automation_u64(*raw_id) : std::nullopt;
        if (!id) return fail("entity_id is required and must be an unsigned integer");
        const auto* entity = state.scene.find_entity(*id);
        if (!entity) return fail("entity_id does not exist in the edit scene");
        const auto world = editor_world_transform(state, *entity);
        std::string components = "[";
        bool first = true;
        for (const auto& component : vespera::kBuiltinComponentTypes) {
            if (!entity->has_component(component.type)) continue;
            if (!first) components += ',';
            first = false;
            components += std::format("\"{}\"", component.key);
        }
        components += "]";
        return ok(std::format(
            "{{\"id\":{},\"name\":\"{}\",\"enabled\":{},\"parent_id\":{},\"tag\":\"{}\",\"layer\":\"{}\","
            "\"local_position\":[{},{},{}],\"local_rotation_degrees\":[{},{},{}],\"local_scale\":[{},{},{}],"
            "\"world_position\":[{},{},{}],\"components\":{}}}",
            entity->id, automation_json_escape(entity->name), entity->enabled ? "true" : "false", entity->parent_id,
            automation_json_escape(entity->tag), automation_json_escape(entity->layer),
            entity->transform.position.x, entity->transform.position.y, entity->transform.position.z,
            entity->transform.rotation.x * kRadiansToDegrees, entity->transform.rotation.y * kRadiansToDegrees, entity->transform.rotation.z * kRadiansToDegrees,
            entity->transform.scale.x, entity->transform.scale.y, entity->transform.scale.z,
            world.position.x, world.position.y, world.position.z, components));
    }

    if (request.command == "vespera_list_assets") {
        std::string json = "[";
        bool first = true;
        const std::string kind_filter = automation_arg(request, "kind") ? *automation_arg(request, "kind") : std::string{};
        for (const auto& asset : state.asset_catalog.records()) {
            if (!kind_filter.empty() && vespera::asset_kind_name(asset.kind) != kind_filter) continue;
            if (!first) json += ',';
            first = false;
            json += std::format("{{\"asset_id\":\"{}\",\"kind\":\"{}\",\"path\":\"{}\",\"name\":\"{}\"}}",
                automation_json_escape(asset.asset_id), vespera::asset_kind_name(asset.kind),
                automation_json_escape(asset.relative_path.generic_string()), automation_json_escape(asset.display_name));
        }
        json += "]";
        return ok(std::move(json));
    }

    if (request.command == "vespera_get_build_manifest") {
        if (!state.project_loaded) return fail("no Vespera project is open");
        const auto manifest = vespera::build_project_asset_manifest(state.project, state.asset_catalog);
        std::string json = std::format(
            "{{\"valid\":{},\"asset_count\":{},\"missing_roots\":{},\"broken_dependencies\":{},\"stale_root_paths\":{},\"assets\":[",
            manifest.valid() ? "true" : "false", manifest.assets.size(), manifest.missing_roots.size(),
            manifest.broken_dependencies.size(), manifest.stale_root_paths.size());
        bool first = true;
        for (const auto* asset : manifest.assets) {
            if (!asset) continue;
            if (!first) json += ',';
            first = false;
            json += std::format("{{\"asset_id\":\"{}\",\"kind\":\"{}\",\"path\":\"{}\"}}",
                automation_json_escape(asset->asset_id), vespera::asset_kind_name(asset->kind),
                automation_json_escape(asset->relative_path.generic_string()));
        }
        json += "]}";
        return ok(std::move(json));
    }

    if (request.command == "vespera_export_project") {
        if (editor_is_playing(state)) return fail("project export is disabled during Play Mode");
        if (!state.project_loaded) return fail("no Vespera project is open");
        const auto* raw_output = automation_arg(request, "output_directory");
        const std::string configuration = automation_arg(request, "configuration")
            ? *automation_arg(request, "configuration") : "Development";
        if (!vespera::valid_package_configuration(configuration)) {
            return fail("configuration must be Debug, Development, or Release");
        }
        vespera::ProjectPackageOptions package_options;
        package_options.configuration = configuration;
        package_options.managed_deployment = state.project.managed_deployment;
        if (const auto* deployment = automation_arg(request, "managed_deployment"); deployment && !deployment->empty()) {
            const auto parsed = vespera::managed_deployment_mode_from_name(*deployment);
            if (!parsed) return fail("managed_deployment must be framework-dependent or portable");
            package_options.managed_deployment = *parsed;
        }
        package_options.clean_output = automation_arg(request, "clean")
            ? automation_bool(*automation_arg(request, "clean")).value_or(true) : true;
        package_options.include_debug_symbols = configuration == "Debug"
            || (configuration == "Development" && state.project.development_diagnostics);
        if (raw_output && !raw_output->empty()) {
            package_options.output_directory = std::filesystem::path(*raw_output);
            if (package_options.output_directory.is_relative()) {
                package_options.output_directory = state.project.root_directory / package_options.output_directory;
            }
        } else {
            std::string safe_name = state.project.name.empty() ? "VesperaGame" : state.project.name;
            for (char& c : safe_name) {
                const bool allowed = std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-';
                if (!allowed) c = '-';
            }
            package_options.output_directory = state.project.build_output_root() / (safe_name + "-" + configuration);
        }
        if (const auto* runtime = automation_arg(request, "runtime_executable"); runtime && !runtime->empty()) {
            package_options.runtime_executable = *runtime;
        } else {
            package_options.runtime_executable = automation_find_runtime_executable(state, configuration);
        }
        if (const auto* managed = automation_arg(request, "managed_directory"); managed && !managed->empty()) {
            package_options.managed_directory = *managed;
        } else {
            package_options.managed_directory = automation_find_managed_directory(state);
        }
        if (package_options.managed_deployment == vespera::ManagedDeploymentMode::Portable) {
            const auto dotnet = editor_find_dotnet_executable();
            if (!dotnet.empty()) {
                std::error_code ec;
                const auto canonical = std::filesystem::weakly_canonical(dotnet, ec);
                package_options.dotnet_root = (ec ? dotnet : canonical).parent_path();
            }
        }
        const auto packaged = vespera::export_project_package(state.project, state.asset_catalog, package_options);
        append_command_audit(state, vespera::editor::EditorCommandKind::ExportProject,
            "Automation export project", packaged.ok, state.current_state_id, state.current_state_id);
        if (!packaged) return fail(packaged.message);
        const bool launch_requested = automation_arg(request, "launch")
            ? automation_bool(*automation_arg(request, "launch")).value_or(false) : false;
        const bool runtime_automation = automation_arg(request, "runtime_automation")
            ? automation_bool(*automation_arg(request, "runtime_automation")).value_or(false) : false;
        if (runtime_automation && !launch_requested) return fail("runtime_automation requires launch=true");
        const bool launched = launch_requested
            ? launch_packaged_runtime(state, packaged.packaged_runtime, runtime_automation) : false;
        if (launch_requested && !launched) return fail("package succeeded but exported runtime launch failed; inspect vespera_get_console");
        return ok(std::format(
            "{{\"output_directory\":\"{}\",\"runtime\":\"{}\",\"report\":\"{}\",\"assets\":{},\"asset_bytes\":{},"
            "\"metadata\":{},\"managed_files\":{},\"runtime_files\":{},\"warnings\":{},\"launched\":{},\"runtime_automation\":{}}}",
            automation_json_escape(packaged.package_directory.generic_string()),
            automation_json_escape(packaged.packaged_runtime.generic_string()), automation_json_escape(packaged.package_report.generic_string()),
            packaged.copied_assets, packaged.asset_bytes, packaged.copied_metadata, packaged.copied_managed_files,
            packaged.copied_runtime_files, packaged.warnings.size(), launched ? "true" : "false",
            (launched && runtime_automation) ? "true" : "false"));
    }

    if (request.command == "vespera_list_capabilities") {
        std::string json = std::format("{{\"extension_api_version\":{},\"extensions\":[", vespera::editor::kEditorExtensionApiVersion);
        bool first = true;
        for (const auto& extension : state.extension_registry.extensions()) {
            if (!first) json += ',';
            first = false;
            json += std::format("{{\"id\":\"{}\",\"name\":\"{}\",\"version\":\"{}\"}}",
                automation_json_escape(extension.id), automation_json_escape(extension.display_name), automation_json_escape(extension.version));
        }
        json += "],\"commands\":[";
        first = true;
        for (const auto& command : state.extension_registry.commands()) {
            if (!first) json += ',';
            first = false;
            json += std::format("{{\"name\":\"{}\",\"mutating\":{},\"undoable\":{},\"allowed_during_play\":{}}}",
                automation_json_escape(command.name), command.mutating ? "true" : "false",
                command.undoable ? "true" : "false", command.allowed_during_play ? "true" : "false");
        }
        json += "]}";
        return ok(std::move(json));
    }

    if (request.command == "vespera_get_console") {
        std::size_t limit = 50;
        if (const auto* raw_limit = automation_arg(request, "limit")) {
            const auto parsed = automation_u64(*raw_limit);
            if (!parsed || *parsed == 0) return fail("limit must be a positive integer");
            limit = static_cast<std::size_t>(std::min<std::uint64_t>(*parsed, 100));
        }
        const std::size_t begin = state.console.size() > limit ? state.console.size() - limit : 0;
        std::string json = "[";
        for (std::size_t i = begin; i < state.console.size(); ++i) {
            if (i != begin) json += ',';
            const auto& entry = state.console[i];
            const char* level = entry.level == ConsoleEntry::Level::Error
                ? "error" : (entry.level == ConsoleEntry::Level::Warning ? "warning" : "info");
            json += std::format("{{\"level\":\"{}\",\"text\":\"{}\"}}",
                level, automation_json_escape(entry.text));
        }
        json += "]";
        return ok(std::move(json));
    }

    if (request.command == "vespera_get_command_log") {
        std::size_t limit = 50;
        if (const auto* raw_limit = automation_arg(request, "limit")) {
            const auto parsed = automation_u64(*raw_limit);
            if (!parsed || *parsed == 0) return fail("limit must be a positive integer");
            limit = static_cast<std::size_t>(std::min<std::uint64_t>(*parsed, 100));
        }
        const std::size_t begin = state.command_log.size() > limit ? state.command_log.size() - limit : 0;
        std::string json = "[";
        for (std::size_t i = begin; i < state.command_log.size(); ++i) {
            if (i != begin) json += ',';
            const auto& record = state.command_log[i];
            json += std::format(
                "{{\"sequence\":{},\"command\":\"{}\",\"label\":\"{}\",\"succeeded\":{},\"before_state_id\":{},\"after_state_id\":{},\"target_entity_id\":{},\"target_asset_id\":\"{}\"}}",
                record.sequence, vespera::editor::command_name(record.kind), automation_json_escape(record.label),
                record.succeeded ? "true" : "false", record.before_state_id, record.after_state_id,
                record.target_entity_id, automation_json_escape(record.target_asset_id));
        }
        json += "]";
        return ok(std::move(json));
    }

    if (request.command == "vespera_select_entity") {
        if (editor_is_playing(state)) return fail("selection mutation is disabled during Play Mode");
        const auto* raw_id = automation_arg(request, "entity_id");
        const auto id = raw_id ? automation_u64(*raw_id) : std::nullopt;
        if (!id) return fail("entity_id is required and must be an unsigned integer");
        const auto index = entity_index_from_id(state, *id);
        if (!index) return fail("entity_id does not exist in the edit scene");
        select_entity(state, *index);
        append_command_audit(state, vespera::editor::EditorCommandKind::SelectEntity,
            "Automation select entity", true, 0, 0, *id);
        return ok(std::format("{{\"selected_entity_id\":{}}}", *id));
    }

    if (request.command == "vespera_create_entity") {
        if (editor_is_playing(state)) return fail("scene authoring is disabled during Play Mode");
        const std::string kind = automation_arg(request, "kind") ? *automation_arg(request, "kind") : "empty";
        const std::string requested_name = automation_arg(request, "name") ? *automation_arg(request, "name") : std::string{};
        bool created = false;
        if (kind == "empty") created = command_create_entity(state, requested_name);
        else if (kind == "sprite") created = command_create_sprite_entity(state, requested_name);
        else if (kind == "trigger") created = command_create_trigger_entity(state, requested_name);
        else if (kind == "point_light") created = command_create_point_light_entity(state, requested_name);
        else return fail("kind must be empty, sprite, trigger or point_light");
        if (!created) return fail("entity creation failed");
        const auto selected = selected_entity_index(state);
        if (!selected) return fail("entity was created but selection could not be resolved");
        auto& entity = state.scene.entities[*selected];
        if (!state.command_log.empty()) state.command_log.back().target_entity_id = entity.id;
        return ok(std::format("{{\"entity_id\":{},\"name\":\"{}\"}}", entity.id, automation_json_escape(entity.name)));
    }

    if (request.command == "vespera_duplicate_entity") {
        if (editor_is_playing(state)) return fail("scene authoring is disabled during Play Mode");
        const auto* raw_id = automation_arg(request, "entity_id");
        const auto id = raw_id ? automation_u64(*raw_id) : std::nullopt;
        if (!id) return fail("entity_id is required and must be an unsigned integer");
        const auto index = entity_index_from_id(state, *id);
        if (!index) return fail("entity_id does not exist in the edit scene");
        select_entity(state, *index);
        if (!command_duplicate_selected_entity(state)) return fail("duplicate command failed");
        const auto selected = selected_entity_index(state);
        if (!selected) return fail("duplicate completed but selection could not be resolved");
        const auto new_id = state.scene.entities[*selected].id;
        if (!state.command_log.empty()) state.command_log.back().target_entity_id = new_id;
        return ok(std::format("{{\"source_entity_id\":{},\"entity_id\":{}}}", *id, new_id));
    }

    if (request.command == "vespera_delete_entity") {
        if (editor_is_playing(state)) return fail("scene authoring is disabled during Play Mode");
        const auto* raw_id = automation_arg(request, "entity_id");
        const auto id = raw_id ? automation_u64(*raw_id) : std::nullopt;
        if (!id) return fail("entity_id is required and must be an unsigned integer");
        const auto index = entity_index_from_id(state, *id);
        if (!index) return fail("entity_id does not exist in the edit scene");
        select_entity(state, *index);
        if (!command_delete_selected_entity(state)) return fail("delete command failed");
        if (!state.command_log.empty()) state.command_log.back().target_entity_id = *id;
        return ok(std::format("{{\"deleted_entity_id\":{}}}", *id));
    }

    if (request.command == "vespera_reparent_entity") {
        if (editor_is_playing(state)) return fail("scene authoring is disabled during Play Mode");
        const auto* raw_child = automation_arg(request, "entity_id");
        const auto child = raw_child ? automation_u64(*raw_child) : std::nullopt;
        if (!child) return fail("entity_id is required and must be an unsigned integer");
        if (!state.scene.find_entity(*child)) return fail("entity_id does not exist in the edit scene");
        vespera::SceneObjectId parent = vespera::kInvalidSceneObjectId;
        if (const auto* raw_parent = automation_arg(request, "parent_id"); raw_parent && !raw_parent->empty() && *raw_parent != "0") {
            const auto parsed = automation_u64(*raw_parent);
            if (!parsed) return fail("parent_id must be an unsigned integer");
            parent = *parsed;
            if (!state.scene.find_entity(parent)) return fail("parent_id does not exist in the edit scene");
        }
        if (!command_reparent_entity(state, *child, parent)) return fail("reparent command failed; the relationship may be unchanged or cyclic");
        if (!state.command_log.empty()) state.command_log.back().target_entity_id = *child;
        return ok(std::format("{{\"entity_id\":{},\"parent_id\":{}}}", *child, parent));
    }

    if (request.command == "vespera_create_primitive") {
        if (editor_is_playing(state)) return fail("scene authoring is disabled during Play Mode");
        const std::string primitive = automation_arg(request, "primitive")
            ? *automation_arg(request, "primitive") : "cube";
        std::optional<vespera::PrimitiveMeshType> type = vespera::primitive_mesh_from_name(primitive);
        if (!type) return fail("primitive must be cube, plane, cylinder or sphere");
        if (!command_create_primitive_entity(state, *type)) return fail("primitive creation failed");
        const auto selected = selected_entity_index(state);
        if (!selected) return fail("primitive was created but selection could not be resolved");
        const auto id = state.scene.entities[*selected].id;
        if (!state.command_log.empty()) state.command_log.back().target_entity_id = id;
        return ok(std::format("{{\"entity_id\":{},\"name\":\"{}\"}}",
            id, automation_json_escape(state.scene.entities[*selected].name)));
    }

    if (request.command == "vespera_set_transform") {
        if (editor_is_playing(state)) return fail("scene authoring is disabled during Play Mode");
        const auto* raw_id = automation_arg(request, "entity_id");
        const auto id = raw_id ? automation_u64(*raw_id) : std::nullopt;
        if (!id) return fail("entity_id is required and must be an unsigned integer");
        if (!state.scene.find_entity(*id)) return fail("entity_id does not exist in the edit scene");
        const bool world_space = !automation_arg(request, "space") || *automation_arg(request, "space") != "local";
        const auto position = automation_arg(request, "position") ? automation_vec3(*automation_arg(request, "position")) : std::optional<vespera::Vec3>{};
        const auto rotation_degrees = automation_arg(request, "rotation_degrees") ? automation_vec3(*automation_arg(request, "rotation_degrees")) : std::optional<vespera::Vec3>{};
        const auto scale = automation_arg(request, "scale") ? automation_vec3(*automation_arg(request, "scale")) : std::optional<vespera::Vec3>{};
        if (automation_arg(request, "position") && !position) return fail("position must be x,y,z");
        if (automation_arg(request, "rotation_degrees") && !rotation_degrees) return fail("rotation_degrees must be x,y,z");
        if (automation_arg(request, "scale") && !scale) return fail("scale must be x,y,z");
        if (!position && !rotation_degrees && !scale) return fail("provide position, rotation_degrees and/or scale");

        const bool changed = execute_editor_command(state, vespera::editor::EditorCommandKind::SetEntityTransform,
            "Automation set entity transform", [&]() {
                auto* entity = state.scene.find_entity(*id);
                if (!entity) return false;
                vespera::TransformComponent transform = world_space ? editor_world_transform(state, *entity) : entity->transform;
                if (position) transform.position = *position;
                if (rotation_degrees) transform.rotation = {
                    rotation_degrees->x * kDegreesToRadians,
                    rotation_degrees->y * kDegreesToRadians,
                    rotation_degrees->z * kDegreesToRadians};
                if (scale) transform.scale = *scale;
                if (world_space) set_editor_world_transform(state, *id, transform);
                else entity->transform = transform;
                return true;
            });
        if (!changed) return fail("transform command failed");
        if (!state.command_log.empty()) state.command_log.back().target_entity_id = *id;
        return ok(std::format("{{\"entity_id\":{},\"space\":\"{}\"}}", *id, world_space ? "world" : "local"));
    }

    if (request.command == "vespera_add_component") {
        if (editor_is_playing(state)) return fail("scene authoring is disabled during Play Mode");
        const auto* raw_id = automation_arg(request, "entity_id");
        const auto id = raw_id ? automation_u64(*raw_id) : std::nullopt;
        const auto* component_key = automation_arg(request, "component");
        if (!id || !component_key || component_key->empty()) return fail("entity_id and component are required");
        const auto* info = vespera::builtin_component_info(*component_key);
        if (!info) return fail("unknown built-in component key");
        if (info->type == vespera::BuiltinComponentType::Transform) return fail("Transform always exists and cannot be added");
        if (!state.scene.find_entity(*id)) return fail("entity_id does not exist in the edit scene");
        const bool changed = execute_editor_command(state, vespera::editor::EditorCommandKind::AddComponent,
            "Automation add component", [&]() {
                auto* entity = state.scene.find_entity(*id);
                return entity && entity->add_component(info->type);
            });
        if (!changed) return fail("component already exists or could not be added");
        if (!state.command_log.empty()) state.command_log.back().target_entity_id = *id;
        return ok(std::format("{{\"entity_id\":{},\"component\":\"{}\"}}", *id, automation_json_escape(*component_key)));
    }

    if (request.command == "vespera_remove_component") {
        if (editor_is_playing(state)) return fail("scene authoring is disabled during Play Mode");
        const auto* raw_id = automation_arg(request, "entity_id");
        const auto id = raw_id ? automation_u64(*raw_id) : std::nullopt;
        const auto* component_key = automation_arg(request, "component");
        if (!id || !component_key || component_key->empty()) return fail("entity_id and component are required");
        const auto* info = vespera::builtin_component_info(*component_key);
        if (!info) return fail("unknown built-in component key");
        if (!info->removable) return fail("component is not removable");
        if (!state.scene.find_entity(*id)) return fail("entity_id does not exist in the edit scene");
        const bool changed = execute_editor_command(state, vespera::editor::EditorCommandKind::RemoveComponent,
            "Automation remove component", [&]() {
                auto* entity = state.scene.find_entity(*id);
                return entity && entity->remove_component(info->type);
            });
        if (!changed) return fail("component is not present or could not be removed");
        if (!state.command_log.empty()) state.command_log.back().target_entity_id = *id;
        return ok(std::format("{{\"entity_id\":{},\"component\":\"{}\"}}", *id, automation_json_escape(*component_key)));
    }

    if (request.command == "vespera_get_component_property") {
        const auto* raw_id = automation_arg(request, "entity_id");
        const auto id = raw_id ? automation_u64(*raw_id) : std::nullopt;
        const auto* component_key = automation_arg(request, "component");
        const auto* property_key = automation_arg(request, "property");
        if (!id || !component_key || !property_key) return fail("entity_id, component and property are required");
        const auto* entity = state.scene.find_entity(*id);
        if (!entity) return fail("entity_id does not exist in the edit scene");
        const auto* property_info = automation_property_info(*component_key, *property_key);
        if (!property_info) return fail("unknown reflected component property");
        const auto value = vespera::get_builtin_component_property(*entity, *component_key, *property_key);
        if (!value) return fail("component is missing or property could not be read");
        return ok(std::format("{{\"entity_id\":{},\"component\":\"{}\",\"property\":\"{}\",\"value\":{}}}",
            *id, automation_json_escape(*component_key), automation_json_escape(*property_key), automation_property_json(*value)));
    }

    if (request.command == "vespera_set_component_property") {
        if (editor_is_playing(state)) return fail("scene authoring is disabled during Play Mode");
        const auto* raw_id = automation_arg(request, "entity_id");
        const auto id = raw_id ? automation_u64(*raw_id) : std::nullopt;
        const auto* component_key = automation_arg(request, "component");
        const auto* property_key = automation_arg(request, "property");
        const auto* raw_value = automation_arg(request, "value");
        if (!id || !component_key || !property_key || !raw_value) return fail("entity_id, component, property and value are required");
        const auto* property_info = automation_property_info(*component_key, *property_key);
        if (!property_info) return fail("unknown reflected component property");
        const auto parsed_value = automation_parse_property_value(property_info->type, *raw_value);
        if (!parsed_value) return fail("value could not be parsed for the reflected property type");
        if (!state.scene.find_entity(*id)) return fail("entity_id does not exist in the edit scene");
        const bool changed = execute_editor_command(state, vespera::editor::EditorCommandKind::SetComponentProperty,
            "Automation set component property", [&]() {
                auto* entity = state.scene.find_entity(*id);
                if (!entity) return false;
                const bool set = vespera::set_builtin_component_property(*entity, *component_key, *property_key, *parsed_value);
                if (set && *component_key == "sectorline.mesh_renderer"
                    && (*property_key == "material_asset_id" || *property_key == "material_path")) {
                    (void)vespera::hydrate_scene_materials(state.scene, state.asset_catalog);
                }
                return set;
            });
        if (!changed) return fail("property mutation was rejected by component validation");
        if (!state.command_log.empty()) state.command_log.back().target_entity_id = *id;
        return ok(std::format("{{\"entity_id\":{},\"component\":\"{}\",\"property\":\"{}\"}}",
            *id, automation_json_escape(*component_key), automation_json_escape(*property_key)));
    }

    if (request.command == "vespera_instantiate_prefab") {
        if (editor_is_playing(state)) return fail("scene authoring is disabled during Play Mode");
        const auto* record = automation_asset_record(state, request);
        if (!record) return fail("provide a valid prefab asset_id or project-relative path");
        if (record->kind != vespera::AssetKind::EntityPrefab) return fail("asset is not an Entity Prefab");
        std::optional<vespera::Vec3> position;
        if (const auto* raw_position = automation_arg(request, "position")) {
            position = automation_vec3(*raw_position);
            if (!position) return fail("position must be x,y,z");
        }
        if (!command_instantiate_prefab(state, record->absolute_path, position)) return fail("prefab instantiation failed");
        const auto selected = selected_entity_index(state);
        if (!selected) return fail("prefab was instantiated but selection could not be resolved");
        const auto id = state.scene.entities[*selected].id;
        if (!state.command_log.empty()) {
            state.command_log.back().target_entity_id = id;
            state.command_log.back().target_asset_id = record->asset_id;
        }
        return ok(std::format("{{\"entity_id\":{},\"asset_id\":\"{}\"}}", id, automation_json_escape(record->asset_id)));
    }

    if (request.command == "vespera_assign_material") {
        if (editor_is_playing(state)) return fail("scene authoring is disabled during Play Mode");
        const auto* raw_id = automation_arg(request, "entity_id");
        const auto id = raw_id ? automation_u64(*raw_id) : std::nullopt;
        if (!id) return fail("entity_id is required");
        const auto* record = automation_asset_record(state, request);
        if (!record) return fail("provide a valid Material asset_id or project-relative path");
        if (record->kind != vespera::AssetKind::Material) return fail("asset is not a Material");
        auto* entity = state.scene.find_entity(*id);
        if (!entity) return fail("entity_id does not exist in the edit scene");
        if (!entity->mesh_renderer) return fail("entity does not have a Mesh Renderer");
        const bool changed = execute_editor_command(state, vespera::editor::EditorCommandKind::AssignMaterialAsset,
            "Automation assign Material", [&]() {
                auto* target = state.scene.find_entity(*id);
                if (!target || !target->mesh_renderer) return false;
                target->mesh_renderer->material = {record->asset_id, record->relative_path};
                target->mesh_renderer->material_resolved = false;
                (void)vespera::hydrate_scene_materials(state.scene, state.asset_catalog);
                return true;
            });
        if (!changed) return fail("Material assignment failed");
        if (!state.command_log.empty()) {
            state.command_log.back().target_entity_id = *id;
            state.command_log.back().target_asset_id = record->asset_id;
        }
        return ok(std::format("{{\"entity_id\":{},\"asset_id\":\"{}\"}}", *id, automation_json_escape(record->asset_id)));
    }

    if (request.command == "vespera_create_material") {
        if (editor_is_playing(state)) return fail("asset authoring is disabled during Play Mode");
        if (state.assets_root.empty()) return fail("no project Assets root is open");
        std::filesystem::path relative = automation_arg(request, "path")
            ? std::filesystem::path(*automation_arg(request, "path")) : std::filesystem::path{};
        if (relative.empty()) {
            relative = std::filesystem::relative(unique_material_asset_path(state), state.assets_root).lexically_normal();
        }
        if (relative.is_absolute() || relative.extension() != ".slmat" || relative.lexically_normal().generic_string().starts_with("..")) {
            return fail("path must be a project-relative .slmat path inside Assets");
        }
        const auto absolute = (state.assets_root / relative).lexically_normal();
        std::error_code ec;
        if (std::filesystem::exists(absolute, ec)) return fail("Material path already exists");
        vespera::MaterialAsset material;
        material.name = automation_arg(request, "name") && !automation_arg(request, "name")->empty()
            ? *automation_arg(request, "name") : relative.stem().string();
        const std::string shader = automation_arg(request, "shader") ? *automation_arg(request, "shader") : "lit";
        if (shader == "lit") material.properties.shader = vespera::BuiltinMaterialShader::Lit;
        else if (shader == "unlit") material.properties.shader = vespera::BuiltinMaterialShader::Unlit;
        else return fail("shader must be lit or unlit");
        std::filesystem::create_directories(absolute.parent_path(), ec);
        if (ec) return fail("could not create Material folder: " + ec.message());
        const auto saved = vespera::save_material_asset(absolute, material);
        if (!saved) return fail(saved.message);
        refresh_asset_catalog(state, false, true);
        const auto* record = state.asset_catalog.find(relative.generic_string());
        append_command_audit(state, vespera::editor::EditorCommandKind::CreateMaterialAsset,
            "Automation create Material asset", record != nullptr, state.current_state_id, state.current_state_id,
            vespera::kInvalidSceneObjectId, record ? record->asset_id : std::string{});
        if (!record) return fail("Material was written but catalog refresh did not register it");
        return ok(std::format("{{\"asset_id\":\"{}\",\"path\":\"{}\"}}",
            automation_json_escape(record->asset_id), automation_json_escape(record->relative_path.generic_string())));
    }

    if (request.command == "vespera_refresh_assets") {
        if (editor_is_playing(state)) return fail("asset refresh is disabled during Play Mode");
        const bool rehash = automation_arg(request, "force_rehash")
            ? automation_bool(*automation_arg(request, "force_rehash")).value_or(false) : false;
        refresh_asset_catalog(state, true, rehash);
        append_command_audit(state, vespera::editor::EditorCommandKind::RefreshAssets,
            "Automation refresh assets", true, state.current_state_id, state.current_state_id);
        return ok(std::format("{{\"assets\":{},\"dependencies\":{},\"broken\":{},\"stale\":{}}}",
            state.asset_catalog.records().size(), state.last_asset_report.dependency_edges,
            state.last_asset_report.broken_dependencies, state.last_asset_report.stale_fallback_paths));
    }

    if (request.command == "vespera_build_csharp") {
        const auto before_console = state.console.size();
        build_managed_scripts(state);
        bool saw_error = false;
        for (std::size_t i = before_console; i < state.console.size(); ++i) {
            if (state.console[i].level == ConsoleEntry::Level::Error) saw_error = true;
        }
        append_command_audit(state, vespera::editor::EditorCommandKind::BuildManagedScripts,
            "Automation Build C#", !saw_error, state.current_state_id, state.current_state_id);
        return saw_error ? fail("C# build reported errors; inspect vespera_get_console")
            : ok("{\"build_requested\":true,\"errors_observed\":false}");
    }

    if (request.command == "vespera_open_scene") {
        if (editor_is_playing(state)) return fail("scene opening is disabled during Play Mode");
        const auto* record = automation_asset_record(state, request);
        if (!record) return fail("provide a valid Scene asset_id or project-relative path");
        if (record->kind != vespera::AssetKind::Scene) return fail("asset is not a Scene");
        const bool opened = open_scene(state, record->absolute_path);
        append_command_audit(state, vespera::editor::EditorCommandKind::OpenScene,
            "Automation open Scene", opened, state.current_state_id, state.current_state_id,
            vespera::kInvalidSceneObjectId, record->asset_id);
        return opened ? ok(std::format("{{\"asset_id\":\"{}\",\"path\":\"{}\"}}",
            automation_json_escape(record->asset_id), automation_json_escape(record->relative_path.generic_string())))
            : fail("Scene open failed; inspect vespera_get_console");
    }

    if (request.command == "vespera_get_project_settings") {
        if (!state.project_loaded) return fail("no Vespera project is open");
        std::string build_includes = "[";
        for (std::size_t i = 0; i < state.project.build_includes.size(); ++i) {
            if (i != 0) build_includes += ',';
            const std::string asset_id = i < state.project.build_include_asset_ids.size()
                ? state.project.build_include_asset_ids[i] : std::string{};
            build_includes += std::format("{{\"asset_id\":\"{}\",\"path\":\"{}\"}}",
                automation_json_escape(asset_id),
                automation_json_escape(state.project.build_includes[i].generic_string()));
        }
        build_includes += ']';
        return ok(std::format(
            "{{\"name\":\"{}\",\"company_name\":\"{}\",\"product_version\":\"{}\",\"package_name\":\"{}\","
            "\"managed_deployment\":\"{}\",\"executable_name\":\"{}\",\"build_output_directory\":\"{}\","
            "\"game_icon\":\"{}\",\"game_icon_asset_id\":\"{}\","
            "\"startup_ui\":\"{}\",\"startup_ui_asset_id\":\"{}\","
            "\"lua_entry\":\"{}\",\"lua_entry_asset_id\":\"{}\",\"development_diagnostics\":{},"
            "\"window_title\":\"{}\",\"window_width\":{},\"window_height\":{},"
            "\"window_resizable\":{},\"relative_mouse\":{},\"escape_quits\":{},\"vsync\":{},\"game_target\":\"{}\","
            "\"build_includes\":{}}}",
            automation_json_escape(state.project.name), automation_json_escape(state.project.company_name),
            automation_json_escape(state.project.product_version), automation_json_escape(state.project.package_name),
            vespera::managed_deployment_mode_name(state.project.managed_deployment),
            automation_json_escape(state.project.executable_name), automation_json_escape(state.project.build_output_directory.generic_string()),
            automation_json_escape(state.project.game_icon.generic_string()), automation_json_escape(state.project.game_icon_asset_id),
            automation_json_escape(state.project.startup_ui.generic_string()), automation_json_escape(state.project.startup_ui_asset_id),
            automation_json_escape(state.project.lua_entry.generic_string()), automation_json_escape(state.project.lua_entry_asset_id),
            state.project.development_diagnostics ? "true" : "false",
            automation_json_escape(state.project.window_title), state.project.window_width, state.project.window_height,
            state.project.window_resizable ? "true" : "false", state.project.relative_mouse ? "true" : "false",
            state.project.escape_quits ? "true" : "false", state.project.vsync ? "true" : "false",
            automation_json_escape(state.project.game_target), build_includes));
    }

    if (request.command == "vespera_set_project_setting") {
        if (editor_is_playing(state)) return fail("project settings are disabled during Play Mode");
        if (!state.project_loaded) return fail("no Vespera project is open");
        const auto* key = automation_arg(request, "key");
        const auto* value = automation_arg(request, "value");
        if (!key || !value) return fail("key and value are required");
        const auto previous_project = state.project;
        bool changed = true;
        if (*key == "window_title") state.project.window_title = *value;
        else if (*key == "window_width" || *key == "window_height") {
            const auto parsed = automation_int(*value);
            if (!parsed) return fail("window size value must be an integer");
            if (*key == "window_width") state.project.window_width = *parsed; else state.project.window_height = *parsed;
        } else if (*key == "window_resizable" || *key == "relative_mouse" || *key == "escape_quits" || *key == "vsync") {
            const auto parsed = automation_bool(*value);
            if (!parsed) return fail("boolean project setting must be true or false");
            if (*key == "window_resizable") state.project.window_resizable = *parsed;
            else if (*key == "relative_mouse") state.project.relative_mouse = *parsed;
            else if (*key == "escape_quits") state.project.escape_quits = *parsed;
            else state.project.vsync = *parsed;
        } else if (*key == "company_name") state.project.company_name = *value;
        else if (*key == "product_version") {
            if (value->empty()) return fail("product_version cannot be empty");
            state.project.product_version = *value;
        } else if (*key == "package_name") state.project.package_name = *value;
        else if (*key == "executable_name") state.project.executable_name = *value;
        else if (*key == "build_output_directory") state.project.build_output_directory = *value;
        else if (*key == "game_icon") { state.project.game_icon = *value; state.project.game_icon_asset_id.clear(); }
        else if (*key == "startup_ui") {
            if (value->empty()) {
                state.project.startup_ui.clear();
                state.project.startup_ui_asset_id.clear();
            } else {
                const vespera::AssetRecord* record = state.asset_catalog.find(*value);
                if (!record) record = state.asset_catalog.find_by_id(*value);
                if (!record) return fail("startup_ui must identify an existing project RML asset by path or stable ID");
                if (record->kind != vespera::AssetKind::RmlDocument) return fail("startup_ui must identify an RML document");
                state.project.startup_ui = record->relative_path;
                state.project.startup_ui_asset_id = record->asset_id;
            }
        }
        else if (*key == "lua_entry") {
            if (value->empty()) {
                state.project.lua_entry.clear();
                state.project.lua_entry_asset_id.clear();
            } else {
                const vespera::AssetRecord* record = state.asset_catalog.find(*value);
                if (!record) record = state.asset_catalog.find_by_id(*value);
                if (!record) return fail("lua_entry must identify an existing project Lua asset by path or stable ID");
                if (record->kind != vespera::AssetKind::LuaScript) return fail("lua_entry must identify a .lua script");
                state.project.lua_entry = record->relative_path;
                state.project.lua_entry_asset_id = record->asset_id;
            }
        }
        else if (*key == "development_diagnostics") {
            const auto parsed = automation_bool(*value);
            if (!parsed) return fail("development_diagnostics must be true or false");
            state.project.development_diagnostics = *parsed;
        }
        else if (*key == "managed_deployment") {
            const auto parsed = vespera::managed_deployment_mode_from_name(*value);
            if (!parsed) return fail("managed_deployment must be framework-dependent or portable");
            state.project.managed_deployment = *parsed;
        } else changed = false;
        if (!changed) return fail("project setting is not allow-listed for automation");
        vespera::ProjectValidationOptions validation_options;
        validation_options.require_managed_source = false;
        const auto issues = vespera::validate_vespera_project(state.project, validation_options);
        for (const auto& issue : issues) {
            if (issue.severity == vespera::ProjectValidationSeverity::Error) {
                state.project = previous_project;
                return fail("project setting produced validation error: " + issue.message);
            }
        }
        const bool persist = automation_arg(request, "save")
            ? automation_bool(*automation_arg(request, "save")).value_or(true) : true;
        if (persist) {
            const auto saved = vespera::save_vespera_project(state.project, state.project_path);
            if (!saved) {
                state.project = previous_project;
                return fail(saved.message);
            }
        }
        append_command_audit(state, vespera::editor::EditorCommandKind::SetProjectSetting,
            "Automation set project setting " + *key, true, state.current_state_id, state.current_state_id);
        return ok(std::format("{{\"key\":\"{}\",\"saved\":{}}}", automation_json_escape(*key), persist ? "true" : "false"));
    }

    if (request.command == "vespera_set_build_include") {
        if (editor_is_playing(state)) return fail("build-root authoring is disabled during Play Mode");
        if (!state.project_loaded) return fail("no Vespera project is open");
        const auto* raw_operation = automation_arg(request, "operation");
        if (!raw_operation || (*raw_operation != "add" && *raw_operation != "remove")) {
            return fail("operation must be add or remove");
        }
        const auto previous_project = state.project;
        const auto* record = automation_asset_record(state, request);
        if (!record) return fail("provide a valid asset_id or project-relative path");
        if (state.project.build_include_asset_ids.size() < state.project.build_includes.size()) {
            state.project.build_include_asset_ids.resize(state.project.build_includes.size());
        }
        bool changed = false;
        if (*raw_operation == "add") {
            for (std::size_t i = 0; i < state.project.build_includes.size(); ++i) {
                if ((!record->asset_id.empty() && state.project.build_include_asset_ids[i] == record->asset_id)
                    || state.project.build_includes[i].lexically_normal() == record->relative_path.lexically_normal()) {
                    return ok(std::format("{{\"operation\":\"add\",\"changed\":false,\"asset_id\":\"{}\",\"path\":\"{}\"}}",
                        automation_json_escape(record->asset_id), automation_json_escape(record->relative_path.generic_string())));
                }
            }
            state.project.build_includes.push_back(record->relative_path.lexically_normal());
            state.project.build_include_asset_ids.push_back(record->asset_id);
            changed = true;
        } else {
            for (std::size_t i = state.project.build_includes.size(); i-- > 0;) {
                const bool matches = (!record->asset_id.empty() && state.project.build_include_asset_ids[i] == record->asset_id)
                    || state.project.build_includes[i].lexically_normal() == record->relative_path.lexically_normal();
                if (!matches) continue;
                state.project.build_includes.erase(state.project.build_includes.begin() + static_cast<std::ptrdiff_t>(i));
                state.project.build_include_asset_ids.erase(state.project.build_include_asset_ids.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
            }
            if (!changed) {
                return ok(std::format("{{\"operation\":\"remove\",\"changed\":false,\"asset_id\":\"{}\",\"path\":\"{}\"}}",
                    automation_json_escape(record->asset_id), automation_json_escape(record->relative_path.generic_string())));
            }
        }
        const bool persist = automation_arg(request, "save")
            ? automation_bool(*automation_arg(request, "save")).value_or(true) : true;
        if (persist) {
            const auto saved = vespera::save_vespera_project(state.project, state.project_path);
            if (!saved) {
                state.project = previous_project;
                return fail("build include update could not be saved: " + saved.message);
            }
        }
        append_command_audit(state, vespera::editor::EditorCommandKind::SetProjectSetting,
            "Automation " + *raw_operation + " build include", true, state.current_state_id, state.current_state_id,
            vespera::kInvalidSceneObjectId, record->asset_id);
        return ok(std::format("{{\"operation\":\"{}\",\"changed\":true,\"saved\":{},\"asset_id\":\"{}\",\"path\":\"{}\"}}",
            automation_json_escape(*raw_operation), persist ? "true" : "false",
            automation_json_escape(record->asset_id), automation_json_escape(record->relative_path.generic_string())));
    }

    if (request.command == "vespera_check_asset_delete") {
        if (!state.project_loaded) return fail("no Vespera project is open");
        const auto* record = automation_asset_record(state, request);
        if (!record) return fail("provide a valid asset_id or project-relative path");
        const auto preflight = vespera::preflight_delete_project_asset(state.asset_catalog, state.project, record->asset_id);
        std::string blockers = "[";
        for (std::size_t i = 0; i < preflight.blockers.size(); ++i) {
            if (i) blockers += ',';
            blockers += std::format("\"{}\"", automation_json_escape(preflight.blockers[i]));
        }
        blockers += "]";
        return ok(std::format("{{\"allowed\":{},\"asset_id\":\"{}\",\"path\":\"{}\",\"blockers\":{}}}",
            preflight.allowed ? "true" : "false", automation_json_escape(record->asset_id),
            automation_json_escape(record->relative_path.generic_string()), blockers));
    }

    if (request.command == "vespera_move_asset") {
        if (editor_is_playing(state)) return fail("asset authoring is disabled during Play Mode");
        const auto* record = automation_asset_record(state, request);
        const auto* destination = automation_arg(request, "destination");
        if (!record) return fail("provide a valid asset_id or project-relative path");
        if (!destination || destination->empty()) return fail("destination is required");
        const auto old_absolute = record->absolute_path;
        const auto moved = vespera::move_project_asset(state.asset_catalog, record->asset_id, *destination);
        if (!moved) return fail(moved.message);
        if (!state.scene_path.empty() && old_absolute.lexically_normal() == state.scene_path.lexically_normal()) {
            state.scene_path = (state.assets_root / moved.new_path).lexically_normal();
        }
        refresh_asset_catalog(state, false, false);
        auto repaired = vespera::repair_stable_asset_fallbacks(state.project, state.asset_catalog);
        if (repaired.project_references_repaired != 0) {
            const auto saved = vespera::save_vespera_project(state.project, state.project_path);
            if (!saved) return fail("asset moved, but project fallback repair save failed: " + saved.message);
        }
        if (repaired.descriptor_files_rewritten != 0) refresh_asset_catalog(state, false, false);
        append_command_audit(state, vespera::editor::EditorCommandKind::MoveAsset,
            "Automation move asset", true, state.current_state_id, state.current_state_id,
            vespera::kInvalidSceneObjectId, moved.asset_id);
        return ok(std::format("{{\"asset_id\":\"{}\",\"old_path\":\"{}\",\"new_path\":\"{}\",\"repaired_references\":{},\"rml_references_rewritten\":{},\"rml_files_rewritten\":{}}}",
            automation_json_escape(moved.asset_id), automation_json_escape(moved.old_path.generic_string()),
            automation_json_escape(moved.new_path.generic_string()), repaired.total_references_repaired(),
            moved.rml_references_rewritten, moved.rml_files_rewritten));
    }

    if (request.command == "vespera_repair_asset_fallbacks") {
        if (editor_is_playing(state)) return fail("asset authoring is disabled during Play Mode");
        if (!state.project_loaded) return fail("no Vespera project is open");
        auto report = vespera::repair_stable_asset_fallbacks(state.project, state.asset_catalog);
        const bool persist = automation_arg(request, "save_project")
            ? automation_bool(*automation_arg(request, "save_project")).value_or(true) : true;
        if (persist && report.project_references_repaired != 0) {
            const auto saved = vespera::save_vespera_project(state.project, state.project_path);
            if (!saved) return fail(saved.message);
        }
        if (report.descriptor_files_rewritten != 0) refresh_asset_catalog(state, false, false);
        append_command_audit(state, vespera::editor::EditorCommandKind::RepairAssetFallbacks,
            "Automation repair stable fallbacks", report.warnings.empty(), state.current_state_id, state.current_state_id);
        std::string warning_json = "[";
        for (std::size_t i = 0; i < report.warnings.size(); ++i) {
            if (i != 0) warning_json += ',';
            warning_json += "\"" + automation_json_escape(report.warnings[i]) + "\"";
        }
        warning_json += "]";
        return ok(std::format("{{\"project_references\":{},\"descriptor_references\":{},\"files_rewritten\":{},\"warnings\":{},\"warning_messages\":{}}}",
            report.project_references_repaired, report.descriptor_references_repaired,
            report.descriptor_files_rewritten, report.warnings.size(), warning_json));
    }

    if (request.command == "vespera_create_ui_document") {
        if (editor_is_playing(state)) return fail("UI asset authoring is disabled during Play Mode");
        if (state.assets_root.empty()) return fail("no project Assets root is open");
        const auto* raw_path = automation_arg(request, "path");
        if (!raw_path || raw_path->empty()) return fail("path is required");
        const std::filesystem::path relative = std::filesystem::path(*raw_path).lexically_normal();
        if (relative.is_absolute() || relative.extension() != ".slui" || relative.generic_string().starts_with("..")) {
            return fail("path must be a project-relative .slui path inside Assets");
        }
        const auto absolute = (state.assets_root / relative).lexically_normal();
        std::error_code ec;
        if (std::filesystem::exists(absolute, ec)) return fail("UI document path already exists");
        std::filesystem::create_directories(absolute.parent_path(), ec);
        if (ec) return fail("could not create UI document folder: " + ec.message());
        vespera::UiDocument document;
        auto& canvas = document.create_node(vespera::UiNodeType::Canvas,
            automation_arg(request, "name") && !automation_arg(request, "name")->empty()
                ? *automation_arg(request, "name") : "Canvas");
        canvas.rect.anchor_max = {1.0f, 1.0f};
        const auto saved = vespera::save_ui_document(document, absolute);
        if (!saved) return fail(saved.message);
        refresh_asset_catalog(state, false, true);
        const auto* record = state.asset_catalog.find(relative.generic_string());
        append_command_audit(state, vespera::editor::EditorCommandKind::CreateUiDocument,
            "Automation create UI document", record != nullptr, state.current_state_id, state.current_state_id,
            vespera::kInvalidSceneObjectId, record ? record->asset_id : std::string{});
        if (!record) return fail("UI document was written but catalog refresh did not register it");
        return ok(std::format("{{\"asset_id\":\"{}\",\"path\":\"{}\",\"canvas_id\":{}}}",
            automation_json_escape(record->asset_id), automation_json_escape(record->relative_path.generic_string()), canvas.id));
    }

    if (request.command == "vespera_get_ui_document") {
        const auto* record = automation_asset_record(state, request);
        if (!record) return fail("provide a valid UI asset_id or project-relative path");
        if (record->kind != vespera::AssetKind::UiDocument) return fail("asset is not a UI Document");
        vespera::UiDocument document;
        const auto loaded = vespera::load_ui_document(document, record->absolute_path);
        if (!loaded) return fail(loaded.message);
        const auto layout = vespera::resolve_ui_layout(document, 1280.0f, 720.0f);
        std::string nodes = "[";
        bool first = true;
        for (const auto& node : document.nodes()) {
            if (!first) nodes += ',';
            first = false;
            const auto* resolved = layout.find(node.id);
            nodes += std::format("{{\"id\":{},\"parent_id\":{},\"type\":\"{}\",\"name\":\"{}\",\"enabled\":{},\"z_order\":{}",
                node.id, node.parent_id, vespera::ui_node_type_name(node.type), automation_json_escape(node.name),
                node.enabled ? "true" : "false", node.z_order);
            if (resolved) nodes += std::format(",\"rect\":[{},{},{},{}]", resolved->rect.x, resolved->rect.y, resolved->rect.width, resolved->rect.height);
            if (node.type == vespera::UiNodeType::Text || node.type == vespera::UiNodeType::Button || node.type == vespera::UiNodeType::TextInput)
                nodes += std::format(",\"text\":\"{}\"", automation_json_escape(node.text.text));
            if (node.type == vespera::UiNodeType::ProgressBar)
                nodes += std::format(",\"value\":{},\"minimum\":{},\"maximum\":{}", node.progress.value, node.progress.minimum, node.progress.maximum);
            nodes += std::format(",\"opacity\":{},\"corner_radius\":{},\"border_width\":{}",
                node.surface.opacity, node.surface.corner_radius, node.surface.border_width);
            if (node.type == vespera::UiNodeType::Text || node.type == vespera::UiNodeType::Button || node.type == vespera::UiNodeType::TextInput
                || node.type == vespera::UiNodeType::ProgressBar || node.type == vespera::UiNodeType::Modal || node.type == vespera::UiNodeType::Tooltip)
                nodes += std::format(",\"font_family\":\"{}\",\"font_weight\":{},\"wrap\":{}", automation_json_escape(node.text.font_family), node.text.font_weight, node.text.wrap ? "true" : "false");
            if (node.layout.mode != vespera::UiLayoutMode::None)
                nodes += std::format(",\"layout_mode\":{}", static_cast<int>(node.layout.mode));
            nodes += "}";
        }
        nodes += "]";
        return ok(std::format("{{\"asset_id\":\"{}\",\"path\":\"{}\",\"node_count\":{},\"layout_warnings\":{},\"nodes\":{}}}",
            automation_json_escape(record->asset_id), automation_json_escape(record->relative_path.generic_string()),
            document.nodes().size(), layout.warnings.size(), nodes));
    }

    if (request.command == "vespera_add_ui_node") {
        if (editor_is_playing(state)) return fail("UI asset authoring is disabled during Play Mode");
        const auto* record = automation_asset_record(state, request);
        if (!record || record->kind != vespera::AssetKind::UiDocument) return fail("provide a valid UI Document asset");
        const auto* type_text = automation_arg(request, "type");
        if (!type_text) return fail("type is required");
        const auto type = automation_ui_node_type(*type_text);
        if (!type) return fail("type is not a supported Vespera UI node type");
        vespera::UiDocument document;
        const auto loaded = vespera::load_ui_document(document, record->absolute_path);
        if (!loaded) return fail(loaded.message);
        auto& node = document.create_node(*type, automation_arg(request, "name") ? *automation_arg(request, "name") : std::string{});
        if (const auto* raw_parent = automation_arg(request, "parent_id")) {
            const auto parent = automation_u64(*raw_parent);
            if (!parent) return fail("parent_id must be an integer");
            std::string reparent_error;
            if (!document.reparent(node.id, *parent, &reparent_error)) return fail(reparent_error);
        }
        const auto node_id = node.id;
        const std::string asset_id = record->asset_id;
        const auto saved = vespera::save_ui_document(document, record->absolute_path);
        if (!saved) return fail(saved.message);
        refresh_asset_catalog(state, false, true);
        append_command_audit(state, vespera::editor::EditorCommandKind::AddUiNode,
            "Automation add UI node", true, state.current_state_id, state.current_state_id,
            vespera::kInvalidSceneObjectId, asset_id);
        return ok(std::format("{{\"asset_id\":\"{}\",\"node_id\":{},\"type\":\"{}\"}}",
            automation_json_escape(asset_id), node_id, vespera::ui_node_type_name(*type)));
    }

    if (request.command == "vespera_set_ui_node") {
        if (editor_is_playing(state)) return fail("UI asset authoring is disabled during Play Mode");
        const auto* record = automation_asset_record(state, request);
        if (!record || record->kind != vespera::AssetKind::UiDocument) return fail("provide a valid UI Document asset");
        const auto* raw_node = automation_arg(request, "node_id");
        const auto node_id = raw_node ? automation_u64(*raw_node) : std::nullopt;
        if (!node_id) return fail("node_id is required");
        vespera::UiDocument document;
        const auto loaded = vespera::load_ui_document(document, record->absolute_path);
        if (!loaded) return fail(loaded.message);
        auto* node = document.find(*node_id);
        if (!node) return fail("UI node does not exist");
        if (const auto* value = automation_arg(request, "name")) node->name = *value;
        if (const auto* value = automation_arg(request, "enabled")) {
            const auto parsed = automation_bool(*value); if (!parsed) return fail("enabled must be true or false"); node->enabled = *parsed;
        }
        if (const auto* value = automation_arg(request, "z_order")) {
            const auto parsed = automation_int(*value); if (!parsed) return fail("z_order must be an integer"); node->z_order = *parsed;
        }
        if (const auto* value = automation_arg(request, "parent_id")) {
            const auto parsed = automation_u64(*value); if (!parsed) return fail("parent_id must be an integer");
            std::string error; if (!document.reparent(node->id, *parsed, &error)) return fail(error);
            node = document.find(*node_id);
        }
        const auto set_vec2 = [&](std::string_view key, vespera::UiVec2& target) -> std::optional<std::string> {
            const auto* raw = automation_arg(request, key); if (!raw) return std::nullopt;
            const auto parsed = automation_vec2(*raw); if (!parsed) return std::string(key) + " must contain two numbers";
            target = {parsed->x, parsed->z}; return std::nullopt;
        };
        if (const auto error = set_vec2("anchor_min", node->rect.anchor_min)) return fail(*error);
        if (const auto error = set_vec2("anchor_max", node->rect.anchor_max)) return fail(*error);
        if (const auto error = set_vec2("offset_min", node->rect.offset_min)) return fail(*error);
        if (const auto error = set_vec2("offset_max", node->rect.offset_max)) return fail(*error);
        if (const auto* value = automation_arg(request, "text")) node->text.text = *value;
        if (const auto* value = automation_arg(request, "font_size")) {
            const auto parsed = automation_float(*value); if (!parsed || *parsed < 1.0f) return fail("font_size must be >= 1"); node->text.font_size = *parsed;
        }
        if (const auto* value = automation_arg(request, "font_family")) {
            if (value->empty()) return fail("font_family must not be empty");
            node->text.font_family = *value;
        }
        if (const auto* value = automation_arg(request, "font_weight")) {
            const auto parsed = automation_int(*value); if (!parsed || *parsed < 100 || *parsed > 900) return fail("font_weight must be between 100 and 900");
            node->text.font_weight = static_cast<std::uint16_t>(*parsed);
        }
        if (const auto* value = automation_arg(request, "italic")) {
            const auto parsed = automation_bool(*value); if (!parsed) return fail("italic must be true or false"); node->text.italic = *parsed;
        }
        if (const auto* value = automation_arg(request, "wrap")) {
            const auto parsed = automation_bool(*value); if (!parsed) return fail("wrap must be true or false"); node->text.wrap = *parsed;
        }
        if (const auto* value = automation_arg(request, "line_spacing")) {
            const auto parsed = automation_float(*value); if (!parsed || *parsed < 0.5f || *parsed > 4.0f) return fail("line_spacing must be between 0.5 and 4.0"); node->text.line_spacing = *parsed;
        }
        if (const auto* value = automation_arg(request, "horizontal_alignment")) {
            const auto parsed = automation_ui_horizontal_alignment(*value); if (!parsed) return fail("horizontal_alignment must be left, center or right"); node->text.horizontal_alignment = *parsed;
        }
        if (const auto* value = automation_arg(request, "vertical_alignment")) {
            const auto parsed = automation_ui_vertical_alignment(*value); if (!parsed) return fail("vertical_alignment must be top, middle or bottom"); node->text.vertical_alignment = *parsed;
        }
        if (automation_arg(request, "image_asset_id") || automation_arg(request, "image_path")) {
            const std::string id = automation_arg(request, "image_asset_id") ? *automation_arg(request, "image_asset_id") : std::string{};
            const std::string path = automation_arg(request, "image_path") ? *automation_arg(request, "image_path") : std::string{};
            vespera::AssetReference image{id, path};
            if (!image.empty()) {
                const auto resolved = state.asset_catalog.resolve_reference(image);
                if (!resolved || resolved.record->kind != vespera::AssetKind::Texture) return fail("UI image reference must resolve to a Texture asset");
                image = {resolved.record->asset_id, resolved.record->relative_path};
            }
            node->visual.image = std::move(image);
        }
        if (automation_arg(request, "font_asset_id") || automation_arg(request, "font_path")) {
            const std::string id = automation_arg(request, "font_asset_id") ? *automation_arg(request, "font_asset_id") : std::string{};
            const std::string path = automation_arg(request, "font_path") ? *automation_arg(request, "font_path") : std::string{};
            vespera::AssetReference font{id, path};
            if (!font.empty()) {
                const auto resolved = state.asset_catalog.resolve_reference(font);
                if (!resolved || resolved.record->kind != vespera::AssetKind::Font) return fail("UI font reference must resolve to a Font asset");
                font = {resolved.record->asset_id, resolved.record->relative_path};
            }
            node->text.font = std::move(font);
        }
        if (const auto* value = automation_arg(request, "opacity")) {
            const auto parsed = automation_float(*value); if (!parsed || *parsed < 0.0f || *parsed > 1.0f) return fail("opacity must be between 0 and 1"); node->surface.opacity = *parsed;
        }
        if (const auto* value = automation_arg(request, "corner_radius")) {
            const auto parsed = automation_float(*value); if (!parsed || *parsed < 0.0f) return fail("corner_radius must be >= 0"); node->surface.corner_radius = *parsed;
        }
        if (const auto* value = automation_arg(request, "border_width")) {
            const auto parsed = automation_float(*value); if (!parsed || *parsed < 0.0f) return fail("border_width must be >= 0"); node->surface.border_width = *parsed;
        }
        if (const auto error = set_vec2("shadow_offset", node->surface.shadow_offset)) return fail(*error);
        if (const auto* value = automation_arg(request, "shadow_softness")) {
            const auto parsed = automation_float(*value); if (!parsed || *parsed < 0.0f || *parsed > 32.0f) return fail("shadow_softness must be between 0 and 32"); node->surface.shadow_softness = *parsed;
        }
        if (const auto error = set_vec2("text_shadow_offset", node->text.shadow_offset)) return fail(*error);
        if (const auto* value = automation_arg(request, "image_fit")) {
            const auto parsed = automation_ui_image_fit(*value); if (!parsed) return fail("image_fit must be stretch, contain or cover"); node->surface.image_fit = *parsed;
        }
        if (const auto* value = automation_arg(request, "nine_slice")) {
            const auto parsed = automation_color4(*value); if (!parsed) return fail("nine_slice must contain four numbers");
            if ((*parsed)[0] < 0 || (*parsed)[1] < 0 || (*parsed)[2] < 0 || (*parsed)[3] < 0) return fail("nine_slice values must be >= 0");
            node->surface.nine_slice = {(*parsed)[0],(*parsed)[1],(*parsed)[2],(*parsed)[3]};
        }
        if (const auto* value = automation_arg(request, "interactable")) {
            const auto parsed = automation_bool(*value); if (!parsed) return fail("interactable must be true or false"); node->button.interactable = *parsed;
        }
        if (const auto* value = automation_arg(request, "progress_value")) {
            const auto parsed = automation_float(*value); if (!parsed) return fail("progress_value must be a number"); node->progress.value = *parsed;
        }
        if (const auto* value = automation_arg(request, "progress_min")) {
            const auto parsed = automation_float(*value); if (!parsed) return fail("progress_min must be a number"); node->progress.minimum = *parsed;
        }
        if (const auto* value = automation_arg(request, "progress_max")) {
            const auto parsed = automation_float(*value); if (!parsed) return fail("progress_max must be a number"); node->progress.maximum = *parsed;
        }
        if (node->progress.maximum < node->progress.minimum) std::swap(node->progress.minimum, node->progress.maximum);
        node->progress.value = std::clamp(node->progress.value, node->progress.minimum, node->progress.maximum);
        if (const auto* value = automation_arg(request, "layout_mode")) {
            const auto parsed = automation_ui_layout_mode(*value); if (!parsed) return fail("layout_mode must be none, horizontal, vertical or grid"); node->layout.mode = *parsed;
        }
        if (const auto error = set_vec2("layout_spacing", node->layout.spacing)) return fail(*error);
        if (const auto error = set_vec2("cell_size", node->layout.cell_size)) return fail(*error);
        if (const auto* value = automation_arg(request, "columns")) {
            const auto parsed = automation_int(*value); if (!parsed || *parsed < 1) return fail("columns must be >= 1"); node->layout.columns = static_cast<std::uint32_t>(*parsed);
        }
        if (const auto* value = automation_arg(request, "clip_children")) {
            const auto parsed = automation_bool(*value); if (!parsed) return fail("clip_children must be true or false"); node->layout.clip_children = *parsed;
        }
        if (const auto error = set_vec2("scroll_offset", node->scroll.offset)) return fail(*error);
        if (const auto* value = automation_arg(request, "active_tab")) {
            const auto parsed = automation_int(*value); if (!parsed || *parsed < 0) return fail("active_tab must be >= 0"); node->tabs.active_index = static_cast<std::uint32_t>(*parsed);
        }
        if (const auto* value = automation_arg(request, "placeholder")) node->input.placeholder = *value;
        if (const auto* value = automation_arg(request, "max_length")) {
            const auto parsed = automation_int(*value); if (!parsed || *parsed < 1) return fail("max_length must be >= 1"); node->input.max_length = static_cast<std::uint32_t>(*parsed);
        }
        if (const auto* value = automation_arg(request, "read_only")) {
            const auto parsed = automation_bool(*value); if (!parsed) return fail("read_only must be true or false"); node->input.read_only = *parsed;
        }
        const auto set_color = [&](std::string_view key, std::array<float,4>& target) -> std::optional<std::string> {
            const auto* raw = automation_arg(request, key); if (!raw) return std::nullopt;
            const auto parsed = automation_color4(*raw); if (!parsed) return std::string(key) + " must contain four numbers";
            target = *parsed; return std::nullopt;
        };
        if (const auto error = set_color("color", node->visual.color)) return fail(*error);
        if (const auto error = set_color("text_color", node->text.color)) return fail(*error);
        if (const auto error = set_color("border_color", node->surface.border_color)) return fail(*error);
        if (const auto error = set_color("shadow_color", node->surface.shadow_color)) return fail(*error);
        if (const auto error = set_color("text_shadow_color", node->text.shadow_color)) return fail(*error);
        if (const auto error = set_color("button_normal_color", node->button.normal_color)) return fail(*error);
        if (const auto error = set_color("button_hovered_color", node->button.hovered_color)) return fail(*error);
        if (const auto error = set_color("button_pressed_color", node->button.pressed_color)) return fail(*error);
        if (const auto error = set_color("button_disabled_color", node->button.disabled_color)) return fail(*error);
        if (const auto error = set_color("progress_fill_color", node->progress.fill_color)) return fail(*error);
        if (const auto error = set_color("progress_background_color", node->progress.background_color)) return fail(*error);
        const std::string asset_id = record->asset_id;
        const auto saved = vespera::save_ui_document(document, record->absolute_path);
        if (!saved) return fail(saved.message);
        refresh_asset_catalog(state, false, true);
        append_command_audit(state, vespera::editor::EditorCommandKind::SetUiNode,
            "Automation set UI node", true, state.current_state_id, state.current_state_id,
            vespera::kInvalidSceneObjectId, asset_id);
        return ok(std::format("{{\"asset_id\":\"{}\",\"node_id\":{}}}", automation_json_escape(asset_id), *node_id));
    }

    if (request.command == "vespera_delete_ui_node") {
        if (editor_is_playing(state)) return fail("UI asset authoring is disabled during Play Mode");
        const auto* record = automation_asset_record(state, request);
        if (!record || record->kind != vespera::AssetKind::UiDocument) return fail("provide a valid UI Document asset");
        const auto* raw_node = automation_arg(request, "node_id");
        const auto node_id = raw_node ? automation_u64(*raw_node) : std::nullopt;
        if (!node_id) return fail("node_id is required");
        vespera::UiDocument document;
        const auto loaded = vespera::load_ui_document(document, record->absolute_path);
        if (!loaded) return fail(loaded.message);
        const auto* node = document.find(*node_id);
        if (!node) return fail("UI node does not exist");
        if (node->type == vespera::UiNodeType::Canvas) return fail("Canvas deletion is blocked; a UI document must retain a Canvas root");
        if (!document.destroy_node(*node_id)) return fail("UI node could not be deleted");
        const std::string asset_id = record->asset_id;
        const auto saved = vespera::save_ui_document(document, record->absolute_path);
        if (!saved) return fail(saved.message);
        refresh_asset_catalog(state, false, true);
        append_command_audit(state, vespera::editor::EditorCommandKind::DeleteUiNode,
            "Automation delete UI node", true, state.current_state_id, state.current_state_id,
            vespera::kInvalidSceneObjectId, asset_id);
        return ok(std::format("{{\"asset_id\":\"{}\",\"deleted_node_id\":{},\"remaining_nodes\":{}}}",
            automation_json_escape(asset_id), *node_id, document.nodes().size()));
    }

    if (request.command == "vespera_reparent_ui_node") {
        if (editor_is_playing(state)) return fail("UI asset authoring is disabled during Play Mode");
        const auto* record = automation_asset_record(state, request);
        if (!record || record->kind != vespera::AssetKind::UiDocument) return fail("provide a valid UI Document asset");
        const auto* raw_node = automation_arg(request, "node_id");
        const auto* raw_parent = automation_arg(request, "parent_id");
        const auto node_id = raw_node ? automation_u64(*raw_node) : std::nullopt;
        const auto parent_id = raw_parent ? automation_u64(*raw_parent) : std::nullopt;
        if (!node_id || !parent_id) return fail("node_id and parent_id are required integers; use parent_id=0 to unparent");
        vespera::UiDocument document;
        const auto loaded = vespera::load_ui_document(document, record->absolute_path);
        if (!loaded) return fail(loaded.message);
        std::string reparent_error;
        if (!document.reparent(*node_id, *parent_id, &reparent_error)) return fail(reparent_error);
        const std::string asset_id = record->asset_id;
        const auto saved = vespera::save_ui_document(document, record->absolute_path);
        if (!saved) return fail(saved.message);
        refresh_asset_catalog(state, false, true);
        append_command_audit(state, vespera::editor::EditorCommandKind::ReparentUiNode,
            "Automation reparent UI node", true, state.current_state_id, state.current_state_id,
            vespera::kInvalidSceneObjectId, asset_id);
        return ok(std::format("{{\"asset_id\":\"{}\",\"node_id\":{},\"parent_id\":{}}}",
            automation_json_escape(asset_id), *node_id, *parent_id));
    }

    if (request.command == "vespera_validate_ui_document") {
        const auto* record = automation_asset_record(state, request);
        if (!record || record->kind != vespera::AssetKind::UiDocument) return fail("provide a valid UI Document asset");
        vespera::UiDocument document;
        const auto loaded = vespera::load_ui_document(document, record->absolute_path);
        if (!loaded) return fail(loaded.message);
        std::size_t canvas_roots = 0;
        std::size_t focusable = 0;
        for (const auto& node : document.nodes()) {
            if (node.type == vespera::UiNodeType::Canvas && node.parent_id == vespera::kInvalidUiNodeId) ++canvas_roots;
            if ((node.type == vespera::UiNodeType::Button && node.button.interactable) ||
                (node.type == vespera::UiNodeType::TextInput && !node.input.read_only)) ++focusable;
        }
        const auto layout720 = vespera::resolve_ui_layout(document, 1280.0f, 720.0f);
        const auto layout1080 = vespera::resolve_ui_layout(document, 1920.0f, 1080.0f);
        const std::size_t warnings = layout720.warnings.size() + layout1080.warnings.size();
        return ok(std::format("{{\"asset_id\":\"{}\",\"nodes\":{},\"canvas_roots\":{},\"focusable\":{},\"warnings_720p\":{},\"warnings_1080p\":{},\"valid\":{}}}",
            automation_json_escape(record->asset_id), document.nodes().size(), canvas_roots, focusable,
            layout720.warnings.size(), layout1080.warnings.size(), (canvas_roots == 1 && warnings == 0) ? "true" : "false"));
    }

    if (request.command == "vespera_get_ui_layout") {
        const auto* record = automation_asset_record(state, request);
        if (!record || record->kind != vespera::AssetKind::UiDocument) return fail("provide a valid UI Document asset");
        int width = 1280;
        int height = 720;
        if (const auto* raw = automation_arg(request, "width")) {
            const auto parsed = automation_int(*raw); if (!parsed || *parsed < 1 || *parsed > 16384) return fail("width must be 1..16384"); width = *parsed;
        }
        if (const auto* raw = automation_arg(request, "height")) {
            const auto parsed = automation_int(*raw); if (!parsed || *parsed < 1 || *parsed > 16384) return fail("height must be 1..16384"); height = *parsed;
        }
        vespera::UiDocument document;
        const auto loaded = vespera::load_ui_document(document, record->absolute_path);
        if (!loaded) return fail(loaded.message);
        const auto layout = vespera::resolve_ui_layout(document, static_cast<float>(width), static_cast<float>(height));
        std::string nodes = "[";
        bool first = true;
        for (const auto& resolved : layout.nodes) {
            if (!first) nodes += ',';
            first = false;
            nodes += std::format("{{\"id\":{},\"type\":\"{}\",\"enabled\":{},\"rect\":[{},{},{},{}],\"clip_enabled\":{},\"clip\":[{},{},{},{}]}}",
                resolved.id, automation_json_escape(vespera::ui_node_type_name(resolved.type)), resolved.enabled ? "true" : "false",
                resolved.rect.x, resolved.rect.y, resolved.rect.width, resolved.rect.height,
                resolved.clip_enabled ? "true" : "false", resolved.clip_rect.x, resolved.clip_rect.y,
                resolved.clip_rect.width, resolved.clip_rect.height);
        }
        nodes += "]";
        return ok(std::format("{{\"asset_id\":\"{}\",\"viewport\":[{},{}],\"canvas_scale\":{},\"warnings\":{},\"nodes\":{}}}",
            automation_json_escape(record->asset_id), width, height, layout.canvas_scale, layout.warnings.size(), nodes));
    }

    if (request.command == "vespera_set_entity_metadata") {
        if (editor_is_playing(state)) return fail("scene authoring is disabled during Play Mode");
        const auto* raw_id = automation_arg(request, "entity_id");
        const auto id = raw_id ? automation_u64(*raw_id) : std::nullopt;
        if (!id) return fail("entity_id is required and must be an unsigned integer");
        if (!state.scene.find_entity(*id)) return fail("entity_id does not exist in the edit scene");
        const auto* name = automation_arg(request, "name");
        const auto* tag = automation_arg(request, "tag");
        const auto* layer = automation_arg(request, "layer");
        const auto* raw_enabled = automation_arg(request, "enabled");
        const auto enabled = raw_enabled ? automation_bool(*raw_enabled) : std::optional<bool>{};
        if (raw_enabled && !enabled) return fail("enabled must be true or false");
        if (!name && !tag && !layer && !raw_enabled) return fail("supply at least one of name, tag, layer or enabled");
        const bool changed = execute_editor_command(state, vespera::editor::EditorCommandKind::SetEntityMetadata,
            "Automation set entity metadata", [&]() {
                auto* entity = state.scene.find_entity(*id);
                if (!entity) return false;
                if (name) entity->name = *name;
                if (tag) entity->tag = *tag;
                if (layer) entity->layer = *layer;
                if (enabled) entity->enabled = *enabled;
                return true;
            });
        if (!changed) return fail("no valid metadata field was supplied or the edit failed");
        if (!state.command_log.empty()) state.command_log.back().target_entity_id = *id;
        const auto* entity = state.scene.find_entity(*id);
        return ok(std::format("{{\"entity_id\":{},\"name\":\"{}\",\"tag\":\"{}\",\"layer\":\"{}\",\"enabled\":{}}}",
            *id, automation_json_escape(entity->name), automation_json_escape(entity->tag), automation_json_escape(entity->layer), entity->enabled ? "true" : "false"));
    }

    if (request.command == "vespera_reorder_entity") {
        if (editor_is_playing(state)) return fail("scene authoring is disabled during Play Mode");
        const auto id = automation_arg(request, "entity_id") ? automation_u64(*automation_arg(request, "entity_id")) : std::nullopt;
        const auto before = automation_arg(request, "before_entity_id") ? automation_u64(*automation_arg(request, "before_entity_id")) : std::nullopt;
        if (!id || !before) return fail("entity_id and before_entity_id are required unsigned integers");
        if (!state.scene.find_entity(*id) || !state.scene.find_entity(*before)) return fail("entity_id or before_entity_id does not exist");
        if (!command_reorder_entity(state, *id, *before)) return fail("reorder command failed or would not change order");
        if (!state.command_log.empty()) state.command_log.back().target_entity_id = *id;
        return ok(std::format("{{\"entity_id\":{},\"before_entity_id\":{}}}", *id, *before));
    }

    if (request.command == "vespera_get_asset_dependencies") {
        const auto* record = automation_asset_record(state, request);
        if (!record) return fail("provide a valid asset_id or project-relative asset path");
        std::string dependencies = "["; bool first = true;
        for (const auto* dep : state.asset_catalog.dependencies_of(record->asset_id)) {
            if (!dep) continue;
            if (!first) dependencies += ',';
            first = false;
            dependencies += std::format("{{\"target_asset_id\":\"{}\",\"reference\":\"{}\",\"reason\":\"{}\",\"resolved\":{},\"stable\":{},\"stale_fallback\":{}}}",
                automation_json_escape(dep->target_asset_id), automation_json_escape(dep->reference), automation_json_escape(dep->reason), dep->resolved ? "true" : "false", dep->requested_asset_id.empty() ? "false" : "true", dep->stale_fallback_path ? "true" : "false");
        }
        dependencies += "]";
        std::string dependents = "["; first = true;
        for (const auto* dep : state.asset_catalog.dependents_of(record->asset_id)) {
            if (!dep) continue;
            if (!first) dependents += ',';
            first = false;
            dependents += std::format("{{\"source_asset_id\":\"{}\",\"reference\":\"{}\",\"reason\":\"{}\",\"resolved\":{},\"stable\":{},\"stale_fallback\":{}}}",
                automation_json_escape(dep->source_asset_id), automation_json_escape(dep->reference), automation_json_escape(dep->reason), dep->resolved ? "true" : "false", dep->requested_asset_id.empty() ? "false" : "true", dep->stale_fallback_path ? "true" : "false");
        }
        dependents += "]";
        return ok(std::format("{{\"asset_id\":\"{}\",\"path\":\"{}\",\"dependencies\":{},\"dependents\":{}}}", automation_json_escape(record->asset_id), automation_json_escape(record->relative_path.generic_string()), dependencies, dependents));
    }

    if (request.command == "vespera_check_project_integrity") {
        if (!state.project_loaded) return fail("no Vespera project is open");
        const auto project_issues = vespera::validate_vespera_project(state.project);
        std::size_t project_errors = 0, project_warnings = 0;
        for (const auto& issue : project_issues) { if (issue.severity == vespera::ProjectValidationSeverity::Error) ++project_errors; else ++project_warnings; }
        const auto scene_issues = vespera::validate_scene(state.scene);
        std::size_t scene_errors = 0, scene_warnings = 0;
        for (const auto& issue : scene_issues) { if (issue.severity == vespera::SceneValidationSeverity::Error) ++scene_errors; else ++scene_warnings; }
        const auto build = vespera::build_project_asset_manifest(state.project, state.asset_catalog);
        std::size_t ui_documents = 0, ui_invalid = 0, ui_warnings = 0;
        for (const auto* asset : state.asset_catalog.records_of_kind(vespera::AssetKind::UiDocument)) {
            if (!asset) continue;
            ++ui_documents;
            vespera::UiDocument document; const auto loaded = vespera::load_ui_document(document, asset->absolute_path);
            if (!loaded) { ++ui_invalid; continue; }
            const auto a = vespera::resolve_ui_layout(document, 1280.0f, 720.0f); const auto b = vespera::resolve_ui_layout(document, 1920.0f, 1080.0f);
            ui_warnings += a.warnings.size() + b.warnings.size();
            std::size_t roots = 0; for (const auto& node : document.nodes()) if (node.type == vespera::UiNodeType::Canvas && node.parent_id == vespera::kInvalidUiNodeId) ++roots;
            if (roots != 1) ++ui_invalid;
        }
        const std::size_t broken = state.asset_catalog.broken_dependencies().size(); std::size_t stale = 0;
        for (const auto& dep : state.asset_catalog.dependencies()) if (dep.stale_fallback_path) ++stale;
        const bool clean = project_errors == 0 && scene_errors == 0 && broken == 0 && stale == 0 && build.valid() && ui_invalid == 0 && ui_warnings == 0;
        const auto report = std::format("{{\"clean\":{},\"project_errors\":{},\"project_warnings\":{},\"scene_errors\":{},\"scene_warnings\":{},\"broken_dependencies\":{},\"stale_fallbacks\":{},\"build_valid\":{},\"build_assets\":{},\"build_missing_roots\":{},\"build_broken\":{},\"ui_documents\":{},\"ui_invalid\":{},\"ui_layout_warnings\":{},\"managed_metadata_loaded\":{}}}",
            clean ? "true" : "false", project_errors, project_warnings, scene_errors, scene_warnings, broken, stale, build.valid() ? "true" : "false", build.assets.size(), build.missing_roots.size(), build.broken_dependencies.size(), ui_documents, ui_invalid, ui_warnings, state.managed_metadata.loaded ? "true" : "false");
        return clean ? ok(report) : fail(report);
    }

    if (request.command == "vespera_read_csharp_source") {
        const auto* raw_path = automation_arg(request, "path"); std::string path_error;
        const auto path = raw_path ? automation_managed_source_path(state, *raw_path, &path_error) : std::nullopt;
        if (!path) return fail(path_error.empty() ? "path is required" : path_error);
        std::ifstream in(*path, std::ios::binary); if (!in) return fail("managed source file does not exist or could not be opened");
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (content.size() > 1024 * 1024) return fail("managed source is larger than the 1 MiB automation read limit");
        return ok(std::format("{{\"path\":\"{}\",\"bytes\":{},\"content\":\"{}\"}}", automation_json_escape(std::filesystem::relative(*path, state.project.managed_project_path().parent_path()).generic_string()), content.size(), automation_json_escape(content)));
    }

    if (request.command == "vespera_write_csharp_source") {
        const auto* raw_path = automation_arg(request, "path"); const auto* content = automation_arg(request, "content");
        if (!raw_path || !content) return fail("path and content are required");
        std::string path_error; const auto path = automation_managed_source_path(state, *raw_path, &path_error); if (!path) return fail(path_error);
        std::string write_error; const bool wrote = automation_guarded_replace_text(*path, *content, &write_error);
        append_command_audit(state, vespera::editor::EditorCommandKind::WriteManagedSource, "Automation write C# source", wrote, state.current_state_id, state.current_state_id);
        if (!wrote) return fail(write_error);
        return ok(std::format("{{\"path\":\"{}\",\"bytes\":{}}}", automation_json_escape(*raw_path), content->size()));
    }

    if (request.command == "vespera_attach_csharp_script") {
        if (editor_is_playing(state)) return fail("scene authoring is disabled during Play Mode");
        const auto id = automation_arg(request, "entity_id") ? automation_u64(*automation_arg(request, "entity_id")) : std::nullopt;
        const auto* class_name = automation_arg(request, "class_name");
        if (!id || !class_name || class_name->empty()) return fail("entity_id and class_name are required");
        if (!state.scene.find_entity(*id)) return fail("entity_id does not exist in the edit scene");
        if (state.managed_metadata.loaded && !state.managed_metadata.find(*class_name)) return fail("class_name is not present in current managed metadata; build C# first");
        const bool attached = execute_editor_command(state, vespera::editor::EditorCommandKind::AttachManagedScript, "Automation attach C# script", [&]() {
            auto* entity = state.scene.find_entity(*id); if (!entity) return false;
            for (const auto& script : entity->managed_scripts) if (script.class_name == *class_name) return false;
            entity->add_managed_script(*class_name); return true;
        });
        if (!attached) return fail("script attachment failed or class is already attached");
        if (!state.command_log.empty()) state.command_log.back().target_entity_id = *id;
        return ok(std::format("{{\"entity_id\":{},\"class_name\":\"{}\"}}", *id, automation_json_escape(*class_name)));
    }

    if (request.command == "vespera_run_qa_scenario") {
        if (!state.project_loaded) return fail("no Vespera project is open");
        const std::string scenario = automation_arg(request, "scenario") ? *automation_arg(request, "scenario") : std::string{};
        std::size_t count = 24;
        if (const auto* raw = automation_arg(request, "count")) { const auto parsed = automation_u64(*raw); if (!parsed || *parsed < 2 || *parsed > 128) return fail("count must be 2..128"); count = static_cast<std::size_t>(*parsed); }
        const bool cleanup = automation_arg(request, "cleanup") ? automation_bool(*automation_arg(request, "cleanup")).value_or(true) : true;
        const auto before_entities = state.scene.entities.size(); const auto before_undo = state.undo_stack.size(); bool passed = true; std::string detail;
        if (scenario == "hierarchy") {
            if (editor_is_playing(state)) return fail("hierarchy QA requires edit mode");
            std::vector<vespera::SceneObjectId> ids; ids.reserve(count);
            for (std::size_t i = 0; i < count; ++i) {
                if (!command_create_entity(state, std::format("QA Node {:03}", i))) { passed = false; detail = "entity creation failed"; break; }
                const auto selected = selected_entity_index(state); if (!selected) { passed = false; detail = "created entity selection missing"; break; }
                ids.push_back(state.scene.entities[*selected].id);
                if (i > 0 && !command_reparent_entity(state, ids[i], ids[i - 1])) { passed = false; detail = "deep reparent failed"; break; }
            }
            if (passed && ids.size() >= 2 && command_reparent_entity(state, ids.front(), ids.back())) { passed = false; detail = "hierarchy cycle was incorrectly accepted"; }
            if (passed && ids.size() >= 4) {
                const auto middle = ids[ids.size()/2]; const auto index = entity_index_from_id(state, middle);
                if (!index) { passed = false; detail = "middle hierarchy node missing"; }
                else { select_entity(state, *index); if (!command_delete_selected_entity(state)) { passed = false; detail = "middle hierarchy delete failed"; } else if (!undo(state)) { passed = false; detail = "undo after delete failed"; } else if (!redo(state)) { passed = false; detail = "redo after delete failed"; } else if (!undo(state)) { passed = false; detail = "second undo after delete failed"; } }
            }
            for (const auto& issue : vespera::validate_scene(state.scene)) if (issue.severity == vespera::SceneValidationSeverity::Error) passed = false;
            if (cleanup) while (state.undo_stack.size() > before_undo) { if (!undo(state)) { passed = false; detail = "cleanup undo failed"; break; } }
            if (detail.empty()) detail = "deep hierarchy/cycle rejection/delete/undo/redo completed";
        } else if (scenario == "play_cycle") {
            if (editor_is_playing(state)) stop_play_mode(state);
            for (std::size_t i = 0; i < std::min<std::size_t>(count, 64); ++i) {
                start_play_mode(state); if (!editor_is_playing(state)) { passed = false; detail = "Play Mode did not start"; break; }
                toggle_play_pause(state); step_play_mode(state); if (state.play_state == EditorPlayState::Paused) toggle_play_pause(state); stop_play_mode(state);
                if (editor_is_playing(state) || state.scene.entities.size() != before_entities) { passed = false; detail = "Play/Stop did not restore edit scene entity count"; break; }
            }
            if (detail.empty()) detail = "repeated Play/Pause/Step/Stop restoration completed";
        } else if (scenario == "ui_layout") {
            const auto* record = automation_asset_record(state, request); if (!record || record->kind != vespera::AssetKind::UiDocument) return fail("ui_layout scenario requires a valid UI Document asset_id or path");
            vespera::UiDocument document; const auto loaded = vespera::load_ui_document(document, record->absolute_path); if (!loaded) return fail(loaded.message);
            const std::array<std::pair<int,int>,6> viewports{{{320,200},{640,360},{1280,720},{1600,900},{1920,1080},{3440,1440}}}; std::size_t warnings = 0;
            for (const auto& [w, h] : viewports) {
                warnings += vespera::resolve_ui_layout(document, static_cast<float>(w), static_cast<float>(h)).warnings.size();
            }
            passed = warnings == 0; detail = std::format("resolved {} UI nodes at {} viewports with {} warning(s)", document.nodes().size(), viewports.size(), warnings);
        } else if (scenario == "managed_build_recovery") {
            if (editor_is_playing(state)) return fail("managed_build_recovery QA requires edit mode");
            const auto* raw_path = automation_arg(request, "path");
            if (!raw_path || raw_path->empty()) return fail("managed_build_recovery requires a managed source path");
            std::string path_error;
            const auto path = automation_managed_source_path(state, *raw_path, &path_error);
            if (!path) return fail(path_error);
            std::ifstream source_in(*path, std::ios::binary);
            if (!source_in) return fail("managed_build_recovery source file could not be opened");
            const std::string original((std::istreambuf_iterator<char>(source_in)), std::istreambuf_iterator<char>());
            source_in.close();
            std::string write_error;
            const std::string broken = original + "\n// Vespera managed-build recovery QA sentinel\nVESPERA_QA_INTENTIONAL_COMPILER_ERROR\n";
            if (!automation_guarded_replace_text(*path, broken, &write_error)) return fail("could not inject managed compiler error: " + write_error);
            const auto failed_build_console = state.console.size();
            build_managed_scripts(state);
            bool expected_failure = false;
            for (std::size_t i = failed_build_console; i < state.console.size(); ++i) {
                if (state.console[i].level == ConsoleEntry::Level::Error) { expected_failure = true; break; }
            }
            write_error.clear();
            if (!automation_guarded_replace_text(*path, original, &write_error)) {
                return fail("managed source restore failed after intentional compiler error: " + write_error);
            }
            const auto restored_build_console = state.console.size();
            build_managed_scripts(state);
            bool restored_failed = false;
            for (std::size_t i = restored_build_console; i < state.console.size(); ++i) {
                if (state.console[i].level == ConsoleEntry::Level::Error) { restored_failed = true; break; }
            }
            passed = expected_failure && !restored_failed;
            detail = std::format("intentional failure observed={} exact restore rebuild succeeded={}",
                expected_failure ? "true" : "false", restored_failed ? "false" : "true");
        } else if (scenario == "asset_move_save_reopen") {
            if (editor_is_playing(state)) return fail("asset_move_save_reopen QA requires edit mode");
            if (!cleanup) return fail("asset_move_save_reopen requires cleanup=true because it mutates a real asset and scene");
            if (state.scene_path.empty()) return fail("asset_move_save_reopen requires an open saved scene");
            const auto* record = automation_asset_record(state, request);
            if (!record || record->kind != vespera::AssetKind::EntityPrefab) {
                return fail("asset_move_save_reopen requires a valid Prefab asset_id or path");
            }
            const std::string prefab_id = record->asset_id;
            const std::filesystem::path original_relative = record->relative_path.lexically_normal();
            const std::filesystem::path original_scene = state.scene_path.lexically_normal();
            const bool scene_references_prefab = std::any_of(state.scene.entities.begin(), state.scene.entities.end(), [&](const auto& entity) {
                return !entity.prefab_source.asset_id.empty() && entity.prefab_source.asset_id == prefab_id;
            });
            if (!scene_references_prefab) return fail("the currently open scene does not reference the selected Prefab by stable ID");

            std::filesystem::path destination = automation_arg(request, "destination")
                ? std::filesystem::path(*automation_arg(request, "destination")).lexically_normal()
                : (original_relative.parent_path() / ("__vespera_qa_move_" + original_relative.filename().string())).lexically_normal();
            if (destination.empty() || destination.is_absolute() || destination.extension() != original_relative.extension()
                || destination == original_relative || destination.generic_string().starts_with("..")) {
                return fail("destination must be a different project-relative Prefab path with the same extension");
            }
            for (const auto& part : destination) if (part == "..") return fail("destination may not escape Assets");
            if (state.asset_catalog.find(destination.generic_string())) return fail("asset_move_save_reopen destination already exists");

            bool moved_away = false;
            auto repair_after_catalog_change = [&]() -> bool {
                auto repair = vespera::repair_stable_asset_fallbacks(state.project, state.asset_catalog);
                if (repair.project_references_repaired != 0) {
                    const auto saved_project = vespera::save_vespera_project(state.project, state.project_path);
                    if (!saved_project) {
                        detail = "project fallback repair save failed: " + saved_project.message;
                        return false;
                    }
                }
                if (repair.descriptor_files_rewritten != 0) refresh_asset_catalog(state, false, false);
                if (!repair.warnings.empty()) {
                    detail = "fallback repair produced warning: " + repair.warnings.front();
                    return false;
                }
                return true;
            };

            const auto moved = vespera::move_project_asset(state.asset_catalog, prefab_id, destination.generic_string());
            if (!moved) {
                passed = false;
                detail = "prefab move failed: " + moved.message;
            } else {
                moved_away = true;
                refresh_asset_catalog(state, false, false);
                if (!repair_after_catalog_change()) passed = false;
            }

            if (passed && !save_scene(state, original_scene)) {
                passed = false;
                detail = "scene save after prefab move failed";
            }
            if (passed) {
                refresh_asset_catalog(state, false, false);
                if (state.last_asset_report.stale_fallback_paths != 0) {
                    passed = false;
                    detail = std::format("scene save reintroduced {} stale fallback path(s)", state.last_asset_report.stale_fallback_paths);
                }
            }
            if (passed && !open_scene(state, original_scene)) {
                passed = false;
                detail = "scene reopen after prefab move failed";
            }
            if (passed) {
                const bool canonicalized = std::any_of(state.scene.entities.begin(), state.scene.entities.end(), [&](const auto& entity) {
                    return entity.prefab_source.asset_id == prefab_id
                        && entity.prefab_source.path.lexically_normal() == destination;
                });
                if (!canonicalized) {
                    passed = false;
                    detail = "reopened scene did not retain the moved Prefab's canonical fallback path";
                }
            }

            // Always attempt to restore the authored asset and scene before reporting.
            if (moved_away) {
                const auto restore = vespera::move_project_asset(state.asset_catalog, prefab_id, original_relative.generic_string());
                if (!restore) {
                    passed = false;
                    detail = detail.empty() ? "cleanup move-back failed: " + restore.message : detail + "; cleanup move-back failed: " + restore.message;
                } else {
                    refresh_asset_catalog(state, false, false);
                    if (!repair_after_catalog_change()) passed = false;
                    if (!save_scene(state, original_scene)) {
                        passed = false;
                        detail = detail.empty() ? "cleanup scene save failed" : detail + "; cleanup scene save failed";
                    }
                    refresh_asset_catalog(state, false, false);
                    if (!open_scene(state, original_scene)) {
                        passed = false;
                        detail = detail.empty() ? "cleanup scene reopen failed" : detail + "; cleanup scene reopen failed";
                    }
                }
            }
            if (passed && state.last_asset_report.stale_fallback_paths != 0) {
                passed = false;
                detail = std::format("cleanup left {} stale fallback path(s)", state.last_asset_report.stale_fallback_paths);
            }
            if (detail.empty()) detail = "Prefab move -> repair -> save -> refresh -> reopen preserved stable ID/canonical fallback and cleanup restored original path";
        } else return fail("scenario must be hierarchy, play_cycle, ui_layout, managed_build_recovery or asset_move_save_reopen");
        append_command_audit(state, vespera::editor::EditorCommandKind::RunQaScenario, "Automation QA scenario: " + scenario, passed, state.current_state_id, state.current_state_id);
        const auto after_entities = state.scene.entities.size();
        return passed ? ok(std::format("{{\"scenario\":\"{}\",\"passed\":true,\"detail\":\"{}\",\"entities_before\":{},\"entities_after\":{},\"cleanup\":{}}}", automation_json_escape(scenario), automation_json_escape(detail), before_entities, after_entities, cleanup ? "true" : "false")) : fail(std::format("QA scenario '{}' failed: {}", scenario, detail));
    }

    if (request.command == "vespera_validate_scene") {
        const auto issues = vespera::validate_scene(editor_is_playing(state) ? state.play_scene : state.scene);
        int errors = 0;
        int warnings = 0;
        for (const auto& issue : issues) {
            if (issue.severity == vespera::SceneValidationSeverity::Error) ++errors;
            else ++warnings;
        }
        validate_scene_to_console(state);
        return ok(std::format("{{\"errors\":{},\"warnings\":{},\"issue_count\":{}}}", errors, warnings, issues.size()));
    }

    if (request.command == "vespera_undo") {
        if (editor_is_playing(state)) return fail("undo is disabled during Play Mode");
        const bool succeeded = undo(state);
        append_command_audit(state, vespera::editor::EditorCommandKind::Undo, "Automation undo", succeeded);
        return succeeded ? ok("{\"undone\":true}") : fail("nothing to undo");
    }

    if (request.command == "vespera_redo") {
        if (editor_is_playing(state)) return fail("redo is disabled during Play Mode");
        const bool succeeded = redo(state);
        append_command_audit(state, vespera::editor::EditorCommandKind::Redo, "Automation redo", succeeded);
        return succeeded ? ok("{\"redone\":true}") : fail("nothing to redo");
    }

    if (request.command == "vespera_play") {
        const std::string action = automation_arg(request, "action") ? *automation_arg(request, "action") : "enter";
        if (action == "enter") {
            if (!editor_is_playing(state)) start_play_mode(state);
        } else if (action == "stop") {
            if (editor_is_playing(state)) stop_play_mode(state);
        } else if (action == "pause") {
            if (!editor_is_playing(state)) return fail("Play Mode is not active");
            if (state.play_state != EditorPlayState::Paused) toggle_play_pause(state);
        } else if (action == "resume") {
            if (!editor_is_playing(state)) return fail("Play Mode is not active");
            if (state.play_state == EditorPlayState::Paused) toggle_play_pause(state);
        } else if (action == "step") {
            if (!editor_is_playing(state)) return fail("Play Mode is not active");
            step_play_mode(state);
        } else {
            return fail("action must be enter, stop, pause, resume or step");
        }
        const std::string current = state.play_state == EditorPlayState::Editing
            ? "editing" : (state.play_state == EditorPlayState::Paused ? "paused" : "playing");
        return ok(std::format("{{\"play_state\":\"{}\"}}", current));
    }

    if (request.command == "vespera_inject_input_action") {
        if (!editor_is_playing(state) || !state.play_runtime) return fail("runtime input injection requires active Play Mode");
        const auto* action = automation_arg(request, "action");
        if (!action || action->empty()) return fail("action is required");
        const std::string phase = automation_arg(request, "phase") ? *automation_arg(request, "phase") : "set";
        float value = 1.0f;
        if (const auto* raw = automation_arg(request, "value")) {
            const auto parsed = automation_float(*raw); if (!parsed) return fail("value must be numeric"); value = std::clamp(*parsed, -1.0f, 1.0f);
        }
        if (phase == "press" || phase == "set") state.play_runtime->queue_action_override(*action, phase == "press" ? std::max(0.5f, value) : value);
        else if (phase == "release") state.play_runtime->queue_action_override(*action, 0.0f);
        else if (phase == "clear") state.play_runtime->queue_clear_action_override(*action);
        else return fail("phase must be set, press, release or clear");
        append_command_audit(state, vespera::editor::EditorCommandKind::InjectRuntimeInput,
            "Automation inject Input action", true, state.current_state_id, state.current_state_id);
        return ok(std::format("{{\"action\":\"{}\",\"phase\":\"{}\",\"value\":{},\"queued\":true}}",
            automation_json_escape(*action), automation_json_escape(phase), value));
    }

    if (request.command == "vespera_pointer_event") {
        if (!editor_is_playing(state) || !state.play_runtime || (!state.play_ui_loaded && !state.play_rml_ui_loaded)) {
            return fail("pointer injection requires active Play Mode with a runtime UI surface");
        }
        const auto* raw_x = automation_arg(request, "x"); const auto* raw_y = automation_arg(request, "y");
        const auto x = raw_x ? automation_float(*raw_x) : std::nullopt; const auto y = raw_y ? automation_float(*raw_y) : std::nullopt;
        if (!x || !y) return fail("x and y are required numeric coordinates");
        int width = state.play_rml_ui_loaded
            ? std::max(1, state.play_rml_view_width)
            : std::max(1, state.project_loaded ? state.project.window_width : 1280);
        int height = state.play_rml_ui_loaded
            ? std::max(1, state.play_rml_view_height)
            : std::max(1, state.project_loaded ? state.project.window_height : 720);
        if (const auto* raw = automation_arg(request, "width")) { const auto parsed=automation_int(*raw); if (!parsed || *parsed<1) return fail("width must be >= 1"); width=*parsed; }
        if (const auto* raw = automation_arg(request, "height")) { const auto parsed=automation_int(*raw); if (!parsed || *parsed<1) return fail("height must be >= 1"); height=*parsed; }
        const bool normalized = automation_arg(request, "normalized") ? automation_bool(*automation_arg(request, "normalized")).value_or(false) : false;
        const float point_x = normalized ? *x * static_cast<float>(width) : *x;
        const float point_y = normalized ? *y * static_cast<float>(height) : *y;
        const std::string phase = automation_arg(request, "phase") ? *automation_arg(request, "phase") : "move";

        if (state.play_rml_ui_loaded && state.play_rml_ui) {
            state.play_rml_view_width = width;
            state.play_rml_view_height = height;
            state.play_rml_ui->resize(width, height);
            if (!state.play_runtime->inject_rml_pointer(point_x, point_y, phase)) {
                return fail("RmlUi pointer phase must be move, down, up or click");
            }
            append_command_audit(state, vespera::editor::EditorCommandKind::InjectRuntimeUi,
                "Automation inject RmlUi pointer", true, state.current_state_id, state.current_state_id);
            return ok(std::format(
                "{{\"backend\":\"rmlui\",\"phase\":\"{}\",\"hovered_id\":\"{}\",\"focused_id\":\"{}\"}}",
                automation_json_escape(phase), automation_json_escape(state.play_rml_ui->hovered_id()),
                automation_json_escape(state.play_rml_ui->focused_id())));
        }

        vespera::UiVec2 point{point_x, point_y};
        auto apply = [&](bool down, bool pressed, bool released) {
            vespera::UiPointerState pointer{}; pointer.available=true; pointer.position=point; pointer.primary_down=down; pointer.primary_pressed=pressed; pointer.primary_released=released;
            (void)state.play_ui_cache.build_packet(state.play_ui_document, static_cast<float>(width), static_cast<float>(height), pointer,
                &state.play_runtime->ui_runtime_state());
        };
        if (phase == "move") apply(false,false,false);
        else if (phase == "down") apply(true,true,false);
        else if (phase == "up") apply(false,false,true);
        else if (phase == "click") { apply(true,true,false); apply(false,false,true); }
        else return fail("phase must be move, down, up or click");
        append_command_audit(state, vespera::editor::EditorCommandKind::InjectRuntimeUi,
            "Automation inject legacy runtime UI pointer", true, state.current_state_id, state.current_state_id);
        const auto& ui = state.play_runtime->ui_runtime_state();
        return ok(std::format("{{\"backend\":\"slui\",\"phase\":\"{}\",\"hovered\":{},\"pressed\":{},\"focused\":{},\"pending_clicks\":{}}}",
            automation_json_escape(phase), ui.hovered.value_or(0), ui.pressed.value_or(0), ui.focused.value_or(0), ui.pending_clicks.size()));
    }

    if (request.command == "vespera_ui_navigation") {
        if (!editor_is_playing(state) || !state.play_runtime || (!state.play_ui_loaded && !state.play_rml_ui_loaded)) {
            return fail("UI navigation injection requires active Play Mode with a runtime UI surface");
        }
        const auto* raw_action = automation_arg(request, "action"); if (!raw_action) return fail("action is required");
        if (*raw_action != "next" && *raw_action != "previous" && *raw_action != "activate") {
            return fail("action must be next, previous or activate");
        }
        if (state.play_rml_ui_loaded && state.play_rml_ui) {
            if (!state.play_runtime->inject_rml_navigation(*raw_action)) return fail("RmlUi navigation injection failed");
            append_command_audit(state, vespera::editor::EditorCommandKind::InjectRuntimeUi,
                "Automation inject RmlUi navigation", true, state.current_state_id, state.current_state_id);
            return ok(std::format(
                "{{\"backend\":\"rmlui\",\"action\":\"{}\",\"focused_id\":\"{}\"}}",
                automation_json_escape(*raw_action), automation_json_escape(state.play_rml_ui->focused_id())));
        }
        vespera::UiNavigationState nav{};
        if (*raw_action == "next") nav.focus_next = true;
        else if (*raw_action == "previous") nav.focus_previous = true;
        else nav.activate_pressed = true;
        const float width = static_cast<float>(std::max(1, state.project_loaded ? state.project.window_width : 1280));
        const float height = static_cast<float>(std::max(1, state.project_loaded ? state.project.window_height : 720));
        (void)state.play_ui_cache.build_packet(state.play_ui_document, width, height, {}, &state.play_runtime->ui_runtime_state(), nav);
        append_command_audit(state, vespera::editor::EditorCommandKind::InjectRuntimeUi,
            "Automation inject legacy runtime UI navigation", true, state.current_state_id, state.current_state_id);
        const auto& ui = state.play_runtime->ui_runtime_state();
        return ok(std::format("{{\"backend\":\"slui\",\"action\":\"{}\",\"focused\":{},\"pending_clicks\":{}}}",
            automation_json_escape(*raw_action), ui.focused.value_or(0), ui.pending_clicks.size()));
    }

    if (request.command == "vespera_text_input") {
        if (!editor_is_playing(state) || !state.play_runtime || (!state.play_ui_loaded && !state.play_rml_ui_loaded)) {
            return fail("text input injection requires active Play Mode with a runtime UI surface");
        }
        const std::string text = automation_arg(request, "text") ? *automation_arg(request, "text") : std::string{};
        const bool backspace = automation_arg(request, "backspace") ? automation_bool(*automation_arg(request, "backspace")).value_or(false) : false;
        const bool clear = automation_arg(request, "clear") ? automation_bool(*automation_arg(request, "clear")).value_or(false) : false;
        if (state.play_rml_ui_loaded && state.play_rml_ui) {
            if (!state.play_runtime->inject_rml_text(text, backspace, clear)) {
                return fail("RmlUi text input requires a focused editable element");
            }
            const std::string focused = state.play_rml_ui->focused_id();
            bool read_only = false;
            (void)state.play_rml_ui->read_only(focused, read_only);
            append_command_audit(state, vespera::editor::EditorCommandKind::InjectRuntimeUi,
                "Automation inject RmlUi text", true, state.current_state_id, state.current_state_id);
            return ok(std::format(
                "{{\"backend\":\"rmlui\",\"focused_id\":\"{}\",\"value\":\"{}\",\"read_only\":{}}}",
                automation_json_escape(focused), automation_json_escape(state.play_rml_ui->value(focused)),
                read_only ? "true" : "false"));
        }
        vespera::UiTextInputState input{};
        input.text = text;
        input.backspace = backspace;
        input.clear = clear;
        const float width = static_cast<float>(std::max(1, state.project_loaded ? state.project.window_width : 1280));
        const float height = static_cast<float>(std::max(1, state.project_loaded ? state.project.window_height : 720));
        (void)state.play_ui_cache.build_packet(state.play_ui_document, width, height, {}, &state.play_runtime->ui_runtime_state(), {}, input);
        const auto focused = state.play_runtime->ui_runtime_state().focused;
        const auto* node = focused ? state.play_ui_document.find(*focused) : nullptr;
        if (!node || node->type != vespera::UiNodeType::TextInput) return fail("no TextInput node is currently focused");
        append_command_audit(state, vespera::editor::EditorCommandKind::InjectRuntimeUi,
            "Automation inject legacy runtime UI text", true, state.current_state_id, state.current_state_id);
        return ok(std::format("{{\"backend\":\"slui\",\"focused\":{},\"text\":\"{}\",\"read_only\":{},\"max_length\":{}}}",
            node->id, automation_json_escape(node->text.text), node->input.read_only ? "true" : "false", node->input.max_length));
    }

    if (request.command == "vespera_get_ui_runtime_state") {
        if (!editor_is_playing(state) || !state.play_runtime || (!state.play_ui_loaded && !state.play_rml_ui_loaded)) {
            return fail("runtime UI state requires active Play Mode with a runtime UI surface");
        }
        if (state.play_rml_ui_loaded && state.play_rml_ui) {
            const std::string hovered = state.play_rml_ui->hovered_id();
            const std::string focused = state.play_rml_ui->focused_id();
            bool read_only = false;
            if (!focused.empty()) (void)state.play_rml_ui->read_only(focused, read_only);
            return ok(std::format(
                "{{\"backend\":\"rmlui\",\"hovered_id\":\"{}\",\"focused_id\":\"{}\",\"focused_value\":\"{}\",\"focused_read_only\":{}}}",
                automation_json_escape(hovered), automation_json_escape(focused),
                automation_json_escape(focused.empty() ? std::string{} : state.play_rml_ui->value(focused)),
                read_only ? "true" : "false"));
        }
        const auto& ui = state.play_runtime->ui_runtime_state();
        std::string clicks = "[";
        for (std::size_t i=0;i<ui.pending_clicks.size();++i) { if (i) clicks += ','; clicks += std::to_string(ui.pending_clicks[i]); }
        clicks += "]";
        std::string focused_name, focused_type, focused_text; bool focused_read_only=false;
        if (ui.focused) if (const auto* node = state.play_ui_document.find(*ui.focused)) {
            focused_name=node->name; focused_type=std::string(vespera::ui_node_type_name(node->type)); focused_text=node->text.text; focused_read_only=node->input.read_only;
        }
        return ok(std::format("{{\"backend\":\"slui\",\"hovered\":{},\"pressed\":{},\"focused\":{},\"pointer_capture\":{},\"pending_clicks\":{},\"focused_name\":\"{}\",\"focused_type\":\"{}\",\"focused_text\":\"{}\",\"focused_read_only\":{}}}",
            ui.hovered.value_or(0), ui.pressed.value_or(0), ui.focused.value_or(0), ui.pointer_capture, clicks,
            automation_json_escape(focused_name), automation_json_escape(focused_type), automation_json_escape(focused_text), focused_read_only ? "true" : "false"));
    }

    if (request.command == "vespera_get_runtime_events") {
        std::uint64_t after = 0; std::size_t limit = 100;
        if (const auto* raw=automation_arg(request,"after_sequence")) { const auto parsed=automation_u64(*raw); if (!parsed) return fail("after_sequence must be an unsigned integer"); after=*parsed; }
        if (const auto* raw=automation_arg(request,"limit")) { const auto parsed=automation_u64(*raw); if (!parsed || *parsed==0) return fail("limit must be positive"); limit=static_cast<std::size_t>(std::min<std::uint64_t>(*parsed,500)); }
        std::vector<vespera::ManagedRuntimeEvent> events;
        std::uint32_t generation = state.last_runtime_assembly_generation;
        std::uint64_t latest = state.last_runtime_event_sequence;
        const bool active = editor_is_playing(state) && state.play_runtime;
        if (active) {
            events = state.play_runtime->runtime_events_since(after);
            generation = state.play_runtime->assembly_generation();
            latest = state.play_runtime->latest_runtime_event_sequence();
        } else {
            for (const auto& event : state.last_runtime_events) if (event.sequence > after) events.push_back(event);
        }
        if (events.size() > limit) events.erase(events.begin(), events.end()-static_cast<std::ptrdiff_t>(limit));
        std::string json="["; bool first=true;
        for (const auto& event:events) { if(!first) json+=','; first=false; json += std::format("{{\"sequence\":{},\"frame\":{},\"generation\":{},\"entity_id\":{},\"component\":\"{}\",\"callback\":\"{}\"}}", event.sequence,event.frame,event.assembly_generation,event.entity_id,automation_json_escape(event.component),automation_json_escape(event.callback)); }
        json += "]";
        return ok(std::format("{{\"active\":{},\"assembly_generation\":{},\"latest_sequence\":{},\"events\":{}}}", active ? "true" : "false", generation, latest, json));
    }

    if (request.command == "vespera_clear_runtime_events") {
        if (editor_is_playing(state) && state.play_runtime) {
            state.play_runtime->clear_runtime_events();
        } else {
            state.last_runtime_events.clear();
        }
        append_command_audit(state, vespera::editor::EditorCommandKind::ClearRuntimeEvents,
            "Automation clear runtime events", true, state.current_state_id, state.current_state_id);
        const auto latest = state.play_runtime ? state.play_runtime->latest_runtime_event_sequence() : state.last_runtime_event_sequence;
        return ok(std::format("{{\"cleared\":true,\"latest_sequence\":{}}}", latest));
    }

    if (request.command == "vespera_save_scene") {
        if (editor_is_playing(state)) return fail("saving is disabled during Play Mode");
        std::filesystem::path path = state.scene_path;
        if (const auto* requested_path = automation_arg(request, "path"); requested_path && !requested_path->empty()) {
            path = *requested_path;
        }
        const bool succeeded = save_scene(state, path);
        append_command_audit(state, vespera::editor::EditorCommandKind::SaveScene,
            "Automation save scene", succeeded);
        return succeeded
            ? ok(std::format("{{\"path\":\"{}\"}}", automation_json_escape(std::filesystem::absolute(path).lexically_normal().generic_string())))
            : fail("scene save failed; inspect the editor Console for details");
    }

    return fail("unknown automation command: " + request.command);
}
void poll_automation_server(EditorState& state) {
    if (!state.automation_server || !state.automation_server->running()) return;
    for (const auto& incoming : state.automation_server->poll()) {
        auto response = dispatch_automation_request(state, incoming.request);
        if (!state.automation_server->respond(incoming.client_id, response)) {
            push_console(state, ConsoleEntry::Level::Warning,
                "Automation response could not be delivered to the local client.");
        }
    }
}
bool start_automation_server(EditorState& state, std::uint16_t port) {
    if (!state.automation_server) state.automation_server = std::make_unique<vespera::editor::AutomationServer>();
    if (state.automation_server->running()) state.automation_server->stop();
    std::string error;
    if (!state.automation_server->start(port, &error)) {
        push_console(state, ConsoleEntry::Level::Error, "Automation server failed: " + error);
        return false;
    }
    state.automation_port = port;
    push_console(state, ConsoleEntry::Level::Info,
        std::format("Local editor automation server listening on 127.0.0.1:{} (main-thread command dispatch).", port));
    return true;
}
void stop_automation_server(EditorState& state) {
    if (!state.automation_server || !state.automation_server->running()) return;
    state.automation_server->stop();
    push_console(state, ConsoleEntry::Level::Info, "Local editor automation server stopped.");
}

} // namespace vespera::editor
