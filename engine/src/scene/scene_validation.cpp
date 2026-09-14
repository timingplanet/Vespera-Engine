#include <vespera/scene/scene_validation.hpp>

#include <vespera/scene/scene.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace vespera {
namespace {

constexpr float kEpsilon = 1.0e-5f;

void add_issue(
    std::vector<SceneValidationIssue>& issues,
    SceneValidationSeverity severity,
    std::string message
) {
    issues.push_back({severity, std::move(message)});
}

bool convex_counter_clockwise(const Sector& sector) {
    if (sector.vertices.size() < 3) return false;
    float sign = 0.0f;
    for (std::size_t i = 0; i < sector.vertices.size(); ++i) {
        const Vec2 a = sector.vertices[i];
        const Vec2 b = sector.vertices[(i + 1) % sector.vertices.size()];
        const Vec2 c = sector.vertices[(i + 2) % sector.vertices.size()];
        const float abx = b.x - a.x;
        const float abz = b.z - a.z;
        const float bcx = c.x - b.x;
        const float bcz = c.z - b.z;
        const float cross = abx * bcz - abz * bcx;
        if (std::abs(cross) <= kEpsilon) continue;
        if (sign == 0.0f) sign = cross;
        else if ((sign > 0.0f) != (cross > 0.0f)) return false;
    }
    return sign > 0.0f;
}

} // namespace

