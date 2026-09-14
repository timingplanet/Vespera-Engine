#pragma once

#include <vespera/assets/asset_reference.hpp>
#include <vespera/math/types.hpp>
#include <vespera/render/material.hpp>
#include <vespera/world/sector_world.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vespera {

using SceneObjectId = std::uint64_t;
constexpr SceneObjectId kInvalidSceneObjectId = 0;

struct Camera {
    Vec3 position{0.0f, 1.65f, -2.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    float vertical_fov_degrees = 75.0f;
    float near_plane = 0.05f;
    float far_plane = 500.0f;
};

// Every entity owns a Transform. Rotation is stored as XYZ Euler radians for
// now; sprite-facing uses rotation.y. Keeping transform data independent from
// render/gameplay components is the basis of the shared C#/Lua/editor API.
struct TransformComponent {
    Vec3 position{};
    Vec3 rotation{};
    Vec3 scale{1.0f, 1.0f, 1.0f};
};

// Direction-major sprite animation resource. A clip with 8 directions and
// 3 frames stores 24 texture references: all 3 frames for direction 0, then
// all 3 frames for direction 1, and so on. direction 0 is the actor's front;
// subsequent directions rotate clockwise when viewed from above.
struct SpriteAnimationClip {
    std::string name;
    std::uint32_t direction_count = 1;
    std::uint32_t frame_count = 1;
    float frames_per_second = 0.0f;
    bool loop = true;
    std::vector<TextureId> textures;

    [[nodiscard]] bool valid() const {
        return !name.empty()
            && (direction_count == 1u || direction_count == 4u || direction_count == 8u)
            && frame_count > 0u
            && textures.size() == static_cast<std::size_t>(direction_count) * frame_count;
    }

