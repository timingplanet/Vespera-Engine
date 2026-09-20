#include <vespera/scene/scene_io.hpp>

#include <vespera/scene/scene.hpp>
#include <vespera/assets/texture_importer.hpp>
#include <vespera/scene/scene_validation.hpp>
#include <vespera/world/sector_world.hpp>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace vespera {
namespace {

constexpr int kSceneFormatVersion = 15;
constexpr int kOldestSupportedSceneFormatVersion = 1;
constexpr const char* kNoReference = "-";

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

SceneIoResult fail(std::size_t line, std::string message) {
    if (line > 0) {
        message = "line " + std::to_string(line) + ": " + std::move(message);
    }
    return {false, std::move(message)};
}

std::unordered_map<std::string, TextureId> texture_lookup(const SectorWorld& world) {
    std::unordered_map<std::string, TextureId> lookup;
    const auto& textures = world.textures();
    for (std::size_t i = 0; i < textures.size(); ++i) {
        if (!textures[i].name.empty() && !lookup.contains(textures[i].name)) {
            lookup.emplace(textures[i].name, static_cast<TextureId>(i));
        }
    }
    return lookup;
}

std::string canonical_texture_name(std::string_view name) {
    std::string result;
    result.reserve(name.size());
    for (unsigned char c : name) {
        if (c >= 'A' && c <= 'Z') c = static_cast<unsigned char>(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            result.push_back(static_cast<char>(c));
        }
    }
    return result;
}

std::optional<TextureData> normalized_texture_alias(const SectorWorld& world, std::string_view requested_name) {
    const std::string requested_key = canonical_texture_name(requested_name);
    if (requested_key.empty()) return std::nullopt;

    const TextureData* match = nullptr;
    for (const auto& texture : world.textures()) {
        if (texture.name.empty() || canonical_texture_name(texture.name) != requested_key) continue;
        if (match != nullptr) {
            // Never guess when two registered assets normalize to the same name.
            return std::nullopt;
        }
        match = &texture;
    }
    if (!match) return std::nullopt;

    TextureData alias = *match;
    alias.name = std::string(requested_name);
    return alias;
}

std::string material_name(const SectorWorld& world, MaterialId id) {
    if (id == kInvalidMaterial || id >= world.materials().size()) {
        return kNoReference;
    }
    return world.materials()[id].name;
}

std::string texture_name(const SectorWorld& world, TextureId id) {
    if (id == kInvalidTexture || id >= world.textures().size()) {
        return kNoReference;
    }
    return world.textures()[id].name;
}

bool parse_quoted(std::istringstream& stream, std::string& out) {
    stream >> std::quoted(out);
    return static_cast<bool>(stream);
}

MaterialId resolve_material(
    const std::unordered_map<std::string, MaterialId>& lookup,
    const std::string& name
) {
    if (name == kNoReference) {
        return kInvalidMaterial;
    }
    const auto found = lookup.find(name);
    return found == lookup.end() ? kInvalidMaterial : found->second;
}

TextureId resolve_texture(
    const std::unordered_map<std::string, TextureId>& lookup,
    const std::string& name
) {
    if (name == kNoReference) {
        return kInvalidTexture;
    }
    const auto found = lookup.find(name);
    return found == lookup.end() ? kInvalidTexture : found->second;
}

struct PendingSector {
    Sector sector;
    std::string floor_material;
    std::string ceiling_material;
    std::string wall_material;
    std::vector<std::pair<std::string, int>> sides;
    std::size_t source_line = 0;
};

} // namespace

