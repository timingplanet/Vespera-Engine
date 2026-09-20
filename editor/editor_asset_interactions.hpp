#pragma once
#include "editor_state.hpp"
#include <filesystem>
#include <string>
#include <string_view>
namespace vespera::editor {
std::string canonical_editor_asset_name(std::string_view value);
vespera::TextureId scene_texture_for_asset(const EditorState& state, const vespera::AssetRecord& record);
const vespera::AssetRecord* asset_record_from_payload(EditorState& state, const ImGuiPayload* payload);
void begin_project_asset_drag(const vespera::AssetRecord& record);
bool accept_texture_asset_drop(EditorState& state, vespera::TextureId& texture, const char* field_name);
std::filesystem::path unique_material_asset_path(const EditorState& state);
bool command_create_material_asset(EditorState& state);
bool accept_material_asset_drop(EditorState& state, vespera::MeshRendererComponent& mesh);
}
