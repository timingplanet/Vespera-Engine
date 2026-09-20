#pragma once

#include "editor_state.hpp"

#include <optional>
#include <utility>
#include <vector>

namespace vespera::editor {

inline constexpr float kEditorDegreesToRadians = 0.017453292519943295f;
inline constexpr float kEditorRadiansToDegrees = 57.29577951308232f;

struct EditorCameraBasis {
    vespera::Vec3 forward{};
    vespera::Vec3 right{};
    vespera::Vec3 up{};
};

struct ProjectedPoint {
    ImVec2 screen{};
    float depth = 0.0f;
    bool visible = false;
};

std::optional<vespera::Vec3> selected_focus_point_3d(const EditorState& state);
void point_editor_camera_at(vespera::Camera& camera, const vespera::Vec3& target);
vespera::Vec3 vec3_add(vespera::Vec3 a, vespera::Vec3 b);
vespera::Vec3 vec3_sub(vespera::Vec3 a, vespera::Vec3 b);
vespera::Vec3 vec3_mul(vespera::Vec3 a, float scalar);
float vec3_dot(vespera::Vec3 a, vespera::Vec3 b);
vespera::Vec3 vec3_cross(vespera::Vec3 a, vespera::Vec3 b);
vespera::Vec3 vec3_normalize(vespera::Vec3 value);
EditorCameraBasis editor_camera_basis(const vespera::Camera& camera);
ProjectedPoint project_scene_point(const vespera::Camera& camera, ImVec2 canvas_min, ImVec2 canvas_size, vespera::Vec3 world);
vespera::Vec3 rotate_axis_euler(vespera::Vec3 axis, const vespera::Vec3& rotation);
vespera::Vec3 gizmo_world_axis(const vespera::TransformComponent& transform, GizmoAxis axis, bool local_space);
float snap_scalar(float value, float step, bool bypass);
float screen_distance_sq(ImVec2 a, ImVec2 b);
bool angle_in_arc(float angle, float begin, float end);
std::optional<std::size_t> pick_entity_in_3d(const EditorState& state, ImVec2 canvas_min, ImVec2 canvas_size, ImVec2 mouse);
vespera::Vec3 selected_entity_gizmo_pivot(const EditorState& state, bool center_pivot);
std::vector<std::pair<vespera::SceneObjectId, vespera::TransformComponent>> capture_selected_entity_transforms(const EditorState& state);
vespera::Vec3 rotate_vector_around_axis(vespera::Vec3 value, vespera::Vec3 axis, float radians);
vespera::editor::EditorCommandKind transform_command_kind(SceneTool tool);
const char* scene_tool_name(SceneTool tool);
void cancel_3d_gizmo_drag(EditorState& state);
void commit_3d_gizmo_drag(EditorState& state);
void frame_3d_selection(EditorState& state);

} // namespace vespera::editor