SceneIoResult load_scene_text(Scene& scene, const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        return {false, "could not open scene file: " + path.string()};
    }

    const auto textures = scene.world.textures();
    const auto textures_by_name = texture_lookup(scene.world);

    Camera loaded_camera = scene.camera;
    std::vector<WorldMaterial> loaded_materials;
    std::vector<PendingSector> loaded_sectors;
    PendingSector* current_sector = nullptr;
    bool header_seen = false;
    bool camera_seen = false;
    bool end_seen = false;
    int loaded_scene_version = 0;
    std::vector<SpriteAnimationClip> loaded_sprite_clips;
    std::vector<Entity> loaded_entities;
    std::unordered_set<SceneObjectId> loaded_entity_ids;
    Entity* current_entity = nullptr;
    bool current_entity_identity_seen = false;
    bool current_entity_prefab_source_seen = false;
    bool current_entity_parent_seen = false;
    bool current_entity_transform_seen = false;
    ManagedScriptComponent* current_managed_script = nullptr;

    std::string raw_line;
    std::size_t line_number = 0;
    while (std::getline(input, raw_line)) {
        ++line_number;
        const auto comment = raw_line.find('#');
        if (comment != std::string::npos) {
            raw_line.erase(comment);
        }
        const std::string line = trim(raw_line);
        if (line.empty()) {
            continue;
        }

        std::istringstream stream(line);
        std::string command;
        stream >> command;

        if (!header_seen) {
            if (command != "sectorline_scene") {
                return fail(line_number, "expected 'sectorline_scene <version>' header");
            }
            int version = 0;
            if (!(stream >> version)) {
                return fail(line_number, "missing scene format version");
            }
            if (version < kOldestSupportedSceneFormatVersion || version > kSceneFormatVersion) {
                return fail(line_number, "unsupported scene format version " + std::to_string(version));
            }
            loaded_scene_version = version;
            header_seen = true;
            continue;
        }

        if (end_seen) {
            return fail(line_number, "content appears after 'end'");
        }

        if (command == "camera") {
            if (current_sector || current_entity) {
                return fail(line_number, "camera must appear at scene top level");
            }
            if (!(stream
                >> loaded_camera.position.x
                >> loaded_camera.position.y
                >> loaded_camera.position.z
                >> loaded_camera.yaw
                >> loaded_camera.pitch
                >> loaded_camera.vertical_fov_degrees
                >> loaded_camera.near_plane
                >> loaded_camera.far_plane)) {
                return fail(line_number, "invalid camera record");
            }
            camera_seen = true;
            continue;
        }

        if (command == "material") {
            if (current_sector || current_entity) {
                return fail(line_number, "material must appear at scene top level");
            }

            WorldMaterial material;
            std::string texture_ref;
            if (!parse_quoted(stream, material.name)
                || !(stream
                    >> material.color[0]
                    >> material.color[1]
                    >> material.color[2]
                    >> material.color[3])
                || !parse_quoted(stream, texture_ref)
                || !(stream >> material.uv_scale.x >> material.uv_scale.z)) {
                return fail(line_number, "invalid material record");
            }
            if (material.name.empty()) {
                return fail(line_number, "material name cannot be empty");
            }
            if (std::any_of(loaded_materials.begin(), loaded_materials.end(), [&](const WorldMaterial& existing) {
                    return existing.name == material.name;
                })) {
                return fail(line_number, "duplicate material name '" + material.name + "'");
            }

            material.texture = resolve_texture(textures_by_name, texture_ref);
            if (texture_ref != kNoReference && material.texture == kInvalidTexture) {
                return fail(line_number, "texture reference '" + texture_ref + "' is not registered");
            }
            loaded_materials.push_back(std::move(material));
            continue;
        }

        if (command == "sprite_clip") {
            if (current_sector || current_entity) {
                return fail(line_number, "sprite_clip must appear at scene top level");
            }
            if (loaded_scene_version < 3) {
                return fail(line_number, "sprite_clip records require scene format version 3");
            }

            SpriteAnimationClip clip;
            int loop_value = 1;
            if (!parse_quoted(stream, clip.name)
                || !(stream >> clip.direction_count >> clip.frame_count >> clip.frames_per_second >> loop_value)) {
                return fail(line_number, "invalid sprite_clip header");
            }
            if (clip.name.empty()) {
                return fail(line_number, "sprite clip name cannot be empty");
            }
            if (clip.direction_count != 1u && clip.direction_count != 4u && clip.direction_count != 8u) {
                return fail(line_number, "sprite clip direction count must be 1, 4, or 8");
            }
            if (clip.frame_count == 0u) {
                return fail(line_number, "sprite clip frame count must be positive");
            }
            if (clip.frames_per_second < 0.0f) {
                return fail(line_number, "sprite clip frames-per-second cannot be negative");
            }
            if (loop_value != 0 && loop_value != 1) {
                return fail(line_number, "sprite clip loop value must be 0 or 1");
            }
            if (std::any_of(loaded_sprite_clips.begin(), loaded_sprite_clips.end(), [&](const SpriteAnimationClip& existing) {
                    return existing.name == clip.name;
                })) {
                return fail(line_number, "duplicate sprite clip name '" + clip.name + "'");
            }
            clip.loop = loop_value != 0;
            const std::size_t texture_count = static_cast<std::size_t>(clip.direction_count) * clip.frame_count;
            clip.textures.reserve(texture_count);
            for (std::size_t texture_index = 0; texture_index < texture_count; ++texture_index) {
                std::string texture_ref;
                if (!parse_quoted(stream, texture_ref)) {
                    return fail(line_number, "sprite clip is missing one or more texture references");
                }
                const TextureId texture = resolve_texture(textures_by_name, texture_ref);
                if (texture_ref != kNoReference && texture == kInvalidTexture) {
                    return fail(line_number, "sprite clip texture reference '" + texture_ref + "' is not registered");
                }
                clip.textures.push_back(texture);
            }
            std::string trailing;
            if (stream >> trailing) {
                return fail(line_number, "sprite_clip has unexpected trailing data");
            }
            loaded_sprite_clips.push_back(std::move(clip));
            continue;
        }

        if (command == "entity") {
            if (current_sector || current_entity) {
                return fail(line_number, "nested entity/sector blocks are not allowed");
            }
            if (loaded_scene_version < 5) {
                return fail(line_number, "entity records require scene format version 5");
            }
            int enabled_value = 1;
            Entity entity;
            if (!(stream >> entity.id >> enabled_value) || !parse_quoted(stream, entity.name)) {
                return fail(line_number, "invalid entity header");
            }
            if (entity.id == kInvalidSceneObjectId) {
                return fail(line_number, "entity object id must be non-zero");
            }
            if (!loaded_entity_ids.insert(entity.id).second) {
                return fail(line_number, "duplicate entity object id " + std::to_string(entity.id));
            }
            if (enabled_value != 0 && enabled_value != 1) {
                return fail(line_number, "entity enabled value must be 0 or 1");
            }
            if (entity.name.empty()) {
                return fail(line_number, "entity name cannot be empty");
            }
            entity.enabled = enabled_value != 0;
            loaded_entities.push_back(std::move(entity));
            current_entity = &loaded_entities.back();
            current_entity_identity_seen = false;
            current_entity_prefab_source_seen = false;
            current_entity_parent_seen = false;
            current_entity_transform_seen = false;
            current_managed_script = nullptr;
            continue;
        }

        if (current_managed_script && command != "managed_field" && command != "end_managed_script") {
            return fail(line_number, "C# Script block must end with end_managed_script before '" + command + "'");
        }

        if (command == "identity") {
            if (!current_entity) {
                return fail(line_number, "identity must appear inside an entity block");
            }
            if (loaded_scene_version < 7) {
                return fail(line_number, "identity records require scene format version 7");
            }
            if (current_entity_identity_seen) {
                return fail(line_number, "entity contains more than one identity record");
            }
            if (!parse_quoted(stream, current_entity->tag)
                || !parse_quoted(stream, current_entity->layer)) {
                return fail(line_number, "invalid entity identity record");
            }
            if (current_entity->tag.empty()) {
                return fail(line_number, "entity tag cannot be empty");
            }
            if (current_entity->layer.empty()) {
                return fail(line_number, "entity layer cannot be empty");
            }
            std::string trailing;
            if (stream >> trailing) {
                return fail(line_number, "identity has unexpected trailing data");
            }
            current_entity_identity_seen = true;
            continue;
        }

        if (command == "prefab_source" || command == "prefab_source_asset") {
            if (!current_entity) {
                return fail(line_number, "prefab source must appear inside an entity block");
            }
            if (loaded_scene_version < 8) {
                return fail(line_number, "prefab_source records require scene format version 8");
            }
            if (command == "prefab_source_asset" && loaded_scene_version < 12) {
                return fail(line_number, "prefab_source_asset records require scene format version 12");
            }
            if (current_entity_prefab_source_seen) {
                return fail(line_number, "entity contains more than one prefab source record");
            }
            std::string fallback;
            if (command == "prefab_source_asset") {
                if (!parse_quoted(stream, current_entity->prefab_source.asset_id)
                    || !parse_quoted(stream, fallback)
                    || current_entity->prefab_source.asset_id.empty()
                    || fallback.empty()) {
                    return fail(line_number, "invalid prefab_source_asset record");
                }
            } else {
                if (!parse_quoted(stream, fallback) || fallback.empty()) {
                    return fail(line_number, "invalid prefab_source record");
                }
                current_entity->prefab_source.asset_id.clear();
            }
            current_entity->prefab_source.path = fallback;
            std::string trailing;
            if (stream >> trailing) {
                return fail(line_number, "prefab source has unexpected trailing data");
            }
            current_entity_prefab_source_seen = true;
            continue;
        }

        if (command == "parent") {
            if (!current_entity) return fail(line_number, "parent must appear inside an entity block");
            if (loaded_scene_version < 14) return fail(line_number, "parent records require scene format version 14");
            if (current_entity_parent_seen) return fail(line_number, "entity contains more than one parent record");
            if (!(stream >> current_entity->parent_id)) return fail(line_number, "invalid parent record");
            if (current_entity->parent_id == current_entity->id) return fail(line_number, "entity cannot parent itself");
            std::string trailing;
            if (stream >> trailing) return fail(line_number, "parent has unexpected trailing data");
            current_entity_parent_seen = true;
            continue;
        }

        if (command == "transform") {
            if (!current_entity) {
                return fail(line_number, "transform must appear inside an entity block");
            }
            if (current_entity_transform_seen) {
                return fail(line_number, "entity contains more than one Transform component");
            }
            auto& t = current_entity->transform;
            if (!(stream
                >> t.position.x >> t.position.y >> t.position.z
                >> t.rotation.x >> t.rotation.y >> t.rotation.z
                >> t.scale.x >> t.scale.y >> t.scale.z)) {
                return fail(line_number, "invalid transform component record");
            }
            current_entity_transform_seen = true;
            continue;
        }

        if (command == "sprite_renderer") {
            if (!current_entity) {
                return fail(line_number, "sprite_renderer must appear inside an entity block");
            }
            if (current_entity->sprite_renderer) {
                return fail(line_number, "entity contains more than one Sprite Renderer component");
            }
            SpriteRendererComponent sprite;
            std::string texture_ref;
            std::string clip_ref;
            int paused_value = 0;
            if (!(stream >> sprite.size.x >> sprite.size.z)
                || !parse_quoted(stream, texture_ref)
                || !parse_quoted(stream, clip_ref)
                || !(stream
                    >> sprite.animation_speed
                    >> sprite.animation_time_offset
                    >> paused_value
                    >> sprite.color[0] >> sprite.color[1] >> sprite.color[2] >> sprite.color[3])) {
                return fail(line_number, "invalid sprite_renderer component record");
            }
            if (sprite.size.x <= 0.0f || sprite.size.z <= 0.0f) {
                return fail(line_number, "sprite renderer size must be positive");
            }
            if (sprite.animation_speed < 0.0f) {
                return fail(line_number, "sprite renderer animation speed cannot be negative");
            }
            if (paused_value != 0 && paused_value != 1) {
                return fail(line_number, "sprite renderer paused value must be 0 or 1");
            }
            sprite.texture = resolve_texture(textures_by_name, texture_ref);
            if (texture_ref != kNoReference && sprite.texture == kInvalidTexture) {
                return fail(line_number, "sprite renderer texture reference '" + texture_ref + "' is not registered");
            }
            sprite.animation_clip = clip_ref == kNoReference ? std::string{} : clip_ref;
            sprite.animation_paused = paused_value != 0;
            current_entity->sprite_renderer = std::move(sprite);
            continue;
        }

        if (command == "mesh_renderer") {
            if (!current_entity) {
                return fail(line_number, "mesh_renderer must appear inside an entity block");
            }
            if (loaded_scene_version < 13) {
                return fail(line_number, "mesh_renderer records require scene format version 13");
            }
            if (current_entity->mesh_renderer) {
                return fail(line_number, "entity contains more than one Mesh Renderer component");
            }
            MeshRendererComponent mesh;
            std::string primitive_ref;
            std::string texture_ref;
            if (!parse_quoted(stream, primitive_ref)
                || !parse_quoted(stream, texture_ref)
                || !(stream >> mesh.color[0] >> mesh.color[1] >> mesh.color[2] >> mesh.color[3])) {
                return fail(line_number, "invalid mesh_renderer component record");
            }
            const auto primitive = primitive_mesh_from_name(primitive_ref);
            if (!primitive) return fail(line_number, "unknown primitive mesh '" + primitive_ref + "'");
            mesh.primitive = *primitive;
            mesh.texture = resolve_texture(textures_by_name, texture_ref);
            if (texture_ref != kNoReference && mesh.texture == kInvalidTexture) {
                return fail(line_number, "mesh renderer texture reference '" + texture_ref + "' is not registered");
            }
            current_entity->mesh_renderer = mesh;
            continue;
        }

        if (command == "mesh_material_asset") {
            if (!current_entity || !current_entity->mesh_renderer) {
                return fail(line_number, "mesh_material_asset must follow a Mesh Renderer component");
            }
            if (loaded_scene_version < 15) {
                return fail(line_number, "mesh_material_asset records require scene format version 15");
            }
            std::string asset_id;
            std::string fallback_path;
            if (!(stream >> std::quoted(asset_id) >> std::quoted(fallback_path))) {
                return fail(line_number, "invalid mesh_material_asset record");
            }
            current_entity->mesh_renderer->material.asset_id = asset_id == kNoReference ? std::string{} : asset_id;
            current_entity->mesh_renderer->material.path = fallback_path == kNoReference ? std::filesystem::path{} : std::filesystem::path(fallback_path);
            continue;
        }

        if (command == "cylinder_collider") {
            if (!current_entity) {
                return fail(line_number, "cylinder_collider must appear inside an entity block");
            }
            if (loaded_scene_version < 6) {
                return fail(line_number, "cylinder_collider records require scene format version 6");
            }
            if (current_entity->cylinder_collider) {
                return fail(line_number, "entity contains more than one Cylinder Collider component");
            }
            CylinderColliderComponent collider;
            int trigger_value = 0;
            if (!(stream
                >> collider.radius
                >> collider.height
                >> collider.center.x
                >> collider.center.y
                >> collider.center.z
                >> trigger_value)) {
                return fail(line_number, "invalid cylinder_collider component record");
            }
            if (collider.radius <= 0.0f || collider.height <= 0.0f) {
                return fail(line_number, "cylinder collider radius and height must be positive");
            }
            if (trigger_value != 0 && trigger_value != 1) {
                return fail(line_number, "cylinder collider trigger value must be 0 or 1");
            }
            collider.is_trigger = trigger_value != 0;
            current_entity->cylinder_collider = collider;
            continue;
        }


        if (command == "point_light") {
            if (!current_entity) {
                return fail(line_number, "point_light must appear inside an entity block");
            }
            if (loaded_scene_version < 9) {
                return fail(line_number, "point_light records require scene format version 9");
            }
            if (current_entity->point_light) {
                return fail(line_number, "entity contains more than one Point Light component");
            }
            PointLightComponent light;
            if (!(stream
                >> light.color[0]
                >> light.color[1]
                >> light.color[2]
                >> light.color[3]
                >> light.intensity
                >> light.radius)) {
                return fail(line_number, "invalid point_light component record");
            }
            if (light.intensity < 0.0f || light.radius <= 0.0f) {
                return fail(line_number, "point light intensity must be non-negative and radius must be positive");
            }
            current_entity->point_light = light;
            continue;
        }

        if (command == "managed_script") {
            if (!current_entity) return fail(line_number, "managed_script must appear inside an entity block");
            if (loaded_scene_version < 10) return fail(line_number, "managed_script records require scene format version 10");
            if (loaded_scene_version == 10 && !current_entity->managed_scripts.empty()) {
                return fail(line_number, "scene v10 entities can contain only one C# Script component");
            }
            ManagedScriptComponent script;
            int enabled = 0;
            if (!parse_quoted(stream, script.class_name) || !(stream >> enabled) || (enabled != 0 && enabled != 1)) {
                return fail(line_number, "invalid managed_script component record");
            }
            if (script.class_name.empty()) return fail(line_number, "managed_script class name cannot be empty");
            std::string trailing;
            if (stream >> trailing) return fail(line_number, "managed_script has unexpected trailing data");
            script.enabled = enabled != 0;
            current_entity->managed_scripts.push_back(std::move(script));
            if (loaded_scene_version >= 11) current_managed_script = &current_entity->managed_scripts.back();
            continue;
        }

        if (command == "managed_field") {
            if (loaded_scene_version < 11) return fail(line_number, "managed_field records require scene format version 11");
            if (!current_entity || !current_managed_script) {
                return fail(line_number, "managed_field must appear inside a managed_script block");
            }
            ManagedScriptFieldValue field;
            if (!parse_quoted(stream, field.field_name)
                || !parse_quoted(stream, field.type_name)
                || !parse_quoted(stream, field.serialized_value)) {
                return fail(line_number, "invalid managed_field record");
            }
            if (field.field_name.empty() || field.type_name.empty()) {
                return fail(line_number, "managed_field name and type cannot be empty");
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
            if (loaded_scene_version < 11) return fail(line_number, "end_managed_script requires scene format version 11");
            if (!current_entity || !current_managed_script) {
                return fail(line_number, "end_managed_script without managed_script");
            }
            std::string trailing;
            if (stream >> trailing) return fail(line_number, "end_managed_script has unexpected trailing data");
            current_managed_script = nullptr;
            continue;
        }

        if (command == "endentity") {
            if (!current_entity) {
                return fail(line_number, "endentity without entity");
            }
            if (!current_entity_transform_seen) {
                return fail(line_number, "entity '" + current_entity->name + "' is missing its Transform component");
            }
            if (loaded_scene_version >= 7 && !current_entity_identity_seen) {
                return fail(line_number, "entity '" + current_entity->name + "' is missing its identity record");
            }
            current_entity = nullptr;
            current_entity_identity_seen = false;
            current_entity_prefab_source_seen = false;
            current_entity_parent_seen = false;
            current_entity_transform_seen = false;
            current_managed_script = nullptr;
            continue;
        }

        // Legacy v2-v4 sprite records migrate directly into current entities with a
        // Transform + Sprite Renderer component. This keeps old authored scenes usable.
        if (command == "sprite") {
            if (current_sector || current_entity) {
                return fail(line_number, "sprite must appear at scene top level");
            }
            if (loaded_scene_version < 2 || loaded_scene_version >= 5) {
                return fail(line_number, "legacy sprite records are supported only in scene versions 2-4");
            }

            Entity entity;
            SpriteRendererComponent sprite;
            std::string texture_ref;
            std::string clip_ref;

            if (loaded_scene_version >= 4) {
                int enabled_value = 1;
                if (!(stream >> entity.id >> enabled_value)
                    || !parse_quoted(stream, entity.name)
                    || !(stream
                        >> entity.transform.position.x
                        >> entity.transform.position.y
                        >> entity.transform.position.z
                        >> sprite.size.x
                        >> sprite.size.z)
                    || !parse_quoted(stream, texture_ref)) {
                    return fail(line_number, "invalid scene-v4 sprite record");
                }
                if (entity.id == kInvalidSceneObjectId) {
                    return fail(line_number, "sprite object id must be non-zero");
                }
                if (!loaded_entity_ids.insert(entity.id).second) {
                    return fail(line_number, "duplicate sprite/entity object id " + std::to_string(entity.id));
                }
                if (enabled_value != 0 && enabled_value != 1) {
                    return fail(line_number, "sprite enabled value must be 0 or 1");
                }
                entity.enabled = enabled_value != 0;
            } else {
                if (!parse_quoted(stream, entity.name)
                    || !(stream
                        >> entity.transform.position.x
                        >> entity.transform.position.y
                        >> entity.transform.position.z
                        >> sprite.size.x
                        >> sprite.size.z)
                    || !parse_quoted(stream, texture_ref)) {
                    return fail(line_number, "invalid sprite record");
                }
            }

            if (loaded_scene_version >= 3) {
                int paused_value = 0;
                if (!parse_quoted(stream, clip_ref)
                    || !(stream
                        >> entity.transform.rotation.y
                        >> sprite.animation_speed
                        >> sprite.animation_time_offset
                        >> paused_value
                        >> sprite.color[0]
                        >> sprite.color[1]
                        >> sprite.color[2]
                        >> sprite.color[3])) {
                    return fail(line_number, loaded_scene_version >= 4
                        ? "invalid scene-v4 sprite animation/appearance data"
                        : "invalid scene-v3 sprite record");
                }
                if (paused_value != 0 && paused_value != 1) {
                    return fail(line_number, "sprite paused value must be 0 or 1");
                }
                if (sprite.animation_speed < 0.0f) {
                    return fail(line_number, "sprite animation speed cannot be negative");
                }
                sprite.animation_paused = paused_value != 0;
                if (clip_ref == kNoReference) clip_ref.clear();
            } else {
                if (!(stream
                    >> sprite.color[0]
                    >> sprite.color[1]
                    >> sprite.color[2]
                    >> sprite.color[3])) {
                    return fail(line_number, "invalid scene-v2 sprite record");
                }
            }

            std::string trailing;
            if (stream >> trailing) {
                return fail(line_number, "sprite has unexpected trailing data");
            }
            if (entity.name.empty()) return fail(line_number, "sprite name cannot be empty");
            if (sprite.size.x <= 0.0f || sprite.size.z <= 0.0f) {
                return fail(line_number, "sprite size must be positive");
            }
            sprite.texture = resolve_texture(textures_by_name, texture_ref);
            if (texture_ref != kNoReference && sprite.texture == kInvalidTexture) {
                return fail(line_number, "sprite texture reference '" + texture_ref + "' is not registered");
            }
            sprite.animation_clip = clip_ref;
            entity.sprite_renderer = std::move(sprite);
            loaded_entities.push_back(std::move(entity));
            continue;
        }

        if (command == "sector") {
            if (current_sector || current_entity) {
                return fail(line_number, "nested sector/entity blocks are not allowed");
            }
            loaded_sectors.emplace_back();
            current_sector = &loaded_sectors.back();
            current_sector->source_line = line_number;

            if (!parse_quoted(stream, current_sector->sector.name)
                || !(stream >> current_sector->sector.floor_height >> current_sector->sector.ceiling_height)
                || !parse_quoted(stream, current_sector->floor_material)
                || !parse_quoted(stream, current_sector->ceiling_material)
                || !parse_quoted(stream, current_sector->wall_material)) {
                return fail(line_number, "invalid sector header");
            }
            if (current_sector->sector.name.empty()) {
                return fail(line_number, "sector name cannot be empty");
            }
            if (current_sector->sector.ceiling_height <= current_sector->sector.floor_height) {
                return fail(line_number, "sector ceiling must be above its floor");
            }
            continue;
        }

        if (command == "vertex") {
            if (!current_sector) {
                return fail(line_number, "vertex must appear inside a sector block");
            }
            Vec2 vertex;
            if (!(stream >> vertex.x >> vertex.z)) {
                return fail(line_number, "invalid vertex record");
            }
            current_sector->sector.vertices.push_back(vertex);
            continue;
        }

        if (command == "side") {
            if (!current_sector) {
                return fail(line_number, "side must appear inside a sector block");
            }
            std::string material_ref;
            int adjacent = -1;
            if (!parse_quoted(stream, material_ref) || !(stream >> adjacent)) {
                return fail(line_number, "invalid side record");
            }
            current_sector->sides.emplace_back(std::move(material_ref), adjacent);
            continue;
        }

        if (command == "endsector") {
            if (!current_sector) {
                return fail(line_number, "endsector without sector");
            }
            if (current_sector->sector.vertices.size() < 3) {
                return fail(current_sector->source_line, "sector '" + current_sector->sector.name + "' has fewer than 3 vertices");
            }
            if (!current_sector->sides.empty()
                && current_sector->sides.size() != current_sector->sector.vertices.size()) {
                return fail(current_sector->source_line,
                    "sector '" + current_sector->sector.name + "' side count does not match vertex count");
            }
            current_sector = nullptr;
            continue;
        }

        if (command == "end") {
            if (current_sector || current_entity) {
                return fail(line_number, "scene ended before the current block was closed");
            }
            end_seen = true;
            continue;
        }

        return fail(line_number, "unknown scene command '" + command + "'");
    }

    if (!header_seen) {
        return {false, "scene file is empty"};
    }
    if (current_sector) {
        return fail(current_sector->source_line, "unterminated sector block");
    }
    if (current_entity) {
        return {false, "unterminated entity block"};
    }
    if (!end_seen) {
        return {false, "scene file is missing final 'end' marker"};
    }
    if (!camera_seen) {
        return {false, "scene file is missing a camera record"};
    }

    // Older scene versions did not persist stable object ids. Assign them in
    // file order while migrating legacy sprites into current entities.
    if (loaded_scene_version < 4) {
        SceneObjectId migrated_id = 1;
        for (auto& entity : loaded_entities) {
            entity.id = migrated_id++;
            entity.enabled = true;
            loaded_entity_ids.insert(entity.id);
        }
    }

    // Resolve every cross-reference before mutating the live Scene. This keeps
    // loading transactional: malformed files leave the current scene intact.
    std::unordered_map<std::string, MaterialId> loaded_materials_by_name;
    for (std::size_t i = 0; i < loaded_materials.size(); ++i) {
        loaded_materials_by_name.emplace(loaded_materials[i].name, static_cast<MaterialId>(i));
    }

    std::unordered_map<std::string, std::size_t> loaded_clips_by_name;
    for (std::size_t i = 0; i < loaded_sprite_clips.size(); ++i) {
        loaded_clips_by_name.emplace(loaded_sprite_clips[i].name, i);
    }
    for (const auto& entity : loaded_entities) {
        if (!entity.sprite_renderer) continue;
        const std::string& clip_ref = entity.sprite_renderer->animation_clip;
        if (!clip_ref.empty() && !loaded_clips_by_name.contains(clip_ref)) {
            return {false, "entity '" + entity.name
                + "' references undefined animation clip '" + clip_ref + "'"};
        }
    }

    // Scene v14 hierarchy references are validated transactionally before the
    // live Scene is replaced. Older scenes simply have no parent records.
    std::unordered_map<SceneObjectId, const Entity*> loaded_entities_by_id;
    for (const auto& entity : loaded_entities) loaded_entities_by_id.emplace(entity.id, &entity);
    for (const auto& entity : loaded_entities) {
        if (entity.parent_id == kInvalidSceneObjectId) continue;
        if (!loaded_entities_by_id.contains(entity.parent_id)) {
            return {false, "entity '" + entity.name + "' references missing parent id " + std::to_string(entity.parent_id)};
        }
        std::unordered_set<SceneObjectId> chain;
        SceneObjectId current = entity.id;
        while (current != kInvalidSceneObjectId) {
            if (!chain.insert(current).second) {
                return {false, "entity hierarchy cycle detected at '" + entity.name + "'"};
            }
            const auto found = loaded_entities_by_id.find(current);
            if (found == loaded_entities_by_id.end()) break;
            current = found->second->parent_id;
        }
    }

    for (auto& pending : loaded_sectors) {
        const auto resolve_required = [&](const std::string& name, const char* label, MaterialId& destination) -> SceneIoResult {
            destination = resolve_material(loaded_materials_by_name, name);
            if (name != kNoReference && destination == kInvalidMaterial) {
                return fail(pending.source_line,
                    std::string(label) + " material reference '" + name + "' was not defined");
            }
            return {true, {}};
        };

        if (auto result = resolve_required(pending.floor_material, "floor", pending.sector.floor_material); !result) {
            return result;
        }
        if (auto result = resolve_required(pending.ceiling_material, "ceiling", pending.sector.ceiling_material); !result) {
            return result;
        }
        if (auto result = resolve_required(pending.wall_material, "wall", pending.sector.wall_material); !result) {
            return result;
        }

        pending.sector.sides.clear();
        for (const auto& [side_material_name, adjacent] : pending.sides) {
            SectorSide side;
            side.material = resolve_material(loaded_materials_by_name, side_material_name);
            if (side_material_name != kNoReference && side.material == kInvalidMaterial) {
                return fail(pending.source_line,
                    "side material reference '" + side_material_name + "' was not defined");
            }
            if (adjacent < -1 || adjacent >= static_cast<int>(loaded_sectors.size())) {
                return fail(pending.source_line,
                    "side adjacent sector index " + std::to_string(adjacent) + " is out of range");
            }
            side.adjacent_sector = adjacent;
            pending.sector.sides.push_back(side);
        }
    }

    // All parsing and reference validation succeeded. Replace the live scene in
    // one commit-like step while preserving the already-registered textures.
    scene.world.clear();
    for (const auto& texture : textures) {
        scene.world.add_texture(texture);
    }
    for (auto& material : loaded_materials) {
        scene.world.add_material(std::move(material));
    }
    for (auto& pending : loaded_sectors) {
        scene.world.add_sector(std::move(pending.sector));
    }

    scene.camera = loaded_camera;
    scene.sprite_clips = std::move(loaded_sprite_clips);
    scene.entities = std::move(loaded_entities);
    scene.refresh_object_id_allocator();
    const auto sprite_component_count = std::count_if(scene.entities.begin(), scene.entities.end(), [](const Entity& entity) {
        return entity.sprite_renderer.has_value();
    });
    const auto mesh_component_count = std::count_if(scene.entities.begin(), scene.entities.end(), [](const Entity& entity) {
        return entity.mesh_renderer.has_value();
    });
    const auto collider_component_count = std::count_if(scene.entities.begin(), scene.entities.end(), [](const Entity& entity) {
        return entity.cylinder_collider.has_value();
    });
    const auto light_component_count = std::count_if(scene.entities.begin(), scene.entities.end(), [](const Entity& entity) {
        return entity.point_light.has_value();
    });
    std::size_t managed_component_count = 0;
    for (const auto& entity : scene.entities) managed_component_count += entity.managed_scripts.size();
    return {true, "loaded " + std::to_string(scene.world.sectors().size()) + " sectors, "
        + std::to_string(scene.sprite_clips.size()) + " sprite clips, and "
        + std::to_string(scene.entities.size()) + " entities ("
        + std::to_string(sprite_component_count) + " sprite renderers, "
        + std::to_string(mesh_component_count) + " mesh renderers, "
        + std::to_string(collider_component_count) + " cylinder colliders, "
        + std::to_string(light_component_count) + " point lights, "
        + std::to_string(managed_component_count) + " C# scripts) from " + path.string()};
}

