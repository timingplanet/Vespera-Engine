#pragma once
#include "editor_state.hpp"
namespace vespera::editor {
ImVec2 world_to_screen(const SceneViewState& view, ImVec2 canvas_min, ImVec2 canvas_size, vespera::Vec2 point);
vespera::Vec2 screen_to_world(const SceneViewState& view, ImVec2 canvas_min, ImVec2 canvas_size, ImVec2 point);
float point_segment_distance_sq(ImVec2 point, ImVec2 a, ImVec2 b);
void frame_all(EditorState& state, ImVec2 canvas_size);
void frame_selection(EditorState& state, ImVec2 canvas_size);
float snap_coordinate(float value, const SceneViewState& view, bool temporarily_disable);
vespera::Vec2 snap_world_point(vespera::Vec2 point, const SceneViewState& view, bool temporarily_disable);
}