    [[nodiscard]] TextureId texture(std::uint32_t direction, std::uint32_t frame) const {
        if (!valid()) return kInvalidTexture;
        direction %= direction_count;
        frame = (std::min)(frame, frame_count - 1u);
        return textures[static_cast<std::size_t>(direction) * frame_count + frame];
    }
};

// Built-in render component for Vespera's sprite-first path. Position and
// facing live on Entity::transform rather than inside sprite data.
struct SpriteRendererComponent {
    Vec2 size{1.0f, 1.0f};
    TextureId texture = kInvalidTexture;
    std::string animation_clip;
    float animation_speed = 1.0f;
    float animation_time_offset = 0.0f;
    bool animation_paused = false;
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
};

// Lightweight built-in primitive geometry. 0.8.2 intentionally starts with
// engine-owned primitives rather than a general imported mesh asset so common
// blockout objects can be authored immediately while the later mesh pipeline
// remains free to evolve. Transform supplies position/rotation/scale.
enum class PrimitiveMeshType : std::uint8_t {
    Cube,
    Plane,
    Cylinder,
    Sphere,
};

[[nodiscard]] inline constexpr std::string_view primitive_mesh_name(PrimitiveMeshType type) {
    switch (type) {
        case PrimitiveMeshType::Cube: return "cube";
        case PrimitiveMeshType::Plane: return "plane";
        case PrimitiveMeshType::Cylinder: return "cylinder";
        case PrimitiveMeshType::Sphere: return "sphere";
    }
    return "cube";
}

[[nodiscard]] inline constexpr std::optional<PrimitiveMeshType> primitive_mesh_from_name(std::string_view name) {
    if (name == "cube") return PrimitiveMeshType::Cube;
    if (name == "plane") return PrimitiveMeshType::Plane;
    if (name == "cylinder") return PrimitiveMeshType::Cylinder;
    if (name == "sphere") return PrimitiveMeshType::Sphere;
    return std::nullopt;
}

struct MeshRendererComponent {
    PrimitiveMeshType primitive = PrimitiveMeshType::Cube;
    // Legacy/direct texture + tint remain as per-instance fallback data so old
    // Scene v13/v14 and Prefab v5 content stays load-compatible. 0.8.8 adds a
    // first-class stable Material asset reference. Resolved fields below are
    // transient render caches and are never serialized.
    TextureId texture = kInvalidTexture;
    std::array<float, 4> color{0.72f, 0.74f, 0.78f, 1.0f};
    AssetReference material;
    MaterialProperties resolved_material{};
    TextureId resolved_material_texture = kInvalidTexture;
    bool material_resolved = false;
};

// First gameplay-oriented built-in component. Vespera's world is primarily
// 2.5D, so a vertical cylinder is a useful rotation-independent collision
// primitive for characters, pickups, props, and triggers. The center offset is
// local to the Entity Transform. Runtime collision currently uses the X/Z disc;
// height is serialized/validated now so vertical filtering can expand later.
struct CylinderColliderComponent {
    float radius = 0.5f;
    float height = 1.8f;
    Vec3 center{};
    bool is_trigger = false;
};

// Lightweight unshadowed point light used by the real-time lighting pass.
// The D3D12 backend currently uploads up to 32 active point lights per view.
// Authored scene data stays renderer-independent so a later clustered/tiled
// implementation can raise that budget without changing the component shape.
struct PointLightComponent {
    std::array<float, 4> color{1.0f, 0.82f, 0.62f, 1.0f};
    float intensity = 1.0f;
    float radius = 5.0f;
};

// Serialized override for one explicitly exposed managed field. The native
// scene owns only semantic text data; live CLR FieldInfo/object state remains in
// the managed host. Keeping the declared type alongside the value lets the
// editor preserve unresolved scripts and fields across compile failures.
struct ManagedScriptFieldValue {
    std::string field_name;
    std::string type_name;
    std::string serialized_value;
};

// One user-script attachment. Managed scripts deliberately live in a list on
// Entity rather than BuiltinComponentType so a single Entity can host multiple
// game scripts without turning each project type into a native component kind.
struct ManagedScriptComponent {
    std::string class_name;
    bool enabled = true;
    std::vector<ManagedScriptFieldValue> fields;
};

enum class BuiltinComponentType : std::uint8_t {
    Transform,
    SpriteRenderer,
    MeshRenderer,
    CylinderCollider,
    PointLight,
};

// Lightweight built-in property metadata. It intentionally describes the
// public semantic shape rather than exposing C++ member offsets. That keeps the
// metadata safe to reuse later for C#, Lua, MCP, documentation, and editor
// generation without freezing native struct layout as ABI.
enum class BuiltinPropertyType : std::uint8_t {
    Bool,
    Float,
    String,
    Vec2,
    Vec3,
    Color4,
    Texture,
};

struct BuiltinPropertyInfo {
    std::string_view key;
    std::string_view display_name;
    BuiltinPropertyType type{};
};

inline constexpr std::array<BuiltinPropertyInfo, 3> kTransformProperties{{
    {"position", "Position", BuiltinPropertyType::Vec3},
    {"rotation", "Rotation", BuiltinPropertyType::Vec3},
    {"scale", "Scale", BuiltinPropertyType::Vec3},
}};

inline constexpr std::array<BuiltinPropertyInfo, 7> kSpriteRendererProperties{{
    {"size", "Size", BuiltinPropertyType::Vec2},
    {"texture", "Fallback Texture", BuiltinPropertyType::Texture},
    {"animation_clip", "Animation Clip", BuiltinPropertyType::String},
    {"animation_speed", "Animation Speed", BuiltinPropertyType::Float},
    {"animation_time_offset", "Time Offset", BuiltinPropertyType::Float},
    {"animation_paused", "Animation Paused", BuiltinPropertyType::Bool},
    {"color", "Tint", BuiltinPropertyType::Color4},
}};

inline constexpr std::array<BuiltinPropertyInfo, 5> kMeshRendererProperties{{
    {"primitive", "Primitive", BuiltinPropertyType::String},
    {"texture", "Texture", BuiltinPropertyType::Texture},
    {"color", "Tint", BuiltinPropertyType::Color4},
    {"material_asset_id", "Material Asset ID", BuiltinPropertyType::String},
    {"material_path", "Material Path", BuiltinPropertyType::String},
}};

inline constexpr std::array<BuiltinPropertyInfo, 4> kCylinderColliderProperties{{
    {"radius", "Radius", BuiltinPropertyType::Float},
    {"height", "Height", BuiltinPropertyType::Float},
    {"center", "Center", BuiltinPropertyType::Vec3},
    {"is_trigger", "Is Trigger", BuiltinPropertyType::Bool},
}};

inline constexpr std::array<BuiltinPropertyInfo, 3> kPointLightProperties{{
    {"color", "Color", BuiltinPropertyType::Color4},
    {"intensity", "Intensity", BuiltinPropertyType::Float},
    {"radius", "Radius", BuiltinPropertyType::Float},
}};

[[nodiscard]] inline constexpr std::span<const BuiltinPropertyInfo> builtin_component_properties(BuiltinComponentType type) {
    switch (type) {
        case BuiltinComponentType::Transform: return kTransformProperties;
        case BuiltinComponentType::SpriteRenderer: return kSpriteRendererProperties;
        case BuiltinComponentType::MeshRenderer: return kMeshRendererProperties;
        case BuiltinComponentType::CylinderCollider: return kCylinderColliderProperties;
        case BuiltinComponentType::PointLight: return kPointLightProperties;
    }
    return {};
}

// Stable keys are deliberately language/tool friendly. They are intended to be
// the common identity used later by C#, Lua, docs, MCP, and visual scripting.
struct BuiltinComponentInfo {
    BuiltinComponentType type{};
    std::string_view key;
    std::string_view display_name;
    bool removable = true;
};

inline constexpr std::array<BuiltinComponentInfo, 5> kBuiltinComponentTypes{{
    {BuiltinComponentType::Transform, "sectorline.transform", "Transform", false},
    {BuiltinComponentType::SpriteRenderer, "sectorline.sprite_renderer", "Sprite Renderer", true},
    {BuiltinComponentType::MeshRenderer, "sectorline.mesh_renderer", "Mesh Renderer", true},
    {BuiltinComponentType::CylinderCollider, "sectorline.cylinder_collider", "Cylinder Collider", true},
    {BuiltinComponentType::PointLight, "sectorline.point_light", "Point Light", true},
}};

[[nodiscard]] inline constexpr const BuiltinComponentInfo* builtin_component_info(BuiltinComponentType type) {
    for (const auto& info : kBuiltinComponentTypes) {
        if (info.type == type) return &info;
    }
    return nullptr;
}

[[nodiscard]] inline constexpr const BuiltinComponentInfo* builtin_component_info(std::string_view key) {
    for (const auto& info : kBuiltinComponentTypes) {
        if (info.key == key) return &info;
    }
    return nullptr;
}

struct Entity {
    SceneObjectId id = kInvalidSceneObjectId;
    std::string name;
    bool enabled = true;

