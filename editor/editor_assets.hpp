#pragma once
#include "editor_state.hpp"
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>
namespace vespera::editor {
std::size_t register_catalog_texture_assets(Scene& scene, const AssetCatalog& catalog, std::vector<std::string>* warnings = nullptr);
AssetCatalog build_read_only_catalog(const std::filesystem::path& assets_root);
void refresh_asset_catalog(EditorState& state, bool report_success = false, bool force_rehash = false);
void repair_asset_fallback_paths(EditorState& state, bool persist_project = true);
void request_asset_move(EditorState& state, const AssetRecord& record);
void draw_asset_move_popup(EditorState& state);
std::filesystem::path derive_assets_root(const std::filesystem::path& scene_path);
std::size_t hydrate_scene_asset_references(EditorState& state);
}
