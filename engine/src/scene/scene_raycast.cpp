#include <vespera/scene/scene_raycast.hpp>
#include <vespera/scene/scene_hierarchy.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace vespera {
namespace {

constexpr float kEpsilon = 1.0e-5f;

struct Ray2 {
    float ox = 0.0f;
    float oz = 0.0f;
    float dx = 0.0f;
    float dz = 1.0f;
};

float cross2(float ax, float az, float bx, float bz) {
    return ax * bz - az * bx;
}

bool ray_segment_intersection(
    const Ray2& ray,
    Vec2 a,
    Vec2 b,
    float& distance_out,
    Vec3& normal_out
) {
    const float sx = b.x - a.x;
    const float sz = b.z - a.z;
    const float denominator = cross2(ray.dx, ray.dz, sx, sz);
    if (std::abs(denominator) <= kEpsilon) return false;

    const float qx = a.x - ray.ox;
    const float qz = a.z - ray.oz;
    const float t = cross2(qx, qz, sx, sz) / denominator;
    const float u = cross2(qx, qz, ray.dx, ray.dz) / denominator;
    if (t < 0.0f || u < -kEpsilon || u > 1.0f + kEpsilon) return false;

    const float length = std::sqrt(sx * sx + sz * sz);
    if (length <= kEpsilon) return false;
    // Sector vertices are CCW, so this is the outward wall normal.
    normal_out = {sz / length, 0.0f, -sx / length};
    if (normal_out.x * ray.dx + normal_out.z * ray.dz > 0.0f) {
        normal_out.x = -normal_out.x;
        normal_out.z = -normal_out.z;
    }
    distance_out = t;
    return true;
}

bool portal_open_at_height(const SectorWorld& world, std::size_t sector_index, std::size_t side_index, float y) {
    const auto& sectors = world.sectors();
    if (sector_index >= sectors.size()) return false;
    const auto& sector = sectors[sector_index];
    if (side_index >= sector.sides.size()) return false;
    const int adjacent_index = sector.sides[side_index].adjacent_sector;
    if (adjacent_index < 0 || static_cast<std::size_t>(adjacent_index) >= sectors.size()) return false;
    const auto& adjacent = sectors[static_cast<std::size_t>(adjacent_index)];
    const float opening_floor = std::max(sector.floor_height, adjacent.floor_height);
    const float opening_ceiling = std::min(sector.ceiling_height, adjacent.ceiling_height);
    return y > opening_floor + kEpsilon && y < opening_ceiling - kEpsilon;
}

bool ray_circle_intersection(
    const Ray2& ray,
    float cx,
    float cz,
    float radius,
    float& distance_out,
    Vec3& normal_out
) {
    const float mx = ray.ox - cx;
    const float mz = ray.oz - cz;
    const float b = mx * ray.dx + mz * ray.dz;
    const float c = mx * mx + mz * mz - radius * radius;
    if (c > 0.0f && b > 0.0f) return false;
    const float discriminant = b * b - c;
    if (discriminant < 0.0f) return false;

    float t = -b - std::sqrt(discriminant);
    if (t < 0.0f) t = 0.0f; // Ray begins inside the collider.
    const float hx = ray.ox + ray.dx * t;
    const float hz = ray.oz + ray.dz * t;
    float nx = hx - cx;
    float nz = hz - cz;
    const float nlen = std::sqrt(nx * nx + nz * nz);
    if (nlen > kEpsilon) {
        nx /= nlen;
        nz /= nlen;
    } else {
        nx = -ray.dx;
        nz = -ray.dz;
    }
    distance_out = t;
    normal_out = {nx, 0.0f, nz};
    return true;
}

void collider_world_geometry(
    const Scene& scene,
    const Entity& entity,
    const CylinderColliderComponent& collider,
    float& center_x,
    float& center_y,
    float& center_z,
    float& radius,
    float& height
) {
    const auto transform = entity_world_transform(scene, entity);
    const float local_x = collider.center.x * transform.scale.x;
    const float local_z = collider.center.z * transform.scale.z;
    const float yaw_cos = std::cos(transform.rotation.y);
    const float yaw_sin = std::sin(transform.rotation.y);
    center_x = transform.position.x + local_x * yaw_cos + local_z * yaw_sin;
    center_y = transform.position.y + collider.center.y * transform.scale.y;
    center_z = transform.position.z - local_x * yaw_sin + local_z * yaw_cos;
    radius = collider.radius * std::max(std::abs(transform.scale.x), std::abs(transform.scale.z));
    height = collider.height * std::abs(transform.scale.y);
}

} // namespace

