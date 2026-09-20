#pragma once
#include "editor_state.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
namespace vespera::editor {
std::string sanitize_asset_stem(std::string value);
std::optional<std::string> project_relative_asset_key(const EditorState& state, const std::filesystem::path& absolute_path);
std::optional<std::filesystem::path> resolve_prefab_source_path(const EditorState& state, const vespera::AssetReference& source);
std::optional<vespera::AssetReference> project_asset_reference(const EditorState& state, const std::filesystem::path& absolute_path);
std::filesystem::path unique_prefab_asset_path(const EditorState& state, std::string_view entity_name);
bool save_prefab_verified(EditorState& state, const vespera::Entity& entity, const std::filesystem::path& path);
bool command_create_prefab_from_selected(EditorState& state);
bool command_apply_selected_to_prefab(EditorState& state);
bool command_revert_selected_from_prefab(EditorState& state);
bool command_unpack_selected_prefab(EditorState& state);
bool command_instantiate_prefab(EditorState& state, const std::filesystem::path& prefab_path, std::optional<vespera::Vec3> world_position = std::nullopt, EditorCommandKind command_kind = EditorCommandKind::InstantiatePrefab);
}
