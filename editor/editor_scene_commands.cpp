#include "editor_scene_commands.hpp"
#include "editor_command_runtime.hpp"
#include "editor_selection.hpp"
#include "editor_view_helpers.hpp"
#include <vespera/scene/scene_hierarchy.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
namespace vespera::editor {

bool entity_name_exists(const vespera::Scene& scene, std::string_view name) {
    return std::any_of(scene.entities.begin(), scene.entities.end(), [&](const vespera::Entity& entity) {
        return entity.name == name;
    });
}
std::string unique_entity_name(const vespera::Scene& scene, std::string base) {
    if (!entity_name_exists(scene, base)) {
        return base;
    }
    for (int suffix = 2; suffix < 10000; ++suffix) {
        const std::string candidate = std::format("{} {}", base, suffix);
        if (!entity_name_exists(scene, candidate)) {
            return candidate;
        }
    }
    return base + " Unique";
}
bool clip_name_exists(const vespera::Scene& scene, std::string_view name, std::size_t ignore) {
    for (std::size_t i = 0; i < scene.sprite_clips.size(); ++i) {
        if (i != ignore && scene.sprite_clips[i].name == name) {
            return true;
        }
    }
    return false;
}
std::string unique_clip_name(const vespera::Scene& scene, std::string base) {
    if (!clip_name_exists(scene, base)) {
        return base;
    }
    for (int suffix = 2; suffix < 10000; ++suffix) {
        const std::string candidate = std::format("{} {}", base, suffix);
        if (!clip_name_exists(scene, candidate)) {
            return candidate;
        }
    }
    return base + " Unique";
}
bool sector_name_exists(const vespera::Scene& scene, std::string_view name, std::size_t ignore) {
    const auto& sectors = scene.world.sectors();
    for (std::size_t i = 0; i < sectors.size(); ++i) {
        if (i != ignore && sectors[i].name == name) {
            return true;
        }
    }
    return false;
}
std::string unique_sector_name(const vespera::Scene& scene, std::string base) {
    if (!sector_name_exists(scene, base)) {
        return base;
    }
    for (int suffix = 2; suffix < 10000; ++suffix) {
        const std::string candidate = std::format("{} {}", base, suffix);
        if (!sector_name_exists(scene, candidate)) {
            return candidate;
        }
    }
    return base + " Unique";
}
bool material_name_exists(const vespera::Scene& scene, std::string_view name, std::size_t ignore) {
    const auto& materials = scene.world.materials();
    for (std::size_t i = 0; i < materials.size(); ++i) {
        if (i != ignore && materials[i].name == name) {
            return true;
        }
    }
    return false;
}
std::string unique_material_name(const vespera::Scene& scene, std::string base) {
    if (!material_name_exists(scene, base)) {
        return base;
    }
    for (int suffix = 2; suffix < 10000; ++suffix) {
        const std::string candidate = std::format("{} {}", base, suffix);
        if (!material_name_exists(scene, candidate)) {
            return candidate;
        }
    }
    return base + " Unique";
}
vespera::TextureId default_sprite_texture(const vespera::Scene& scene) {
    const auto& textures = scene.world.textures();
    for (std::size_t i = 0; i < textures.size(); ++i) {
        if (textures[i].name == "Test Sprite") {
            return static_cast<vespera::TextureId>(i);
        }
    }
    return textures.empty() ? vespera::kInvalidTexture : 0u;
}
void resize_clip_frames(
    vespera::SpriteAnimationClip& clip,
    std::uint32_t new_directions,
    std::uint32_t new_frames,
    vespera::TextureId fallback
) {
    new_directions = (new_directions == 4u || new_directions == 8u) ? new_directions : 1u;
    new_frames = std::clamp(new_frames, 1u, 16u);
    std::vector<vespera::TextureId> resized(
        static_cast<std::size_t>(new_directions) * new_frames,
        fallback
    );
    const std::uint32_t copy_directions = std::min(clip.direction_count, new_directions);
    const std::uint32_t copy_frames = std::min(clip.frame_count, new_frames);
    for (std::uint32_t direction = 0; direction < copy_directions; ++direction) {
        for (std::uint32_t frame = 0; frame < copy_frames; ++frame) {
            const auto old_index = static_cast<std::size_t>(direction) * clip.frame_count + frame;
            const auto new_index = static_cast<std::size_t>(direction) * new_frames + frame;
            if (old_index < clip.textures.size()) {
                resized[new_index] = clip.textures[old_index];
            }
        }
    }
    clip.direction_count = new_directions;
    clip.frame_count = new_frames;
    clip.textures = std::move(resized);
}
vespera::Vec2 sector_centroid(const vespera::Sector& sector) {
    vespera::Vec2 center{};
    if (sector.vertices.empty()) {
        return center;
    }
    for (const auto& vertex : sector.vertices) {
        center.x += vertex.x;
        center.z += vertex.z;
    }
    const float inv_count = 1.0f / static_cast<float>(sector.vertices.size());
    center.x *= inv_count;
    center.z *= inv_count;
    return center;
}
float floor_height_at(const vespera::Scene& scene, vespera::Vec2 point, float fallback) {
    if (const auto sector_index = scene.world.find_sector_index(point)) {
        return scene.world.sectors()[*sector_index].floor_height;
    }
    return fallback;
}
vespera::Vec2 default_creation_point(const EditorState& state) {
    if (state.selection.kind == SelectionKind::Sector && state.selection.index < state.scene.world.sectors().size()) {
        return sector_centroid(state.scene.world.sectors()[state.selection.index]);
    }
    if (const auto index = selected_entity_index(state)) {
        const auto& entity = state.scene.entities[*index];
        const auto world = editor_world_transform(state, entity);
        return {world.position.x, world.position.z};
    }
    return {state.scene.camera.position.x, state.scene.camera.position.z};
}
bool command_create_sector(EditorState& state) {
    return execute_editor_command(state, vespera::editor::EditorCommandKind::CreateSector, "Create sector", [&]() {
        vespera::Sector sector;
        sector.name = unique_sector_name(state.scene, "Sector");
        sector.floor_height = 0.0f;
        sector.ceiling_height = 3.0f;

        vespera::Vec2 center = snap_world_point(default_creation_point(state), state.scene_view, false);
        if (state.selection.kind == SelectionKind::Sector
            && state.selection.index < state.scene.world.sectors().size()) {
            const auto& source = state.scene.world.sectors()[state.selection.index];
            sector.floor_height = source.floor_height;
            sector.ceiling_height = source.ceiling_height;
            sector.floor_material = source.floor_material;
            sector.ceiling_material = source.ceiling_material;
            sector.wall_material = source.wall_material;

            float max_x = source.vertices.empty() ? center.x : source.vertices.front().x;
            float min_z = source.vertices.empty() ? center.z : source.vertices.front().z;
            float max_z = min_z;
            for (const auto& vertex : source.vertices) {
                max_x = std::max(max_x, vertex.x);
                min_z = std::min(min_z, vertex.z);
                max_z = std::max(max_z, vertex.z);
            }
            const float gap = state.scene_view.snap_enabled ? std::max(state.scene_view.snap_step, 0.05f) : 0.25f;
            center = snap_world_point({max_x + 2.0f + gap, (min_z + max_z) * 0.5f}, state.scene_view, false);
        } else if (!state.scene.world.materials().empty()) {
            sector.floor_material = 0u;
            sector.ceiling_material = 0u;
            sector.wall_material = 0u;
        }

        constexpr float half_size = 2.0f;
        sector.vertices = {
            {center.x - half_size, center.z - half_size},
            {center.x + half_size, center.z - half_size},
            {center.x + half_size, center.z + half_size},
            {center.x - half_size, center.z + half_size},
        };
        sector.sides.resize(sector.vertices.size());
        for (auto& side : sector.sides) {
            side.material = sector.wall_material;
            side.adjacent_sector = -1;
        }

        const std::size_t index = state.scene.world.add_sector(std::move(sector));
        state.selection = {SelectionKind::Sector, index, kNoSubSelection, vespera::kInvalidSceneObjectId, kNoSubSelection, {}};
        state.scene_view.frame_selection_pending = true;
        return true;
    });
}
bool command_duplicate_selected_sector(EditorState& state) {
    if (state.selection.kind != SelectionKind::Sector
        || state.selection.index >= state.scene.world.sectors().size()) {
        return false;
    }
    const std::size_t source_index = state.selection.index;
    return execute_editor_command(state, vespera::editor::EditorCommandKind::DuplicateSector, "Duplicate sector", [&]() {
        vespera::Sector copy = state.scene.world.sectors()[source_index];
        copy.name = unique_sector_name(state.scene, copy.name + " Copy");

        float min_x = copy.vertices.empty() ? 0.0f : copy.vertices.front().x;
        float max_x = min_x;
        for (const auto& vertex : copy.vertices) {
            min_x = std::min(min_x, vertex.x);
            max_x = std::max(max_x, vertex.x);
        }
        const float gap = state.scene_view.snap_enabled ? std::max(state.scene_view.snap_step, 0.05f) : 0.25f;
        const float offset = std::max(max_x - min_x, 1.0f) + gap;
        for (auto& vertex : copy.vertices) {
            vertex.x = snap_coordinate(vertex.x + offset, state.scene_view, false);
        }
        for (auto& side : copy.sides) {
            // A duplicate must never silently inherit portal links into the old
            // topology. The copied wall materials remain intact.
            side.adjacent_sector = -1;
        }

        const std::size_t index = state.scene.world.add_sector(std::move(copy));
        state.selection = {SelectionKind::Sector, index, kNoSubSelection, vespera::kInvalidSceneObjectId, kNoSubSelection, {}};
        state.scene_view.frame_selection_pending = true;
        return true;
    });
}
bool command_delete_selected_sector(EditorState& state) {
    if (state.selection.kind != SelectionKind::Sector
        || state.selection.index >= state.scene.world.sectors().size()) {
        return false;
    }
    const std::size_t delete_index = state.selection.index;
    return execute_editor_command(state, vespera::editor::EditorCommandKind::DeleteSector, "Delete sector", [&]() {
        if (!state.scene.world.erase_sector(delete_index)) {
            return false;
        }
        const auto& sectors = state.scene.world.sectors();
        if (sectors.empty()) {
            state.selection = {};
        } else {
            state.selection = {SelectionKind::Sector, std::min(delete_index, sectors.size() - 1), kNoSubSelection, vespera::kInvalidSceneObjectId, kNoSubSelection, {}};
        }
        return true;
    });
}
bool command_create_material(EditorState& state) {
    return execute_editor_command(state, vespera::editor::EditorCommandKind::CreateMaterial, "Create material", [&]() {
        vespera::WorldMaterial material;
        material.name = unique_material_name(state.scene, "Material");
        material.color = {1.0f, 1.0f, 1.0f, 1.0f};
        material.texture = state.scene.world.textures().empty() ? vespera::kInvalidTexture : 0u;
        material.uv_scale = {0.5f, 0.5f};
        const auto id = state.scene.world.add_material(std::move(material));
        state.selection = {SelectionKind::Material, static_cast<std::size_t>(id), kNoSubSelection, vespera::kInvalidSceneObjectId, kNoSubSelection, {}};
        return true;
    });
}
bool command_duplicate_selected_material(EditorState& state) {
    if (state.selection.kind != SelectionKind::Material
        || state.selection.index >= state.scene.world.materials().size()) {
        return false;
    }
    const std::size_t source_index = state.selection.index;
    return execute_editor_command(state, vespera::editor::EditorCommandKind::DuplicateMaterial, "Duplicate material", [&]() {
        auto copy = state.scene.world.materials()[source_index];
        copy.name = unique_material_name(state.scene, copy.name + " Copy");
        const auto id = state.scene.world.add_material(std::move(copy));
        state.selection = {SelectionKind::Material, static_cast<std::size_t>(id), kNoSubSelection, vespera::kInvalidSceneObjectId, kNoSubSelection, {}};
        return true;
    });
}
bool command_delete_selected_material(EditorState& state) {
    if (state.selection.kind != SelectionKind::Material
        || state.selection.index >= state.scene.world.materials().size()) {
        return false;
    }
    const std::size_t delete_index = state.selection.index;
    return execute_editor_command(state, vespera::editor::EditorCommandKind::DeleteMaterial, "Delete material", [&]() {
        if (!state.scene.world.erase_material(delete_index)) {
            return false;
        }
        const auto& materials = state.scene.world.materials();
        if (materials.empty()) {
            state.selection = {};
        } else {
            state.selection = {SelectionKind::Material, std::min(delete_index, materials.size() - 1), kNoSubSelection, vespera::kInvalidSceneObjectId, kNoSubSelection, {}};
        }
        return true;
    });
}
bool command_set_sector_portal_target(
    EditorState& state,
    std::size_t sector_index,
    std::size_t side_index,
    int target_sector
) {
    const auto& sectors = state.scene.world.sectors();
    if (sector_index >= sectors.size()
        || side_index >= sectors[sector_index].sides.size()
        || target_sector < -1
        || target_sector >= static_cast<int>(sectors.size())
        || target_sector == static_cast<int>(sector_index)) {
        return false;
    }

    return execute_editor_command(state, vespera::editor::EditorCommandKind::SetPortalTarget, target_sector >= 0 ? "Set portal target" : "Make sector side solid", [&]() {
        // Work on copies so all reciprocal repairs become one undoable edit.
        std::vector<vespera::Sector> edited = state.scene.world.sectors();
        auto& source = edited[sector_index];
        const int previous_target = source.sides[side_index].adjacent_sector;

        if (previous_target >= 0 && previous_target < static_cast<int>(edited.size())) {
            if (const auto old_match = vespera::find_matching_sector_side(
                    state.scene.world, sector_index, side_index, static_cast<std::size_t>(previous_target))) {
                auto& old_target = edited[static_cast<std::size_t>(previous_target)];
                if (*old_match < old_target.sides.size()
                    && old_target.sides[*old_match].adjacent_sector == static_cast<int>(sector_index)) {
                    old_target.sides[*old_match].adjacent_sector = -1;
                }
            }
        }

        source.sides[side_index].adjacent_sector = target_sector;
        int displaced_target = -1;
        if (target_sector >= 0) {
            if (const auto match = vespera::find_matching_sector_side(
                    state.scene.world, sector_index, side_index, static_cast<std::size_t>(target_sector))) {
                auto& target = edited[static_cast<std::size_t>(target_sector)];
                if (*match < target.sides.size()) {
                    displaced_target = target.sides[*match].adjacent_sector;
                    if (displaced_target >= 0
                        && displaced_target < static_cast<int>(edited.size())
                        && displaced_target != static_cast<int>(sector_index)) {
                        if (const auto displaced_match = vespera::find_matching_sector_side(
                                state.scene.world,
                                static_cast<std::size_t>(target_sector),
                                *match,
                                static_cast<std::size_t>(displaced_target))) {
                            auto& displaced = edited[static_cast<std::size_t>(displaced_target)];
                            if (*displaced_match < displaced.sides.size()
                                && displaced.sides[*displaced_match].adjacent_sector == target_sector) {
                                displaced.sides[*displaced_match].adjacent_sector = -1;
                            }
                        }
                    }
                    target.sides[*match].adjacent_sector = static_cast<int>(sector_index);
                }
            }
        }

        bool changed = false;
        for (std::size_t i = 0; i < edited.size(); ++i) {
            if (i == sector_index
                || static_cast<int>(i) == previous_target
                || static_cast<int>(i) == target_sector
                || static_cast<int>(i) == displaced_target) {
                changed |= state.scene.world.set_sector(i, std::move(edited[i]));
            }
        }
        state.selection = {SelectionKind::Sector, sector_index, kNoSubSelection, vespera::kInvalidSceneObjectId, kNoSubSelection, {}};
        state.selection.side_index = side_index;
        return changed;
    });
}
bool command_create_entity(EditorState& state, std::string_view requested_name) {
    return execute_editor_command(state, vespera::editor::EditorCommandKind::CreateEntity, "Create entity", [&]() {
        const vespera::Vec2 point = snap_world_point(default_creation_point(state), state.scene_view, false);
        const std::string base_name = requested_name.empty() ? "Entity" : std::string(requested_name);
        auto& entity = state.scene.create_entity(unique_entity_name(state.scene, base_name));
        entity.transform.position = {point.x, floor_height_at(state.scene, point), point.z};
        select_entity(state, state.scene.entities.size() - 1);
        state.scene_view.frame_selection_pending = true;
        return true;
    });
}
bool command_create_sprite_entity(EditorState& state, std::string_view requested_name) {
    return execute_editor_command(state, vespera::editor::EditorCommandKind::CreateSpriteEntity, "Create sprite entity", [&]() {
        const vespera::Vec2 point = snap_world_point(default_creation_point(state), state.scene_view, false);
        const std::string base_name = requested_name.empty() ? "Sprite Entity" : std::string(requested_name);
        auto& entity = state.scene.create_entity(unique_entity_name(state.scene, base_name));
        entity.transform.position = {point.x, floor_height_at(state.scene, point), point.z};
        auto& sprite = entity.add_sprite_renderer();
        sprite.size = {1.0f, 1.5f};
        sprite.texture = default_sprite_texture(state.scene);
        sprite.color = {1.0f, 1.0f, 1.0f, 1.0f};
        select_entity(state, state.scene.entities.size() - 1);
        state.scene_view.frame_selection_pending = true;
        return true;
    });
}
bool command_create_primitive_entity(EditorState& state, vespera::PrimitiveMeshType primitive) {
    using vespera::editor::EditorCommandKind;
    EditorCommandKind kind = EditorCommandKind::CreateCubeEntity;
    std::string base_name = "Cube";
    switch (primitive) {
        case vespera::PrimitiveMeshType::Cube: kind=EditorCommandKind::CreateCubeEntity; base_name="Cube"; break;
        case vespera::PrimitiveMeshType::Plane: kind=EditorCommandKind::CreatePlaneEntity; base_name="Plane"; break;
        case vespera::PrimitiveMeshType::Cylinder: kind=EditorCommandKind::CreateCylinderEntity; base_name="Cylinder"; break;
        case vespera::PrimitiveMeshType::Sphere: kind=EditorCommandKind::CreateSphereEntity; base_name="Sphere"; break;
    }
    return execute_editor_command(state, kind, "Create " + base_name, [&]() {
        const vespera::Vec2 point = snap_world_point(default_creation_point(state), state.scene_view, false);
        auto& entity = state.scene.create_entity(unique_entity_name(state.scene, base_name));
        const float floor = floor_height_at(state.scene, point);
        entity.transform.position = {point.x, floor + (primitive == vespera::PrimitiveMeshType::Plane ? 0.01f : 0.5f), point.z};
        auto& mesh = entity.add_mesh_renderer(); mesh.primitive = primitive; mesh.texture = vespera::kInvalidTexture;
        select_entity(state, state.scene.entities.size() - 1); state.scene_view.frame_selection_pending = true;
        return true;
    });
}
bool command_create_trigger_entity(EditorState& state, std::string_view requested_name) {
    return execute_editor_command(state, vespera::editor::EditorCommandKind::CreateTriggerEntity, "Create trigger entity", [&]() {
        const vespera::Vec2 point = snap_world_point(default_creation_point(state), state.scene_view, false);
        const std::string base_name = requested_name.empty() ? "Trigger Volume" : std::string(requested_name);
        auto& entity = state.scene.create_entity(unique_entity_name(state.scene, base_name));
        entity.tag = "trigger";
        entity.layer = "Triggers";
        entity.transform.position = {point.x, floor_height_at(state.scene, point), point.z};
        auto& collider = entity.add_cylinder_collider();
        collider.radius = 1.0f;
        collider.height = 2.0f;
        collider.center = {0.0f, 1.0f, 0.0f};
        collider.is_trigger = true;
        select_entity(state, state.scene.entities.size() - 1);
        state.scene_view.frame_selection_pending = true;
        return true;
    });
}
bool command_create_point_light_entity(EditorState& state, std::string_view requested_name) {
    return execute_editor_command(state, vespera::editor::EditorCommandKind::CreatePointLightEntity, "Create point light entity", [&]() {
        const vespera::Vec2 point = snap_world_point(default_creation_point(state), state.scene_view, false);
        const std::string base_name = requested_name.empty() ? "Point Light" : std::string(requested_name);
        auto& entity = state.scene.create_entity(unique_entity_name(state.scene, base_name));
        entity.tag = "light";
        entity.layer = "Lighting";
        entity.transform.position = {point.x, floor_height_at(state.scene, point) + 1.8f, point.z};
        auto& light = entity.add_point_light();
        light.color = {1.0f, 0.72f, 0.42f, 1.0f};
        light.intensity = 1.35f;
        light.radius = 5.0f;
        select_entity(state, state.scene.entities.size() - 1);
        state.scene_view.frame_selection_pending = true;
        return true;
    });
}
bool command_reorder_entity(EditorState& state, vespera::SceneObjectId dragged_id, vespera::SceneObjectId target_id) {
    if (dragged_id == target_id) return false;
    const auto source_index = entity_index_from_id(state, dragged_id);
    const auto target_index = entity_index_from_id(state, target_id);
    if (!source_index || !target_index) return false;
    return execute_editor_command(state, vespera::editor::EditorCommandKind::ReorderEntity, "Reorder entity", [&]() {
        auto source = entity_index_from_id(state, dragged_id);
        auto target = entity_index_from_id(state, target_id);
        if (!source || !target || *source == *target) return false;
        auto moved = std::move(state.scene.entities[*source]);
        state.scene.entities.erase(state.scene.entities.begin() + static_cast<std::ptrdiff_t>(*source));
        std::size_t insertion = *target;
        if (*source < *target && insertion > 0) --insertion;
        insertion = std::min(insertion, state.scene.entities.size());
        state.scene.entities.insert(
            state.scene.entities.begin() + static_cast<std::ptrdiff_t>(insertion),
            std::move(moved)
        );
        repair_selection(state);
        return true;
    });
}
bool command_reparent_entity(EditorState& state, vespera::SceneObjectId child_id, vespera::SceneObjectId parent_id) {
    const bool unparent = parent_id == vespera::kInvalidSceneObjectId;
    return execute_editor_command(
        state,
        unparent ? vespera::editor::EditorCommandKind::UnparentEntity : vespera::editor::EditorCommandKind::ReparentEntity,
        unparent ? "Unparent entity" : "Reparent entity",
        [&]() {
            std::string error;
            if (!vespera::reparent_scene_entity(state.scene, child_id, parent_id, true, &error)) {
                if (!error.empty()) push_console(state, ConsoleEntry::Level::Warning, "Hierarchy: " + error);
                return false;
            }
            // Keep a newly parented subtree adjacent to its parent in authoring
            // order so the flat ImGui Hierarchy still reads like a real tree.
            if (parent_id != vespera::kInvalidSceneObjectId) {
                std::vector<vespera::Entity> subtree;
                std::vector<vespera::Entity> remaining;
                subtree.reserve(state.scene.entities.size());
                remaining.reserve(state.scene.entities.size());
                for (auto& candidate : state.scene.entities) {
                    if (candidate.id == child_id || vespera::scene_entity_is_descendant_of(state.scene, candidate.id, child_id)) {
                        subtree.push_back(std::move(candidate));
                    } else {
                        remaining.push_back(std::move(candidate));
                    }
                }
                const auto parent_it = std::find_if(remaining.begin(), remaining.end(), [parent_id](const vespera::Entity& candidate) {
                    return candidate.id == parent_id;
                });
                if (parent_it != remaining.end()) {
                    const auto insertion = static_cast<std::size_t>(std::distance(remaining.begin(), parent_it)) + 1u;
                    remaining.insert(
                        remaining.begin() + static_cast<std::ptrdiff_t>(insertion),
                        std::make_move_iterator(subtree.begin()),
                        std::make_move_iterator(subtree.end()));
                    state.scene.entities = std::move(remaining);
                }
            }
            repair_selection(state);
            return true;
        });
}
bool command_duplicate_selected_entity(EditorState& state) {
    const auto selected = selected_entity_index(state);
    if (!selected) return false;
    const std::size_t source_index = *selected;
    return execute_editor_command(state, vespera::editor::EditorCommandKind::DuplicateEntity, "Duplicate entity", [&]() {
        const auto source_id = state.scene.entities[source_index].id;
        const std::string copy_name = unique_entity_name(state.scene, state.scene.entities[source_index].name + " Copy");
        vespera::Entity* copy = state.scene.clone_entity(source_id, copy_name);
        if (!copy) return false;
        const float offset = state.scene_view.snap_enabled ? std::max(state.scene_view.snap_step, 0.05f) : 0.25f;
        copy->transform.position.x += offset;
        copy->transform.position.z += offset;
        copy->transform.position.y = floor_height_at(
            state.scene,
            {copy->transform.position.x, copy->transform.position.z},
            copy->transform.position.y);
        select_entity(state, state.scene.entities.size() - 1);
        state.scene_view.frame_selection_pending = true;
        return true;
    });
}
bool command_delete_selected_entity(EditorState& state) {
    const auto selected = selected_entity_index(state);
    if (!selected) return false;
    const std::size_t delete_index = *selected;
    return execute_editor_command(state, vespera::editor::EditorCommandKind::DeleteEntity, "Delete entity", [&]() {
        const auto delete_id = state.scene.entities[delete_index].id;
        if (!state.scene.destroy_entity(delete_id)) return false;
        if (state.scene.entities.empty()) state.selection = {};
        else select_entity(state, std::min(delete_index, state.scene.entities.size() - 1));
        return true;
    });
}
bool command_create_clip(EditorState& state) {
    return execute_editor_command(state, vespera::editor::EditorCommandKind::CreateSpriteClip, "Create sprite clip", [&]() {
        vespera::SpriteAnimationClip clip;
        clip.name = unique_clip_name(state.scene, "Sprite Clip");
        clip.direction_count = 1;
        clip.frame_count = 1;
        clip.frames_per_second = 4.0f;
        clip.loop = true;
        clip.textures = {default_sprite_texture(state.scene)};
        state.scene.sprite_clips.push_back(std::move(clip));
        state.selection = {SelectionKind::SpriteClip, state.scene.sprite_clips.size() - 1, kNoSubSelection, vespera::kInvalidSceneObjectId, kNoSubSelection, {}};
        return true;
    });
}
bool command_duplicate_selected_clip(EditorState& state) {
    if (state.selection.kind != SelectionKind::SpriteClip || state.selection.index >= state.scene.sprite_clips.size()) {
        return false;
    }
    const std::size_t source = state.selection.index;
    return execute_editor_command(state, vespera::editor::EditorCommandKind::DuplicateSpriteClip, "Duplicate sprite clip", [&]() {
        auto copy = state.scene.sprite_clips[source];
        copy.name = unique_clip_name(state.scene, copy.name + " Copy");
        state.scene.sprite_clips.push_back(std::move(copy));
        state.selection = {SelectionKind::SpriteClip, state.scene.sprite_clips.size() - 1, kNoSubSelection, vespera::kInvalidSceneObjectId, kNoSubSelection, {}};
        return true;
    });
}
bool command_delete_selected_clip(EditorState& state) {
    if (state.selection.kind != SelectionKind::SpriteClip || state.selection.index >= state.scene.sprite_clips.size()) {
        return false;
    }
    const std::size_t delete_index = state.selection.index;
    const std::string deleted_name = state.scene.sprite_clips[delete_index].name;
    return execute_editor_command(state, vespera::editor::EditorCommandKind::DeleteSpriteClip, "Delete sprite clip", [&]() {
        for (auto& entity : state.scene.entities) {
            if (entity.sprite_renderer && entity.sprite_renderer->animation_clip == deleted_name) {
                entity.sprite_renderer->animation_clip.clear();
            }
        }
        state.scene.sprite_clips.erase(state.scene.sprite_clips.begin() + static_cast<std::ptrdiff_t>(delete_index));
        if (state.scene.sprite_clips.empty()) {
            state.selection = {};
        } else {
            state.selection = {SelectionKind::SpriteClip, std::min(delete_index, state.scene.sprite_clips.size() - 1), kNoSubSelection, vespera::kInvalidSceneObjectId, kNoSubSelection, {}};
        }
        return true;
    });
}
bool command_duplicate_selected_entities(EditorState& state) {
    if (state.selection.kind != SelectionKind::Entity || state.selected_entity_ids.size() < 2) return false;
    const auto source_ids = state.selected_entity_ids;
    return execute_editor_command(state, vespera::editor::EditorCommandKind::DuplicateEntity, "Duplicate entities", [&]() {
        std::vector<vespera::SceneObjectId> copies;
        std::unordered_map<vespera::SceneObjectId, vespera::SceneObjectId> duplicate_ids;
        const float offset = state.scene_view.snap_enabled ? std::max(state.scene_view.snap_step, 0.05f) : 0.25f;
        for (const auto source_id : source_ids) {
            const auto source_index = entity_index_from_id(state, source_id);
            if (!source_index) continue;
            const auto source_name = state.scene.entities[*source_index].name;
            vespera::Entity* copy = state.scene.clone_entity(source_id, unique_entity_name(state.scene, source_name + " Copy"));
            if (!copy) continue;
            duplicate_ids[source_id] = copy->id;
            copies.push_back(copy->id);
        }
        for (const auto source_id : source_ids) {
            const auto copy_found = duplicate_ids.find(source_id);
            if (copy_found == duplicate_ids.end()) continue;
            auto* copy = state.scene.find_entity(copy_found->second);
            const auto* source = state.scene.find_entity(source_id);
            if (!copy || !source) continue;
            if (const auto parent_copy = duplicate_ids.find(source->parent_id); parent_copy != duplicate_ids.end()) {
                copy->parent_id = parent_copy->second;
            } else {
                auto world = editor_world_transform(state, *copy);
                world.position.x += offset;
                world.position.z += offset;
                set_editor_world_transform(state, copy->id, world);
            }
        }
        if (copies.empty()) return false;
        state.selected_entity_ids = copies;
        const auto primary = entity_index_from_id(state, copies.back());
        if (primary) state.selection = {SelectionKind::Entity, *primary, kNoSubSelection, copies.back(), kNoSubSelection, {}};
        state.hierarchy_anchor_id = copies.front();
        state.scene_view.frame_selection_pending = true;
        return true;
    });
}
bool command_delete_selected_entities(EditorState& state) {
    if (state.selection.kind != SelectionKind::Entity || state.selected_entity_ids.size() < 2) return false;
    const auto delete_ids = state.selected_entity_ids;
    return execute_editor_command(state, vespera::editor::EditorCommandKind::DeleteEntity, "Delete entities", [&]() {
        bool removed = false;
        for (const auto id : delete_ids) removed = state.scene.destroy_entity(id) || removed;
        state.selected_entity_ids.clear();
        state.hierarchy_anchor_id = vespera::kInvalidSceneObjectId;
        if (state.scene.entities.empty()) state.selection = {};
        else select_entity(state, std::min<std::size_t>(state.selection.index, state.scene.entities.size() - 1));
        return removed;
    });
}
bool command_duplicate_selection(EditorState& state) {
    if (state.selection.kind == SelectionKind::Sector) return command_duplicate_selected_sector(state);
    if (state.selection.kind == SelectionKind::Entity) {
        if (state.selected_entity_ids.size() > 1) return command_duplicate_selected_entities(state);
        return command_duplicate_selected_entity(state);
    }
    if (state.selection.kind == SelectionKind::Material) return command_duplicate_selected_material(state);
    if (state.selection.kind == SelectionKind::SpriteClip) return command_duplicate_selected_clip(state);
    return false;
}
bool command_delete_selection(EditorState& state) {
    if (state.selection.kind == SelectionKind::Sector) return command_delete_selected_sector(state);
    if (state.selection.kind == SelectionKind::Entity) {
        if (state.selected_entity_ids.size() > 1) return command_delete_selected_entities(state);
        return command_delete_selected_entity(state);
    }
    if (state.selection.kind == SelectionKind::Material) return command_delete_selected_material(state);
    if (state.selection.kind == SelectionKind::SpriteClip) return command_delete_selected_clip(state);
    return false;
}

} // namespace vespera::editor