std::optional<SceneRaycastHit> raycast_scene_2d(
    const Scene& scene,
    Vec3 origin,
    Vec3 direction,
    const SceneRaycastOptions& options
) {
    const float length = std::sqrt(direction.x * direction.x + direction.z * direction.z);
    if (length <= kEpsilon || options.max_distance < 0.0f) return std::nullopt;

    const Ray2 ray{origin.x, origin.z, direction.x / length, direction.z / length};
    const float max_distance = std::max(options.max_distance, 0.0f);
    float nearest = std::numeric_limits<float>::infinity();
    std::optional<SceneRaycastHit> hit;

    const auto& sectors = scene.world.sectors();
    if (options.include_sector_walls) {
        for (std::size_t sector_index = 0; sector_index < sectors.size(); ++sector_index) {
            const auto& sector = sectors[sector_index];
            if (origin.y < sector.floor_height - kEpsilon || origin.y > sector.ceiling_height + kEpsilon) {
                continue;
            }
            for (std::size_t side_index = 0; side_index < sector.vertices.size(); ++side_index) {
                if (portal_open_at_height(scene.world, sector_index, side_index, origin.y)) continue;
                const Vec2 a = sector.vertices[side_index];
                const Vec2 b = sector.vertices[(side_index + 1u) % sector.vertices.size()];
                float distance = 0.0f;
                Vec3 normal{};
                if (!ray_segment_intersection(ray, a, b, distance, normal)) continue;
                if (distance > max_distance + kEpsilon || distance >= nearest) continue;
                nearest = distance;
                hit = SceneRaycastHit{
                    SceneRaycastHitType::SectorWall,
                    distance,
                    {origin.x + ray.dx * distance, origin.y, origin.z + ray.dz * distance},
                    normal,
                    kInvalidSceneObjectId,
                    sector_index,
                    side_index,
                };
            }
        }
    }

    if (!options.include_entity_colliders) return hit;
    for (const auto& entity : scene.entities) {
        if (!entity.enabled || !entity.cylinder_collider) continue;
        if (entity.id == options.ignore_entity) continue;
        if (!options.required_entity_tag.empty() && entity.tag != options.required_entity_tag) continue;
        if (!options.required_entity_layer.empty() && entity.layer != options.required_entity_layer) continue;
        const auto& collider = *entity.cylinder_collider;
        if (collider.is_trigger && !options.include_trigger_colliders) continue;

        float center_x = 0.0f;
        float center_y = 0.0f;
        float center_z = 0.0f;
        float radius = 0.0f;
        float height = 0.0f;
        collider_world_geometry(scene, entity, collider, center_x, center_y, center_z, radius, height);
        const float half_height = height * 0.5f;
        if (origin.y < center_y - half_height - kEpsilon || origin.y > center_y + half_height + kEpsilon) {
            continue;
        }

        float distance = 0.0f;
        Vec3 normal{};
        if (!ray_circle_intersection(ray, center_x, center_z, std::max(radius, 0.0f), distance, normal)) continue;
        if (distance > max_distance + kEpsilon || distance >= nearest) continue;
        nearest = distance;
        hit = SceneRaycastHit{
            SceneRaycastHitType::EntityCollider,
            distance,
            {origin.x + ray.dx * distance, origin.y, origin.z + ray.dz * distance},
            normal,
            entity.id,
        };
    }

    return hit;
}

} // namespace vespera