    // Lightweight gameplay identity lives on the entity itself rather than in
    // a render/physics component. Tags are intended for semantic queries
    // ("enemy", "checkpoint", "player"), while layers group entities for
    // broad systems such as collision, audio, and editor filtering.
    std::string tag = "Untagged";
    std::string layer = "Default";

    // Optional stable project-asset reference for an entity instantiated from
    // an external .slprefab. The stable ID is authoritative when present while
    // the readable path remains useful in source control and older scenes.
    AssetReference prefab_source;

    // Scene hierarchy identity. Transform is local when parent_id is set and
    // remains world-space for root entities, preserving all older scenes.
    // World-space consumers must resolve through scene_hierarchy.hpp.
    SceneObjectId parent_id = kInvalidSceneObjectId;

    TransformComponent transform;
    std::optional<SpriteRendererComponent> sprite_renderer;
    std::optional<MeshRendererComponent> mesh_renderer;
    std::optional<CylinderColliderComponent> cylinder_collider;
    std::optional<PointLightComponent> point_light;
    std::vector<ManagedScriptComponent> managed_scripts;

    [[nodiscard]] bool has_component(BuiltinComponentType type) const {
        switch (type) {
            case BuiltinComponentType::Transform: return true;
            case BuiltinComponentType::SpriteRenderer: return sprite_renderer.has_value();
            case BuiltinComponentType::MeshRenderer: return mesh_renderer.has_value();
            case BuiltinComponentType::CylinderCollider: return cylinder_collider.has_value();
            case BuiltinComponentType::PointLight: return point_light.has_value();
        }
        return false;
    }

    bool add_component(BuiltinComponentType type) {
        switch (type) {
            case BuiltinComponentType::Transform:
                return false;
            case BuiltinComponentType::SpriteRenderer:
                if (sprite_renderer) return false;
                sprite_renderer.emplace();
                return true;
            case BuiltinComponentType::MeshRenderer:
                if (mesh_renderer) return false;
                mesh_renderer.emplace();
                return true;
            case BuiltinComponentType::CylinderCollider:
                if (cylinder_collider) return false;
                cylinder_collider.emplace();
                return true;
            case BuiltinComponentType::PointLight:
                if (point_light) return false;
                point_light.emplace();
                return true;
        }
        return false;
    }

    bool remove_component(BuiltinComponentType type) {
        switch (type) {
            case BuiltinComponentType::Transform:
                return false;
            case BuiltinComponentType::SpriteRenderer:
                if (!sprite_renderer) return false;
                sprite_renderer.reset();
                return true;
            case BuiltinComponentType::MeshRenderer:
                if (!mesh_renderer) return false;
                mesh_renderer.reset();
                return true;
            case BuiltinComponentType::CylinderCollider:
                if (!cylinder_collider) return false;
                cylinder_collider.reset();
                return true;
            case BuiltinComponentType::PointLight:
                if (!point_light) return false;
                point_light.reset();
                return true;
        }
        return false;
    }

