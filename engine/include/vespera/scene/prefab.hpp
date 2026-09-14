#pragma once

#include <vespera/scene/scene.hpp>

#include <filesystem>
#include <string>

namespace vespera {

struct EntityPrefab {
    // The prototype always carries object id 0. Instantiation allocates a fresh
    // scene id and optionally records the source asset reference on the new entity.
    Entity prototype;
};

struct PrefabIoResult {
    bool ok = false;
    std::string message;

    explicit operator bool() const { return ok; }
};

PrefabIoResult load_entity_prefab(
    const Scene& resource_context,
    EntityPrefab& prefab,
    const std::filesystem::path& path
);

PrefabIoResult save_entity_prefab(
    const Scene& resource_context,
    const Entity& entity,
    const std::filesystem::path& path
);

Entity* instantiate_entity_prefab(
    Scene& scene,
    const EntityPrefab& prefab,
    std::string name_override = {},
    AssetReference source_asset = {}
);

} // namespace vespera
