#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace vespera {

class Scene;

struct SceneIoResult {
    bool ok = false;
    std::string message;

    explicit operator bool() const { return ok; }
};

// Vespera Scene Text v15 (.slscene)
//
// The scene file stores camera/world structure, sprite clips, entities/components, and texture references by name.
// Texture pixel payloads belong to the asset/resource layer and are intentionally
// not embedded in scene files. Loaders resolve texture names against textures
// already registered in Scene::world.
SceneIoResult load_scene_text(Scene& scene, const std::filesystem::path& path);

// Project/runtime convenience loader. If old Scene Text references a texture
// name that is no longer registered, keep loading with a visible placeholder
// carrying that same name. This prevents one missing texture from turning an
// otherwise valid project into an unloaded-project/empty-Play state.
SceneIoResult load_scene_text_resilient(
    Scene& scene,
    const std::filesystem::path& path,
    std::vector<std::string>* warnings = nullptr
);
SceneIoResult save_scene_text(const Scene& scene, const std::filesystem::path& path);

} // namespace vespera