    [[nodiscard]] bool has_component(std::string_view key) const {
        const auto* info = builtin_component_info(key);
        return info ? has_component(info->type) : false;
    }

    bool add_component(std::string_view key) {
        const auto* info = builtin_component_info(key);
        return info ? add_component(info->type) : false;
    }

    bool remove_component(std::string_view key) {
        const auto* info = builtin_component_info(key);
        return info ? remove_component(info->type) : false;
    }

    [[nodiscard]] std::vector<std::string_view> component_keys() const {
        std::vector<std::string_view> keys;
        keys.reserve(kBuiltinComponentTypes.size());
        for (const auto& info : kBuiltinComponentTypes) {
            if (has_component(info.type)) keys.push_back(info.key);
        }
        return keys;
    }

    SpriteRendererComponent& add_sprite_renderer() {
        if (!sprite_renderer) sprite_renderer.emplace();
        return *sprite_renderer;
    }

    bool remove_sprite_renderer() {
        return remove_component(BuiltinComponentType::SpriteRenderer);
    }

    MeshRendererComponent& add_mesh_renderer() {
        if (!mesh_renderer) mesh_renderer.emplace();
        return *mesh_renderer;
    }

    bool remove_mesh_renderer() {
        return remove_component(BuiltinComponentType::MeshRenderer);
    }

    CylinderColliderComponent& add_cylinder_collider() {
        if (!cylinder_collider) cylinder_collider.emplace();
        return *cylinder_collider;
    }

    bool remove_cylinder_collider() {
        return remove_component(BuiltinComponentType::CylinderCollider);
    }

    PointLightComponent& add_point_light() {
        if (!point_light) point_light.emplace();
        return *point_light;
    }

    bool remove_point_light() {
        return remove_component(BuiltinComponentType::PointLight);
    }

    ManagedScriptComponent& add_managed_script(std::string class_name = "Game.MyComponent") {
        ManagedScriptComponent script;
        script.class_name = std::move(class_name);
        managed_scripts.push_back(std::move(script));
        return managed_scripts.back();
    }

    bool remove_managed_script(std::size_t index) {
        if (index >= managed_scripts.size()) return false;
        managed_scripts.erase(managed_scripts.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }
};

class Scene {
public:
    SectorWorld world;
    Camera camera;
    std::vector<SpriteAnimationClip> sprite_clips;
    std::vector<Entity> entities;

    SceneObjectId allocate_object_id() {
        if (next_object_id_ == kInvalidSceneObjectId) ++next_object_id_;
        return next_object_id_++;
    }

    Entity& create_entity(std::string name = "Entity") {
        Entity entity;
        entity.id = allocate_object_id();
        entity.name = std::move(name);
        entities.push_back(std::move(entity));
        entity_lookup_cache_[entities.back().id] = entities.size() - 1u;
        return entities.back();
    }

    bool destroy_entity(SceneObjectId id) {
        const auto found = std::find_if(entities.begin(), entities.end(), [id](const Entity& entity) {
            return entity.id == id;
        });
        if (found == entities.end()) return false;
        std::vector<SceneObjectId> children;
        for (const auto& entity : entities) if (entity.parent_id == id) children.push_back(entity.id);
        for (const auto child_id : children) destroy_entity(child_id);
        const auto refreshed = std::find_if(entities.begin(), entities.end(), [id](const Entity& entity) {
            return entity.id == id;
        });
        if (refreshed != entities.end()) entities.erase(refreshed);
        // Erase shifts every following vector index. Clearing is cheap compared
        // with carrying a subtly stale ID->index table across destructive edits;
        // ordinary lookups repopulate it lazily.
        entity_lookup_cache_.clear();
        return true;
    }

    Entity* clone_entity(SceneObjectId source_id, std::string name = {}) {
        const Entity* source = find_entity(source_id);
        if (!source) return nullptr;
        Entity copy = *source;
        copy.id = allocate_object_id();
        if (!name.empty()) copy.name = std::move(name);
        entities.push_back(std::move(copy));
        entity_lookup_cache_[entities.back().id] = entities.size() - 1u;
        return &entities.back();
    }

