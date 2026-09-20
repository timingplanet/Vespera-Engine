#include "editor_scene_3d_panel.hpp"

#include "editor_asset_interactions.hpp"
#include "editor_command_runtime.hpp"
#include "editor_console.hpp"
#include "editor_history.hpp"
#include "editor_prefabs.hpp"
#include "editor_scene_3d.hpp"
#include "editor_selection.hpp"
#include "editor_state.hpp"
#include "editor_style.hpp"
#include "editor_view_helpers.hpp"

#include <vespera/assets/material_asset.hpp>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace vespera::editor {

ImU32 gizmo_axis_color(GizmoAxis axis, bool active) {
    ImVec4 color;
    switch (axis) {
        case GizmoAxis::X: color = {0.92f, 0.25f, 0.24f, 1.0f}; break;
        case GizmoAxis::Y: color = {0.35f, 0.88f, 0.34f, 1.0f}; break;
        case GizmoAxis::Z: color = {0.27f, 0.52f, 0.98f, 1.0f}; break;
        case GizmoAxis::Uniform: color = {0.88f, 0.88f, 0.90f, 1.0f}; break;
        default: color = {0.7f, 0.7f, 0.72f, 1.0f}; break;
    }
    if (active) {
        color.x = std::min(color.x + 0.16f, 1.0f);
        color.y = std::min(color.y + 0.16f, 1.0f);
        color.z = std::min(color.z + 0.16f, 1.0f);
    }
    return ImGui::GetColorU32(color);
}


void draw_arc(ImDrawList* draw, ImVec2 center, float radius, float begin, float end, ImU32 color, float thickness) {
    constexpr int segments = 28;
    constexpr float two_pi = 6.2831853071795864769f;
    while (end < begin) end += two_pi;
    ImVec2 previous{center.x + std::cos(begin) * radius, center.y + std::sin(begin) * radius};
    for (int i = 1; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        const float angle = begin + (end - begin) * t;
        ImVec2 next{center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius};
        draw->AddLine(previous, next, color, thickness);
        previous = next;
    }
}










