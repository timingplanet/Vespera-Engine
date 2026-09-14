#include <vespera/scene/component_access.hpp>

#include <cmath>

namespace vespera {
namespace {

template <typename T>
const T* value_as(const BuiltinPropertyValue& value) {
    return std::get_if<T>(&value);
}

bool valid_scale(const Vec3& scale) {
    constexpr float kMin = 1.0e-6f;
    return std::abs(scale.x) > kMin && std::abs(scale.y) > kMin && std::abs(scale.z) > kMin;
}

} // namespace

std::optional<BuiltinPropertyValue> get_builtin_component_property(
    const Entity& entity,
    BuiltinComponentType component,
    std::string_view property_key
) {
    switch (component) {
        case BuiltinComponentType::Transform:
            if (property_key == "position") return entity.transform.position;
            if (property_key == "rotation") return entity.transform.rotation;
            if (property_key == "scale") return entity.transform.scale;
            return std::nullopt;

        case BuiltinComponentType::SpriteRenderer: {
            if (!entity.sprite_renderer) return std::nullopt;
            const auto& sprite = *entity.sprite_renderer;
            if (property_key == "size") return sprite.size;
            if (property_key == "texture") return sprite.texture;
            if (property_key == "animation_clip") return sprite.animation_clip;
            if (property_key == "animation_speed") return sprite.animation_speed;
            if (property_key == "animation_time_offset") return sprite.animation_time_offset;
            if (property_key == "animation_paused") return sprite.animation_paused;
            if (property_key == "color") return sprite.color;
            return std::nullopt;
        }

        case BuiltinComponentType::MeshRenderer: {
            if (!entity.mesh_renderer) return std::nullopt;
            const auto& mesh = *entity.mesh_renderer;
            if (property_key == "primitive") return std::string(primitive_mesh_name(mesh.primitive));
            if (property_key == "texture") return mesh.texture;
            if (property_key == "color") return mesh.color;
            if (property_key == "material_asset_id") return mesh.material.asset_id;
            if (property_key == "material_path") return mesh.material.path.generic_string();
            return std::nullopt;
        }

        case BuiltinComponentType::CylinderCollider: {
            if (!entity.cylinder_collider) return std::nullopt;
            const auto& collider = *entity.cylinder_collider;
            if (property_key == "radius") return collider.radius;
            if (property_key == "height") return collider.height;
            if (property_key == "center") return collider.center;
            if (property_key == "is_trigger") return collider.is_trigger;
            return std::nullopt;
        }

        case BuiltinComponentType::PointLight: {
            if (!entity.point_light) return std::nullopt;
            const auto& light = *entity.point_light;
            if (property_key == "color") return light.color;
            if (property_key == "intensity") return light.intensity;
            if (property_key == "radius") return light.radius;
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<BuiltinPropertyValue> get_builtin_component_property(
    const Entity& entity,
    std::string_view component_key,
    std::string_view property_key
) {
    const auto* info = builtin_component_info(component_key);
    if (!info) return std::nullopt;
    return get_builtin_component_property(entity, info->type, property_key);
}

bool set_builtin_component_property(
    Entity& entity,
    BuiltinComponentType component,
    std::string_view property_key,
    const BuiltinPropertyValue& value
) {
    switch (component) {
        case BuiltinComponentType::Transform:
            if (property_key == "position") {
                if (const auto* v = value_as<Vec3>(value)) { entity.transform.position = *v; return true; }
                return false;
            }
            if (property_key == "rotation") {
                if (const auto* v = value_as<Vec3>(value)) { entity.transform.rotation = *v; return true; }
                return false;
            }
            if (property_key == "scale") {
                if (const auto* v = value_as<Vec3>(value); v && valid_scale(*v)) { entity.transform.scale = *v; return true; }
                return false;
            }
            return false;

        case BuiltinComponentType::SpriteRenderer: {
            if (!entity.sprite_renderer) return false;
            auto& sprite = *entity.sprite_renderer;
            if (property_key == "size") {
                if (const auto* v = value_as<Vec2>(value); v && v->x > 0.0f && v->z > 0.0f) { sprite.size = *v; return true; }
                return false;
            }
            if (property_key == "texture") {
                if (const auto* v = value_as<TextureId>(value)) { sprite.texture = *v; return true; }
                return false;
            }
            if (property_key == "animation_clip") {
                if (const auto* v = value_as<std::string>(value)) { sprite.animation_clip = *v; return true; }
                return false;
            }
            if (property_key == "animation_speed") {
                if (const auto* v = value_as<float>(value); v && *v >= 0.0f) { sprite.animation_speed = *v; return true; }
                return false;
            }
            if (property_key == "animation_time_offset") {
                if (const auto* v = value_as<float>(value)) { sprite.animation_time_offset = *v; return true; }
                return false;
            }
            if (property_key == "animation_paused") {
                if (const auto* v = value_as<bool>(value)) { sprite.animation_paused = *v; return true; }
                return false;
            }
            if (property_key == "color") {
                if (const auto* v = value_as<std::array<float, 4>>(value)) { sprite.color = *v; return true; }
                return false;
            }
            return false;
        }

        case BuiltinComponentType::MeshRenderer: {
            if (!entity.mesh_renderer) return false;
            auto& mesh = *entity.mesh_renderer;
            if (property_key == "primitive") {
                if (const auto* v = value_as<std::string>(value)) {
                    if (const auto parsed = primitive_mesh_from_name(*v)) { mesh.primitive = *parsed; return true; }
                }
                return false;
            }
            if (property_key == "texture") {
                if (const auto* v = value_as<TextureId>(value)) { mesh.texture = *v; return true; }
                return false;
            }
            if (property_key == "color") {
                if (const auto* v = value_as<std::array<float, 4>>(value)) { mesh.color = *v; return true; }
                return false;
            }
            if (property_key == "material_asset_id") {
                if (const auto* v = value_as<std::string>(value)) { mesh.material.asset_id = *v; mesh.material_resolved = false; return true; }
                return false;
            }
            if (property_key == "material_path") {
                if (const auto* v = value_as<std::string>(value)) { mesh.material.path = *v; mesh.material_resolved = false; return true; }
                return false;
            }
            return false;
        }

        case BuiltinComponentType::CylinderCollider: {
            if (!entity.cylinder_collider) return false;
            auto& collider = *entity.cylinder_collider;
            if (property_key == "radius") {
                if (const auto* v = value_as<float>(value); v && *v > 0.0f) { collider.radius = *v; return true; }
                return false;
            }
            if (property_key == "height") {
                if (const auto* v = value_as<float>(value); v && *v > 0.0f) { collider.height = *v; return true; }
                return false;
            }
            if (property_key == "center") {
                if (const auto* v = value_as<Vec3>(value)) { collider.center = *v; return true; }
                return false;
            }
            if (property_key == "is_trigger") {
                if (const auto* v = value_as<bool>(value)) { collider.is_trigger = *v; return true; }
                return false;
            }
            return false;
        }

        case BuiltinComponentType::PointLight: {
            if (!entity.point_light) return false;
            auto& light = *entity.point_light;
            if (property_key == "color") {
                if (const auto* v = value_as<std::array<float, 4>>(value)) { light.color = *v; return true; }
                return false;
            }
            if (property_key == "intensity") {
                if (const auto* v = value_as<float>(value); v && *v >= 0.0f) { light.intensity = *v; return true; }
                return false;
            }
            if (property_key == "radius") {
                if (const auto* v = value_as<float>(value); v && *v > 0.0f) { light.radius = *v; return true; }
                return false;
            }
            return false;
        }
    }
    return false;
}

bool set_builtin_component_property(
    Entity& entity,
    std::string_view component_key,
    std::string_view property_key,
    const BuiltinPropertyValue& value
) {
    const auto* info = builtin_component_info(component_key);
    if (!info) return false;
    return set_builtin_component_property(entity, info->type, property_key, value);
}

} // namespace vespera
