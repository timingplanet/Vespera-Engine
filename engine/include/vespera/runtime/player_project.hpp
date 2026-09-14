#pragma once

#include <filesystem>
#include <string>

namespace vespera {

class AssetCatalog;
struct AssetRecord;
struct VesperaProject;

struct PlayerProjectDiscoveryResult {
    bool ok = false;
    std::filesystem::path project_file;
    std::string message;
    [[nodiscard]] explicit operator bool() const { return ok; }
};

// Resolves the project used by the shared Vespera player. An explicit project
// wins. Otherwise the player expects exactly one .vesperaproject beside the
// executable, which is the normal exported-package layout.
[[nodiscard]] PlayerProjectDiscoveryResult discover_player_project(
    const std::filesystem::path& executable_path,
    const std::filesystem::path& requested_project = {}
);

struct RuntimeRmlSelection {
    const AssetRecord* document = nullptr;
    std::size_t candidate_count = 0;
    std::string message;
    [[nodiscard]] explicit operator bool() const { return document != nullptr; }
};

// Resolves the project-owned startup RML document. Project v8 uses the explicit
// startup_ui stable asset reference. Older projects without that field retain a
// compatibility fallback to the first authored RML build root.
[[nodiscard]] RuntimeRmlSelection select_runtime_rml_document(
    const VesperaProject& project,
    const AssetCatalog& catalog
);

} // namespace vespera
