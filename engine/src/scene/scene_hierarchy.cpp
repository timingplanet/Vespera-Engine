#include <vespera/scene/scene_hierarchy.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace vespera {
namespace {

constexpr float kScaleEpsilon = 1.0e-6f;

Vec3 mul_components(Vec3 a, Vec3 b) {
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}

Vec3 div_components(Vec3 a, Vec3 b) {
    const auto safe = [](float value) {
        if (std::abs(value) >= kScaleEpsilon) return value;
        return value < 0.0f ? -kScaleEpsilon : kScaleEpsilon;
    };
    return {a.x / safe(b.x), a.y / safe(b.y), a.z / safe(b.z)};
}

Vec3 add(Vec3 a, Vec3 b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
Vec3 sub(Vec3 a, Vec3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }

Vec3 rotate_xyz(Vec3 value, Vec3 rotation) {
    const float cx = std::cos(rotation.x), sx = std::sin(rotation.x);
    const float cy = std::cos(rotation.y), sy = std::sin(rotation.y);
    const float cz = std::cos(rotation.z), sz = std::sin(rotation.z);

    Vec3 v{value.x, value.y * cx - value.z * sx, value.y * sx + value.z * cx};
    v = {v.x * cy + v.z * sy, v.y, -v.x * sy + v.z * cy};
    v = {v.x * cz - v.y * sz, v.x * sz + v.y * cz, v.z};
    return v;
}

Vec3 inverse_rotate_xyz(Vec3 value, Vec3 rotation) {
    const float cx = std::cos(-rotation.x), sx = std::sin(-rotation.x);
    const float cy = std::cos(-rotation.y), sy = std::sin(-rotation.y);
    const float cz = std::cos(-rotation.z), sz = std::sin(-rotation.z);

    // Reverse the forward XYZ application order: Z^-1, Y^-1, X^-1.
    Vec3 v{value.x * cz - value.y * sz, value.x * sz + value.y * cz, value.z};
    v = {v.x * cy + v.z * sy, v.y, -v.x * sy + v.z * cy};
    v = {v.x, v.y * cx - v.z * sx, v.y * sx + v.z * cx};
    return v;
}

} // namespace

TransformComponent compose_transforms(const TransformComponent& parent_world, const TransformComponent& local) {
    TransformComponent world;
    world.scale = mul_components(parent_world.scale, local.scale);
    world.rotation = add(parent_world.rotation, local.rotation);
    const Vec3 scaled_local = mul_components(local.position, parent_world.scale);
    world.position = add(parent_world.position, rotate_xyz(scaled_local, parent_world.rotation));
    return world;
}

TransformComponent inverse_compose_transform(const TransformComponent& parent_world, const TransformComponent& child_world) {
    TransformComponent local;
    local.scale = div_components(child_world.scale, parent_world.scale);
    local.rotation = sub(child_world.rotation, parent_world.rotation);
    local.position = div_components(
        inverse_rotate_xyz(sub(child_world.position, parent_world.position), parent_world.rotation),
        parent_world.scale
    );
    return local;
}

TransformComponent entity_world_transform(const Scene& scene, const Entity& entity) {
    // Root entities already store world-space transforms. This is the dominant
    // runtime case and avoids allocating an ancestry vector + cycle set for every
    // visible root object each frame.
    if (entity.parent_id == kInvalidSceneObjectId) return entity.transform;

    // Build the ancestry chain first and compose root -> leaf. TransformComponent
    // stores decomposed TRS (not a full matrix), so grouping matters when rotated
    // non-uniform scales are involved. Defining hierarchy evaluation as parent
    // world composed with child local also makes inverse_compose_transform the
    // exact authoring inverse used by preserve-world reparenting at each level.
    std::vector<const Entity*> chain;
    chain.reserve(8);
    std::unordered_set<SceneObjectId> visited;
    const Entity* current = &entity;
    std::size_t depth = 0;
    while (current && depth++ <= scene.entities.size()) {
        if (!visited.insert(current->id).second) break;
        chain.push_back(current);
        if (current->parent_id == kInvalidSceneObjectId) break;
        current = scene.find_entity(current->parent_id);
    }
    if (chain.empty()) return entity.transform;

    TransformComponent result = chain.back()->transform;
    for (std::size_t i = chain.size(); i-- > 1;) {
        result = compose_transforms(result, chain[i - 1]->transform);
    }
    return result;
}

TransformComponent entity_world_transform(const Scene& scene, SceneObjectId entity_id) {
    const Entity* entity = scene.find_entity(entity_id);
    return entity ? entity_world_transform(scene, *entity) : TransformComponent{};
}

bool scene_entity_is_descendant_of(const Scene& scene, SceneObjectId candidate_descendant, SceneObjectId ancestor) {
    if (candidate_descendant == kInvalidSceneObjectId || ancestor == kInvalidSceneObjectId) return false;
    SceneObjectId current = candidate_descendant;
    std::unordered_set<SceneObjectId> visited;
    while (current != kInvalidSceneObjectId && visited.insert(current).second) {
        const Entity* entity = scene.find_entity(current);
        if (!entity) return false;
        current = entity->parent_id;
        if (current == ancestor) return true;
    }
    return false;
}

std::vector<SceneObjectId> scene_entity_children(const Scene& scene, SceneObjectId parent_id) {
    std::vector<SceneObjectId> result;
    for (const auto& entity : scene.entities) if (entity.parent_id == parent_id) result.push_back(entity.id);
    return result;
}

bool reparent_scene_entity(Scene& scene, SceneObjectId child_id, SceneObjectId parent_id, bool preserve_world, std::string* error) {
    auto fail = [&](std::string message) {
        if (error) *error = std::move(message);
        return false;
    };
    Entity* child = scene.find_entity(child_id);
    if (!child) return fail("child entity does not exist");
    if (child_id == parent_id) return fail("an entity cannot parent itself");
    if (parent_id != kInvalidSceneObjectId && !scene.find_entity(parent_id)) return fail("parent entity does not exist");
    if (parent_id != kInvalidSceneObjectId && scene_entity_is_descendant_of(scene, parent_id, child_id)) {
        return fail("reparenting would create a hierarchy cycle");
    }
    if (child->parent_id == parent_id) return true;

    const TransformComponent world_before = entity_world_transform(scene, *child);
    child->parent_id = parent_id;
    if (preserve_world) {
        if (parent_id == kInvalidSceneObjectId) child->transform = world_before;
        else child->transform = inverse_compose_transform(entity_world_transform(scene, parent_id), world_before);
    }
    return true;
}

} // namespace vespera
