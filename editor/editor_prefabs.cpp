#include "editor_prefabs.hpp"
#include "editor_assets.hpp"
#include "editor_command_runtime.hpp"
#include "editor_console.hpp"
#include "editor_history.hpp"
#include "editor_scene_commands.hpp"
#include "editor_selection.hpp"
#include "editor_view_helpers.hpp"
#include <vespera/assets/material_asset.hpp>
#include <vespera/scene/prefab.hpp>
#include <cctype>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
namespace vespera::editor {

std::string sanitize_asset_stem(std::string value) {
    for (char& ch : value) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (!std::isalnum(uch) && ch != '-' && ch != '_') ch = '_';
    }
    while (!value.empty() && value.front() == '_') value.erase(value.begin());
    while (!value.empty() && value.back() == '_') value.pop_back();
    if (value.empty()) value = "Prefab";
    return value;
}
std::optional<std::string> project_relative_asset_key(
    const EditorState& state,
    const std::filesystem::path& absolute_path
) {
    if (state.assets_root.empty()) return std::nullopt;
    std::error_code ec;
    const auto relative = std::filesystem::relative(absolute_path, state.assets_root, ec).lexically_normal();
    if (ec || relative.empty() || relative.is_absolute()) return std::nullopt;
    for (const auto& part : relative) {
        if (part == "..") return std::nullopt;
    }
    return relative.generic_string();
}
std::optional<std::filesystem::path> resolve_prefab_source_path(
    const EditorState& state,
    const vespera::AssetReference& source
) {
    if (state.assets_root.empty() || source.empty()) return std::nullopt;
    const auto resolved = state.asset_catalog.resolve_reference(source);
    if (resolved) return resolved.record->absolute_path.lexically_normal();
    const std::filesystem::path relative(source.path);
    if (relative.empty() || relative.is_absolute()) return std::nullopt;
    for (const auto& part : relative) {
        if (part == "..") return std::nullopt;
    }
    return (state.assets_root / relative).lexically_normal();
}
std::optional<vespera::AssetReference> project_asset_reference(
    const EditorState& state,
    const std::filesystem::path& absolute_path
) {
    const auto relative = project_relative_asset_key(state, absolute_path);
    if (!relative) return std::nullopt;
    vespera::AssetReference reference;
    reference.path = *relative;
    if (const auto* record = state.asset_catalog.find(*relative)) reference.asset_id = record->asset_id;
    return reference;
}
std::filesystem::path unique_prefab_asset_path(const EditorState& state, std::string_view entity_name) {
    const auto directory = state.assets_root / "prefabs";
    const std::string stem = sanitize_asset_stem(std::string(entity_name));
    std::filesystem::path candidate = directory / (stem + ".slprefab");
    std::error_code ec;
    if (!std::filesystem::exists(candidate, ec)) return candidate;
    for (int suffix = 2; suffix < 10000; ++suffix) {
        candidate = directory / std::format("{}_{}.slprefab", stem, suffix);
        ec.clear();
        if (!std::filesystem::exists(candidate, ec)) return candidate;
    }
    return directory / (stem + "_Unique.slprefab");
}
bool save_prefab_verified(EditorState& state, const vespera::Entity& entity, const std::filesystem::path& path) {
    // A single-Entity prefab has one root. Editor-authored prefab assets store
    // that root at local origin; scene placement remains an instance concern.
    vespera::Entity prefab_entity = entity;
    prefab_entity.transform.position = {};

    std::filesystem::path temporary = path;
    temporary += ".sectorline-tmp";
    std::filesystem::path backup = path;
    backup += ".bak";

    std::error_code ec;
    std::filesystem::remove(temporary, ec);

    const auto result = vespera::save_entity_prefab(state.scene, prefab_entity, temporary);
    if (!result) {
        push_console(state, ConsoleEntry::Level::Error, "Prefab save failed: " + result.message);
        return false;
    }

    vespera::EntityPrefab verification;
    const auto verify = vespera::load_entity_prefab(state.scene, verification, temporary);
    if (!verify) {
        std::filesystem::remove(temporary, ec);
        push_console(state, ConsoleEntry::Level::Error, "Prefab verification failed: " + verify.message);
        return false;
    }

    ec.clear();
    const bool had_original = std::filesystem::exists(path, ec) && !ec;
    if (had_original) {
        ec.clear();
        std::filesystem::remove(backup, ec);
        ec.clear();
        std::filesystem::rename(path, backup, ec);
        if (ec) {
            const std::string message = ec.message();
            std::filesystem::remove(temporary, ec);
            push_console(state, ConsoleEntry::Level::Error, "Prefab save failed while creating backup: " + message);
            return false;
        }
    }

    ec.clear();
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        const std::string message = ec.message();
        if (had_original) {
            std::error_code rollback_error;
            std::filesystem::rename(backup, path, rollback_error);
        }
        std::filesystem::remove(temporary, ec);
        push_console(state, ConsoleEntry::Level::Error, "Prefab save failed while installing verified asset: " + message);
        return false;
    }

    push_console(state, ConsoleEntry::Level::Info, "Saved + verified prefab: " + path.string());
    return true;
}
bool command_create_prefab_from_selected(EditorState& state) {
    const auto selected = selected_entity_index(state);
    if (!selected || state.assets_root.empty()) return false;
    const std::size_t entity_index = *selected;
    const auto prefab_path = unique_prefab_asset_path(state, state.scene.entities[entity_index].name);
    if (!save_prefab_verified(state, state.scene.entities[entity_index], prefab_path)) return false;
    refresh_asset_catalog(state, true);
    const auto source = project_asset_reference(state, prefab_path);
    if (!source) {
        push_console(state, ConsoleEntry::Level::Warning, "Prefab was saved, but its project asset reference could not be resolved.");
        return false;
    }

    return execute_editor_command(state, vespera::editor::EditorCommandKind::CreatePrefab, "Link entity to new prefab", [&]() {
        if (entity_index >= state.scene.entities.size()) return false;
        state.scene.entities[entity_index].prefab_source = *source;
        select_entity(state, entity_index);
        return true;
    });
}
bool command_apply_selected_to_prefab(EditorState& state) {
    const auto selected = selected_entity_index(state);
    if (!selected) return false;
    const auto& entity = state.scene.entities[*selected];
    const auto path = resolve_prefab_source_path(state, entity.prefab_source);
    if (!path) {
        push_console(state, ConsoleEntry::Level::Error, "Apply Prefab failed: entity has no valid project-relative prefab source.");
        return false;
    }
    const bool saved = save_prefab_verified(state, entity, *path);
    append_command_audit(state, vespera::editor::EditorCommandKind::ApplyPrefab,
        "Apply prefab", saved, state.current_state_id, state.current_state_id,
        entity.id, entity.prefab_source.asset_id);
    if (saved) {
        refresh_asset_catalog(state);
        push_console(state, ConsoleEntry::Level::Info, "Applied entity to prefab source: " + path->generic_string());
    }
    return saved;
}
bool command_revert_selected_from_prefab(EditorState& state) {
    const auto selected = selected_entity_index(state);
    if (!selected) return false;
    const std::size_t entity_index = *selected;
    const auto source = state.scene.entities[entity_index].prefab_source;
    const auto path = resolve_prefab_source_path(state, source);
    if (!path) {
        push_console(state, ConsoleEntry::Level::Error, "Revert Prefab failed: entity has no valid project-relative prefab source.");
        return false;
    }

    vespera::EntityPrefab prefab;
    const auto load = vespera::load_entity_prefab(state.scene, prefab, *path);
    if (!load) {
        push_console(state, ConsoleEntry::Level::Error, "Revert Prefab failed: " + load.message);
        return false;
    }

    return execute_editor_command(state, vespera::editor::EditorCommandKind::RevertPrefab, "Revert prefab instance", [&]() {
        if (entity_index >= state.scene.entities.size()) return false;
        const auto id = state.scene.entities[entity_index].id;
        const std::string instance_name = state.scene.entities[entity_index].name;
        const auto instance_parent = state.scene.entities[entity_index].parent_id;
        const vespera::Vec3 instance_position = state.scene.entities[entity_index].transform.position;
        vespera::Entity replacement = prefab.prototype;
        replacement.id = id;
        replacement.name = instance_name;
        replacement.parent_id = instance_parent;
        replacement.prefab_source = source;
        replacement.transform.position = instance_position;
        state.scene.entities[entity_index] = std::move(replacement);
        (void)vespera::hydrate_scene_materials(state.scene, state.asset_catalog);
        select_entity(state, entity_index);
        state.scene_view.frame_selection_pending = true;
        return true;
    });
}
bool command_unpack_selected_prefab(EditorState& state) {
    const auto selected = selected_entity_index(state);
    if (!selected || state.scene.entities[*selected].prefab_source.empty()) return false;
    const std::size_t entity_index = *selected;
    return execute_editor_command(state, vespera::editor::EditorCommandKind::UnpackPrefab, "Unpack prefab instance", [&]() {
        if (entity_index >= state.scene.entities.size()) return false;
        state.scene.entities[entity_index].prefab_source = {};
        select_entity(state, entity_index);
        return true;
    });
}
bool command_instantiate_prefab(
    EditorState& state,
    const std::filesystem::path& prefab_path,
    std::optional<vespera::Vec3> world_position,
    vespera::editor::EditorCommandKind command_kind
) {
    vespera::EntityPrefab prefab;
    const auto load = vespera::load_entity_prefab(state.scene, prefab, prefab_path);
    if (!load) {
        push_console(state, ConsoleEntry::Level::Error, "Instantiate Prefab failed: " + load.message);
        return false;
    }
    const auto source = project_asset_reference(state, prefab_path);
    if (!source) {
        push_console(state, ConsoleEntry::Level::Error, "Instantiate Prefab failed: asset is outside the current assets root.");
        return false;
    }

    const std::string label = command_kind == vespera::editor::EditorCommandKind::DropPrefabIntoScene
        ? "Drop prefab into Scene" : "Instantiate prefab";
    return execute_editor_command(state, command_kind, label, [&]() {
        const std::string name = unique_entity_name(state.scene, prefab.prototype.name);
        vespera::Entity* entity = vespera::instantiate_entity_prefab(state.scene, prefab, name, *source);
        if (!entity) return false;
        if (world_position) {
            entity->transform.position = *world_position;
        } else {
            const vespera::Vec2 point = snap_world_point(default_creation_point(state), state.scene_view, false);
            entity->transform.position = {point.x, floor_height_at(state.scene, point), point.z};
        }
        (void)vespera::hydrate_scene_materials(state.scene, state.asset_catalog);
        select_entity(state, state.scene.entities.size() - 1);
        state.scene_view.frame_selection_pending = true;
        state.scene_view_3d.frame_selection_pending = true;
        return true;
    });
}

} // namespace vespera::editor
