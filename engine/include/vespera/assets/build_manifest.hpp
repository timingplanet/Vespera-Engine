#pragma once

#include <vespera/assets/asset_catalog.hpp>
#include <vespera/project/project.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace vespera {

struct BuildAssetManifest {
    std::vector<const AssetRecord*> assets;
    std::vector<std::string> missing_roots;
    std::vector<std::string> stale_root_paths;
    std::vector<const AssetDependency*> broken_dependencies;

    [[nodiscard]] bool valid() const {
        return missing_roots.empty() && broken_dependencies.empty();
    }
};

// Computes the transitive asset closure for the startup scene plus explicit
// project build-includes. This is source-side groundwork for standalone cooking:
// packaging can consume the same deterministic closure instead of copying the
// entire Assets tree.
[[nodiscard]] BuildAssetManifest build_project_asset_manifest(
    const VesperaProject& project,
    const AssetCatalog& catalog
);

// Writes the deterministic source closure as a stable-ID/path index. 0.9's
// cooker/exporter can consume or evolve this instead of rediscovering assets.
[[nodiscard]] bool write_build_asset_index(
    const BuildAssetManifest& manifest,
    const std::filesystem::path& output_path,
    std::string* error_message = nullptr
);

} // namespace vespera
