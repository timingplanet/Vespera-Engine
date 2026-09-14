#include <vespera/scene/scene_collision.hpp>
#include <vespera/scene/scene_hierarchy.hpp>

#include <algorithm>
#include <cmath>

namespace vespera {
namespace {

float world_collider_radius(const TransformComponent& transform, const CylinderColliderComponent& collider) {
    const float scale_x = std::abs(transform.scale.x);
    const float scale_z = std::abs(transform.scale.z);
    return collider.radius * std::max(scale_x, scale_z);
}

bool overlaps_collider(const Scene& scene, const Entity& entity, Vec3 position, float radius) {
    if (!entity.enabled || !entity.cylinder_collider) {
        return false;
    }

    const auto& collider = *entity.cylinder_collider;
    const auto transform = entity_world_transform(scene, entity);
    const float collider_radius = world_collider_radius(transform, collider);
    const float local_x = collider.center.x * transform.scale.x;
    const float local_z = collider.center.z * transform.scale.z;
    const float yaw_cos = std::cos(transform.rotation.y);
    const float yaw_sin = std::sin(transform.rotation.y);
    const float center_x = transform.position.x + local_x * yaw_cos + local_z * yaw_sin;
    const float center_z = transform.position.z - local_x * yaw_sin + local_z * yaw_cos;
    const float dx = position.x - center_x;
    const float dz = position.z - center_z;
    const float combined = std::max(radius, 0.0f) + std::max(collider_radius, 0.0f);
    return dx * dx + dz * dz < combined * combined;
}

} // namespace

bool scene_circle_overlaps_solid_collider(
    const Scene& scene,
    Vec3 position,
    float radius,
    SceneObjectId ignore_entity
) {
    for (const auto& entity : scene.entities) {
        if (ignore_entity != kInvalidSceneObjectId && entity.id == ignore_entity) continue;
        if (!entity.enabled || !entity.cylinder_collider || entity.cylinder_collider->is_trigger) continue;
        if (overlaps_collider(scene, entity, position, radius)) return true;
    }
    return false;
}


std::vector<SceneObjectId> scene_circle_overlapping_colliders(
    const Scene& scene,
    Vec3 position,
    float radius,
    bool include_triggers,
    SceneObjectId ignore_entity
) {
    std::vector<SceneObjectId> result;
    for (const auto& entity : scene.entities) {
        if (ignore_entity != kInvalidSceneObjectId && entity.id == ignore_entity) continue;
        if (!entity.enabled || !entity.cylinder_collider) continue;
        if (!include_triggers && entity.cylinder_collider->is_trigger) continue;
        if (overlaps_collider(scene, entity, position, radius)) result.push_back(entity.id);
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<SceneObjectId> scene_circle_overlapping_triggers(
    const Scene& scene,
    Vec3 position,
    float radius,
    SceneObjectId ignore_entity
) {
    std::vector<SceneObjectId> result;
    for (const auto& entity : scene.entities) {
        if (ignore_entity != kInvalidSceneObjectId && entity.id == ignore_entity) continue;
        if (!entity.enabled || !entity.cylinder_collider || !entity.cylinder_collider->is_trigger) continue;
        if (overlaps_collider(scene, entity, position, radius)) result.push_back(entity.id);
    }
    std::sort(result.begin(), result.end());
    return result;
}

Vec3 resolve_circle_motion_against_scene_colliders(
    const Scene& scene,
    Vec3 from,
    Vec3 candidate,
    float radius,
    SceneObjectId ignore_entity
) {
    if (!scene_circle_overlaps_solid_collider(scene, candidate, radius, ignore_entity)) {
        return candidate;
    }

    Vec3 x_only = from;
    x_only.x = candidate.x;
    x_only.y = candidate.y;
    if (!scene_circle_overlaps_solid_collider(scene, x_only, radius, ignore_entity)) {
        return x_only;
    }

    Vec3 z_only = from;
    z_only.z = candidate.z;
    z_only.y = candidate.y;
    if (!scene_circle_overlaps_solid_collider(scene, z_only, radius, ignore_entity)) {
        return z_only;
    }

    Vec3 stopped = from;
    stopped.y = candidate.y;
    return stopped;
}

} // namespace vespera
