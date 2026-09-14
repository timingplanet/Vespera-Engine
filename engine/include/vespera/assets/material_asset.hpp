#pragma once

#include <vespera/assets/asset_reference.hpp>
#include <vespera/render/material.hpp>
#include <vespera/world/sector_world.hpp>

#include <filesystem>
#include <string>

namespace vespera {

class AssetCatalog;
class Scene;

struct MaterialAsset {
    int version = 1;
    std::string name = "Material";
    AssetReference base_texture;
    MaterialProperties properties{};
};

struct MaterialAssetIoResult {
    bool ok = false;
    MaterialAsset material{};
    std::string message;
    [[nodiscard]] explicit operator bool() const { return ok; }
};

struct MaterialHydrationReport {
    std::size_t resolved = 0;
    std::size_t missing_materials = 0;
    std::size_t missing_textures = 0;
    std::string message;
};

[[nodiscard]] MaterialAssetIoResult load_material_asset(const std::filesystem::path& path);
[[nodiscard]] MaterialAssetIoResult save_material_asset(const std::filesystem::path& path, const MaterialAsset& material);

// Resolve stable .slmat references into transient MeshRenderer caches. Authored
// Scene data keeps only the stable material AssetReference; render-facing cache
// fields are rebuilt after scene load/catalog refresh and are never serialized.
[[nodiscard]] MaterialHydrationReport hydrate_scene_materials(
    Scene& scene,
    const AssetCatalog& catalog
);

} // namespace vespera