SceneIoResult load_scene_text_resilient(
    Scene& scene,
    const std::filesystem::path& path,
    std::vector<std::string>* warnings
) {
    constexpr std::string_view kMissingTexturePrefix = "texture reference '";
    constexpr std::size_t kMaxFallbackTextures = 64;
    std::size_t normalized_alias_count = 0;
    std::size_t fallback_count = 0;

    for (;;) {
        auto result = load_scene_text(scene, path);
        if (result) {
            if (normalized_alias_count != 0) {
                result.message += " | " + std::to_string(normalized_alias_count)
                    + " texture reference(s) matched imported asset names by normalized spelling";
            }
            if (fallback_count != 0) {
                result.message += " | " + std::to_string(fallback_count)
                    + " missing texture reference(s) replaced with visible placeholders";
            }
            return result;
        }

        const auto prefix = result.message.find(kMissingTexturePrefix);
        if (prefix == std::string::npos || fallback_count >= kMaxFallbackTextures) return result;
        const auto name_begin = prefix + kMissingTexturePrefix.size();
        const auto name_end = result.message.find('\'', name_begin);
        if (name_end == std::string::npos || name_end == name_begin) return result;
        const std::string missing_name = result.message.substr(name_begin, name_end - name_begin);

        const auto& textures = scene.world.textures();
        const bool already_present = std::any_of(textures.begin(), textures.end(), [&](const TextureData& texture) {
            return texture.name == missing_name;
        });
        if (already_present) return result; // avoid a retry loop if a different invariant failed.

        // Older/reference content can carry human-readable texture names such as
        // "Floor Tiles" while the project catalog imports floor_tiles.bmp as
        // "floor_tiles". Preserve the scene's serialized name by adding an alias
        // that reuses the already imported pixels. This is deliberately only a
        // resilient-load fallback: exact names still win, and ambiguous normalized
        // matches fall through to the visible missing-texture placeholder.
        if (auto alias = normalized_texture_alias(scene.world, missing_name)) {
            scene.world.add_texture(std::move(*alias));
            ++normalized_alias_count;
            continue;
        }

        scene.world.add_texture(make_missing_texture_placeholder(missing_name));
        ++fallback_count;
        if (warnings) {
            warnings->push_back("Scene texture '" + missing_name
                + "' is unavailable; using the Vespera missing-texture checkerboard until the asset is restored.");
        }
    }
}

