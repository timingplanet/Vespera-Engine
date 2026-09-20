#include "editor_hierarchy.hpp"

#include "editor_history.hpp"
#include "editor_prefabs.hpp"
#include "editor_scene_commands.hpp"
#include "editor_selection.hpp"
#include "editor_state.hpp"
#include "editor_style.hpp"

#include <imgui.h>

#include <format>
#include <optional>
#include <utility>

namespace vespera::editor {

void draw_hierarchy(EditorState& state) {
    ImGui::Begin("Hierarchy");

    const auto& palette = editor_ui_palette();
    if (ImGui::BeginTable(
            "##HierarchyHeader", 2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableSetupColumn("Summary", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Create", ImGuiTableColumnFlags_WidthFixed, 92.0f);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%zu entities  /  %zu sectors", state.scene.entities.size(), state.scene.world.sectors().size());
        if (state.selection.kind == SelectionKind::Entity && state.selected_entity_ids.size() > 1) {
            ImGui::SameLine(0.0f, 10.0f);
            ImGui::TextColored(palette.warning, "%zu selected", state.selected_entity_ids.size());
        }
        ImGui::TableSetColumnIndex(1);
        if (ImGui::Button("+ Create", ImVec2(-1.0f, 0.0f))) ImGui::OpenPopup("HierarchyCreateMenu");
        ImGui::EndTable();
    }
    if (ImGui::BeginPopup("HierarchyCreateMenu")) {
        if (ImGui::MenuItem("Empty Entity")) command_create_entity(state);
        if (ImGui::BeginMenu("3D Object")) {
            if (ImGui::MenuItem("Cube")) command_create_primitive_entity(state, vespera::PrimitiveMeshType::Cube);
            if (ImGui::MenuItem("Plane")) command_create_primitive_entity(state, vespera::PrimitiveMeshType::Plane);
            if (ImGui::MenuItem("Cylinder")) command_create_primitive_entity(state, vespera::PrimitiveMeshType::Cylinder);
            if (ImGui::MenuItem("Sphere")) command_create_primitive_entity(state, vespera::PrimitiveMeshType::Sphere);
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Sprite Entity")) command_create_sprite_entity(state);
        if (ImGui::MenuItem("Trigger Volume")) command_create_trigger_entity(state);
        if (ImGui::MenuItem("Point Light")) command_create_point_light_entity(state);
        ImGui::Separator();
        if (ImGui::MenuItem("Sector")) command_create_sector(state);
        ImGui::EndPopup();
    }
    ImGui::Separator();

    if (ImGui::Selectable("Camera", state.selection.kind == SelectionKind::Camera)) {
        commit_active_edit(state);
        state.selection = {SelectionKind::Camera, 0};
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            state.scene_view.frame_selection_pending = true;
        }
    }

    std::optional<std::size_t> duplicate_sector;
    std::optional<std::size_t> delete_sector;
    const std::string sectors_label = std::format("Sectors ({})###Sectors", state.scene.world.sectors().size());
    if (ImGui::TreeNodeEx(sectors_label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginPopupContextItem("sector_group_context")) {
            if (ImGui::MenuItem("Create Sector")) {
                command_create_sector(state);
            }
            ImGui::EndPopup();
        }
        const auto& sectors = state.scene.world.sectors();
        for (std::size_t i = 0; i < sectors.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            const bool selected = state.selection.kind == SelectionKind::Sector && state.selection.index == i;
            if (ImGui::Selectable(sectors[i].name.c_str(), selected)) {
                commit_active_edit(state);
                state.selection = {SelectionKind::Sector, i};
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    state.scene_view.frame_selection_pending = true;
                }
            }
            if (ImGui::BeginPopupContextItem("sector_context")) {
                if (ImGui::MenuItem("Duplicate", "Ctrl+D")) duplicate_sector = i;
                if (ImGui::MenuItem("Delete", "Del")) delete_sector = i;
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    if (duplicate_sector && *duplicate_sector < state.scene.world.sectors().size()) {
        state.selection = {SelectionKind::Sector, *duplicate_sector};
        command_duplicate_selection(state);
    } else if (delete_sector && *delete_sector < state.scene.world.sectors().size()) {
        state.selection = {SelectionKind::Sector, *delete_sector};
        command_delete_selection(state);
    }

    std::optional<std::size_t> duplicate_entity;
    std::optional<std::size_t> delete_entity;
    std::optional<std::pair<vespera::SceneObjectId, vespera::SceneObjectId>> reorder_entity;
    std::optional<std::pair<vespera::SceneObjectId, vespera::SceneObjectId>> reparent_entity;
    std::optional<vespera::SceneObjectId> unparent_entity;
    std::optional<vespera::SceneObjectId> apply_prefab_entity;
    std::optional<vespera::SceneObjectId> revert_prefab_entity;
    std::optional<vespera::SceneObjectId> unpack_prefab_entity;
    state.entity_filter.Draw("Search entities##HierarchyFilter", -1.0f);
    const std::string entities_label = std::format("Entities ({})###Entities", state.scene.entities.size());
    if (ImGui::TreeNodeEx(entities_label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginPopupContextItem("entity_group_context")) {
            if (ImGui::MenuItem("Create Empty Entity")) command_create_entity(state);
            if (ImGui::BeginMenu("3D Object")) {
                if (ImGui::MenuItem("Cube")) command_create_primitive_entity(state, vespera::PrimitiveMeshType::Cube);
                if (ImGui::MenuItem("Plane")) command_create_primitive_entity(state, vespera::PrimitiveMeshType::Plane);
                if (ImGui::MenuItem("Cylinder")) command_create_primitive_entity(state, vespera::PrimitiveMeshType::Cylinder);
                if (ImGui::MenuItem("Sphere")) command_create_primitive_entity(state, vespera::PrimitiveMeshType::Sphere);
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Create Sprite Entity")) command_create_sprite_entity(state);
            if (ImGui::MenuItem("Create Trigger Volume")) command_create_trigger_entity(state);
            if (ImGui::MenuItem("Create Point Light")) command_create_point_light_entity(state);
            ImGui::EndPopup();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VESPERA_ENTITY_HIERARCHY")) {
                if (payload->DataSize == sizeof(vespera::SceneObjectId)) {
                    unparent_entity = *static_cast<const vespera::SceneObjectId*>(payload->Data);
                }
            }
            ImGui::EndDragDropTarget();
        }

        for (std::size_t i = 0; i < state.scene.entities.size(); ++i) {
            ImGui::PushID(static_cast<int>(i) + 100000);
            const auto& entity = state.scene.entities[i];
            const bool selected = state.selection.kind == SelectionKind::Entity && entity_is_multi_selected(state, entity.id);

            std::string metadata;
            if (entity.tag != "Untagged") metadata += std::format(" [{}]", entity.tag);
            if (!entity.prefab_source.empty()) metadata += " [Prefab]";
            if (entity.sprite_renderer) metadata += " [Sprite]";
            if (entity.mesh_renderer) metadata += " [Mesh]";
            if (entity.cylinder_collider) metadata += entity.cylinder_collider->is_trigger ? " [Trigger]" : " [Collider]";
            if (entity.point_light) metadata += " [Light]";
            if (!entity.managed_scripts.empty()) metadata += std::format(" [C#:{}]", entity.managed_scripts.size());
            if (!entity.enabled) metadata += " [Disabled]";

            const std::string filter_text = entity.name + metadata + " " + entity.layer;
            if (!state.entity_filter.PassFilter(filter_text.c_str())) {
                ImGui::PopID();
                continue;
            }

            const float hierarchy_indent = static_cast<float>(entity_hierarchy_depth(state, entity)) * 16.0f;
            if (hierarchy_indent > 0.0f) ImGui::Indent(hierarchy_indent);
            std::string display_name = entity.name;
            if (entity.parent_id != vespera::kInvalidSceneObjectId) display_name = "> " + display_name;
            if (!entity.enabled) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            const bool clicked = ImGui::Selectable(display_name.c_str(), selected);
            if (!entity.enabled) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(entity.name.c_str());
                ImGui::Separator();
                ImGui::TextDisabled("Tag"); ImGui::SameLine(70.0f); ImGui::TextUnformatted(entity.tag.c_str());
                ImGui::TextDisabled("Layer"); ImGui::SameLine(70.0f); ImGui::TextUnformatted(entity.layer.c_str());
                if (!metadata.empty()) ImGui::TextDisabled("%s", metadata.c_str());
                ImGui::TextDisabled("Double-click to frame  |  drag to parent/reorder");
                ImGui::EndTooltip();
            }
            if (clicked) {
                commit_active_edit(state);
                const auto& io = ImGui::GetIO();
                if (io.KeyShift) select_entity_range(state, i);
                else if (io.KeyCtrl) toggle_entity_selection(state, i);
                else select_entity(state, i);
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) state.scene_view.frame_selection_pending = true;
            }
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                const auto dragged_id = entity.id;
                ImGui::SetDragDropPayload("VESPERA_ENTITY_HIERARCHY", &dragged_id, sizeof(dragged_id));
                ImGui::TextUnformatted(entity.name.c_str());
                ImGui::TextDisabled("Drop on entity = parent  |  Ctrl-drop = reorder  |  drop on Entities = unparent");
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VESPERA_ENTITY_HIERARCHY")) {
                    if (payload->DataSize == sizeof(vespera::SceneObjectId)) {
                        const auto dragged_id = *static_cast<const vespera::SceneObjectId*>(payload->Data);
                        if (dragged_id != entity.id) {
                            if (ImGui::GetIO().KeyCtrl) reorder_entity = std::pair{dragged_id, entity.id};
                            else reparent_entity = std::pair{dragged_id, entity.id};
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
            if (ImGui::BeginPopupContextItem("entity_context")) {
                if (ImGui::MenuItem("Duplicate", "Ctrl+D")) duplicate_entity = i;
                if (entity.parent_id != vespera::kInvalidSceneObjectId && ImGui::MenuItem("Unparent")) unparent_entity = entity.id;
                if (!entity.prefab_source.empty()) {
                    ImGui::SeparatorText("Prefab");
                    if (ImGui::MenuItem("Apply to Prefab")) apply_prefab_entity = entity.id;
                    if (ImGui::MenuItem("Revert from Prefab")) revert_prefab_entity = entity.id;
                    if (ImGui::MenuItem("Unpack Prefab")) unpack_prefab_entity = entity.id;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Delete", "Del")) delete_entity = i;
                ImGui::EndPopup();
            }
            if (hierarchy_indent > 0.0f) ImGui::Unindent(hierarchy_indent);
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    if (duplicate_entity && *duplicate_entity < state.scene.entities.size()) {
        select_entity(state, *duplicate_entity);
        command_duplicate_selection(state);
    } else if (delete_entity && *delete_entity < state.scene.entities.size()) {
        select_entity(state, *delete_entity);
        command_delete_selection(state);
    } else if (reparent_entity) {
        command_reparent_entity(state, reparent_entity->first, reparent_entity->second);
    } else if (unparent_entity) {
        command_reparent_entity(state, *unparent_entity, vespera::kInvalidSceneObjectId);
    } else if (reorder_entity) {
        command_reorder_entity(state, reorder_entity->first, reorder_entity->second);
    } else if (apply_prefab_entity) {
        if (const auto index = entity_index_from_id(state, *apply_prefab_entity)) {
            select_entity(state, *index);
            command_apply_selected_to_prefab(state);
        }
    } else if (revert_prefab_entity) {
        if (const auto index = entity_index_from_id(state, *revert_prefab_entity)) {
            select_entity(state, *index);
            command_revert_selected_from_prefab(state);
        }
    } else if (unpack_prefab_entity) {
        if (const auto index = entity_index_from_id(state, *unpack_prefab_entity)) {
            select_entity(state, *index);
            command_unpack_selected_prefab(state);
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled("Ctrl/Shift-click multi-select  |  Ctrl-drag reorder  |  %zu sprite clip%s",
        state.scene.sprite_clips.size(), state.scene.sprite_clips.size() == 1 ? "" : "s");
    ImGui::End();
}


} // namespace vespera::editor
