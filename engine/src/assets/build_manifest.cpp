#include <vespera/assets/build_manifest.hpp>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <deque>
#include <unordered_set>

namespace vespera {

BuildAssetManifest build_project_asset_manifest(
    const VesperaProject& project,
    const AssetCatalog& catalog
) {
    BuildAssetManifest result;
    std::deque<const AssetRecord*> queue;
    std::unordered_set<std::string> visited;

    const auto enqueue_root = [&](const AssetReference& reference) {
        const auto resolved = catalog.resolve_reference(reference);
        if (!resolved) {
            std::string description = reference.path.lexically_normal().generic_string();
            if (description.empty()) description = reference.asset_id;
            if (!reference.asset_id.empty() && !reference.path.empty()) description += " [" + reference.asset_id + "]";
            result.missing_roots.push_back(std::move(description));
            return;
        }
        if (resolved.stale_fallback_path) {
            result.stale_root_paths.push_back(reference.path.lexically_normal().generic_string()
                + " -> " + resolved.record->relative_path.lexically_normal().generic_string());
        }
        queue.push_back(resolved.record);
    };

    enqueue_root(project.startup_scene_reference());
    if (!project.startup_ui.empty() || !project.startup_ui_asset_id.empty()) enqueue_root(project.startup_ui_reference());
    if (!project.lua_entry.empty() || !project.lua_entry_asset_id.empty()) enqueue_root(project.lua_entry_reference());
    if (!project.game_icon.empty() || !project.game_icon_asset_id.empty()) enqueue_root(project.game_icon_reference());
    for (std::size_t i = 0; i < project.build_includes.size(); ++i) enqueue_root(project.build_include_reference(i));

    while (!queue.empty()) {
        const auto* record = queue.front();
        queue.pop_front();
        if (!record || !visited.insert(record->asset_id).second) continue;
        result.assets.push_back(record);
        for (const auto* dependency : catalog.dependencies_of(record->asset_id)) {
            if (!dependency->resolved) {
                result.broken_dependencies.push_back(dependency);
                continue;
            }
            if (const auto* target = catalog.find_by_id(dependency->target_asset_id)) queue.push_back(target);
        }
    }

    std::sort(result.assets.begin(), result.assets.end(), [](const AssetRecord* a, const AssetRecord* b) {
        return a->relative_path.generic_string() < b->relative_path.generic_string();
    });
    std::sort(result.missing_roots.begin(), result.missing_roots.end());
    result.missing_roots.erase(std::unique(result.missing_roots.begin(), result.missing_roots.end()), result.missing_roots.end());
    std::sort(result.stale_root_paths.begin(), result.stale_root_paths.end());
    result.stale_root_paths.erase(std::unique(result.stale_root_paths.begin(), result.stale_root_paths.end()), result.stale_root_paths.end());
    return result;
}


bool write_build_asset_index(
    const BuildAssetManifest& manifest,
    const std::filesystem::path& output_path,
    std::string* error_message
) {
    if (!manifest.valid()) {
        if (error_message) *error_message = "cannot write build asset index from an invalid manifest";
        return false;
    }
    std::error_code ec;
    if (!output_path.parent_path().empty()) std::filesystem::create_directories(output_path.parent_path(), ec);
    if (ec) {
        if (error_message) *error_message = "could not create build asset index directory: " + ec.message();
        return false;
    }
    std::ofstream output(output_path, std::ios::trunc);
    if (!output) {
        if (error_message) *error_message = "could not create build asset index: " + output_path.string();
        return false;
    }
    output << "vespera_build_asset_index 1\n";
    for (const auto* asset : manifest.assets) {
        if (!asset) continue;
        output << "asset " << std::quoted(asset->asset_id) << " "
               << std::quoted(asset->relative_path.generic_string()) << " "
               << std::quoted(std::string(asset_kind_name(asset->kind))) << "\n";
    }
    output << "end_build_asset_index\n";
    if (!output) {
        if (error_message) *error_message = "failed while writing build asset index";
        return false;
    }
    if (error_message) error_message->clear();
    return true;
}

} // namespace vespera