std::vector<SceneValidationIssue> validate_scene(const Scene& scene) {
    std::vector<SceneValidationIssue> issues;

    if (scene.camera.vertical_fov_degrees < 1.0f || scene.camera.vertical_fov_degrees >= 179.0f) {
        add_issue(issues, SceneValidationSeverity::Error, "Camera vertical FOV must be between 1 and 179 degrees.");
    }
    if (scene.camera.near_plane <= 0.0f || scene.camera.far_plane <= scene.camera.near_plane) {
        add_issue(issues, SceneValidationSeverity::Error, "Camera clipping planes are invalid.");
    }

    const auto& textures = scene.world.textures();
    const auto& materials = scene.world.materials();
    const auto& sectors = scene.world.sectors();

    std::unordered_set<std::string> material_names;
    for (std::size_t i = 0; i < materials.size(); ++i) {
        const auto& material = materials[i];
        if (material.name.empty()) {
            add_issue(issues, SceneValidationSeverity::Error, "Material " + std::to_string(i) + " has an empty name.");
        } else if (!material_names.insert(material.name).second) {
            add_issue(issues, SceneValidationSeverity::Error, "Duplicate material name: " + material.name);
        }
        if (material.texture != kInvalidTexture && material.texture >= textures.size()) {
            add_issue(issues, SceneValidationSeverity::Error, "Material '" + material.name + "' references an invalid texture.");
        }
    }

    for (std::size_t i = 0; i < sectors.size(); ++i) {
        const auto& sector = sectors[i];
        const std::string label = sector.name.empty() ? ("sector " + std::to_string(i)) : ("sector '" + sector.name + "'");
        if (sector.vertices.size() < 3) {
            add_issue(issues, SceneValidationSeverity::Error, label + " has fewer than 3 vertices.");
        } else if (!convex_counter_clockwise(sector)) {
            add_issue(issues, SceneValidationSeverity::Error, label + " must remain convex and counter-clockwise.");
        }
        if (sector.ceiling_height <= sector.floor_height) {
            add_issue(issues, SceneValidationSeverity::Error, label + " ceiling must be above its floor.");
        }
        if (!sector.sides.empty() && sector.sides.size() != sector.vertices.size()) {
            add_issue(issues, SceneValidationSeverity::Error, label + " side count does not match vertex count.");
        }
        const auto check_material = [&](MaterialId id, const char* role) {
            if (id != kInvalidMaterial && id >= materials.size()) {
                add_issue(issues, SceneValidationSeverity::Error, label + " has an invalid " + role + " material.");
            }
        };
        check_material(sector.floor_material, "floor");
        check_material(sector.ceiling_material, "ceiling");
        check_material(sector.wall_material, "wall");
        for (std::size_t side_index = 0; side_index < sector.sides.size(); ++side_index) {
            const auto& side = sector.sides[side_index];
            check_material(side.material, "side");
            if (side.adjacent_sector < -1 || side.adjacent_sector >= static_cast<int>(sectors.size())) {
                add_issue(issues, SceneValidationSeverity::Error, label + " has an out-of-range portal adjacency.");
                continue;
            }
            if (side.adjacent_sector == static_cast<int>(i)) {
                add_issue(issues, SceneValidationSeverity::Error, label + " side " + std::to_string(side_index) + " cannot portal to itself.");
                continue;
            }
            if (side.adjacent_sector >= 0) {
                const std::size_t target_index = static_cast<std::size_t>(side.adjacent_sector);
                const auto matching = find_matching_sector_side(scene.world, i, side_index, target_index);
                if (!matching) {
                    add_issue(issues, SceneValidationSeverity::Warning,
                        label + " side " + std::to_string(side_index)
                        + " portals to sector '" + sectors[target_index].name
                        + "' but no matching shared edge was found.");
                } else {
                    const auto& target = sectors[target_index];
                    if (target.sides.size() != target.vertices.size()
                        || *matching >= target.sides.size()
                        || target.sides[*matching].adjacent_sector != static_cast<int>(i)) {
                        add_issue(issues, SceneValidationSeverity::Warning,
                            label + " side " + std::to_string(side_index)
                            + " has a non-reciprocal portal link to sector '" + target.name + "'.");
                    }
                }
            }
        }
    }

    std::unordered_set<std::string> clip_names;
    for (const auto& clip : scene.sprite_clips) {
        if (!clip.valid()) {
            add_issue(issues, SceneValidationSeverity::Error, "Sprite clip '" + clip.name + "' has an invalid direction/frame layout.");
            continue;
        }
        if (clip.frames_per_second < 0.0f) {
            add_issue(issues, SceneValidationSeverity::Error, "Sprite clip '" + clip.name + "' has a negative frame rate.");
        }
        if (!clip_names.insert(clip.name).second) {
            add_issue(issues, SceneValidationSeverity::Error, "Duplicate sprite clip name: " + clip.name);
        }
        for (const auto texture : clip.textures) {
            if (texture != kInvalidTexture && texture >= textures.size()) {
                add_issue(issues, SceneValidationSeverity::Error, "Sprite clip '" + clip.name + "' references an invalid texture.");
                break;
            }
        }
    }

    std::unordered_set<SceneObjectId> entity_ids;
    std::unordered_map<std::string, int> entity_name_counts;
    for (const auto& entity : scene.entities) {
        if (entity.id == kInvalidSceneObjectId) {
            add_issue(issues, SceneValidationSeverity::Error, "Entity '" + entity.name + "' has object id 0.");
        } else if (!entity_ids.insert(entity.id).second) {
            add_issue(issues, SceneValidationSeverity::Error, "Duplicate entity object id " + std::to_string(entity.id) + ".");
        }
        if (entity.name.empty()) {
            add_issue(issues, SceneValidationSeverity::Error, "An entity has an empty name.");
        } else {
            ++entity_name_counts[entity.name];
        }
        if (entity.tag.empty()) {
            add_issue(issues, SceneValidationSeverity::Error, "Entity '" + entity.name + "' has an empty tag.");
        }
        if (entity.layer.empty()) {
            add_issue(issues, SceneValidationSeverity::Error, "Entity '" + entity.name + "' has an empty layer.");
        }
        if (!entity.prefab_source.empty()) {
            const std::filesystem::path prefab_path(entity.prefab_source.path);
            bool escapes_assets = prefab_path.is_absolute();
            for (const auto& part : prefab_path) {
                if (part == "..") escapes_assets = true;
            }
            if (escapes_assets) {
                add_issue(issues, SceneValidationSeverity::Error,
                    "Entity '" + entity.name + "' has a prefab source that escapes the project assets root.");
            } else if (prefab_path.extension() != ".slprefab") {
                add_issue(issues, SceneValidationSeverity::Warning,
                    "Entity '" + entity.name + "' prefab source does not use the .slprefab extension.");
            }
        }
        const auto& scale = entity.transform.scale;
        if (std::abs(scale.x) <= kEpsilon || std::abs(scale.y) <= kEpsilon || std::abs(scale.z) <= kEpsilon) {
            add_issue(issues, SceneValidationSeverity::Error, "Entity '" + entity.name + "' has a zero Transform scale axis.");
        }

        if (entity.sprite_renderer) {
            const auto& sprite = *entity.sprite_renderer;
            if (sprite.size.x <= 0.0f || sprite.size.z <= 0.0f) {
                add_issue(issues, SceneValidationSeverity::Error, "Entity '" + entity.name + "' has a Sprite Renderer with non-positive size.");
            }
            if (sprite.texture != kInvalidTexture && sprite.texture >= textures.size()) {
                add_issue(issues, SceneValidationSeverity::Error, "Entity '" + entity.name + "' Sprite Renderer references an invalid fallback texture.");
            }
            if (!sprite.animation_clip.empty() && !clip_names.contains(sprite.animation_clip)) {
                add_issue(issues, SceneValidationSeverity::Error, "Entity '" + entity.name + "' Sprite Renderer references missing clip '" + sprite.animation_clip + "'.");
            }
            if (sprite.animation_speed < 0.0f) {
                add_issue(issues, SceneValidationSeverity::Error, "Entity '" + entity.name + "' Sprite Renderer has a negative animation speed.");
            }
        }

        if (entity.mesh_renderer) {
            const auto& mesh = *entity.mesh_renderer;
            if (mesh.texture != kInvalidTexture && mesh.texture >= textures.size()) {
                add_issue(issues, SceneValidationSeverity::Error, "Entity '" + entity.name + "' Mesh Renderer references an invalid texture.");
            }
        }

        if (entity.cylinder_collider) {
            const auto& collider = *entity.cylinder_collider;
            if (collider.radius <= 0.0f) {
                add_issue(issues, SceneValidationSeverity::Error, "Entity '" + entity.name + "' Cylinder Collider has a non-positive radius.");
            }
            if (collider.height <= 0.0f) {
                add_issue(issues, SceneValidationSeverity::Error, "Entity '" + entity.name + "' Cylinder Collider has a non-positive height.");
            }
        }

        if (entity.point_light) {
            const auto& light = *entity.point_light;
            if (light.intensity < 0.0f) {
                add_issue(issues, SceneValidationSeverity::Error, "Entity '" + entity.name + "' Point Light has a negative intensity.");
            }
            if (light.radius <= 0.0f) {
                add_issue(issues, SceneValidationSeverity::Error, "Entity '" + entity.name + "' Point Light has a non-positive radius.");
            }
        }

        for (std::size_t script_index = 0; script_index < entity.managed_scripts.size(); ++script_index) {
            const auto& script = entity.managed_scripts[script_index];
            const std::string script_label = "Entity '" + entity.name + "' C# Script #" + std::to_string(script_index + 1);
            if (script.class_name.empty()) {
                add_issue(issues, SceneValidationSeverity::Error, script_label + " has an empty class name.");
            }
            std::unordered_set<std::string> field_names;
            for (const auto& field : script.fields) {
                if (field.field_name.empty() || field.type_name.empty()) {
                    add_issue(issues, SceneValidationSeverity::Error, script_label + " has an exposed field override with an empty name or type.");
                    continue;
                }
                if (!field_names.insert(field.field_name).second) {
                    add_issue(issues, SceneValidationSeverity::Error,
                        script_label + " contains duplicate exposed field override '" + field.field_name + "'.");
                }
            }
        }
    }
    // Validate parent references and cycles after the full entity ID set is known.
    for (const auto& entity : scene.entities) {
        if (entity.parent_id == kInvalidSceneObjectId) continue;
        if (!entity_ids.contains(entity.parent_id)) {
            add_issue(issues, SceneValidationSeverity::Error,
                "Entity '" + entity.name + "' references missing parent id " + std::to_string(entity.parent_id) + ".");
            continue;
        }
        if (entity.parent_id == entity.id) {
            add_issue(issues, SceneValidationSeverity::Error, "Entity '" + entity.name + "' cannot parent itself.");
            continue;
        }
        std::unordered_set<SceneObjectId> chain;
        SceneObjectId current = entity.id;
        while (current != kInvalidSceneObjectId) {
            if (!chain.insert(current).second) {
                add_issue(issues, SceneValidationSeverity::Error,
                    "Entity hierarchy cycle detected at '" + entity.name + "'.");
                break;
            }
            const Entity* node = scene.find_entity(current);
            if (!node) break;
            current = node->parent_id;
        }
    }

    for (const auto& [name, count] : entity_name_counts) {
        if (count > 1) {
            add_issue(issues, SceneValidationSeverity::Warning, "Multiple entities share the name '" + name + "'. Stable ids keep them distinct, but unique names are easier to author.");
        }
    }

    return issues;
}

bool scene_validation_has_errors(const std::vector<SceneValidationIssue>& issues) {
    return std::any_of(issues.begin(), issues.end(), [](const SceneValidationIssue& issue) {
        return issue.severity == SceneValidationSeverity::Error;
    });
}

} // namespace vespera
