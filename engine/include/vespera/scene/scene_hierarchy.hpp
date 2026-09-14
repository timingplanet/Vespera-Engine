#pragma once

#include <vespera/scene/scene.hpp>

#include <string>
#include <vector>

namespace vespera {

// Vespera 0.8.5 hierarchy semantics: root transforms are world-space; child
// transforms are local to their parent. Composition intentionally matches the
// existing XYZ Euler transform convention without introducing renderer-owned
// matrix types into Scene data.
[[nodiscard]] TransformComponent compose_transforms(
    const TransformComponent& parent_world,
    const TransformComponent& local
);

[[nodiscard]] TransformComponent inverse_compose_transform(
    const TransformComponent& parent_world,
    const TransformComponent& child_world
);

[[nodiscard]] TransformComponent entity_world_transform(const Scene& scene, const Entity& entity);
[[nodiscard]] TransformComponent entity_world_transform(const Scene& scene, SceneObjectId entity_id);

[[nodiscard]] bool scene_entity_is_descendant_of(
    const Scene& scene,
    SceneObjectId candidate_descendant,
    SceneObjectId ancestor
);

[[nodiscard]] std::vector<SceneObjectId> scene_entity_children(
    const Scene& scene,
    SceneObjectId parent_id
);

// Reparent while preserving the entity's current world transform by default.
// Rejects missing IDs, self-parenting and cycles. Passing parent_id=0 unparents.
bool reparent_scene_entity(
    Scene& scene,
    SceneObjectId child_id,
    SceneObjectId parent_id,
    bool preserve_world = true,
    std::string* error = nullptr
);

} // namespace vespera
