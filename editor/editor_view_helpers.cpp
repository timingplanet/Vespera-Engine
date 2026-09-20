#include "editor_view_helpers.hpp"
#include "editor_selection.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace vespera::editor {

ImVec2 world_to_screen(const SceneViewState& view, ImVec2 canvas_min, ImVec2 canvas_size, vespera::Vec2 point) {
    const ImVec2 center{canvas_min.x + canvas_size.x * 0.5f, canvas_min.y + canvas_size.y * 0.5f};
    return {
        center.x + view.pan.x + point.x * view.zoom,
        center.y + view.pan.y + point.z * view.zoom,
    };
}
vespera::Vec2 screen_to_world(const SceneViewState& view, ImVec2 canvas_min, ImVec2 canvas_size, ImVec2 point) {
    const ImVec2 center{canvas_min.x + canvas_size.x * 0.5f, canvas_min.y + canvas_size.y * 0.5f};
    return {
        (point.x - center.x - view.pan.x) / view.zoom,
        (point.y - center.y - view.pan.y) / view.zoom,
    };
}
float point_segment_distance_sq(ImVec2 point, ImVec2 a, ImVec2 b) {
    const float ab_x = b.x - a.x;
    const float ab_y = b.y - a.y;
    const float length_sq = ab_x * ab_x + ab_y * ab_y;
    if (length_sq <= 0.000001f) {
        const float dx = point.x - a.x;
        const float dy = point.y - a.y;
        return dx * dx + dy * dy;
    }
    const float t = std::clamp(((point.x - a.x) * ab_x + (point.y - a.y) * ab_y) / length_sq, 0.0f, 1.0f);
    const float closest_x = a.x + ab_x * t;
    const float closest_y = a.y + ab_y * t;
    const float dx = point.x - closest_x;
    const float dy = point.y - closest_y;
    return dx * dx + dy * dy;
}
void frame_all(EditorState& state, ImVec2 canvas_size) {
    float min_x = std::numeric_limits<float>::max();
    float min_z = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float max_z = std::numeric_limits<float>::lowest();
    bool any = false;

    for (const auto& sector : state.scene.world.sectors()) {
        for (const auto& vertex : sector.vertices) {
            min_x = std::min(min_x, vertex.x);
            min_z = std::min(min_z, vertex.z);
            max_x = std::max(max_x, vertex.x);
            max_z = std::max(max_z, vertex.z);
            any = true;
        }
    }
    for (const auto& entity : state.scene.entities) {
        const auto world = editor_world_transform(state, entity);
        min_x = std::min(min_x, world.position.x);
        min_z = std::min(min_z, world.position.z);
        max_x = std::max(max_x, world.position.x);
        max_z = std::max(max_z, world.position.z);
        any = true;
    }

    min_x = std::min(min_x, state.scene.camera.position.x);
    min_z = std::min(min_z, state.scene.camera.position.z);
    max_x = std::max(max_x, state.scene.camera.position.x);
    max_z = std::max(max_z, state.scene.camera.position.z);
    any = true;

    if (!any || canvas_size.x <= 40.0f || canvas_size.y <= 40.0f) {
        return;
    }

    const float width = std::max(max_x - min_x, 1.0f);
    const float height = std::max(max_z - min_z, 1.0f);
    const float usable_x = std::max(canvas_size.x - 80.0f, 40.0f);
    const float usable_y = std::max(canvas_size.y - 80.0f, 40.0f);
    state.scene_view.zoom = std::clamp(std::min(usable_x / width, usable_y / height), 12.0f, 220.0f);

    const float center_x = (min_x + max_x) * 0.5f;
    const float center_z = (min_z + max_z) * 0.5f;
    state.scene_view.pan = {-center_x * state.scene_view.zoom, -center_z * state.scene_view.zoom};
    state.scene_view.frame_all_pending = false;
}
void frame_selection(EditorState& state, ImVec2 canvas_size) {
    float min_x = 0.0f;
    float min_z = 0.0f;
    float max_x = 0.0f;
    float max_z = 0.0f;
    bool have_bounds = false;

    if (state.selection.kind == SelectionKind::Sector && state.selection.index < state.scene.world.sectors().size()) {
        const auto& sector = state.scene.world.sectors()[state.selection.index];
        if (!sector.vertices.empty()) {
            min_x = max_x = sector.vertices.front().x;
            min_z = max_z = sector.vertices.front().z;
            for (const auto& vertex : sector.vertices) {
                min_x = std::min(min_x, vertex.x);
                min_z = std::min(min_z, vertex.z);
                max_x = std::max(max_x, vertex.x);
                max_z = std::max(max_z, vertex.z);
            }
            have_bounds = true;
        }
    } else if (state.selection.kind == SelectionKind::Entity && state.selection.index < state.scene.entities.size()) {
        const auto& entity = state.scene.entities[state.selection.index];
        const auto world = editor_world_transform(state, entity);
        min_x = max_x = world.position.x;
        min_z = max_z = world.position.z;
        have_bounds = true;
    } else if (state.selection.kind == SelectionKind::Camera) {
        min_x = max_x = state.scene.camera.position.x;
        min_z = max_z = state.scene.camera.position.z;
        have_bounds = true;
    }

    if (!have_bounds || canvas_size.x <= 40.0f || canvas_size.y <= 40.0f) {
        state.scene_view.frame_selection_pending = false;
        return;
    }

    const float width = max_x - min_x;
    const float height = max_z - min_z;
    if (width > 0.05f || height > 0.05f) {
        const float usable_x = std::max(canvas_size.x - 120.0f, 40.0f);
        const float usable_y = std::max(canvas_size.y - 120.0f, 40.0f);
        state.scene_view.zoom = std::clamp(
            std::min(usable_x / std::max(width, 1.0f), usable_y / std::max(height, 1.0f)),
            20.0f,
            180.0f
        );
    } else {
        state.scene_view.zoom = std::clamp(std::max(state.scene_view.zoom, 84.0f), 20.0f, 180.0f);
    }

    const float center_x = (min_x + max_x) * 0.5f;
    const float center_z = (min_z + max_z) * 0.5f;
    state.scene_view.pan = {-center_x * state.scene_view.zoom, -center_z * state.scene_view.zoom};
    state.scene_view.frame_selection_pending = false;
}
float snap_coordinate(float value, const SceneViewState& view, bool temporarily_disable) {
    if (!view.snap_enabled || temporarily_disable || view.snap_step <= 0.0001f) {
        return value;
    }
    return std::round(value / view.snap_step) * view.snap_step;
}
vespera::Vec2 snap_world_point(vespera::Vec2 point, const SceneViewState& view, bool temporarily_disable) {
    return {
        snap_coordinate(point.x, view, temporarily_disable),
        snap_coordinate(point.z, view, temporarily_disable),
    };
}

} // namespace vespera::editor
