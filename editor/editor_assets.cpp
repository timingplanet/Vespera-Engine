#include "editor_assets.hpp"
#include "editor_console.hpp"
#include "editor_selection.hpp"
#include "editor_play_controls.hpp"
#include <vespera/assets/asset_authoring.hpp>
#include <vespera/assets/material_asset.hpp>
#include <vespera/assets/texture_importer.hpp>
#include <vespera/render/texture_data.hpp>
#include <vespera/scene/component_access.hpp>
#include <imgui.h>
#include <imgui_stdlib.h>
#include <format>
#include <string>
#include <utility>
namespace vespera::editor {

std::size_t register_catalog_texture_assets(
    vespera::Scene& scene,
    const vespera::AssetCatalog& catalog,
    std::vector<std::string>* warnings
) {
    std::size_t registered = 0;
    for (const auto* record : catalog.records_of_kind(vespera::AssetKind::Texture)) {
        if (!record) continue;
        const auto imported = vespera::import_texture(record->absolute_path, record->display_name);
        if (imported && imported.texture.valid()) {
            scene.world.add_texture(imported.texture);
        } else {
            scene.world.add_texture(vespera::make_missing_texture_placeholder(record->display_name));
            if (warnings) {
                warnings->push_back("Texture asset '" + record->relative_path.generic_string()
                    + "' could not be decoded; using the Vespera missing-texture checkerboard. "
                    + imported.message);
            }
        }
        ++registered;
    }
    return registered;
}
vespera::AssetCatalog build_read_only_catalog(const std::filesystem::path& assets_root) {
    vespera::AssetCatalog catalog;
    vespera::AssetCatalogRefreshOptions options;
    options.write_metadata = false;
    vespera::AssetCatalogRefreshReport report;
    std::string ignored_error;
    (void)catalog.refresh(assets_root, options, &report, &ignored_error);
    return catalog;
}
void refresh_asset_catalog(EditorState& state, bool report_success, bool force_rehash) {
    if (state.assets_root.empty()) return;
    std::string error;
    vespera::AssetCatalogRefreshReport report;
    vespera::AssetCatalogRefreshOptions options;
    options.force_rehash = force_rehash;
    if (!state.asset_catalog.refresh(state.assets_root, options, &report, &error)) {
        push_console(state, ConsoleEntry::Level::Warning, "Asset catalog refresh failed: " + error);
        return;
    }
    const auto previous_report = state.last_asset_report;
    state.last_asset_report = report;
    repair_selection(state);
    const bool meaningful_change = report.metadata_created != 0
        || report.metadata_updated != 0
        || report.metadata_repaired != 0
        || report.metadata_missing != previous_report.metadata_missing
        || report.orphaned_metadata != previous_report.orphaned_metadata
        || report.dependency_edges != previous_report.dependency_edges
        || report.stable_reference_edges != previous_report.stable_reference_edges
        || report.broken_dependencies != previous_report.broken_dependencies
        || report.stale_fallback_paths != previous_report.stale_fallback_paths
        || report.assets_changed != 0 || report.assets_moved != 0 || report.assets_removed != 0;
    if (report_success || meaningful_change) {
        const std::size_t asset_issues = report.metadata_missing + report.orphaned_metadata
            + report.broken_dependencies + report.stale_fallback_paths;
        const auto level = asset_issues != 0 ? ConsoleEntry::Level::Warning : ConsoleEntry::Level::Info;
        if (asset_issues == 0) {
            push_console(state, level, std::format(
                "Assets refreshed: {} asset(s), no issues.", report.scanned_assets));
        } else {
            push_console(state, level, std::format(
                "Assets refreshed: {} asset(s) | {} missing metadata | {} orphaned metadata | {} broken reference(s) | {} outdated path(s)",
                report.scanned_assets, report.metadata_missing, report.orphaned_metadata,
                report.broken_dependencies, report.stale_fallback_paths));
        }
        for (const auto& change : report.changes) {
            if (change.kind == vespera::AssetCatalogChangeKind::Moved) {
                push_console(state, ConsoleEntry::Level::Info,
                    "Asset moved: " + change.old_path.generic_string() + " -> " + change.new_path.generic_string());
            } else if (change.kind == vespera::AssetCatalogChangeKind::Removed) {
                push_console(state, ConsoleEntry::Level::Warning, "Asset removed: " + change.old_path.generic_string());
            }
        }
    }
    const auto material_report = vespera::hydrate_scene_materials(state.scene, state.asset_catalog);
    if (!material_report.message.empty() && (report_success || meaningful_change)) {
        push_console(state, ConsoleEntry::Level::Warning, "Material hydration: " + material_report.message);
    }
    if (editor_is_playing(state)) {
        (void)vespera::hydrate_scene_materials(state.play_scene, state.asset_catalog);
    }
}
void repair_asset_fallback_paths(EditorState& state, bool persist_project) {
    if (!state.project_loaded) {
        push_console(state, ConsoleEntry::Level::Warning, "Asset reference repair requires an open Vespera project.");
        return;
    }

    auto report = vespera::repair_stable_asset_fallbacks(state.project, state.asset_catalog);
    bool project_saved = true;
    if (persist_project && report.project_references_repaired != 0) {
        const auto saved = vespera::save_vespera_project(state.project, state.project_path);
        project_saved = static_cast<bool>(saved);
        push_console(state, project_saved ? ConsoleEntry::Level::Info : ConsoleEntry::Level::Error, saved.message);
    }
    for (const auto& warning : report.warnings) {
        push_console(state, ConsoleEntry::Level::Warning, "Reference repair: " + warning);
    }
    if (report.descriptor_files_rewritten != 0) refresh_asset_catalog(state, false, true);

    const auto level = (!project_saved || !report.warnings.empty())
        ? ConsoleEntry::Level::Warning : ConsoleEntry::Level::Info;
    push_console(state, level, std::format(
        "Asset reference repair: {} project reference(s) | {} asset reference(s) across {} file(s)",
        report.project_references_repaired,
        report.descriptor_references_repaired,
        report.descriptor_files_rewritten));
}
void request_asset_move(EditorState& state, const vespera::AssetRecord& record) {
    state.asset_move_id = record.asset_id;
    state.asset_move_path = record.relative_path.generic_string();
    state.request_asset_move_popup = true;
}
void draw_asset_move_popup(EditorState& state) {
    if (state.request_asset_move_popup) {
        ImGui::OpenPopup("Move / Rename Asset");
        state.request_asset_move_popup = false;
    }
    bool open = true;
    if (!ImGui::BeginPopupModal("Move / Rename Asset", &open, ImGuiWindowFlags_AlwaysAutoResize)) return;

    const auto* record = state.asset_catalog.find_by_id(state.asset_move_id);
    if (!record) {
        ImGui::TextDisabled("The selected asset is no longer present in the catalog.");
        if (ImGui::Button("Close", ImVec2(100.0f, 0.0f))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    ImGui::TextUnformatted(record->display_name.c_str());
    ImGui::TextDisabled("Asset ID: %s", record->asset_id.c_str());
    ImGui::Spacing();
    ImGui::SetNextItemWidth(520.0f);
    ImGui::InputText("Path within Assets", &state.asset_move_path);
    ImGui::TextDisabled("Moves the asset and its metadata together. Project references are updated automatically.");

    if (ImGui::Button("Move / Rename", ImVec2(140.0f, 0.0f))) {
        const auto old_absolute = record->absolute_path;
        const auto moved = vespera::move_project_asset(state.asset_catalog, record->asset_id, state.asset_move_path);
        if (!moved) {
            push_console(state, ConsoleEntry::Level::Error, "Asset move failed: " + moved.message);
        } else {
            const bool moved_open_scene = !state.scene_path.empty()
                && old_absolute.lexically_normal() == state.scene_path.lexically_normal();
            if (moved_open_scene) state.scene_path = (state.assets_root / moved.new_path).lexically_normal();
            state.asset_browser_folder = moved.new_path.parent_path();
            push_console(state, ConsoleEntry::Level::Info, moved.message);
            refresh_asset_catalog(state, true, false);
            // Safe editor moves also repair readable fallback paths. Manual/external
            // filesystem moves remain supported and can be repaired from Project Settings.
            repair_asset_fallback_paths(state, true);
            state.selection.kind = SelectionKind::Asset;
            state.selection.asset_id = moved.asset_id;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}
std::filesystem::path derive_assets_root(const std::filesystem::path& scene_path) {
    auto parent = scene_path.parent_path();
    if (parent.filename() == "scenes") {
        return parent.parent_path();
    }
    return parent;
}

std::size_t hydrate_scene_asset_references(EditorState& state) {
    std::size_t changed = 0;
    for (auto& entity : state.scene.entities) {
        if (!entity.prefab_source.empty()) {
            const auto resolved = state.asset_catalog.resolve_reference(entity.prefab_source);
            if (resolved) {
                const auto current_path = resolved.record->relative_path.lexically_normal();
                if (entity.prefab_source.asset_id != resolved.record->asset_id
                    || entity.prefab_source.path.lexically_normal() != current_path) {
                    entity.prefab_source.asset_id = resolved.record->asset_id;
                    entity.prefab_source.path = current_path;
                    ++changed;
                }
            }
        }
        if (entity.mesh_renderer && !entity.mesh_renderer->material.empty()) {
            auto& reference = entity.mesh_renderer->material;
            const auto resolved = state.asset_catalog.resolve_reference(reference);
            if (resolved && resolved.record->kind == vespera::AssetKind::Material) {
                const auto current_path = resolved.record->relative_path.lexically_normal();
                if (reference.asset_id != resolved.record->asset_id || reference.path.lexically_normal() != current_path) {
                    reference.asset_id = resolved.record->asset_id;
                    reference.path = current_path;
                    ++changed;
                }
            }
        }
    }
    return changed;
}

} // namespace vespera::editor