void draw_scene_view_3d(EditorState& state) {
    auto& view = state.scene_view_3d;
    view.visible = false;
    view.hovered = false;
    view.focused = false;

    if (!ImGui::Begin("Scene")) {
        ImGui::End();
        return;
    }

    ImGuiWindow* current_window = ImGui::GetCurrentWindow();
    const bool tab_visible = !current_window->DockIsActive || current_window->DockTabIsVisible;

    if (!view.initialized) {
        view.camera = state.scene.camera;
        view.camera.near_plane = std::max(view.camera.near_plane, 0.02f);
        view.initialized = true;
    }

    auto tool_button = [&](const char* label, SceneTool tool, const char* tooltip) {
        const bool active = view.tool == tool;
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(label, ImVec2(30.0f, 0.0f))) {
            if (view.gizmo_dragging) commit_3d_gizmo_drag(state);
            view.tool = tool;
        }
        if (active) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
    };
    tool_button("Q", SceneTool::View, "View tool (Q)");
    ImGui::SameLine(); tool_button("W", SceneTool::Move, "Move tool (W)");
    ImGui::SameLine(); tool_button("E", SceneTool::Rotate, "Rotate tool (E)");
    ImGui::SameLine(); tool_button("R", SceneTool::Scale, "Scale tool (R)");

    draw_toolbar_separator();
    if (ImGui::Button("Frame")) view.frame_selection_pending = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Frame selected (F)");
    ImGui::SameLine();
    if (ImGui::Button("Camera")) {
        view.camera = state.scene.camera;
        view.initialized = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset the editor view to the authored scene camera");

    draw_toolbar_separator();
    if (ImGui::Button(view.local_space ? "Local" : "Global")) view.local_space = !view.local_space;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Transform orientation: %s", view.local_space ? "Local" : "Global");
    ImGui::SameLine();
    if (ImGui::Button(view.center_pivot ? "Center" : "Pivot")) view.center_pivot = !view.center_pivot;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Multi-selection gizmo position: %s", view.center_pivot ? "selection center" : "active entity pivot");

    draw_toolbar_separator();
    ImGui::Checkbox("Snap", &view.snap_enabled);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(68.0f);
    if (view.tool == SceneTool::Move) {
        ImGui::DragFloat("##tool_snap", &view.translation_snap, 0.05f, 0.01f, 10.0f, "%.2f");
        view.translation_snap = std::max(view.translation_snap, 0.01f);
    } else if (view.tool == SceneTool::Rotate) {
        ImGui::DragFloat("##tool_snap", &view.rotation_snap_degrees, 1.0f, 1.0f, 90.0f, "%.0f deg");
        view.rotation_snap_degrees = std::clamp(view.rotation_snap_degrees, 1.0f, 90.0f);
    } else if (view.tool == SceneTool::Scale) {
        ImGui::DragFloat("##tool_snap", &view.scale_snap, 0.05f, 0.01f, 2.0f, "%.2f");
        view.scale_snap = std::max(view.scale_snap, 0.01f);
    } else {
        ImGui::TextDisabled("--");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Speed");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(64.0f);
    ImGui::DragFloat("##scene_move_speed", &view.move_speed, 0.1f, 0.25f, 30.0f, "%.1f");
    view.move_speed = std::clamp(view.move_speed, 0.25f, 30.0f);

    if (ImGui::GetContentRegionAvail().x > 280.0f) {
        ImGui::SameLine();
        ImGui::TextDisabled("RMB + WASD/QE fly   |   Alt bypass snap");
    }

    if (view.frame_selection_pending) frame_3d_selection(state);

    ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    canvas_size.x = std::max(canvas_size.x, 64.0f);
    canvas_size.y = std::max(canvas_size.y, 64.0f);
    const ImVec2 canvas_min = ImGui::GetCursorScreenPos();

    if (!state.direct_render_previews && view.preview_texture != ImTextureID_Invalid) {
        ImGui::Image(
            ImTextureRef(view.preview_texture),
            canvas_size,
            view.preview_uv0,
            view.preview_uv1
        );
    } else {
        ImGui::InvisibleButton(
            "##scene_view_3d_canvas",
            canvas_size,
            ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight
        );
    }
    const ImVec2 canvas_max = ImGui::GetItemRectMax();
    view.content_min = ImGui::GetItemRectMin();
    view.content_max = canvas_max;
    view.hovered = tab_visible && ImGui::IsItemHovered();
    view.focused = tab_visible && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    view.visible = tab_visible && canvas_size.x > 1.0f && canvas_size.y > 1.0f;

    if (state.direct_render_previews && view.visible && state.direct_scene_draw_callback) {
        ImDrawList* preview_draw = ImGui::GetWindowDrawList();
        preview_draw->AddCallback(state.direct_scene_draw_callback, state.direct_scene_draw_user_data);
        preview_draw->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    }

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VESPERA_PROJECT_ASSET")) {
            if (const auto* record = asset_record_from_payload(state, payload); record) {
                if (record->kind == vespera::AssetKind::EntityPrefab) {
                    const float cos_pitch = std::cos(view.camera.pitch);
                    const vespera::Vec3 forward{
                        std::sin(view.camera.yaw) * cos_pitch,
                        std::sin(view.camera.pitch),
                        std::cos(view.camera.yaw) * cos_pitch,
                    };
                    const vespera::Vec3 drop_position{
                        view.camera.position.x + forward.x * 3.0f,
                        view.camera.position.y + forward.y * 3.0f,
                        view.camera.position.z + forward.z * 3.0f,
                    };
                    command_instantiate_prefab(state, record->absolute_path, drop_position,
                        vespera::editor::EditorCommandKind::DropPrefabIntoScene);
                } else if (record->kind == vespera::AssetKind::Material) {
                    const auto picked = pick_entity_in_3d(state, canvas_min, canvas_size, ImGui::GetIO().MousePos);
                    if (picked && *picked < state.scene.entities.size() && state.scene.entities[*picked].mesh_renderer) {
                        commit_active_edit(state);
                        const std::uint64_t before_state_id = state.current_state_id;
                        HistorySnapshot before = capture_snapshot(state);
                        auto& entity = state.scene.entities[*picked];
                        entity.mesh_renderer->material = {record->asset_id, record->relative_path};
                        entity.mesh_renderer->material_resolved = false;
                        (void)vespera::hydrate_scene_materials(state.scene, state.asset_catalog);
                        const auto entity_id = entity.id;
                        select_entity(state, *picked);
                        record_immediate_edit(state, std::move(before), "Assign Material from Project");
                        append_command_audit(state, vespera::editor::EditorCommandKind::AssignMaterialAsset,
                            "Assign Material from Project", true, before_state_id, state.current_state_id,
                            entity_id, record->asset_id);
                        push_console(state, ConsoleEntry::Level::Info,
                            "Assigned Material '" + record->display_name + "' to " + entity.name + ".");
                    } else {
                        push_console(state, ConsoleEntry::Level::Warning,
                            "Material drop needs a Mesh Renderer under the cursor.");
                    }
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImGuiIO& io = ImGui::GetIO();
    if (view.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        ImGui::SetWindowFocus();
    }

    const bool navigating = view.hovered && ImGui::IsMouseDown(ImGuiMouseButton_Right);
    if (navigating) {
        constexpr float kLookSensitivity = 0.0045f;
        view.camera.yaw += io.MouseDelta.x * kLookSensitivity;
        view.camera.pitch = std::clamp(
            view.camera.pitch - io.MouseDelta.y * kLookSensitivity,
            -1.50f,
            1.50f
        );

        const float dt = std::clamp(io.DeltaTime, 0.0f, 0.1f);
        const float speed = view.move_speed * (ImGui::IsKeyDown(ImGuiKey_LeftShift) ? 3.0f : 1.0f);
        const float cos_pitch = std::cos(view.camera.pitch);
        const vespera::Vec3 forward{
            std::sin(view.camera.yaw) * cos_pitch,
            std::sin(view.camera.pitch),
            std::cos(view.camera.yaw) * cos_pitch,
        };
        const vespera::Vec3 right{
            std::cos(view.camera.yaw),
            0.0f,
            -std::sin(view.camera.yaw),
        };

        float move_forward = 0.0f;
        float move_right = 0.0f;
        float move_up = 0.0f;
        if (ImGui::IsKeyDown(ImGuiKey_W)) move_forward += 1.0f;
        if (ImGui::IsKeyDown(ImGuiKey_S)) move_forward -= 1.0f;
        if (ImGui::IsKeyDown(ImGuiKey_D)) move_right += 1.0f;
        if (ImGui::IsKeyDown(ImGuiKey_A)) move_right -= 1.0f;
        if (ImGui::IsKeyDown(ImGuiKey_E)) move_up += 1.0f;
        if (ImGui::IsKeyDown(ImGuiKey_Q)) move_up -= 1.0f;

        const float step = speed * dt;
        view.camera.position.x += (forward.x * move_forward + right.x * move_right) * step;
        view.camera.position.y += (forward.y * move_forward + move_up) * step;
        view.camera.position.z += (forward.z * move_forward + right.z * move_right) * step;
    }

    if (view.hovered && std::abs(io.MouseWheel) > 0.001f && !navigating) {
        view.move_speed = std::clamp(view.move_speed * std::pow(1.15f, io.MouseWheel), 0.25f, 30.0f);
    }

    if (view.focused && !navigating && !io.WantTextInput && !view.gizmo_dragging) {
        if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) view.tool = SceneTool::View;
        if (ImGui::IsKeyPressed(ImGuiKey_W, false)) view.tool = SceneTool::Move;
        if (ImGui::IsKeyPressed(ImGuiKey_E, false)) view.tool = SceneTool::Rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) view.tool = SceneTool::Scale;
        if (ImGui::IsKeyPressed(ImGuiKey_F, false)) view.frame_selection_pending = true;
    }

    const auto selected_index = selected_entity_index(state);
    const ImVec2 live_canvas_size{canvas_max.x - canvas_min.x, canvas_max.y - canvas_min.y};
    bool gizmo_consumed_click = false;

    if (selected_index && *selected_index < state.scene.entities.size() && view.tool != SceneTool::View) {
        auto& entity = state.scene.entities[*selected_index];
        const vespera::Vec3 gizmo_origin = selected_entity_gizmo_pivot(state, view.center_pivot);
        const auto origin_projected = project_scene_point(view.camera, canvas_min, live_canvas_size, gizmo_origin);
        if (origin_projected.visible) {
            const float world_per_pixel = (2.0f * origin_projected.depth
                * std::tan(std::clamp(view.camera.vertical_fov_degrees, 30.0f, 130.0f) * kEditorDegreesToRadians * 0.5f))
                / std::max(live_canvas_size.y, 1.0f);
            const float gizmo_world_length = std::clamp(world_per_pixel * 76.0f, 0.15f, 25.0f);

            std::array<GizmoAxis, 3> axes{GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z};
            struct AxisProjection { GizmoAxis axis; ImVec2 end; ImVec2 unit; float screen_length; };
            std::vector<AxisProjection> projected_axes;
            projected_axes.reserve(3);
            for (const auto axis : axes) {
                const auto world_axis = gizmo_world_axis(editor_world_transform(state, entity), axis, view.local_space);
                const auto endpoint = project_scene_point(
                    view.camera, canvas_min, live_canvas_size,
                    vec3_add(gizmo_origin, vec3_mul(world_axis, gizmo_world_length))
                );
                if (!endpoint.visible) continue;
                const float dx = endpoint.screen.x - origin_projected.screen.x;
                const float dy = endpoint.screen.y - origin_projected.screen.y;
                const float len = std::sqrt(dx * dx + dy * dy);
                if (len < 12.0f) continue;
                projected_axes.push_back({axis, endpoint.screen, {dx / len, dy / len}, len});
            }

            if (view.tool == SceneTool::Move || view.tool == SceneTool::Scale) {
                if (view.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    float best_distance_sq = 8.0f * 8.0f;
                    GizmoAxis picked = GizmoAxis::None;
                    ImVec2 picked_unit{};
                    for (const auto& axis : projected_axes) {
                        const float d = point_segment_distance_sq(io.MousePos, origin_projected.screen, axis.end);
                        if (d <= best_distance_sq) {
                            best_distance_sq = d;
                            picked = axis.axis;
                            picked_unit = axis.unit;
                        }
                    }
                    if (view.tool == SceneTool::Scale
                        && screen_distance_sq(io.MousePos, origin_projected.screen) <= 9.0f * 9.0f) {
                        picked = GizmoAxis::Uniform;
                        picked_unit = {0.7071067f, -0.7071067f};
                    }
                    if (picked != GizmoAxis::None) {
                        commit_active_edit(state);
                        view.active_axis = picked;
                        view.drag_entity_id = entity.id;
                        view.drag_start_transform = editor_world_transform(state, entity);
                        view.drag_start_transforms = capture_selected_entity_transforms(state);
                        view.drag_pivot = gizmo_origin;
                        view.drag_start_mouse = io.MousePos;
                        view.drag_axis_screen_unit = picked_unit;
                        view.drag_world_length = gizmo_world_length;
                        view.gizmo_dragging = true;
                        view.gizmo_drag_changed = false;
                        begin_edit(state, capture_snapshot(state),
                            view.tool == SceneTool::Move ? "Move entity gizmo" : "Scale entity gizmo");
                        gizmo_consumed_click = true;
                    }
                }
            } else if (view.tool == SceneTool::Rotate) {
                constexpr float ring_radius = 54.0f;
                if (view.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    const float dx = io.MousePos.x - origin_projected.screen.x;
                    const float dy = io.MousePos.y - origin_projected.screen.y;
                    const float radius = std::sqrt(dx * dx + dy * dy);
                    if (std::abs(radius - ring_radius) <= 9.0f) {
                        float angle = std::atan2(dy, dx);
                        if (angle < 0.0f) angle += 6.2831853071795864769f;
                        GizmoAxis picked = GizmoAxis::None;
                        if (angle_in_arc(angle, -0.15f, 1.82f)) picked = GizmoAxis::X;
                        else if (angle_in_arc(angle, 1.95f, 3.92f)) picked = GizmoAxis::Y;
                        else if (angle_in_arc(angle, 4.05f, 6.02f)) picked = GizmoAxis::Z;
                        if (picked != GizmoAxis::None) {
                            commit_active_edit(state);
                            view.active_axis = picked;
                            view.drag_entity_id = entity.id;
                            view.drag_start_transform = editor_world_transform(state, entity);
                            view.drag_start_transforms = capture_selected_entity_transforms(state);
                            view.drag_pivot = gizmo_origin;
                            view.drag_start_mouse = io.MousePos;
                            view.drag_start_angle = std::atan2(dy, dx);
                            view.gizmo_dragging = true;
                            view.gizmo_drag_changed = false;
                            begin_edit(state, capture_snapshot(state), "Rotate entity gizmo");
                            gizmo_consumed_click = true;
                        }
                    }
                }
            }
        }
    }

    if (view.gizmo_dragging) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            cancel_3d_gizmo_drag(state);
        } else if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (const auto drag_index = entity_index_from_id(state, view.drag_entity_id)) {
                (void)drag_index;
                const bool bypass_snap = io.KeyAlt || !view.snap_enabled;
                const bool multi = view.drag_start_transforms.size() > 1;
                const auto world_axis = gizmo_world_axis(view.drag_start_transform, view.active_axis, view.local_space);
                const float mouse_dx = io.MousePos.x - view.drag_start_mouse.x;
                const float mouse_dy = io.MousePos.y - view.drag_start_mouse.y;
                const float pixels = mouse_dx * view.drag_axis_screen_unit.x + mouse_dy * view.drag_axis_screen_unit.y;
                float move_amount = snap_scalar(pixels / 76.0f * view.drag_world_length, view.translation_snap, bypass_snap);
                float scale_amount = snap_scalar(pixels / 90.0f, view.scale_snap, bypass_snap);

                float rotation_radians = 0.0f;
                if (view.tool == SceneTool::Rotate) {
                    const auto projected = project_scene_point(view.camera, canvas_min, live_canvas_size, view.drag_pivot);
                    const float angle = std::atan2(io.MousePos.y - projected.screen.y, io.MousePos.x - projected.screen.x);
                    float delta = angle - view.drag_start_angle;
                    while (delta > 3.14159265f) delta -= 6.28318531f;
                    while (delta < -3.14159265f) delta += 6.28318531f;
                    const float degrees = snap_scalar(delta * kEditorRadiansToDegrees, view.rotation_snap_degrees, bypass_snap);
                    rotation_radians = degrees * kEditorDegreesToRadians;
                }

                bool any_changed = false;
                for (const auto& [id, start_transform] : view.drag_start_transforms) {
                    const auto index = entity_index_from_id(state, id);
                    if (!index) continue;
                    vespera::TransformComponent next = start_transform;

                    if (view.tool == SceneTool::Move) {
                        next.position = vec3_add(start_transform.position, vec3_mul(world_axis, move_amount));
                    } else if (view.tool == SceneTool::Scale) {
                        if (!multi) {
                            if (view.active_axis == GizmoAxis::Uniform) {
                                next.scale.x = std::max(0.01f, start_transform.scale.x + scale_amount);
                                next.scale.y = std::max(0.01f, start_transform.scale.y + scale_amount);
                                next.scale.z = std::max(0.01f, start_transform.scale.z + scale_amount);
                            } else if (view.active_axis == GizmoAxis::X) next.scale.x = std::max(0.01f, start_transform.scale.x + scale_amount);
                            else if (view.active_axis == GizmoAxis::Y) next.scale.y = std::max(0.01f, start_transform.scale.y + scale_amount);
                            else if (view.active_axis == GizmoAxis::Z) next.scale.z = std::max(0.01f, start_transform.scale.z + scale_amount);
                        } else {
                            const float factor = std::max(0.01f, 1.0f + scale_amount);
                            vespera::Vec3 relative{
                                start_transform.position.x - view.drag_pivot.x,
                                start_transform.position.y - view.drag_pivot.y,
                                start_transform.position.z - view.drag_pivot.z
                            };
                            if (view.active_axis == GizmoAxis::Uniform) {
                                relative = vec3_mul(relative, factor);
                                next.scale = vec3_mul(start_transform.scale, factor);
                            } else {
                                const float along = vec3_dot(relative, world_axis);
                                relative = vec3_add(relative, vec3_mul(world_axis, along * (factor - 1.0f)));
                                if (view.active_axis == GizmoAxis::X) next.scale.x = std::max(0.01f, start_transform.scale.x * factor);
                                else if (view.active_axis == GizmoAxis::Y) next.scale.y = std::max(0.01f, start_transform.scale.y * factor);
                                else if (view.active_axis == GizmoAxis::Z) next.scale.z = std::max(0.01f, start_transform.scale.z * factor);
                            }
                            next.position = vec3_add(view.drag_pivot, relative);
                        }
                    } else if (view.tool == SceneTool::Rotate) {
                        if (multi) {
                            vespera::Vec3 relative{
                                start_transform.position.x - view.drag_pivot.x,
                                start_transform.position.y - view.drag_pivot.y,
                                start_transform.position.z - view.drag_pivot.z
                            };
                            next.position = vec3_add(
                                view.drag_pivot,
                                rotate_vector_around_axis(relative, world_axis, rotation_radians)
                            );
                        }
                        if (view.active_axis == GizmoAxis::X) next.rotation.x = start_transform.rotation.x + rotation_radians;
                        else if (view.active_axis == GizmoAxis::Y) next.rotation.y = start_transform.rotation.y + rotation_radians;
                        else if (view.active_axis == GizmoAxis::Z) next.rotation.z = start_transform.rotation.z + rotation_radians;
                    }

                    const auto changed_vec3 = [](vespera::Vec3 a, vespera::Vec3 b) {
                        return std::abs(a.x-b.x) > 0.00001f || std::abs(a.y-b.y) > 0.00001f || std::abs(a.z-b.z) > 0.00001f;
                    };
                    const auto live_transform = editor_world_transform(state, state.scene.entities[*index]);
                    if (changed_vec3(live_transform.position, next.position)
                        || changed_vec3(live_transform.rotation, next.rotation)
                        || changed_vec3(live_transform.scale, next.scale)) {
                        set_editor_world_transform(state, id, next);
                        any_changed = true;
                    }
                }

                if (any_changed) {
                    view.gizmo_drag_changed = true;
                    state.active_edit_changed = true;
                    state.dirty = true;
                }
            }
        } else {
            commit_3d_gizmo_drag(state);
        }
    }

    // Scene click selection. Gizmo handles consume their click first; otherwise
    // entity markers are picked in projected screen space. Sector topology stays
    // intentionally authored in the dedicated Sector tab for now.
    if (view.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
        && !navigating && !gizmo_consumed_click && !view.gizmo_dragging) {
        commit_active_edit(state);
        if (const auto picked = pick_entity_in_3d(state, canvas_min, live_canvas_size, io.MousePos)) {
            if (io.KeyCtrl) toggle_entity_selection(state, *picked);
            else select_entity(state, *picked);
        } else if (!io.KeyCtrl) {
            state.selection = {};
            state.selected_entity_ids.clear();
        }
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (!state.direct_render_previews && view.preview_texture == ImTextureID_Invalid) {
        const char* waiting = "Preparing live preview...";
        const ImVec2 waiting_size = ImGui::CalcTextSize(waiting);
        draw->AddText(
            {(canvas_min.x + canvas_max.x - waiting_size.x) * 0.5f,
             (canvas_min.y + canvas_max.y - waiting_size.y) * 0.5f},
            ImGui::GetColorU32(ImVec4(0.72f, 0.74f, 0.80f, 1.0f)),
            waiting
        );
    }
    const ImU32 border = ImGui::GetColorU32(ImVec4(0.36f, 0.39f, 0.45f, 0.9f));
    draw->AddRect(canvas_min, canvas_max, border, 0.0f, 0, 1.0f);
    const ImVec2 center{(canvas_min.x + canvas_max.x) * 0.5f, (canvas_min.y + canvas_max.y) * 0.5f};
    const ImU32 crosshair = ImGui::GetColorU32(ImVec4(0.92f, 0.92f, 0.92f, 0.65f));
    draw->AddLine({center.x - 5.0f, center.y}, {center.x + 5.0f, center.y}, crosshair, 1.0f);
    draw->AddLine({center.x, center.y - 5.0f}, {center.x, center.y + 5.0f}, crosshair, 1.0f);

    if (state.selection.kind == SelectionKind::Entity && state.selected_entity_ids.size() > 1) {
        for (const auto id : state.selected_entity_ids) {
            const auto index = entity_index_from_id(state, id);
            if (!index) continue;
            const auto world = editor_world_transform(state, state.scene.entities[*index]);
            const auto point = project_scene_point(view.camera, canvas_min, live_canvas_size, world.position);
            if (point.visible) draw->AddCircle(point.screen, 8.0f, ImGui::GetColorU32(ImVec4(0.96f, 0.70f, 0.20f, 0.78f)), 20, 1.5f);
        }
    }

    if (const auto draw_selected = selected_entity_index(state)) {
        const auto& entity = state.scene.entities[*draw_selected];
        const vespera::Vec3 gizmo_origin = selected_entity_gizmo_pivot(state, view.center_pivot);
        const auto origin = project_scene_point(view.camera, canvas_min, live_canvas_size, gizmo_origin);
        if (origin.visible) {
            // Selection marker remains visible even with View tool active.
            draw->AddCircle(origin.screen, 11.0f, ImGui::GetColorU32(ImVec4(1.0f, 0.78f, 0.22f, 0.95f)), 24, 2.0f);
            const std::string label = state.selected_entity_ids.size() > 1
                ? std::format("{} selected", state.selected_entity_ids.size())
                : (entity.name.empty() ? std::format("Entity {}", entity.id) : entity.name);
            draw->AddText({origin.screen.x + 14.0f, origin.screen.y - 18.0f},
                ImGui::GetColorU32(ImVec4(1.0f, 0.91f, 0.68f, 0.96f)), label.c_str());

            if (view.tool != SceneTool::View) {
                const float world_per_pixel = (2.0f * origin.depth
                    * std::tan(std::clamp(view.camera.vertical_fov_degrees, 30.0f, 130.0f) * kEditorDegreesToRadians * 0.5f))
                    / std::max(live_canvas_size.y, 1.0f);
                const float length = std::clamp(world_per_pixel * 76.0f, 0.15f, 25.0f);
                if (view.tool == SceneTool::Move || view.tool == SceneTool::Scale) {
                    for (const auto axis : {GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z}) {
                        const auto world_axis = gizmo_world_axis(editor_world_transform(state, entity), axis, view.local_space);
                        const auto endpoint = project_scene_point(view.camera, canvas_min, live_canvas_size,
                            vec3_add(gizmo_origin, vec3_mul(world_axis, length)));
                        if (!endpoint.visible) continue;
                        const bool active = view.gizmo_dragging && view.active_axis == axis;
                        const ImU32 color = gizmo_axis_color(axis, active);
                        draw->AddLine(origin.screen, endpoint.screen, color, active ? 4.0f : 3.0f);
                        const float dx = endpoint.screen.x - origin.screen.x;
                        const float dy = endpoint.screen.y - origin.screen.y;
                        const float len = std::sqrt(dx*dx + dy*dy);
                        if (len > 4.0f) {
                            const ImVec2 unit{dx/len, dy/len};
                            if (view.tool == SceneTool::Move) {
                                const ImVec2 perp{-unit.y, unit.x};
                                const ImVec2 tip = endpoint.screen;
                                const ImVec2 a{tip.x - unit.x*10.0f + perp.x*5.0f, tip.y - unit.y*10.0f + perp.y*5.0f};
                                const ImVec2 b{tip.x - unit.x*10.0f - perp.x*5.0f, tip.y - unit.y*10.0f - perp.y*5.0f};
                                draw->AddTriangleFilled(tip, a, b, color);
                            } else {
                                draw->AddRectFilled({endpoint.screen.x-4.5f, endpoint.screen.y-4.5f},
                                    {endpoint.screen.x+4.5f, endpoint.screen.y+4.5f}, color, 1.0f);
                            }
                        }
                    }
                    if (view.tool == SceneTool::Scale) {
                        const ImU32 center_color = gizmo_axis_color(GizmoAxis::Uniform,
                            view.gizmo_dragging && view.active_axis == GizmoAxis::Uniform);
                        draw->AddRectFilled({origin.screen.x-5.0f, origin.screen.y-5.0f},
                            {origin.screen.x+5.0f, origin.screen.y+5.0f}, center_color, 1.0f);
                    }
                } else if (view.tool == SceneTool::Rotate) {
                    constexpr float ring_radius = 54.0f;
                    draw_arc(draw, origin.screen, ring_radius, -0.15f, 1.82f,
                        gizmo_axis_color(GizmoAxis::X, view.gizmo_dragging && view.active_axis == GizmoAxis::X), 3.0f);
                    draw_arc(draw, origin.screen, ring_radius, 1.95f, 3.92f,
                        gizmo_axis_color(GizmoAxis::Y, view.gizmo_dragging && view.active_axis == GizmoAxis::Y), 3.0f);
                    draw_arc(draw, origin.screen, ring_radius, 4.05f, 6.02f,
                        gizmo_axis_color(GizmoAxis::Z, view.gizmo_dragging && view.active_axis == GizmoAxis::Z), 3.0f);
                    draw->AddText({origin.screen.x + 41.0f, origin.screen.y + 20.0f}, gizmo_axis_color(GizmoAxis::X, false), "X");
                    draw->AddText({origin.screen.x - 45.0f, origin.screen.y + 12.0f}, gizmo_axis_color(GizmoAxis::Y, false), "Y");
                    draw->AddText({origin.screen.x + 12.0f, origin.screen.y - 58.0f}, gizmo_axis_color(GizmoAxis::Z, false), "Z");
                }
            }
        }
    }

    const std::string camera_text = std::format(
        "Editor Camera  X {:.2f}  Y {:.2f}  Z {:.2f}",
        view.camera.position.x,
        view.camera.position.y,
        view.camera.position.z
    );
    const ImVec2 text_pos{canvas_min.x + 10.0f, canvas_min.y + 9.0f};
    const ImVec2 text_size = ImGui::CalcTextSize(camera_text.c_str());
    draw->AddRectFilled(
        {text_pos.x - 5.0f, text_pos.y - 4.0f},
        {text_pos.x + text_size.x + 5.0f, text_pos.y + text_size.y + 4.0f},
        ImGui::GetColorU32(ImVec4(0.03f, 0.035f, 0.045f, 0.72f)),
        3.0f
    );
    draw->AddText(text_pos, ImGui::GetColorU32(ImVec4(0.88f, 0.90f, 0.94f, 1.0f)), camera_text.c_str());

    ImGui::End();
}


} // namespace vespera::editor
