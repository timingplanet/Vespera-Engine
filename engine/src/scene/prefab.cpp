#include <vespera/scene/prefab.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

namespace vespera {
namespace {

constexpr int kPrefabFormatVersion = 6;
constexpr const char* kNoReference = "-";

PrefabIoResult fail(std::size_t line, std::string message) {
    if (line > 0) message = "line " + std::to_string(line) + ": " + std::move(message);
    return {false, std::move(message)};
}

std::unordered_map<std::string, TextureId> texture_lookup(const SectorWorld& world) {
    std::unordered_map<std::string, TextureId> result;
    const auto& textures = world.textures();
    for (std::size_t i = 0; i < textures.size(); ++i) {
        if (!textures[i].name.empty() && !result.contains(textures[i].name)) {
            result.emplace(textures[i].name, static_cast<TextureId>(i));
        }
    }
    return result;
}

std::string texture_name(const SectorWorld& world, TextureId id) {
    if (id == kInvalidTexture || id >= world.textures().size()) return kNoReference;
    return world.textures()[id].name;
}

TextureId resolve_texture(const std::unordered_map<std::string, TextureId>& lookup, const std::string& name) {
    if (name == kNoReference) return kInvalidTexture;
    const auto found = lookup.find(name);
    return found == lookup.end() ? kInvalidTexture : found->second;
}

bool valid_texture_reference(const SectorWorld& world, TextureId id) {
    return id == kInvalidTexture || id < world.textures().size();
}

} // namespace

PrefabIoResult load_entity_prefab(
    const Scene& resource_context,
    EntityPrefab& prefab,
    const std::filesystem::path& path
) {
    std::ifstream input(path);
    if (!input) return {false, "could not open prefab file: " + path.string()};

    const auto textures = texture_lookup(resource_context.world);
    Entity loaded;
    loaded.id = kInvalidSceneObjectId;
    loaded.prefab_source = {};

    bool header_seen = false;
    bool name_seen = false;
    bool enabled_seen = false;
    bool identity_seen = false;
    bool transform_seen = false;
    bool end_seen = false;
    int format_version = 0;
    std::size_t line_number = 0;
    std::string line;
    ManagedScriptComponent* current_managed_script = nullptr;

    while (std::getline(input, line)) {
        ++line_number;
        const auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos || line[first] == '#') continue;

        std::istringstream stream(line.substr(first));
        std::string command;
        stream >> command;

        if (end_seen) return fail(line_number, "content appears after end_prefab");

        if (command == "sectorline_prefab") {
            if (header_seen) return fail(line_number, "duplicate prefab header");
            if (!(stream >> format_version) || format_version < 1 || format_version > kPrefabFormatVersion) {
                return fail(line_number, "unsupported prefab format version");
            }
            header_seen = true;
            continue;
        }
        if (!header_seen) return fail(line_number, "prefab header must be first");

        if (current_managed_script && command != "managed_field" && command != "end_managed_script") {
            return fail(line_number, "C# Script block must end with end_managed_script before '" + command + "'");
        }

        if (command == "name") {
            if (name_seen || !(stream >> std::quoted(loaded.name)) || loaded.name.empty()) {
                return fail(line_number, "invalid or duplicate prefab name");
            }
            name_seen = true;
            continue;
        }
        if (command == "enabled") {
            int enabled = 0;
            if (enabled_seen || !(stream >> enabled) || (enabled != 0 && enabled != 1)) {
                return fail(line_number, "invalid or duplicate enabled value");
            }
            loaded.enabled = enabled != 0;
            enabled_seen = true;
            continue;
        }
        if (command == "identity") {
            if (identity_seen || !(stream >> std::quoted(loaded.tag) >> std::quoted(loaded.layer))
                || loaded.tag.empty() || loaded.layer.empty()) {
                return fail(line_number, "invalid or duplicate identity record");
            }
            identity_seen = true;
            continue;
        }
        if (command == "transform") {
            auto& t = loaded.transform;
            if (transform_seen
                || !(stream >> t.position.x >> t.position.y >> t.position.z
                    >> t.rotation.x >> t.rotation.y >> t.rotation.z
                    >> t.scale.x >> t.scale.y >> t.scale.z)) {
                return fail(line_number, "invalid or duplicate Transform component");
            }
            if (std::abs(t.scale.x) <= 1.0e-6f || std::abs(t.scale.y) <= 1.0e-6f || std::abs(t.scale.z) <= 1.0e-6f) {
                return fail(line_number, "Transform scale components must be non-zero");
            }
            transform_seen = true;
            continue;
        }
        if (command == "sprite_renderer") {
            if (loaded.sprite_renderer) return fail(line_number, "duplicate Sprite Renderer component");
            SpriteRendererComponent sprite;
            std::string texture_ref;
            int paused = 0;
            if (!(stream >> sprite.size.x >> sprite.size.z
                >> std::quoted(texture_ref)
                >> std::quoted(sprite.animation_clip)
                >> sprite.animation_speed
                >> sprite.animation_time_offset
                >> paused
                >> sprite.color[0] >> sprite.color[1] >> sprite.color[2] >> sprite.color[3])) {
                return fail(line_number, "invalid Sprite Renderer component");
            }
            if (sprite.size.x <= 0.0f || sprite.size.z <= 0.0f || sprite.animation_speed < 0.0f || (paused != 0 && paused != 1)) {
                return fail(line_number, "invalid Sprite Renderer values");
            }
            sprite.texture = resolve_texture(textures, texture_ref);
            if (texture_ref != kNoReference && sprite.texture == kInvalidTexture) {
                return fail(line_number, "unknown texture reference '" + texture_ref + "'");
            }
            if (!sprite.animation_clip.empty() && !resource_context.find_sprite_clip(sprite.animation_clip)) {
                return fail(line_number, "unknown sprite animation clip '" + sprite.animation_clip + "'");
            }
            sprite.animation_paused = paused != 0;
            loaded.sprite_renderer = std::move(sprite);
            continue;
        }
        if (command == "mesh_renderer") {
            if (format_version < 5) return fail(line_number, "Mesh Renderer requires prefab format version 5");
            if (loaded.mesh_renderer) return fail(line_number, "duplicate Mesh Renderer component");
            MeshRendererComponent mesh;
            std::string primitive_ref;
            std::string texture_ref;
            if (!(stream >> std::quoted(primitive_ref) >> std::quoted(texture_ref)
                >> mesh.color[0] >> mesh.color[1] >> mesh.color[2] >> mesh.color[3])) {
                return fail(line_number, "invalid Mesh Renderer component");
            }
            const auto primitive = primitive_mesh_from_name(primitive_ref);
            if (!primitive) return fail(line_number, "unknown primitive mesh '" + primitive_ref + "'");
            mesh.primitive = *primitive;
            mesh.texture = resolve_texture(textures, texture_ref);
            if (texture_ref != kNoReference && mesh.texture == kInvalidTexture) {
                return fail(line_number, "unknown texture reference '" + texture_ref + "'");
            }
            loaded.mesh_renderer = mesh;
            continue;
        }
        if (command == "mesh_material_asset") {
            if (format_version < 6) return fail(line_number, "Mesh material reference requires prefab format version 6");
            if (!loaded.mesh_renderer) return fail(line_number, "Mesh material reference requires Mesh Renderer");
            std::string asset_id;
            std::string fallback_path;
            if (!(stream >> std::quoted(asset_id) >> std::quoted(fallback_path))) return fail(line_number, "invalid Mesh material reference");
            loaded.mesh_renderer->material.asset_id = asset_id == kNoReference ? std::string{} : asset_id;
            loaded.mesh_renderer->material.path = fallback_path == kNoReference ? std::filesystem::path{} : std::filesystem::path(fallback_path);
            continue;
        }
        if (command == "cylinder_collider") {
            if (loaded.cylinder_collider) return fail(line_number, "duplicate Cylinder Collider component");
            CylinderColliderComponent collider;
            int trigger = 0;
            if (!(stream >> collider.radius >> collider.height
                >> collider.center.x >> collider.center.y >> collider.center.z >> trigger)) {
                return fail(line_number, "invalid Cylinder Collider component");
            }
            if (collider.radius <= 0.0f || collider.height <= 0.0f || (trigger != 0 && trigger != 1)) {
                return fail(line_number, "invalid Cylinder Collider values");
            }
            collider.is_trigger = trigger != 0;
            loaded.cylinder_collider = collider;
            continue;
        }
        if (command == "point_light") {
            if (format_version < 2) return fail(line_number, "Point Light requires prefab format version 2");
            if (loaded.point_light) return fail(line_number, "duplicate Point Light component");
            PointLightComponent light;
            if (!(stream >> light.color[0] >> light.color[1] >> light.color[2] >> light.color[3]
                >> light.intensity >> light.radius)) {
                return fail(line_number, "invalid Point Light component");
            }
            if (light.intensity < 0.0f || light.radius <= 0.0f) {
                return fail(line_number, "invalid Point Light values");
            }
            loaded.point_light = light;
            continue;
        }
        if (command == "managed_script") {
            if (format_version < 3) return fail(line_number, "C# Script requires prefab format version 3");
            if (format_version == 3 && !loaded.managed_scripts.empty()) {
                return fail(line_number, "prefab v3 can contain only one C# Script component");
            }
            ManagedScriptComponent script;
            int enabled = 0;
            if (!(stream >> std::quoted(script.class_name) >> enabled) || script.class_name.empty() || (enabled != 0 && enabled != 1)) {
                return fail(line_number, "invalid C# Script component");
            }
            std::string trailing;
            if (stream >> trailing) return fail(line_number, "managed_script has unexpected trailing data");
            script.enabled = enabled != 0;
            loaded.managed_scripts.push_back(std::move(script));
            if (format_version >= 4) current_managed_script = &loaded.managed_scripts.back();
            continue;
        }
        if (command == "managed_field") {
            if (format_version < 4) return fail(line_number, "managed_field requires prefab format version 4");
            if (!current_managed_script) return fail(line_number, "managed_field must appear inside a managed_script block");
            ManagedScriptFieldValue field;
            if (!(stream >> std::quoted(field.field_name) >> std::quoted(field.type_name) >> std::quoted(field.serialized_value))
                || field.field_name.empty() || field.type_name.empty()) {
                return fail(line_number, "invalid managed_field record");
            }
            if (std::any_of(current_managed_script->fields.begin(), current_managed_script->fields.end(), [&](const auto& existing) {
                    return existing.field_name == field.field_name;
                })) {
                return fail(line_number, "duplicate managed_field '" + field.field_name + "'");
            }
            std::string trailing;
            if (stream >> trailing) return fail(line_number, "managed_field has unexpected trailing data");
            current_managed_script->fields.push_back(std::move(field));
            continue;
        }
        if (command == "end_managed_script") {
            if (format_version < 4) return fail(line_number, "end_managed_script requires prefab format version 4");
            if (!current_managed_script) return fail(line_number, "end_managed_script without managed_script");
            std::string trailing;
            if (stream >> trailing) return fail(line_number, "end_managed_script has unexpected trailing data");
            current_managed_script = nullptr;
            continue;
        }
        if (command == "end_prefab") {
            end_seen = true;
            continue;
        }

        return fail(line_number, "unknown prefab command '" + command + "'");
    }

