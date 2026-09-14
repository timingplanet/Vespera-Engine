#pragma once

#include <vespera/assets/asset_catalog.hpp>
#include <vespera/project/project.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace vespera {

struct AssetMoveResult {
    bool ok = false;
    std::string message;
    std::string asset_id;
    std::filesystem::path old_path;
    std::filesystem::path new_path;
    std::size_t rml_references_rewritten = 0;
    std::size_t rml_files_rewritten = 0;
    explicit operator bool() const { return ok; }
};

// Moves/renames one project asset and its .vmeta sidecar as one guarded authoring
// workflow. The filesystem renames are not an atomic multi-file transaction; a
// sidecar failure triggers a checked rollback attempt. The source bytes are never
// transcoded, so the extension stays the same and stable identity follows .vmeta.
// Controlled moves also rewrite ordinary relative RML/RCSS href/src/url(...)
// references and rebase a moved RML/RCSS source file's own local references.
[[nodiscard]] AssetMoveResult move_project_asset(
    const AssetCatalog& catalog,
    std::string_view asset_id,
    const std::filesystem::path& destination_relative_path
);

struct AssetDeletePreflight {
    bool allowed = false;
    std::string asset_id;
    std::filesystem::path path;
    std::vector<std::string> blockers;

    explicit operator bool() const { return allowed; }
};

// Non-destructive safety check used by the editor today and by the future typed
// Editor Command/MCP layer. It considers project roots and catalog dependents.
[[nodiscard]] AssetDeletePreflight preflight_delete_project_asset(
    const AssetCatalog& catalog,
    const VesperaProject& project,
    std::string_view asset_id
);

struct AssetReferenceRepairReport {
    std::size_t project_references_repaired = 0;
    std::size_t descriptor_references_repaired = 0;
    std::size_t descriptor_files_rewritten = 0;
    std::vector<std::filesystem::path> modified_descriptor_files;
    std::vector<std::string> warnings;

    [[nodiscard]] std::size_t total_references_repaired() const {
        return project_references_repaired + descriptor_references_repaired;
    }
};

// Refreshes the human-readable fallback paths carried beside stable asset IDs.
// Project roots are updated in memory; authored Sprite Sheet v2 / Audio Clip v2
// descriptors are rewritten on disk. Stable IDs remain unchanged.
[[nodiscard]] AssetReferenceRepairReport repair_stable_asset_fallbacks(
    VesperaProject& project,
    const AssetCatalog& catalog
);

} // namespace vespera
