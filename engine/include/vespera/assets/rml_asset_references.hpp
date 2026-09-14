#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vespera {

// One standards-friendly local path reference found inside authored RML/RCSS.
// Vespera deliberately keeps the file contents as ordinary relative paths rather
// than introducing an engine-specific URI scheme; stable asset IDs are used by
// authoring operations to decide which paths must be rewritten after a move.
struct RmlAssetPathReference {
    std::size_t value_offset = 0;
    std::size_t value_length = 0;
    std::string raw_value;
    std::filesystem::path project_path;
    std::string reason;
    bool root_relative = false;
};

struct RmlAssetPathMove {
    std::filesystem::path old_project_path;
    std::filesystem::path new_project_path;
};

struct RmlAssetRewriteResult {
    std::string text;
    std::size_t references_rewritten = 0;
};

// Extract local href/src/url(...) references and resolve them to Assets-relative
// project paths using the source document's current project-relative location.
[[nodiscard]] std::vector<RmlAssetPathReference> scan_rml_asset_path_references(
    std::string_view text,
    const std::filesystem::path& source_project_path,
    bool stylesheet
);

// Rebase local references when the RML/RCSS source itself moves and/or rewrite
// references to assets that moved. Query/fragment suffixes are preserved.
[[nodiscard]] RmlAssetRewriteResult rewrite_rml_asset_path_references(
    std::string_view text,
    const std::filesystem::path& old_source_project_path,
    const std::filesystem::path& new_source_project_path,
    std::span<const RmlAssetPathMove> moved_assets,
    bool stylesheet
);

} // namespace vespera