SceneIoResult save_scene_text(const Scene& scene, const std::filesystem::path& path) {
    const auto validation = validate_scene(scene);
    if (scene_validation_has_errors(validation)) {
        for (const auto& issue : validation) {
            if (issue.severity == SceneValidationSeverity::Error) {
                return {false, "scene validation failed: " + issue.message};
            }
        }
    }

    std::unordered_set<SceneObjectId> entity_ids;
    for (const auto& entity : scene.entities) {
        if (entity.id == kInvalidSceneObjectId) {
            return {false, "cannot save entity '" + entity.name + "' with object id 0"};
        }
        if (!entity_ids.insert(entity.id).second) {
            return {false, "cannot save duplicate entity object id " + std::to_string(entity.id)};
        }
    }

    std::error_code directory_error;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), directory_error);
        if (directory_error) {
            return {false, "could not create scene directory: " + directory_error.message()};
        }
    }

    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        return {false, "could not open scene file for writing: " + path.string()};
    }

    output << std::setprecision(9);
    output << "sectorline_scene " << kSceneFormatVersion << "\n";
    output << "# Vespera Scene Text v15\n\n";

    const Camera& camera = scene.camera;
    output << "camera "
           << camera.position.x << ' '
           << camera.position.y << ' '
           << camera.position.z << ' '
           << camera.yaw << ' '
           << camera.pitch << ' '
           << camera.vertical_fov_degrees << ' '
           << camera.near_plane << ' '
           << camera.far_plane << "\n\n";

    for (const WorldMaterial& material : scene.world.materials()) {
        output << "material " << std::quoted(material.name) << ' '
               << material.color[0] << ' '
               << material.color[1] << ' '
               << material.color[2] << ' '
               << material.color[3] << ' '
               << std::quoted(texture_name(scene.world, material.texture)) << ' '
               << material.uv_scale.x << ' '
               << material.uv_scale.z << "\n";
    }

    output << '\n';
    for (const SpriteAnimationClip& clip : scene.sprite_clips) {
        if (!clip.valid()) {
            continue;
        }
        output << "sprite_clip " << std::quoted(clip.name) << ' '
               << clip.direction_count << ' '
               << clip.frame_count << ' '
               << clip.frames_per_second << ' '
               << (clip.loop ? 1 : 0);
        for (const TextureId texture : clip.textures) {
            output << ' ' << std::quoted(texture_name(scene.world, texture));
        }
        output << "\n";
    }

    output << '\n';
    for (const Sector& sector : scene.world.sectors()) {
        output << "sector " << std::quoted(sector.name) << ' '
               << sector.floor_height << ' '
               << sector.ceiling_height << ' '
               << std::quoted(material_name(scene.world, sector.floor_material)) << ' '
               << std::quoted(material_name(scene.world, sector.ceiling_material)) << ' '
               << std::quoted(material_name(scene.world, sector.wall_material)) << "\n";

        for (const Vec2 vertex : sector.vertices) {
            output << "  vertex " << vertex.x << ' ' << vertex.z << "\n";
        }

        if (!sector.sides.empty()) {
            for (const SectorSide& side : sector.sides) {
                output << "  side " << std::quoted(material_name(scene.world, side.material)) << ' '
                       << side.adjacent_sector << "\n";
            }
        }
        output << "endsector\n\n";
    }

    if (!scene.entities.empty()) {
        for (const Entity& entity : scene.entities) {
            output << "entity " << entity.id << ' ' << (entity.enabled ? 1 : 0) << ' ' << std::quoted(entity.name) << "\n";
            output << "  identity " << std::quoted(entity.tag) << ' ' << std::quoted(entity.layer) << "\n";
            if (entity.parent_id != kInvalidSceneObjectId) output << "  parent " << entity.parent_id << "\n";
            if (!entity.prefab_source.empty()) {
                const std::string fallback = entity.prefab_source.path.lexically_normal().generic_string();
                if (!entity.prefab_source.asset_id.empty() && !fallback.empty()) {
                    output << "  prefab_source_asset " << std::quoted(entity.prefab_source.asset_id)
                        << ' ' << std::quoted(fallback) << "\n";
                } else if (!fallback.empty()) {
                    output << "  prefab_source " << std::quoted(fallback) << "\n";
                }
            }
            const auto& t = entity.transform;
            output << "  transform "
                << t.position.x << ' ' << t.position.y << ' ' << t.position.z << ' '
                << t.rotation.x << ' ' << t.rotation.y << ' ' << t.rotation.z << ' '
                << t.scale.x << ' ' << t.scale.y << ' ' << t.scale.z << "\n";
            if (entity.sprite_renderer) {
                const auto& sprite = *entity.sprite_renderer;
                output << "  sprite_renderer "
                    << sprite.size.x << ' ' << sprite.size.z << ' '
                    << std::quoted(texture_name(scene.world, sprite.texture)) << ' '
                    << std::quoted(sprite.animation_clip.empty() ? kNoReference : sprite.animation_clip) << ' '
                    << sprite.animation_speed << ' '
                    << sprite.animation_time_offset << ' '
                    << (sprite.animation_paused ? 1 : 0) << ' '
                    << sprite.color[0] << ' ' << sprite.color[1] << ' '
                    << sprite.color[2] << ' ' << sprite.color[3] << "\n";
            }
            if (entity.mesh_renderer) {
                const auto& mesh = *entity.mesh_renderer;
                output << "  mesh_renderer "
                    << std::quoted(std::string(primitive_mesh_name(mesh.primitive))) << ' '
                    << std::quoted(texture_name(scene.world, mesh.texture)) << ' '
                    << mesh.color[0] << ' ' << mesh.color[1] << ' ' << mesh.color[2] << ' ' << mesh.color[3] << "\n";
                if (!mesh.material.empty()) {
                    output << "  mesh_material_asset "
                        << std::quoted(mesh.material.asset_id.empty() ? std::string(kNoReference) : mesh.material.asset_id) << ' '
                        << std::quoted(mesh.material.path.empty() ? std::string(kNoReference) : mesh.material.path.generic_string()) << "\n";
                }
            }
            if (entity.cylinder_collider) {
                const auto& collider = *entity.cylinder_collider;
                output << "  cylinder_collider "
                    << collider.radius << ' '
                    << collider.height << ' '
                    << collider.center.x << ' '
                    << collider.center.y << ' '
                    << collider.center.z << ' '
                    << (collider.is_trigger ? 1 : 0) << "\n";
            }
            if (entity.point_light) {
                const auto& light = *entity.point_light;
                output << "  point_light "
                    << light.color[0] << ' '
                    << light.color[1] << ' '
                    << light.color[2] << ' '
                    << light.color[3] << ' '
                    << light.intensity << ' '
                    << light.radius << "\n";
            }
            for (const auto& script : entity.managed_scripts) {
                output << "  managed_script " << std::quoted(script.class_name) << ' '
                    << (script.enabled ? 1 : 0) << "\n";
                for (const auto& field : script.fields) {
                    output << "    managed_field " << std::quoted(field.field_name) << ' '
                        << std::quoted(field.type_name) << ' '
                        << std::quoted(field.serialized_value) << "\n";
                }
                output << "  end_managed_script\n";
            }
            output << "endentity\n\n";
        }
    }

    output << "end\n";
    if (!output) {
        return {false, "failed while writing scene file: " + path.string()};
    }

    const auto sprite_component_count = std::count_if(scene.entities.begin(), scene.entities.end(), [](const Entity& entity) {
        return entity.sprite_renderer.has_value();
    });
    const auto mesh_component_count = std::count_if(scene.entities.begin(), scene.entities.end(), [](const Entity& entity) {
        return entity.mesh_renderer.has_value();
    });
    const auto collider_component_count = std::count_if(scene.entities.begin(), scene.entities.end(), [](const Entity& entity) {
        return entity.cylinder_collider.has_value();
    });
    const auto light_component_count = std::count_if(scene.entities.begin(), scene.entities.end(), [](const Entity& entity) {
        return entity.point_light.has_value();
    });
    std::size_t managed_component_count = 0;
    for (const auto& entity : scene.entities) managed_component_count += entity.managed_scripts.size();
    return {true, "saved " + std::to_string(scene.world.sectors().size()) + " sectors, "
        + std::to_string(scene.sprite_clips.size()) + " sprite clips, and "
        + std::to_string(scene.entities.size()) + " entities ("
        + std::to_string(sprite_component_count) + " sprite renderers, "
        + std::to_string(mesh_component_count) + " mesh renderers, "
        + std::to_string(collider_component_count) + " cylinder colliders, "
        + std::to_string(light_component_count) + " point lights, "
        + std::to_string(managed_component_count) + " C# scripts) to " + path.string()};
}

} // namespace vespera