    void refresh_object_id_allocator() {
        SceneObjectId next = 1;
        for (const auto& entity : entities) {
            if (entity.id != kInvalidSceneObjectId) next = (std::max)(next, entity.id + 1);
        }
        next_object_id_ = next;
        // This method is commonly called after bulk scene loading/direct vector
        // edits. Discard cached indices so the next lookup rebuilds what it uses.
        entity_lookup_cache_.clear();
    }

    Entity* find_entity(SceneObjectId id) {
        return const_cast<Entity*>(std::as_const(*this).find_entity(id));
    }

    const Entity* find_entity(SceneObjectId id) const {
        if (id == kInvalidSceneObjectId) return nullptr;

        if (const auto cached = entity_lookup_cache_.find(id); cached != entity_lookup_cache_.end()) {
            const std::size_t index = cached->second;
            if (index < entities.size() && entities[index].id == id) return &entities[index];
            // entities is intentionally public for editor/serializer workflows.
            // Verify cached indices on every hit so an external reorder can never
            // return the wrong entity; stale entries simply self-heal below.
            entity_lookup_cache_.erase(cached);
        }

        for (std::size_t index = 0; index < entities.size(); ++index) {
            if (entities[index].id != id) continue;
            entity_lookup_cache_[id] = index;
            return &entities[index];
        }
        return nullptr;
    }

    Entity* find_entity_by_name(std::string_view name) {
        for (auto& entity : entities) if (entity.name == name) return &entity;
        return nullptr;
    }

    const Entity* find_entity_by_name(std::string_view name) const {
        for (const auto& entity : entities) if (entity.name == name) return &entity;
        return nullptr;
    }

    Entity* find_entity_with_tag(std::string_view tag) {
        for (auto& entity : entities) if (entity.tag == tag) return &entity;
        return nullptr;
    }

    const Entity* find_entity_with_tag(std::string_view tag) const {
        for (const auto& entity : entities) if (entity.tag == tag) return &entity;
        return nullptr;
    }

    [[nodiscard]] std::vector<Entity*> find_entities_with_tag(std::string_view tag) {
        std::vector<Entity*> result;
        for (auto& entity : entities) if (entity.tag == tag) result.push_back(&entity);
        return result;
    }

    [[nodiscard]] std::vector<const Entity*> find_entities_with_tag(std::string_view tag) const {
        std::vector<const Entity*> result;
        for (const auto& entity : entities) if (entity.tag == tag) result.push_back(&entity);
        return result;
    }

    [[nodiscard]] std::vector<Entity*> find_entities_on_layer(std::string_view layer) {
        std::vector<Entity*> result;
        for (auto& entity : entities) if (entity.layer == layer) result.push_back(&entity);
        return result;
    }

    [[nodiscard]] std::vector<const Entity*> find_entities_on_layer(std::string_view layer) const {
        std::vector<const Entity*> result;
        for (const auto& entity : entities) if (entity.layer == layer) result.push_back(&entity);
        return result;
    }

    [[nodiscard]] std::vector<Entity*> find_entities_with_component(BuiltinComponentType type) {
        std::vector<Entity*> result;
        for (auto& entity : entities) if (entity.has_component(type)) result.push_back(&entity);
        return result;
    }

    [[nodiscard]] std::vector<const Entity*> find_entities_with_component(BuiltinComponentType type) const {
        std::vector<const Entity*> result;
        for (const auto& entity : entities) if (entity.has_component(type)) result.push_back(&entity);
        return result;
    }

    [[nodiscard]] std::vector<Entity*> find_entities_with_component(std::string_view key) {
        std::vector<Entity*> result;
        if (!builtin_component_info(key)) return result;
        for (auto& entity : entities) if (entity.has_component(key)) result.push_back(&entity);
        return result;
    }

    [[nodiscard]] std::vector<const Entity*> find_entities_with_component(std::string_view key) const {
        std::vector<const Entity*> result;
        if (!builtin_component_info(key)) return result;
        for (const auto& entity : entities) if (entity.has_component(key)) result.push_back(&entity);
        return result;
    }

    const SpriteAnimationClip* find_sprite_clip(std::string_view name) const {
        for (const auto& clip : sprite_clips) if (clip.name == name) return &clip;
        return nullptr;
    }

private:
    SceneObjectId next_object_id_ = 1;
    // Hot managed/runtime paths address entities by stable SceneObjectId. Keeping
    // vector indices here removes repeated O(N) scans while preserving the public
    // vector representation. Entries are always identity-checked before use.
    mutable std::unordered_map<SceneObjectId, std::size_t> entity_lookup_cache_;
};

} // namespace vespera