    if (!header_seen) return {false, "missing prefab header"};
    if (!name_seen) return {false, "prefab is missing its name"};
    if (!enabled_seen) return {false, "prefab is missing its enabled state"};
    if (!identity_seen) return {false, "prefab is missing its identity record"};
    if (!transform_seen) return {false, "prefab is missing its Transform component"};
    if (!end_seen) return {false, "prefab is missing end_prefab"};

    prefab.prototype = std::move(loaded);
    return {true, "loaded entity prefab '" + prefab.prototype.name + "' from " + path.string()};
}

PrefabIoResult save_entity_prefab(
    const Scene& resource_context,
    const Entity& entity,
    const std::filesystem::path& path
) {
    if (entity.name.empty()) return {false, "cannot save prefab from an entity with an empty name"};
    if (entity.tag.empty() || entity.layer.empty()) return {false, "cannot save prefab with an empty tag or layer"};
    if (std::abs(entity.transform.scale.x) <= 1.0e-6f
        || std::abs(entity.transform.scale.y) <= 1.0e-6f
        || std::abs(entity.transform.scale.z) <= 1.0e-6f) {
        return {false, "cannot save prefab with a zero Transform scale component"};
    }
    if (entity.sprite_renderer) {
        const auto& sprite = *entity.sprite_renderer;
        if (sprite.size.x <= 0.0f || sprite.size.z <= 0.0f || sprite.animation_speed < 0.0f) {
            return {false, "cannot save prefab with invalid Sprite Renderer values"};
        }
        if (!valid_texture_reference(resource_context.world, sprite.texture)) {
            return {false, "cannot save prefab with an invalid texture id"};
        }
        if (!sprite.animation_clip.empty() && !resource_context.find_sprite_clip(sprite.animation_clip)) {
            return {false, "cannot save prefab with missing sprite animation clip '" + sprite.animation_clip + "'"};
        }
    }
    if (entity.mesh_renderer && !valid_texture_reference(resource_context.world, entity.mesh_renderer->texture)) {
        return {false, "cannot save prefab with an invalid Mesh Renderer texture id"};
    }
    if (entity.cylinder_collider
        && (entity.cylinder_collider->radius <= 0.0f || entity.cylinder_collider->height <= 0.0f)) {
        return {false, "cannot save prefab with invalid Cylinder Collider values"};
    }
    if (entity.point_light
        && (entity.point_light->intensity < 0.0f || entity.point_light->radius <= 0.0f)) {
        return {false, "cannot save prefab with invalid Point Light values"};
    }
    for (const auto& script : entity.managed_scripts) {
        if (script.class_name.empty()) return {false, "cannot save prefab with an empty C# script class name"};
        for (const auto& field : script.fields) {
            if (field.field_name.empty() || field.type_name.empty()) {
                return {false, "cannot save prefab with an invalid C# exposed field override"};
            }
        }
    }

    std::error_code ec;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) return {false, "could not create prefab directory: " + ec.message()};
    }

    std::ofstream output(path, std::ios::trunc);
    if (!output) return {false, "could not create prefab file: " + path.string()};

    output << std::setprecision(9);
    output << "sectorline_prefab " << kPrefabFormatVersion << "\n";
    output << "name " << std::quoted(entity.name) << "\n";
    output << "enabled " << (entity.enabled ? 1 : 0) << "\n";
    output << "identity " << std::quoted(entity.tag) << ' ' << std::quoted(entity.layer) << "\n";
    const auto& t = entity.transform;
    output << "transform "
        << t.position.x << ' ' << t.position.y << ' ' << t.position.z << ' '
        << t.rotation.x << ' ' << t.rotation.y << ' ' << t.rotation.z << ' '
        << t.scale.x << ' ' << t.scale.y << ' ' << t.scale.z << "\n";

    if (entity.sprite_renderer) {
        const auto& sprite = *entity.sprite_renderer;
        output << "sprite_renderer "
            << sprite.size.x << ' ' << sprite.size.z << ' '
            << std::quoted(texture_name(resource_context.world, sprite.texture)) << ' '
            << std::quoted(sprite.animation_clip) << ' '
            << sprite.animation_speed << ' ' << sprite.animation_time_offset << ' '
            << (sprite.animation_paused ? 1 : 0) << ' '
            << sprite.color[0] << ' ' << sprite.color[1] << ' ' << sprite.color[2] << ' ' << sprite.color[3]
            << "\n";
    }
    if (entity.mesh_renderer) {
        const auto& mesh = *entity.mesh_renderer;
        output << "mesh_renderer "
            << std::quoted(std::string(primitive_mesh_name(mesh.primitive))) << ' '
            << std::quoted(texture_name(resource_context.world, mesh.texture)) << ' '
            << mesh.color[0] << ' ' << mesh.color[1] << ' ' << mesh.color[2] << ' ' << mesh.color[3] << "\n";
        if (!mesh.material.empty()) {
            output << "mesh_material_asset "
                << std::quoted(mesh.material.asset_id.empty() ? std::string(kNoReference) : mesh.material.asset_id) << ' '
                << std::quoted(mesh.material.path.empty() ? std::string(kNoReference) : mesh.material.path.generic_string()) << "\n";
        }
    }
    if (entity.cylinder_collider) {
        const auto& collider = *entity.cylinder_collider;
        output << "cylinder_collider "
            << collider.radius << ' ' << collider.height << ' '
            << collider.center.x << ' ' << collider.center.y << ' ' << collider.center.z << ' '
            << (collider.is_trigger ? 1 : 0) << "\n";
    }
    if (entity.point_light) {
        const auto& light = *entity.point_light;
        output << "point_light "
            << light.color[0] << ' ' << light.color[1] << ' ' << light.color[2] << ' ' << light.color[3] << ' '
            << light.intensity << ' ' << light.radius << "\n";
    }
    for (const auto& script : entity.managed_scripts) {
        output << "managed_script " << std::quoted(script.class_name) << ' '
            << (script.enabled ? 1 : 0) << "\n";
        for (const auto& field : script.fields) {
            output << "  managed_field " << std::quoted(field.field_name) << ' '
                << std::quoted(field.type_name) << ' '
                << std::quoted(field.serialized_value) << "\n";
        }
        output << "end_managed_script\n";
    }
    output << "end_prefab\n";

    if (!output) return {false, "failed while writing prefab file: " + path.string()};
    return {true, "saved entity prefab '" + entity.name + "' to " + path.string()};
}

Entity* instantiate_entity_prefab(
    Scene& scene,
    const EntityPrefab& prefab,
    std::string name_override,
    AssetReference source_asset
) {
    Entity entity = prefab.prototype;
    entity.id = scene.allocate_object_id();
    if (!name_override.empty()) entity.name = std::move(name_override);
    entity.prefab_source = std::move(source_asset);
    scene.entities.push_back(std::move(entity));
    return &scene.entities.back();
}

} // namespace vespera
