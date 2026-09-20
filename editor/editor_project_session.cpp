#include "editor_project_session.hpp"
#include "editor_assets.hpp"
#include "editor_console.hpp"
#include "editor_history.hpp"
#include "editor_managed.hpp"
#include "editor_play_controls.hpp"
#include <vespera/project/project.hpp>
#include <vespera/scene/scene_io.hpp>
#include <vespera/scene/scene_validation.hpp>
#include <filesystem>
#include <format>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
namespace vespera::editor {

void validate_scene_to_console(EditorState& state) {
    const auto issues = vespera::validate_scene(state.scene);
    if (issues.empty()) {
        push_console(state, ConsoleEntry::Level::Info, "Scene validation passed with no issues.");
        return;
    }
    int errors = 0;
    int warnings = 0;
    for (const auto& issue : issues) {
        if (issue.severity == vespera::SceneValidationSeverity::Error) {
            ++errors;
            push_console(state, ConsoleEntry::Level::Error, issue.message);
        } else {
            ++warnings;
            push_console(state, ConsoleEntry::Level::Warning, issue.message);
        }
    }
    push_console(state, errors > 0 ? ConsoleEntry::Level::Error : ConsoleEntry::Level::Warning,
        std::format("Scene validation: {} error(s), {} warning(s).", errors, warnings));
}
bool open_project(EditorState& state, const std::filesystem::path& path) {
    vespera::VesperaProject candidate;
    const auto result = vespera::load_vespera_project(candidate, path);
    if (!result) {
        push_console(state, ConsoleEntry::Level::Error, "Project open failed: " + result.message);
        return false;
    }

    const auto previous_project = state.project;
    const auto previous_project_path = state.project_path;
    const auto previous_assets_root = state.assets_root;
    const auto previous_asset_browser_folder = state.asset_browser_folder;
    const bool previous_project_loaded = state.project_loaded;

    state.project = std::move(candidate);
    state.project_path = state.project.project_file;
    state.project_path_text = state.project_path.string();
    state.project_loaded = true;
    state.assets_root = state.project.assets_root();
    state.asset_browser_folder.clear();
    refresh_asset_catalog(state, true);
    const auto startup_resolution = state.asset_catalog.resolve_reference(state.project.startup_scene_reference());
    const auto startup_path = startup_resolution ? startup_resolution.record->absolute_path : state.project.startup_scene_path();
    if (startup_resolution.stale_fallback_path) {
        push_console(state, ConsoleEntry::Level::Warning,
            "Startup scene path updated to match the current asset location: "
            + state.project.startup_scene.generic_string() + " -> " + startup_resolution.record->relative_path.generic_string());
    }
    if (!open_scene(state, startup_path)) {
        state.project = previous_project;
        state.project_path = previous_project_path;
        state.project_loaded = previous_project_loaded;
        state.assets_root = previous_assets_root;
        state.asset_browser_folder = previous_asset_browser_folder;
        if (!state.assets_root.empty()) refresh_asset_catalog(state);
        return false;
    }

    load_managed_metadata(state, false);
    push_console(state, ConsoleEntry::Level::Info, result.message);
    push_console(state, ConsoleEntry::Level::Info, "Project file: " + state.project_path.string());
    return true;
}
bool open_scene(EditorState& state, const std::filesystem::path& path) {
    if (editor_is_playing(state)) stop_play_mode(state);
    if (path.empty()) {
        push_console(state, ConsoleEntry::Level::Error, "Open failed: scene path is empty.");
        return false;
    }

    const auto absolute_path = std::filesystem::absolute(path).lexically_normal();
    const auto target_assets_root = state.project_loaded
        ? state.project.assets_root()
        : derive_assets_root(absolute_path);

    vespera::Scene candidate;
    std::vector<std::string> texture_warnings;
    if (state.project_loaded) {
        (void)register_catalog_texture_assets(candidate, state.asset_catalog, &texture_warnings);
    } else {
        const auto local_catalog = build_read_only_catalog(target_assets_root);
        (void)register_catalog_texture_assets(candidate, local_catalog, &texture_warnings);
    }
    const auto result = vespera::load_scene_text_resilient(candidate, absolute_path, &texture_warnings);
    if (!result) {
        push_console(state, ConsoleEntry::Level::Error, "Open failed: " + result.message);
        return false;
    }
    for (const auto& warning : texture_warnings) {
        push_console(state, ConsoleEntry::Level::Warning, warning);
    }

    state.scene = std::move(candidate);
    state.scene_path = absolute_path;
    state.assets_root = target_assets_root;
    refresh_asset_catalog(state);
    const std::size_t hydrated_asset_references = hydrate_scene_asset_references(state);
    if (hydrated_asset_references != 0) {
        push_console(state, ConsoleEntry::Level::Info, "Updated scene asset reference(s): "
            + std::to_string(hydrated_asset_references));
    }
    state.selection = {};
    state.scene_view.drag_kind = SceneDragKind::None;
    state.scene_view.frame_all_pending = true;
    state.scene_view.frame_selection_pending = false;
    state.scene_view_3d.initialized = false;
    state.scene_view_3d.frame_selection_pending = true;
    state.open_path_text = state.scene_path.string();
    state.save_path_text = state.scene_path.string();
    reset_history_after_open(state);
    push_console(state, ConsoleEntry::Level::Info, "Opened: " + state.scene_path.string());
    push_console(state, ConsoleEntry::Level::Info, result.message);
    return true;
}
bool save_scene(EditorState& state, const std::filesystem::path& path) {
    if (editor_is_playing(state)) {
        push_console(state, ConsoleEntry::Level::Warning, "Save is disabled during Play Mode; Stop first to return to the edit scene.");
        return false;
    }
    commit_active_edit(state);
    if (path.empty()) {
        push_console(state, ConsoleEntry::Level::Error, "Save failed: scene path is empty.");
        return false;
    }

    const auto absolute_path = std::filesystem::absolute(path).lexically_normal();

    // Stable IDs are authoritative. Asset moves repair descriptors on disk, but
    // the currently open edit Scene can still hold an older human-readable
    // fallback path in memory. Canonicalize those references against the live
    // AssetCatalog immediately before serialization so Save can never write an
    // already-stale prefab/material fallback back into a repaired descriptor.
    const std::size_t refreshed_asset_references = hydrate_scene_asset_references(state);
    if (refreshed_asset_references != 0) {
        push_console(state, ConsoleEntry::Level::Info, std::format(
            "Updated {} asset reference path(s) before saving.",
            refreshed_asset_references));
    }

    std::filesystem::path temporary = absolute_path;
    temporary += ".sectorline-tmp";
    std::filesystem::path backup = absolute_path;
    backup += ".bak";

    const auto result = vespera::save_scene_text(state.scene, temporary);
    if (!result) {
        push_console(state, ConsoleEntry::Level::Error, "Save failed: " + result.message);
        return false;
    }

    // Verify the exact bytes we are about to install can be loaded back through
    // the public scene loader before touching the user's current scene file.
    vespera::Scene verification;
    std::vector<std::string> verification_texture_warnings;
    const auto verify_assets_root = state.project_loaded ? state.project.assets_root() : derive_assets_root(absolute_path);
    if (state.project_loaded) {
        (void)register_catalog_texture_assets(verification, state.asset_catalog, &verification_texture_warnings);
    } else {
        const auto local_catalog = build_read_only_catalog(verify_assets_root);
        (void)register_catalog_texture_assets(verification, local_catalog, &verification_texture_warnings);
    }
    const auto verify_result = vespera::load_scene_text_resilient(verification, temporary, &verification_texture_warnings);
    if (!verify_result) {
        std::error_code cleanup_error;
        std::filesystem::remove(temporary, cleanup_error);
        push_console(state, ConsoleEntry::Level::Error, "Save validation failed: " + verify_result.message);
        return false;
    }
    for (const auto& warning : verification_texture_warnings) {
        push_console(state, ConsoleEntry::Level::Warning, "Save validation: " + warning);
    }

    std::error_code ec;
    const bool had_original = std::filesystem::exists(absolute_path, ec) && !ec;
    if (had_original) {
        ec.clear();
        std::filesystem::remove(backup, ec); // Keep at most one previous-save backup.
        ec.clear();
        std::filesystem::rename(absolute_path, backup, ec);
        if (ec) {
            const std::string message = ec.message();
            std::error_code cleanup_error;
            std::filesystem::remove(temporary, cleanup_error);
            push_console(state, ConsoleEntry::Level::Error, "Save failed while creating backup: " + message);
            return false;
        }
    }

    ec.clear();
    std::filesystem::rename(temporary, absolute_path, ec);
    if (ec) {
        const std::string message = ec.message();
        if (had_original) {
            std::error_code rollback_error;
            std::filesystem::rename(backup, absolute_path, rollback_error);
            if (rollback_error) {
                push_console(state, ConsoleEntry::Level::Error,
                    "Save failed and backup rollback also failed: " + rollback_error.message());
                return false;
            }
        }
        std::error_code cleanup_error;
        std::filesystem::remove(temporary, cleanup_error);
        push_console(state, ConsoleEntry::Level::Error, "Save failed while installing verified scene: " + message);
        return false;
    }

    state.scene_path = absolute_path;
    state.assets_root = state.project_loaded ? state.project.assets_root() : derive_assets_root(state.scene_path);
    refresh_asset_catalog(state);
    state.open_path_text = state.scene_path.string();
    state.save_path_text = state.scene_path.string();
    state.saved_state_id = state.current_state_id;
    refresh_dirty(state);
    push_console(state, ConsoleEntry::Level::Info, "Saved + verified: " + state.scene_path.string());
    if (had_original) {
        push_console(state, ConsoleEntry::Level::Info, "Previous scene backup: " + backup.string());
    }
    return true;
}

} // namespace vespera::editor
