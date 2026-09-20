#include "editor_selection.hpp"

#include <vespera/scene/scene_hierarchy.hpp>

#include <algorithm>

namespace vespera::editor {

vespera::TransformComponent editor_world_transform(const EditorState& state, const vespera::Entity& entity) {
    return vespera::entity_world_transform(state.scene, entity);
}
void set_editor_world_transform(EditorState& state, vespera::SceneObjectId id, const vespera::TransformComponent& world) {
    auto* entity = state.scene.find_entity(id);
    if (!entity) return;
    if (entity->parent_id == vespera::kInvalidSceneObjectId) entity->transform = world;
    else entity->transform = vespera::inverse_compose_transform(
        vespera::entity_world_transform(state.scene, entity->parent_id), world);
}
std::size_t entity_hierarchy_depth(const EditorState& state, const vespera::Entity& entity) {
    std::size_t depth = 0;
    vespera::SceneObjectId parent = entity.parent_id;
    while (parent != vespera::kInvalidSceneObjectId && depth < state.scene.entities.size()) {
        const auto* node = state.scene.find_entity(parent);
        if (!node) break;
        ++depth;
        parent = node->parent_id;
    }
    return depth;
}
bool entity_is_multi_selected(const EditorState& state, vespera::SceneObjectId id) {
    return std::find(state.selected_entity_ids.begin(), state.selected_entity_ids.end(), id) != state.selected_entity_ids.end();
}
void select_entity(EditorState& state, std::size_t index) {
    if (index >= state.scene.entities.size()) {
        state.selection = {};
        state.selected_entity_ids.clear();
        state.hierarchy_anchor_id = vespera::kInvalidSceneObjectId;
        return;
    }
    const auto id = state.scene.entities[index].id;
    state.selection = {SelectionKind::Entity, index, kNoSubSelection, id, kNoSubSelection, {}};
    state.selected_entity_ids.assign(1, id);
    state.hierarchy_anchor_id = id;
}
void toggle_entity_selection(EditorState& state, std::size_t index) {
    if (index >= state.scene.entities.size()) return;
    const auto id = state.scene.entities[index].id;
    if (state.selection.kind != SelectionKind::Entity) state.selected_entity_ids.clear();
    const auto it = std::find(state.selected_entity_ids.begin(), state.selected_entity_ids.end(), id);
    if (it == state.selected_entity_ids.end()) {
        state.selected_entity_ids.push_back(id);
        state.selection = {SelectionKind::Entity, index, kNoSubSelection, id, kNoSubSelection, {}};
    } else {
        state.selected_entity_ids.erase(it);
        if (state.selected_entity_ids.empty()) {
            state.selection = {};
        } else if (state.selection.object_id == id) {
            const auto next_id = state.selected_entity_ids.back();
            if (const auto next_index = entity_index_from_id(state, next_id)) {
                state.selection = {SelectionKind::Entity, *next_index, kNoSubSelection, next_id, kNoSubSelection, {}};
            }
        }
    }
    state.hierarchy_anchor_id = id;
}
std::optional<std::size_t> entity_index_from_id(const EditorState& state, vespera::SceneObjectId id) {
    if (id == vespera::kInvalidSceneObjectId) return std::nullopt;
    for (std::size_t i = 0; i < state.scene.entities.size(); ++i) {
        if (state.scene.entities[i].id == id) return i;
    }
    return std::nullopt;
}
void select_entity_range(EditorState& state, std::size_t index) {
    if (index >= state.scene.entities.size()) return;
    std::size_t anchor = index;
    if (state.selection.kind == SelectionKind::Entity) {
        if (const auto resolved = entity_index_from_id(state, state.hierarchy_anchor_id)) anchor = *resolved;
    } else {
        state.selected_entity_ids.clear();
        state.hierarchy_anchor_id = state.scene.entities[index].id;
    }
    const auto first = std::min(anchor, index);
    const auto last = std::max(anchor, index);
    state.selected_entity_ids.clear();
    for (std::size_t i = first; i <= last; ++i) state.selected_entity_ids.push_back(state.scene.entities[i].id);
    const auto id = state.scene.entities[index].id;
    state.selection = {SelectionKind::Entity, index, kNoSubSelection, id, kNoSubSelection, {}};
    if (state.hierarchy_anchor_id == vespera::kInvalidSceneObjectId) state.hierarchy_anchor_id = id;
}
std::optional<std::size_t> selected_entity_index(const EditorState& state) {
    if (state.selection.kind != SelectionKind::Entity) {
        return std::nullopt;
    }
    if (state.selection.object_id != vespera::kInvalidSceneObjectId) {
        for (std::size_t i = 0; i < state.scene.entities.size(); ++i) {
            if (state.scene.entities[i].id == state.selection.object_id) {
                return i;
            }
        }
    }
    if (state.selection.index < state.scene.entities.size()) {
        return state.selection.index;
    }
    return std::nullopt;
}
void repair_selection(EditorState& state) {
    if (state.selection.kind == SelectionKind::Entity) {
        state.selected_entity_ids.erase(
            std::remove_if(state.selected_entity_ids.begin(), state.selected_entity_ids.end(), [&](vespera::SceneObjectId id) {
                return !entity_index_from_id(state, id).has_value();
            }),
            state.selected_entity_ids.end());
        const auto index = selected_entity_index(state);
        if (!index) {
            state.selection = {};
            state.selected_entity_ids.clear();
            return;
        }
        state.selection.index = *index;
        state.selection.object_id = state.scene.entities[*index].id;
        if (!entity_is_multi_selected(state, state.selection.object_id)) state.selected_entity_ids.push_back(state.selection.object_id);
    } else if (state.selection.kind == SelectionKind::Sector) {
        const auto& sectors = state.scene.world.sectors();
        if (state.selection.index >= sectors.size()) {
            state.selection = {};
            return;
        }
        const auto& sector = sectors[state.selection.index];
        if (state.selection.sub_index >= sector.vertices.size()) {
            state.selection.sub_index = kNoSubSelection;
        }
        if (state.selection.side_index >= sector.sides.size()) {
            state.selection.side_index = kNoSubSelection;
        }
    } else if (state.selection.kind == SelectionKind::Material
        && state.selection.index >= state.scene.world.materials().size()) {
        state.selection = {};
    } else if (state.selection.kind == SelectionKind::SpriteClip
        && state.selection.index >= state.scene.sprite_clips.size()) {
        state.selection = {};
    } else if (state.selection.kind == SelectionKind::Asset
        && (state.selection.asset_id.empty() || !state.asset_catalog.find_by_id(state.selection.asset_id))) {
        state.selection = {};
    }
}

} // namespace vespera::editor
