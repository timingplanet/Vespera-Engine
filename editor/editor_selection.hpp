#pragma once

#include "editor_state.hpp"

#include <cstddef>
#include <optional>

namespace vespera::editor {

vespera::TransformComponent editor_world_transform(const EditorState& state, const vespera::Entity& entity);
void set_editor_world_transform(EditorState& state, vespera::SceneObjectId id, const vespera::TransformComponent& world);
std::size_t entity_hierarchy_depth(const EditorState& state, const vespera::Entity& entity);
bool entity_is_multi_selected(const EditorState& state, vespera::SceneObjectId id);
void select_entity(EditorState& state, std::size_t index);
void toggle_entity_selection(EditorState& state, std::size_t index);
void select_entity_range(EditorState& state, std::size_t index);
std::optional<std::size_t> selected_entity_index(const EditorState& state);
std::optional<std::size_t> entity_index_from_id(const EditorState& state, vespera::SceneObjectId id);
void repair_selection(EditorState& state);

} // namespace vespera::editor
