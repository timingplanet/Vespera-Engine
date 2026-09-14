#pragma once

#include <vespera/math/types.hpp>
#include <vespera/scene/scene.hpp>

#include <cstddef>
#include <optional>
#include <string_view>

namespace vespera {

enum class SceneRaycastHitType {
    SectorWall,
    EntityCollider,
};

struct SceneRaycastHit {
    SceneRaycastHitType type = SceneRaycastHitType::SectorWall;
    float distance = 0.0f;
    Vec3 position{};
    Vec3 normal{};

    // Entity collider hits populate entity_id. Sector wall hits populate the
    // sector/side pair. Unused fields keep their invalid/default values.
    SceneObjectId entity_id = kInvalidSceneObjectId;
    std::size_t sector_index = static_cast<std::size_t>(-1);
    std::size_t side_index = static_cast<std::size_t>(-1);
};

struct SceneRaycastOptions {
    float max_distance = 100.0f;
    bool include_sector_walls = true;
    bool include_entity_colliders = true;
    bool include_trigger_colliders = false;
    SceneObjectId ignore_entity = kInvalidSceneObjectId;

    // Empty filters match every entity. They apply only to entity-collider hits;
    // sector-wall participation is controlled independently above.
    std::string_view required_entity_tag{};
    std::string_view required_entity_layer{};
};

// Casts a constant-height ray on Vespera's X/Z gameplay plane. It tests
// solid sector wall segments plus enabled Cylinder Colliders and returns the
// nearest hit. Portal edges are transparent when the ray height is inside the
// portal's vertical opening. This is intentionally a 2.5D gameplay query; a
// later mesh/physics raycast can coexist without changing this API's meaning.
[[nodiscard]] std::optional<SceneRaycastHit> raycast_scene_2d(
    const Scene& scene,
    Vec3 origin,
    Vec3 direction,
    const SceneRaycastOptions& options = {}
);

} // namespace vespera
