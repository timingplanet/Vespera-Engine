#include "editor_sector_view.hpp"

#include "editor_history.hpp"
#include "editor_selection.hpp"
#include "editor_state.hpp"
#include "editor_style.hpp"
#include "editor_view_helpers.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <vector>

namespace vespera::editor {

ImU32 material_color(const vespera::SectorWorld& world, vespera::MaterialId id, float alpha = 0.34f) {
    if (id == vespera::kInvalidMaterial || id >= world.materials().size()) {
        return ImGui::GetColorU32(ImVec4(0.30f, 0.33f, 0.38f, alpha));
    }
    const auto& color = world.materials()[id].color;
    return ImGui::GetColorU32(ImVec4(color[0], color[1], color[2], alpha));
}



bool close_enough(vespera::Vec2 a, vespera::Vec2 b, float epsilon = 0.0001f) {
    return std::abs(a.x - b.x) <= epsilon && std::abs(a.z - b.z) <= epsilon;
}

bool valid_edit_polygon(const vespera::Sector& sector) {
    if (sector.vertices.size() < 3) {
        return false;
    }

    float area_twice = 0.0f;
    for (std::size_t i = 0; i < sector.vertices.size(); ++i) {
        const auto& a = sector.vertices[i];
        const auto& b = sector.vertices[(i + 1) % sector.vertices.size()];
        const float dx = b.x - a.x;
        const float dz = b.z - a.z;
        if (dx * dx + dz * dz < 0.000001f) {
            return false;
        }
        area_twice += a.x * b.z - b.x * a.z;
    }
    if (area_twice <= 0.0001f) {
        return false; // Vespera sectors are authored counter-clockwise.
    }

    // Collinear split points are allowed because portal boundaries may divide a
    // straight wall. A negative turn would make the polygon concave/invalid for
    // the current convex-sector mesher.
    for (std::size_t i = 0; i < sector.vertices.size(); ++i) {
        const auto& a = sector.vertices[i];
        const auto& b = sector.vertices[(i + 1) % sector.vertices.size()];
        const auto& c = sector.vertices[(i + 2) % sector.vertices.size()];
        const float abx = b.x - a.x;
        const float abz = b.z - a.z;
        const float bcx = c.x - b.x;
        const float bcz = c.z - b.z;
        const float cross = abx * bcz - abz * bcx;
        if (cross < -0.0001f) {
            return false;
        }
    }
    return true;
}

bool move_welded_vertex(EditorState& state, std::size_t sector_index, std::size_t vertex_index, vespera::Vec2 destination) {
    const auto& source_sectors = state.scene.world.sectors();
    if (sector_index >= source_sectors.size() || vertex_index >= source_sectors[sector_index].vertices.size()) {
        return false;
    }
    const vespera::Vec2 original = source_sectors[sector_index].vertices[vertex_index];
    if (close_enough(original, destination)) {
        return false;
    }

    std::vector<vespera::Sector> edited = source_sectors;
    std::vector<bool> changed(edited.size(), false);
    for (std::size_t sector_i = 0; sector_i < edited.size(); ++sector_i) {
        for (auto& vertex : edited[sector_i].vertices) {
            if (close_enough(vertex, original)) {
                vertex = destination;
                changed[sector_i] = true;
            }
        }
        if (changed[sector_i] && !valid_edit_polygon(edited[sector_i])) {
            return false;
        }
    }

    bool any = false;
    for (std::size_t sector_i = 0; sector_i < edited.size(); ++sector_i) {
        if (changed[sector_i]) {
            any |= state.scene.world.set_sector(sector_i, std::move(edited[sector_i]));
        }
    }
    return any;
}

void draw_grid(ImDrawList* draw, const SceneViewState& view, ImVec2 canvas_min, ImVec2 canvas_max, ImVec2 canvas_size) {
    float grid_world = 1.0f;
    while (grid_world * view.zoom < 26.0f) {
        grid_world *= 2.0f;
    }
    while (grid_world * view.zoom > 120.0f) {
        grid_world *= 0.5f;
    }

    const auto world_min = screen_to_world(view, canvas_min, canvas_size, canvas_min);
    const auto world_max = screen_to_world(view, canvas_min, canvas_size, canvas_max);
    const float start_x = std::floor(std::min(world_min.x, world_max.x) / grid_world) * grid_world;
    const float end_x = std::ceil(std::max(world_min.x, world_max.x) / grid_world) * grid_world;
    const float start_z = std::floor(std::min(world_min.z, world_max.z) / grid_world) * grid_world;
    const float end_z = std::ceil(std::max(world_min.z, world_max.z) / grid_world) * grid_world;

    const ImU32 minor = ImGui::GetColorU32(ImVec4(0.28f, 0.30f, 0.34f, 0.42f));
    const ImU32 axis = ImGui::GetColorU32(ImVec4(0.48f, 0.50f, 0.55f, 0.62f));

    int line_budget = 400;
    for (float x = start_x; x <= end_x && line_budget-- > 0; x += grid_world) {
        const ImVec2 a = world_to_screen(view, canvas_min, canvas_size, {x, start_z});
        const ImVec2 b = world_to_screen(view, canvas_min, canvas_size, {x, end_z});
        draw->AddLine(a, b, std::abs(x) < 0.0001f ? axis : minor, 1.0f);
    }
    for (float z = start_z; z <= end_z && line_budget-- > 0; z += grid_world) {
        const ImVec2 a = world_to_screen(view, canvas_min, canvas_size, {start_x, z});
        const ImVec2 b = world_to_screen(view, canvas_min, canvas_size, {end_x, z});
        draw->AddLine(a, b, std::abs(z) < 0.0001f ? axis : minor, 1.0f);
    }
}

void select_at_world(EditorState& state, vespera::Vec2 world_point) {
    // Entity markers get priority over sectors.
    const float entity_pick_radius = 12.0f / std::max(state.scene_view.zoom, 1.0f);
    for (std::size_t i = state.scene.entities.size(); i-- > 0;) {
        const auto& entity = state.scene.entities[i];
        const auto transform = editor_world_transform(state, entity);
        const float dx = transform.position.x - world_point.x;
        const float dz = transform.position.z - world_point.z;
        if ((dx * dx + dz * dz) <= entity_pick_radius * entity_pick_radius) {
            select_entity(state, i);
            return;
        }
    }

    const float camera_dx = state.scene.camera.position.x - world_point.x;
    const float camera_dz = state.scene.camera.position.z - world_point.z;
    const float camera_pick_radius = 13.0f / std::max(state.scene_view.zoom, 1.0f);
    if ((camera_dx * camera_dx + camera_dz * camera_dz) <= camera_pick_radius * camera_pick_radius) {
        state.selection = {SelectionKind::Camera, 0};
        return;
    }

    const auto& sectors = state.scene.world.sectors();
    for (std::size_t i = sectors.size(); i-- > 0;) {
        if (vespera::point_inside_sector(sectors[i], world_point)) {
            state.selection = {SelectionKind::Sector, i};
            return;
        }
    }

    state.selection = {};
}

void draw_scene_view(EditorState& state) {
    ImGui::Begin("Sector");

    if (ImGui::Button("Frame All")) {
        state.scene_view.frame_all_pending = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Frame Selected")) {
        state.scene_view.frame_selection_pending = true;
    }
    draw_toolbar_separator();
    ImGui::Checkbox("Snap", &state.scene_view.snap_enabled);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(68.0f);
    ImGui::DragFloat("##snap_step", &state.scene_view.snap_step, 0.05f, 0.05f, 10.0f, "%.2f");
    state.scene_view.snap_step = std::max(state.scene_view.snap_step, 0.05f);
    ImGui::SameLine();
    if (ImGui::SmallButton("?##SectorHelp")) {}
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::SetTooltip("Left-drag selected vertices/entities\nAlt: bypass snap\nMiddle-drag: pan\nMouse wheel: zoom\nDouble-click items in Hierarchy to frame them");
    }

    ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    canvas_size.x = std::max(canvas_size.x, 100.0f);
    canvas_size.y = std::max(canvas_size.y, 100.0f);
    const ImVec2 canvas_min = ImGui::GetCursorScreenPos();
    const ImVec2 canvas_max{canvas_min.x + canvas_size.x, canvas_min.y + canvas_size.y};

