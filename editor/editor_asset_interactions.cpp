#include "editor_asset_interactions.hpp"
#include "editor_assets.hpp"
#include "editor_console.hpp"
#include "editor_history.hpp"
#include <vespera/assets/material_asset.hpp>
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <string>
#include <system_error>
namespace vespera::editor {

std::string canonical_editor_asset_name(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (unsigned char c : value) {
        if (c >= 'A' && c <= 'Z') c = static_cast<unsigned char>(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) result.push_back(static_cast<char>(c));
    }
    return result;
}
vespera::TextureId scene_texture_for_asset(
    const EditorState& state,
    const vespera::AssetRecord& record
) {
    if (record.kind != vespera::AssetKind::Texture) return vespera::kInvalidTexture;
    const std::string wanted = canonical_editor_asset_name(record.display_name);
    const auto& textures = state.scene.world.textures();
    for (std::size_t i = 0; i < textures.size(); ++i) {
        if (canonical_editor_asset_name(textures[i].name) == wanted) {
            return static_cast<vespera::TextureId>(i);
        }
    }
    return vespera::kInvalidTexture;
}
const vespera::AssetRecord* asset_record_from_payload(EditorState& state, const ImGuiPayload* payload) {
    if (!payload || !payload->Data || payload->DataSize <= 1) return nullptr;
    const char* value = static_cast<const char*>(payload->Data);
    if (value[payload->DataSize - 1] != '\0') return nullptr;
    return state.asset_catalog.find_by_id(value);
}
void begin_project_asset_drag(const vespera::AssetRecord& record) {
    if (!ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) return;
    ImGui::SetDragDropPayload("VESPERA_PROJECT_ASSET", record.asset_id.c_str(), record.asset_id.size() + 1);
    ImGui::TextUnformatted(record.display_name.c_str());
    ImGui::TextDisabled("%s", vespera::asset_kind_name(record.kind).data());
    if (record.kind == vespera::AssetKind::EntityPrefab) ImGui::TextDisabled("Drop into Scene to instantiate");
    else if (record.kind == vespera::AssetKind::Texture) ImGui::TextDisabled("Drop onto a Texture field in Inspector");
    else if (record.kind == vespera::AssetKind::Material) ImGui::TextDisabled("Drop onto a Mesh Renderer Material field");
    else if (record.kind == vespera::AssetKind::Font) ImGui::TextDisabled("Drop onto a runtime UI Font field");
    ImGui::EndDragDropSource();
}
bool accept_texture_asset_drop(EditorState& state, vespera::TextureId& texture, const char* field_name) {
    if (!ImGui::BeginDragDropTarget()) return false;
    bool changed = false;
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VESPERA_PROJECT_ASSET")) {
        if (const auto* record = asset_record_from_payload(state, payload); record && record->kind == vespera::AssetKind::Texture) {
            const auto resolved = scene_texture_for_asset(state, *record);
            if (resolved != vespera::kInvalidTexture) {
                texture = resolved;
                changed = true;
                append_command_audit(state, vespera::editor::EditorCommandKind::AssignTextureAsset,
                    std::string("Assign texture asset to ") + field_name, true,
                    state.current_state_id, state.current_state_id,
                    state.selection.object_id, record->asset_id);
            } else {
                push_console(state, ConsoleEntry::Level::Warning,
                    "Texture drop ignored: asset is not registered as a texture resource in the open scene: "
                    + record->relative_path.generic_string());
            }
        }
    }
    ImGui::EndDragDropTarget();
    return changed;
}
std::filesystem::path unique_material_asset_path(const EditorState& state) {
    std::filesystem::path folder = state.asset_browser_folder;
    if (folder.empty()) folder = "materials";
    const auto directory = state.assets_root / folder;
    std::filesystem::path candidate = directory / "new_material.slmat";
    std::error_code ec;
    if (!std::filesystem::exists(candidate, ec)) return candidate;
    for (int suffix = 2; suffix < 10000; ++suffix) {
        candidate = directory / std::format("new_material_{}.slmat", suffix);
        ec.clear();
        if (!std::filesystem::exists(candidate, ec)) return candidate;
    }
    return directory / "new_material_unique.slmat";
}
bool command_create_material_asset(EditorState& state) {
    if (state.assets_root.empty()) {
        push_console(state, ConsoleEntry::Level::Warning, "Create Material Asset requires an open project/assets root.");
        return false;
    }
    const auto path = unique_material_asset_path(state);
    vespera::MaterialAsset material;
    material.name = path.stem().string();
    material.properties.shader = vespera::BuiltinMaterialShader::Lit;
    const auto saved = vespera::save_material_asset(path, material);
    if (!saved) {
        push_console(state, ConsoleEntry::Level::Error, "Material asset creation failed: " + saved.message);
        append_command_audit(state, vespera::editor::EditorCommandKind::CreateMaterialAsset,
            "Create Material Asset", false, state.current_state_id, state.current_state_id);
        return false;
    }
    refresh_asset_catalog(state, true, true);
    const auto relative = std::filesystem::relative(path, state.assets_root).lexically_normal();
    if (const auto* record = state.asset_catalog.find(relative.generic_string())) {
        state.selection = {SelectionKind::Asset, 0, kNoSubSelection, vespera::kInvalidSceneObjectId, kNoSubSelection, record->asset_id};
        state.material_asset_edit = {};
        append_command_audit(state, vespera::editor::EditorCommandKind::CreateMaterialAsset,
            "Create Material Asset", true, state.current_state_id, state.current_state_id,
            vespera::kInvalidSceneObjectId, record->asset_id);
    }
    state.asset_browser_folder = relative.parent_path();
    push_console(state, ConsoleEntry::Level::Info, "Created Material asset: " + relative.generic_string());
    return true;
}
bool accept_material_asset_drop(EditorState& state, vespera::MeshRendererComponent& mesh) {
    if (!ImGui::BeginDragDropTarget()) return false;
    bool changed = false;
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VESPERA_PROJECT_ASSET")) {
        if (const auto* record = asset_record_from_payload(state, payload); record && record->kind == vespera::AssetKind::Material) {
            mesh.material = {record->asset_id, record->relative_path};
            mesh.material_resolved = false;
            (void)vespera::hydrate_scene_materials(state.scene, state.asset_catalog);
            append_command_audit(state, vespera::editor::EditorCommandKind::AssignMaterialAsset,
                "Assign Material asset", true, state.current_state_id, state.current_state_id,
                state.selection.object_id, record->asset_id);
            changed = true;
        }
    }
    ImGui::EndDragDropTarget();
    return changed;
}

} // namespace vespera::editor
