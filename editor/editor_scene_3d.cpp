#include "editor_scene_3d.hpp"

#include "editor_console.hpp"
#include "editor_history.hpp"
#include "editor_selection.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>

namespace vespera::editor {

namespace {
constexpr float kDegreesToRadians = 0.017453292519943295f;
}

std::optional<vespera::Vec3> selected_focus_point_3d(const EditorState& state) {
    switch (state.selection.kind) {
        case SelectionKind::Entity: {
            const auto index = selected_entity_index(state);
            if (!index) return std::nullopt;
            if (state.selected_entity_ids.size() > 1) {
                vespera::Vec3 point{};
                std::size_t count = 0;
                for (const auto id : state.selected_entity_ids) {
                    const auto entity_index = entity_index_from_id(state, id);
                    if (!entity_index) continue;
                    const auto world = editor_world_transform(state, state.scene.entities[*entity_index]);
                    point.x += world.position.x;
                    point.y += world.position.y;
                    point.z += world.position.z;
                    ++count;
                }
                if (count > 0) {
                    const float inv = 1.0f / static_cast<float>(count);
                    point.x *= inv; point.y *= inv; point.z *= inv;
                    return point;
                }
            }
            const auto& entity = state.scene.entities[*index];
            const auto world = editor_world_transform(state, entity);
            vespera::Vec3 point = world.position;
            if (entity.sprite_renderer) point.y += entity.sprite_renderer->size.z * world.scale.y * 0.5f;
            return point;
        }
        case SelectionKind::Camera:
            return state.scene.camera.position;
        case SelectionKind::Sector: {
            const auto& sectors = state.scene.world.sectors();
            if (state.selection.index >= sectors.size() || sectors[state.selection.index].vertices.empty()) return std::nullopt;
            const auto& sector = sectors[state.selection.index];
            vespera::Vec3 point{};
            for (const auto& v : sector.vertices) {
                point.x += v.x;
                point.z += v.z;
            }
            const float inv = 1.0f / static_cast<float>(sector.vertices.size());
            point.x *= inv;
            point.z *= inv;
            point.y = (sector.floor_height + sector.ceiling_height) * 0.5f;
            return point;
        }
        default:
            return std::nullopt;
    }
}
void point_editor_camera_at(vespera::Camera& camera, const vespera::Vec3& target) {
    const float dx = target.x - camera.position.x;
    const float dy = target.y - camera.position.y;
    const float dz = target.z - camera.position.z;
    const float horizontal = std::sqrt(dx * dx + dz * dz);
    if (horizontal < 0.0001f && std::abs(dy) < 0.0001f) return;
    camera.yaw = std::atan2(dx, dz);
    camera.pitch = std::atan2(dy, std::max(horizontal, 0.0001f));
}
vespera::Vec3 vec3_add(vespera::Vec3 a, vespera::Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
vespera::Vec3 vec3_sub(vespera::Vec3 a, vespera::Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
vespera::Vec3 vec3_mul(vespera::Vec3 a, float scalar) {
    return {a.x * scalar, a.y * scalar, a.z * scalar};
}
float vec3_dot(vespera::Vec3 a, vespera::Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
vespera::Vec3 vec3_cross(vespera::Vec3 a, vespera::Vec3 b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}
vespera::Vec3 vec3_normalize(vespera::Vec3 value) {
    const float len_sq = vec3_dot(value, value);
    if (len_sq <= 0.000001f) return {0.0f, 0.0f, 0.0f};
    return vec3_mul(value, 1.0f / std::sqrt(len_sq));
}
EditorCameraBasis editor_camera_basis(const vespera::Camera& camera) {
    const float cos_pitch = std::cos(camera.pitch);
    EditorCameraBasis basis;
    basis.forward = vec3_normalize({
        std::sin(camera.yaw) * cos_pitch,
        std::sin(camera.pitch),
        std::cos(camera.yaw) * cos_pitch,
    });
    basis.right = vec3_normalize({std::cos(camera.yaw), 0.0f, -std::sin(camera.yaw)});
    basis.up = vec3_normalize(vec3_cross(basis.forward, basis.right));
    return basis;
}
ProjectedPoint project_scene_point(
    const vespera::Camera& camera,
    ImVec2 canvas_min,
    ImVec2 canvas_size,
    vespera::Vec3 world
) {
    const auto basis = editor_camera_basis(camera);
    const auto relative = vec3_sub(world, camera.position);
    const float depth = vec3_dot(relative, basis.forward);
    if (depth <= std::max(camera.near_plane, 0.01f)) return {{}, depth, false};

    const float aspect = canvas_size.x / std::max(canvas_size.y, 1.0f);
    const float fov = std::clamp(camera.vertical_fov_degrees, 30.0f, 130.0f) * kDegreesToRadians;
    const float tan_half = std::tan(fov * 0.5f);
    if (tan_half <= 0.00001f) return {{}, depth, false};
    const float view_x = vec3_dot(relative, basis.right);
    const float view_y = vec3_dot(relative, basis.up);
    const float ndc_x = view_x / (depth * tan_half * aspect);
    const float ndc_y = view_y / (depth * tan_half);
    const ImVec2 screen{
        canvas_min.x + (ndc_x * 0.5f + 0.5f) * canvas_size.x,
        canvas_min.y + (0.5f - ndc_y * 0.5f) * canvas_size.y,
    };
    const bool visible = ndc_x >= -1.15f && ndc_x <= 1.15f && ndc_y >= -1.15f && ndc_y <= 1.15f;
    return {screen, depth, visible};
}
vespera::Vec3 rotate_axis_euler(vespera::Vec3 axis, const vespera::Vec3& rotation) {
    // XYZ Euler basis matching the Transform semantic API. This is editor-only
    // orientation math; no renderer handles or matrices leak into Scene data.
    const float cx = std::cos(rotation.x), sx = std::sin(rotation.x);
    const float cy = std::cos(rotation.y), sy = std::sin(rotation.y);
    const float cz = std::cos(rotation.z), sz = std::sin(rotation.z);

    vespera::Vec3 v{
        axis.x,
        axis.y * cx - axis.z * sx,
        axis.y * sx + axis.z * cx,
    };
    v = {
        v.x * cy + v.z * sy,
        v.y,
        -v.x * sy + v.z * cy,
    };
    v = {
        v.x * cz - v.y * sz,
        v.x * sz + v.y * cz,
        v.z,
    };
    return vec3_normalize(v);
}
vespera::Vec3 gizmo_world_axis(const vespera::TransformComponent& transform, GizmoAxis axis, bool local_space) {
    vespera::Vec3 value{};
    switch (axis) {
        case GizmoAxis::X: value = {1.0f, 0.0f, 0.0f}; break;
        case GizmoAxis::Y: value = {0.0f, 1.0f, 0.0f}; break;
        case GizmoAxis::Z: value = {0.0f, 0.0f, 1.0f}; break;
        default: return {};
    }
    return local_space ? rotate_axis_euler(value, transform.rotation) : value;
}
float snap_scalar(float value, float step, bool bypass) {
    if (bypass || step <= 0.0001f) return value;
    return std::round(value / step) * step;
}
float screen_distance_sq(ImVec2 a, ImVec2 b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return dx * dx + dy * dy;
}
bool angle_in_arc(float angle, float begin, float end) {
    auto norm = [](float v) {
        constexpr float full_turn = 6.2831853071795864769f;
        while (v < 0.0f) v += full_turn;
        while (v >= full_turn) v -= full_turn;
        return v;
    };
    angle = norm(angle); begin = norm(begin); end = norm(end);
    if (begin <= end) return angle >= begin && angle <= end;
    return angle >= begin || angle <= end;
}
std::optional<std::size_t> pick_entity_in_3d(
    const EditorState& state,
    ImVec2 canvas_min,
    ImVec2 canvas_size,
    ImVec2 mouse
) {
    std::optional<std::size_t> best;
    float best_depth = std::numeric_limits<float>::max();
    float best_distance_sq = 20.0f * 20.0f;
    for (std::size_t i = 0; i < state.scene.entities.size(); ++i) {
        const auto& entity = state.scene.entities[i];
        if (!entity.enabled) continue;
        const auto world = editor_world_transform(state, entity);
        vespera::Vec3 pick_point = world.position;
        if (entity.sprite_renderer) {
            pick_point.y += entity.sprite_renderer->size.z * world.scale.y * 0.5f;
        }
        const auto projected = project_scene_point(state.scene_view_3d.camera, canvas_min, canvas_size, pick_point);
        if (!projected.visible) continue;
        float pick_radius = entity.sprite_renderer ? 26.0f : 15.0f;
        if (entity.point_light) pick_radius = std::max(pick_radius, 18.0f);
        const float distance_sq = screen_distance_sq(projected.screen, mouse);
        if (distance_sq <= pick_radius * pick_radius
            && (distance_sq < best_distance_sq - 0.01f || projected.depth < best_depth)) {
            best = i;
            best_distance_sq = distance_sq;
            best_depth = projected.depth;
        }
    }
    return best;
}
vespera::Vec3 selected_entity_gizmo_pivot(const EditorState& state, bool center_pivot) {
    const auto selected = selected_entity_index(state);
    if (!selected) return {};
    if (!center_pivot || state.selected_entity_ids.size() <= 1) {
        return editor_world_transform(state, state.scene.entities[*selected]).position;
    }
    vespera::Vec3 center{};
    std::size_t count = 0;
    for (const auto id : state.selected_entity_ids) {
        const auto index = entity_index_from_id(state, id);
        if (!index) continue;
        center = vec3_add(center, editor_world_transform(state, state.scene.entities[*index]).position);
        ++count;
    }
    return count > 0 ? vec3_mul(center, 1.0f / static_cast<float>(count))
                     : editor_world_transform(state, state.scene.entities[*selected]).position;
}
std::vector<std::pair<vespera::SceneObjectId, vespera::TransformComponent>> capture_selected_entity_transforms(
    const EditorState& state
) {
    std::vector<std::pair<vespera::SceneObjectId, vespera::TransformComponent>> result;
    if (state.selection.kind != SelectionKind::Entity) return result;
    result.reserve(std::max<std::size_t>(state.selected_entity_ids.size(), 1));
    if (!state.selected_entity_ids.empty()) {
        for (const auto id : state.selected_entity_ids) {
            const auto index = entity_index_from_id(state, id);
            if (index) result.emplace_back(id, editor_world_transform(state, state.scene.entities[*index]));
        }
    } else if (const auto selected = selected_entity_index(state)) {
        const auto& entity = state.scene.entities[*selected];
        result.emplace_back(entity.id, editor_world_transform(state, entity));
    }
    std::stable_sort(result.begin(), result.end(), [&](const auto& a, const auto& b) {
        const auto* ea = state.scene.find_entity(a.first);
        const auto* eb = state.scene.find_entity(b.first);
        const std::size_t da = ea ? entity_hierarchy_depth(state, *ea) : 0;
        const std::size_t db = eb ? entity_hierarchy_depth(state, *eb) : 0;
        return da < db;
    });
    return result;
}
vespera::Vec3 rotate_vector_around_axis(vespera::Vec3 value, vespera::Vec3 axis, float radians) {
    const float axis_length = std::sqrt(vec3_dot(axis, axis));
    if (axis_length <= 0.000001f) return value;
    axis = vec3_mul(axis, 1.0f / axis_length);
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return vec3_add(
        vec3_add(vec3_mul(value, c), vec3_mul(vec3_cross(axis, value), s)),
        vec3_mul(axis, vec3_dot(axis, value) * (1.0f - c))
    );
}
vespera::editor::EditorCommandKind transform_command_kind(SceneTool tool) {
    switch (tool) {
        case SceneTool::Move: return vespera::editor::EditorCommandKind::MoveEntityTransform;
        case SceneTool::Rotate: return vespera::editor::EditorCommandKind::RotateEntityTransform;
        case SceneTool::Scale: return vespera::editor::EditorCommandKind::ScaleEntityTransform;
        case SceneTool::View: break;
    }
    return vespera::editor::EditorCommandKind::Unknown;
}
const char* scene_tool_name(SceneTool tool) {
    switch (tool) {
        case SceneTool::View: return "View";
        case SceneTool::Move: return "Move";
        case SceneTool::Rotate: return "Rotate";
        case SceneTool::Scale: return "Scale";
    }
    return "Tool";
}
void cancel_3d_gizmo_drag(EditorState& state) {
    auto& view = state.scene_view_3d;
    if (!view.gizmo_dragging) return;
    for (const auto& [id, transform] : view.drag_start_transforms) {
        set_editor_world_transform(state, id, transform);
    }
    clear_active_edit(state);
    refresh_dirty(state);
    view.gizmo_dragging = false;
    view.gizmo_drag_changed = false;
    view.active_axis = GizmoAxis::None;
    view.drag_entity_id = vespera::kInvalidSceneObjectId;
    view.drag_start_transforms.clear();
}
void commit_3d_gizmo_drag(EditorState& state) {
    auto& view = state.scene_view_3d;
    if (!view.gizmo_dragging) return;
    const bool changed = view.gizmo_drag_changed;
    const SceneTool tool = view.tool;
    const std::size_t transform_count = view.drag_start_transforms.size();
    commit_active_edit(state);
    if (changed) {
        append_command_audit(
            state,
            transform_command_kind(tool),
            transform_count > 1
                ? std::format("{} {} entity transforms", scene_tool_name(tool), transform_count)
                : std::string(scene_tool_name(tool)) + " entity transform"
        );
    }
    view.gizmo_dragging = false;
    view.gizmo_drag_changed = false;
    view.active_axis = GizmoAxis::None;
    view.drag_entity_id = vespera::kInvalidSceneObjectId;
    view.drag_start_transforms.clear();
}
void frame_3d_selection(EditorState& state) {
    auto focus = selected_focus_point_3d(state);
    if (!focus) {
        const auto& sectors = state.scene.world.sectors();
        if (!sectors.empty()) {
            vespera::Vec3 center{};
            std::size_t count = 0;
            float min_floor = std::numeric_limits<float>::max();
            float max_ceil = std::numeric_limits<float>::lowest();
            for (const auto& sector : sectors) {
                for (const auto& vertex : sector.vertices) {
                    center.x += vertex.x;
                    center.z += vertex.z;
                    ++count;
                }
                min_floor = std::min(min_floor, sector.floor_height);
                max_ceil = std::max(max_ceil, sector.ceiling_height);
            }
            if (count > 0) {
                center.x /= static_cast<float>(count);
                center.z /= static_cast<float>(count);
                center.y = (min_floor + max_ceil) * 0.5f;
                focus = center;
            }
        }
    }
    if (!focus) return;

    state.scene_view_3d.camera.position = {
        focus->x - 5.5f,
        focus->y + 3.5f,
        focus->z - 5.5f,
    };
    point_editor_camera_at(state.scene_view_3d.camera, *focus);
    state.scene_view_3d.frame_selection_pending = false;
}

} // namespace vespera::editor
