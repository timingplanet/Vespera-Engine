#pragma once
#include "editor_state.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
namespace vespera::editor {
bool entity_name_exists(const vespera::Scene& scene, std::string_view name);
std::string unique_entity_name(const vespera::Scene& scene, std::string base);
bool clip_name_exists(const vespera::Scene& scene, std::string_view name, std::size_t ignore = kNoSubSelection);
std::string unique_clip_name(const vespera::Scene& scene, std::string base);
bool sector_name_exists(const vespera::Scene& scene, std::string_view name, std::size_t ignore = kNoSubSelection);
std::string unique_sector_name(const vespera::Scene& scene, std::string base);
bool material_name_exists(const vespera::Scene& scene, std::string_view name, std::size_t ignore = kNoSubSelection);
std::string unique_material_name(const vespera::Scene& scene, std::string base);
vespera::TextureId default_sprite_texture(const vespera::Scene& scene);
void resize_clip_frames(vespera::SpriteAnimationClip& clip, std::uint32_t new_directions, std::uint32_t new_frames, vespera::TextureId fallback);
vespera::Vec2 sector_centroid(const vespera::Sector& sector);
float floor_height_at(const vespera::Scene& scene, vespera::Vec2 point, float fallback = 0.0f);
vespera::Vec2 default_creation_point(const EditorState& state);
bool command_create_sector(EditorState& state);
bool command_duplicate_selected_sector(EditorState& state);
bool command_delete_selected_sector(EditorState& state);
bool command_create_material(EditorState& state);
bool command_duplicate_selected_material(EditorState& state);
bool command_delete_selected_material(EditorState& state);
bool command_set_sector_portal_target(EditorState& state, std::size_t sector_index, std::size_t side_index, int target_sector);
bool command_create_entity(EditorState& state, std::string_view requested_name = {});
bool command_create_sprite_entity(EditorState& state, std::string_view requested_name = {});
bool command_create_primitive_entity(EditorState& state, vespera::PrimitiveMeshType primitive);
bool command_create_trigger_entity(EditorState& state, std::string_view requested_name = {});
bool command_create_point_light_entity(EditorState& state, std::string_view requested_name = {});
bool command_reorder_entity(EditorState& state, vespera::SceneObjectId dragged_id, vespera::SceneObjectId target_id);
bool command_reparent_entity(EditorState& state, vespera::SceneObjectId child_id, vespera::SceneObjectId parent_id);
bool command_duplicate_selected_entity(EditorState& state);
bool command_delete_selected_entity(EditorState& state);
bool command_create_clip(EditorState& state);
bool command_duplicate_selected_clip(EditorState& state);
bool command_delete_selected_clip(EditorState& state);
bool command_duplicate_selected_entities(EditorState& state);
bool command_delete_selected_entities(EditorState& state);
bool command_duplicate_selection(EditorState& state);
bool command_delete_selection(EditorState& state);
}
