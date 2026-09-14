#pragma once

#include <vespera/scene/scene.hpp>

#include <cstddef>

namespace vespera {

struct SceneStats {
    std::size_t sectors = 0;
    std::size_t entities = 0;
    std::size_t enabled_entities = 0;
    std::size_t sprite_renderers = 0;
    std::size_t mesh_renderers = 0;
    std::size_t colliders = 0;
    std::size_t triggers = 0;
    std::size_t point_lights = 0;
    std::size_t managed_scripts = 0;
    std::size_t sprite_clips = 0;
};

[[nodiscard]] inline SceneStats collect_scene_stats(const Scene& scene) {
    SceneStats result;
    result.sectors = scene.world.sectors().size();
    result.entities = scene.entities.size();
    result.sprite_clips = scene.sprite_clips.size();
    for (const auto& entity : scene.entities) {
        if (entity.enabled) ++result.enabled_entities;
        if (entity.sprite_renderer) ++result.sprite_renderers;
        if (entity.mesh_renderer) ++result.mesh_renderers;
        if (entity.cylinder_collider) {
            ++result.colliders;
            if (entity.cylinder_collider->is_trigger) ++result.triggers;
        }
        if (entity.point_light) ++result.point_lights;
        result.managed_scripts += entity.managed_scripts.size();
    }
    return result;
}

} // namespace vespera