    ImGui::InvisibleButton("##scene_canvas", canvas_size);
    const bool hovered = ImGui::IsItemHovered();
    const ImGuiIO& io = ImGui::GetIO();

    if (state.scene_view.frame_all_pending) {
        frame_all(state, canvas_size);
    }
    if (state.scene_view.frame_selection_pending) {
        frame_selection(state, canvas_size);
    }

    if (hovered && std::abs(io.MouseWheel) > 0.001f && state.scene_view.drag_kind == SceneDragKind::None) {
        const float old_zoom = state.scene_view.zoom;
        const float factor = std::pow(1.12f, io.MouseWheel);
        state.scene_view.zoom = std::clamp(old_zoom * factor, 8.0f, 320.0f);
        const ImVec2 mouse = io.MousePos;
        const auto before = screen_to_world({old_zoom, state.scene_view.pan, false}, canvas_min, canvas_size, mouse);
        const ImVec2 after_screen = world_to_screen(state.scene_view, canvas_min, canvas_size, before);
        state.scene_view.pan.x += mouse.x - after_screen.x;
        state.scene_view.pan.y += mouse.y - after_screen.y;
    }

    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) && state.scene_view.drag_kind == SceneDragKind::None) {
        state.scene_view.pan.x += io.MouseDelta.x;
        state.scene_view.pan.y += io.MouseDelta.y;
    }

    // Begin direct manipulation. Selected sector vertices get first priority,
    // then sprite/camera markers, then ordinary sector selection.
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        commit_active_edit(state);
        bool began_drag = false;
        if (state.selection.kind == SelectionKind::Sector
            && state.selection.index < state.scene.world.sectors().size()) {
            const auto& sector = state.scene.world.sectors()[state.selection.index];
            float best_distance_sq = 11.0f * 11.0f;
            std::size_t best_vertex = kNoSubSelection;
            for (std::size_t vertex_i = 0; vertex_i < sector.vertices.size(); ++vertex_i) {
                const ImVec2 p = world_to_screen(state.scene_view, canvas_min, canvas_size, sector.vertices[vertex_i]);
                const float dx = p.x - io.MousePos.x;
                const float dy = p.y - io.MousePos.y;
                const float distance_sq = dx * dx + dy * dy;
                if (distance_sq <= best_distance_sq) {
                    best_distance_sq = distance_sq;
                    best_vertex = vertex_i;
                }
            }
            if (best_vertex != kNoSubSelection) {
                state.selection.sub_index = best_vertex;
                state.selection.side_index = kNoSubSelection;
                state.scene_view.drag_kind = SceneDragKind::SectorVertex;
                state.scene_view.drag_index = state.selection.index;
                state.scene_view.drag_sub_index = best_vertex;
                state.scene_view.drag_changed = false;
                begin_edit(state, capture_snapshot(state), "Move sector vertex");
                began_drag = true;
            }
        }

        if (!began_drag
            && state.selection.kind == SelectionKind::Sector
            && state.selection.index < state.scene.world.sectors().size()) {
            const auto& sector = state.scene.world.sectors()[state.selection.index];
            float best_distance_sq = 7.0f * 7.0f;
            std::size_t best_side = kNoSubSelection;
            for (std::size_t side = 0; side < sector.vertices.size(); ++side) {
                const ImVec2 a = world_to_screen(state.scene_view, canvas_min, canvas_size, sector.vertices[side]);
                const ImVec2 b = world_to_screen(state.scene_view, canvas_min, canvas_size, sector.vertices[(side + 1u) % sector.vertices.size()]);
                const float distance_sq = point_segment_distance_sq(io.MousePos, a, b);
                if (distance_sq <= best_distance_sq) {
                    best_distance_sq = distance_sq;
                    best_side = side;
                }
            }
            if (best_side != kNoSubSelection) {
                state.selection.sub_index = kNoSubSelection;
                state.selection.side_index = best_side;
                began_drag = true; // The click was consumed as an edge selection.
            }
        }

        if (!began_drag) {
            const auto clicked_world = screen_to_world(state.scene_view, canvas_min, canvas_size, io.MousePos);
            const float entity_pick_radius = 12.0f / std::max(state.scene_view.zoom, 1.0f);
            for (std::size_t i = state.scene.entities.size(); i-- > 0;) {
                const auto& entity = state.scene.entities[i];
                const auto world = editor_world_transform(state, entity);
                const float dx = world.position.x - clicked_world.x;
                const float dz = world.position.z - clicked_world.z;
                if (dx * dx + dz * dz <= entity_pick_radius * entity_pick_radius) {
                    select_entity(state, i);
                    state.scene_view.drag_kind = SceneDragKind::Entity;
                    state.scene_view.drag_index = i;
                    state.scene_view.drag_changed = false;
                    begin_edit(state, capture_snapshot(state), "Move entity");
                    began_drag = true;
                    break;
                }
            }
        }

        if (!began_drag) {
            const auto clicked_world = screen_to_world(state.scene_view, canvas_min, canvas_size, io.MousePos);
            const float dx = state.scene.camera.position.x - clicked_world.x;
            const float dz = state.scene.camera.position.z - clicked_world.z;
            const float pick_radius = 13.0f / std::max(state.scene_view.zoom, 1.0f);
            if (dx * dx + dz * dz <= pick_radius * pick_radius) {
                state.selection = {SelectionKind::Camera, 0};
                state.scene_view.drag_kind = SceneDragKind::Camera;
                state.scene_view.drag_changed = false;
                begin_edit(state, capture_snapshot(state), "Move camera");
                began_drag = true;
            }
        }

        if (!began_drag) {
            state.selection.sub_index = kNoSubSelection;
            state.selection.side_index = kNoSubSelection;
            select_at_world(state, screen_to_world(state.scene_view, canvas_min, canvas_size, io.MousePos));
        }
    }

    if (state.scene_view.drag_kind != SceneDragKind::None && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        vespera::Vec2 destination = screen_to_world(state.scene_view, canvas_min, canvas_size, io.MousePos);
        destination = snap_world_point(destination, state.scene_view, io.KeyAlt);
        bool changed = false;
        if (state.scene_view.drag_kind == SceneDragKind::SectorVertex) {
            changed = move_welded_vertex(
                state,
                state.scene_view.drag_index,
                state.scene_view.drag_sub_index,
                destination
            );
        } else if (state.scene_view.drag_kind == SceneDragKind::Entity
            && state.scene_view.drag_index < state.scene.entities.size()) {
            auto& entity = state.scene.entities[state.scene_view.drag_index];
            auto world = editor_world_transform(state, entity);
            if (std::abs(world.position.x - destination.x) > 0.0001f
                || std::abs(world.position.z - destination.z) > 0.0001f) {
                world.position.x = destination.x;
                world.position.z = destination.z;
                set_editor_world_transform(state, entity.id, world);
                changed = true;
            }
        } else if (state.scene_view.drag_kind == SceneDragKind::Camera) {
            auto& camera = state.scene.camera;
            if (std::abs(camera.position.x - destination.x) > 0.0001f
                || std::abs(camera.position.z - destination.z) > 0.0001f) {
                camera.position.x = destination.x;
                camera.position.z = destination.z;
                changed = true;
            }
        }
        state.scene_view.drag_changed = state.scene_view.drag_changed || changed;
        state.active_edit_changed = state.active_edit_changed || changed;
        if (changed) {
            state.dirty = true;
        }
    }

    if (state.scene_view.drag_kind != SceneDragKind::None && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        commit_active_edit(state);
        state.scene_view.drag_kind = SceneDragKind::None;
        state.scene_view.drag_sub_index = kNoSubSelection;
        state.scene_view.drag_changed = false;
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(canvas_min, canvas_max, true);
    draw->AddRectFilled(canvas_min, canvas_max, ImGui::GetColorU32(ImVec4(0.075f, 0.082f, 0.095f, 1.0f)));
    draw_grid(draw, state.scene_view, canvas_min, canvas_max, canvas_size);

    const auto& sectors = state.scene.world.sectors();
    for (std::size_t i = 0; i < sectors.size(); ++i) {
        const auto& sector = sectors[i];
        if (sector.vertices.size() < 3) {
            continue;
        }

        std::vector<ImVec2> points;
        points.reserve(sector.vertices.size());
        for (const auto& vertex : sector.vertices) {
            points.push_back(world_to_screen(state.scene_view, canvas_min, canvas_size, vertex));
        }

        const bool selected = state.selection.kind == SelectionKind::Sector && state.selection.index == i;
        const ImU32 fill = material_color(state.scene.world, sector.floor_material, selected ? 0.52f : 0.28f);
        const ImU32 outline = selected
            ? ImGui::GetColorU32(ImVec4(0.95f, 0.73f, 0.26f, 1.0f))
            : ImGui::GetColorU32(ImVec4(0.66f, 0.70f, 0.78f, 0.86f));
        draw->AddConvexPolyFilled(points.data(), static_cast<int>(points.size()), fill);
        draw->AddPolyline(points.data(), static_cast<int>(points.size()), outline, ImDrawFlags_Closed, selected ? 2.5f : 1.5f);

        for (std::size_t side = 0; side < sector.sides.size() && side < points.size(); ++side) {
            if (sector.sides[side].adjacent_sector >= 0) {
                const ImVec2 a = points[side];
                const ImVec2 b = points[(side + 1) % points.size()];
                draw->AddLine(a, b, ImGui::GetColorU32(ImVec4(0.27f, 0.72f, 0.95f, 1.0f)), 3.0f);
            }
        }
        if (selected && state.selection.side_index < points.size()) {
            const std::size_t side = state.selection.side_index;
            draw->AddLine(
                points[side],
                points[(side + 1u) % points.size()],
                ImGui::GetColorU32(ImVec4(1.0f, 0.62f, 0.18f, 1.0f)),
                5.0f
            );
        }

        if (selected) {
            for (std::size_t vertex_i = 0; vertex_i < points.size(); ++vertex_i) {
                const bool vertex_selected = state.selection.sub_index == vertex_i;
                const float radius = vertex_selected ? 6.0f : 4.5f;
                draw->AddCircleFilled(points[vertex_i], radius,
                    vertex_selected
                        ? ImGui::GetColorU32(ImVec4(1.0f, 0.78f, 0.28f, 1.0f))
                        : ImGui::GetColorU32(ImVec4(0.90f, 0.92f, 0.96f, 1.0f)));
                draw->AddCircle(points[vertex_i], radius + 1.5f,
                    ImGui::GetColorU32(ImVec4(0.08f, 0.09f, 0.11f, 1.0f)), 0, 1.5f);
            }
        }
    }

    for (std::size_t i = 0; i < state.scene.entities.size(); ++i) {
        const auto& entity = state.scene.entities[i];
        const auto world_transform = editor_world_transform(state, entity);
        const ImVec2 p = world_to_screen(
            state.scene_view, canvas_min, canvas_size,
            {world_transform.position.x, world_transform.position.z});
        const bool selected = state.selection.kind == SelectionKind::Entity && state.selection.index == i;
        const float radius = selected ? 8.0f : 6.0f;
        const float marker_alpha = entity.enabled ? 1.0f : 0.32f;
        if (entity.point_light) {
            const auto& light = *entity.point_light;
            const float scale_radius = (std::max)({
                std::abs(world_transform.scale.x),
                std::abs(world_transform.scale.y),
                std::abs(world_transform.scale.z)
            });
            const float screen_light_radius = std::max(light.radius * scale_radius * state.scene_view.zoom, 3.0f);
            const ImVec4 light_color{
                std::clamp(light.color[0], 0.0f, 1.0f),
                std::clamp(light.color[1], 0.0f, 1.0f),
                std::clamp(light.color[2], 0.0f, 1.0f),
                selected ? 0.72f : 0.36f
            };
            draw->AddCircle(p, screen_light_radius, ImGui::GetColorU32(light_color), 0, selected ? 2.5f : 1.5f);
            draw->AddLine({p.x - 7.0f, p.y}, {p.x + 7.0f, p.y}, ImGui::GetColorU32(light_color), 2.0f);
            draw->AddLine({p.x, p.y - 7.0f}, {p.x, p.y + 7.0f}, ImGui::GetColorU32(light_color), 2.0f);
        }
        if (entity.cylinder_collider) {
            const auto& collider = *entity.cylinder_collider;
            const float world_radius = collider.radius * std::max(std::abs(world_transform.scale.x), std::abs(world_transform.scale.z));
            const float screen_radius = world_radius * state.scene_view.zoom;
            const float local_center_x = collider.center.x * world_transform.scale.x;
            const float local_center_z = collider.center.z * world_transform.scale.z;
            const float yaw_cos = std::cos(world_transform.rotation.y);
            const float yaw_sin = std::sin(world_transform.rotation.y);
            const ImVec2 collider_p = world_to_screen(
                state.scene_view, canvas_min, canvas_size,
                {
                    world_transform.position.x + local_center_x * yaw_cos + local_center_z * yaw_sin,
                    world_transform.position.z - local_center_x * yaw_sin + local_center_z * yaw_cos
                });
            const ImVec4 collider_color = collider.is_trigger
                ? ImVec4(0.72f, 0.48f, 0.95f, selected ? 0.95f : 0.58f)
                : ImVec4(0.30f, 0.86f, 0.52f, selected ? 0.95f : 0.58f);
            draw->AddCircle(collider_p, std::max(screen_radius, 2.0f), ImGui::GetColorU32(collider_color), 0, selected ? 2.5f : 1.5f);
        }
        ImVec4 marker_color{0.46f, 0.68f, 0.98f, marker_alpha};
        if (entity.sprite_renderer) {
            const auto& color = entity.sprite_renderer->color;
            marker_color = {color[0], color[1], color[2], marker_alpha};
        }
        if (entity.point_light) {
            const auto& color = entity.point_light->color;
            marker_color = {color[0], color[1], color[2], marker_alpha};
        }
        draw->AddCircleFilled(p, radius, ImGui::GetColorU32(marker_color));
        draw->AddCircle(p, radius + 2.0f, selected
            ? ImGui::GetColorU32(ImVec4(1.0f, 0.78f, 0.28f, 1.0f))
            : ImGui::GetColorU32(ImVec4(0.10f, 0.12f, 0.15f, 1.0f)), 0, selected ? 2.5f : 1.5f);

        if (selected || (entity.sprite_renderer && !entity.sprite_renderer->animation_clip.empty())) {
            const float face_x = std::sin(world_transform.rotation.y);
            const float face_z = std::cos(world_transform.rotation.y);
            const float arrow_length = selected ? 24.0f : 15.0f;
            const ImVec2 face_tip{p.x + face_x * arrow_length, p.y + face_z * arrow_length};
            draw->AddLine(p, face_tip,
                selected
                    ? ImGui::GetColorU32(ImVec4(1.0f, 0.78f, 0.28f, 1.0f))
                    : ImGui::GetColorU32(ImVec4(0.68f, 0.78f, 0.90f, 0.85f)),
                selected ? 2.5f : 1.5f);
        }
        if (selected) {
            std::string base_label = entity.name;
            if (entity.sprite_renderer && !entity.sprite_renderer->animation_clip.empty()) {
                base_label = std::format("{}  [{}]", entity.name, entity.sprite_renderer->animation_clip);
            } else if (!entity.sprite_renderer) {
                base_label = std::format("{}  [Transform]", entity.name);
            }
            if (entity.cylinder_collider) {
                base_label += entity.cylinder_collider->is_trigger ? "  [Trigger]" : "  [Collider]";
            }
            if (entity.tag != "Untagged" || entity.layer != "Default") {
                base_label += std::format("  [tag:{} | layer:{}]", entity.tag, entity.layer);
            }
            const std::string label = entity.enabled ? base_label : std::format("{}  (disabled)", base_label);
            draw->AddText({p.x + 10.0f, p.y - 18.0f}, ImGui::GetColorU32(ImVec4(0.96f, 0.96f, 0.98f, 1.0f)), label.c_str());
        }
    }

    const auto& camera = state.scene.camera;
    const ImVec2 camera_pos = world_to_screen(state.scene_view, canvas_min, canvas_size, {camera.position.x, camera.position.z});
    const float dir_x = std::sin(camera.yaw);
    const float dir_z = std::cos(camera.yaw);
    const ImVec2 tip{camera_pos.x + dir_x * 22.0f, camera_pos.y + dir_z * 22.0f};
    const bool camera_selected = state.selection.kind == SelectionKind::Camera;
    draw->AddCircleFilled(camera_pos, camera_selected ? 7.0f : 5.5f,
        ImGui::GetColorU32(ImVec4(0.96f, 0.43f, 0.32f, 1.0f)));
    draw->AddLine(camera_pos, tip,
        camera_selected ? ImGui::GetColorU32(ImVec4(1.0f, 0.82f, 0.38f, 1.0f)) : ImGui::GetColorU32(ImVec4(0.96f, 0.43f, 0.32f, 1.0f)), 2.5f);

    if (hovered) {
        const auto mouse_world = screen_to_world(state.scene_view, canvas_min, canvas_size, io.MousePos);
        const std::string snap_suffix = state.scene_view.snap_enabled
            ? std::format("   snap {:.2f}", state.scene_view.snap_step)
            : std::string{};
        const std::string coords = std::format(
            "X {:.2f}   Z {:.2f}   {:.0f}%{}",
            mouse_world.x,
            mouse_world.z,
            state.scene_view.zoom / 64.0f * 100.0f,
            snap_suffix
        );
        const ImVec2 text_size = ImGui::CalcTextSize(coords.c_str());
        const ImVec2 box_min{canvas_min.x + 8.0f, canvas_max.y - text_size.y - 14.0f};
        const ImVec2 box_max{box_min.x + text_size.x + 12.0f, canvas_max.y - 6.0f};
        draw->AddRectFilled(box_min, box_max, ImGui::GetColorU32(ImVec4(0.03f, 0.035f, 0.045f, 0.86f)), 4.0f);
        draw->AddText({box_min.x + 6.0f, box_min.y + 4.0f}, ImGui::GetColorU32(ImGuiCol_TextDisabled), coords.c_str());
    }

    draw->PopClipRect();
    ImGui::End();
}


} // namespace vespera::editor
