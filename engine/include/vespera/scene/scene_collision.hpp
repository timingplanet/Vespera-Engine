#pragma once

#include <vespera/math/types.hpp>
#include <vespera/scene/scene.hpp>

#include <vector>

namespace vespera {

// Returns true when an X/Z circle overlaps any enabled, non-trigger Cylinder
// Collider in the scene. ignore_entity can be used by an entity to exclude its
// own collider from a query.
[[nodiscard]] bool scene_circle_overlaps_solid_collider(
    const Scene& scene,
    Vec3 position,
    float radius,
    SceneObjectId ignore_entity = kInvalidSceneObjectId
);



// Returns ids for enabled Cylinder Colliders overlapping the X/Z query circle.
// include_triggers controls whether trigger colliders participate. This is the
// allocation-friendly native basis for C#/Lua overlap queries.
[[nodiscard]] std::vector<SceneObjectId> scene_circle_overlapping_colliders(
    const Scene& scene,
    Vec3 position,
    float radius,
    bool include_triggers = false,
    SceneObjectId ignore_entity = kInvalidSceneObjectId
);

// Returns ids for every enabled trigger Cylinder Collider overlapping the X/Z
// query circle. This is deliberately allocation-friendly/simple for the first
// gameplay event layer; TriggerTracker turns these snapshots into enter/exit
// events without coupling the engine to a specific scripting language.
[[nodiscard]] std::vector<SceneObjectId> scene_circle_overlapping_triggers(
    const Scene& scene,
    Vec3 position,
    float radius,
    SceneObjectId ignore_entity = kInvalidSceneObjectId
);

// Tiny deterministic 2.5D collision helper used by the reference game. It
// prefers the full candidate, then tries X-only and Z-only motion to retain a
// basic sliding feel before falling back to the original position.
[[nodiscard]] Vec3 resolve_circle_motion_against_scene_colliders(
    const Scene& scene,
    Vec3 from,
    Vec3 candidate,
    float radius,
    SceneObjectId ignore_entity = kInvalidSceneObjectId
);

} // namespace vespera
