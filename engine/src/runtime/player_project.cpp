#include <vespera/runtime/player_project.hpp>

#include <vespera/assets/asset_catalog.hpp>
#include <vespera/project/project.hpp>

#include <algorithm>
#include <system_error>
#include <vector>

namespace vespera {
namespace {

std::filesystem::path absolute_normalized(const std::filesystem::path& path) {
    std::error_code ec;
    const auto absolute = std::filesystem::absolute(path, ec);
    return (ec ? path : absolute).lexically_normal();
}

bool valid_project_file(const std::filesystem::path& path) {
    std::error_code ec;
    return path.extension() == ".vesperaproject" && std::filesystem::is_regular_file(path, ec) && !ec;
}

} // namespace

PlayerProjectDiscoveryResult discover_player_project(
    const std::filesystem::path& executable_path,
    const std::filesystem::path& requested_project
) {
    if (!requested_project.empty()) {
        const auto candidate = absolute_normalized(requested_project);
        if (!valid_project_file(candidate)) {
            return {false, {}, "requested Vespera project does not exist or is not a .vesperaproject file: " + candidate.string()};
        }
        return {true, candidate, "using requested project: " + candidate.string()};
    }

    const auto executable = absolute_normalized(executable_path);
    const auto directory = executable.has_parent_path() ? executable.parent_path() : std::filesystem::current_path();
    std::vector<std::filesystem::path> projects;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(directory, ec), end; it != end && !ec; it.increment(ec)) {
        if (!it->is_regular_file(ec) || ec) continue;
        if (it->path().extension() == ".vesperaproject") projects.push_back(it->path().lexically_normal());
    }
    if (ec) {
        return {false, {}, "could not scan player directory for a Vespera project: " + ec.message()};
    }
    std::sort(projects.begin(), projects.end());
    if (projects.empty()) {
        return {false, {}, "no .vesperaproject file was found beside the Vespera player; pass --project <path> for a source project"};
    }
    if (projects.size() > 1u) {
        std::string names;
        for (const auto& project : projects) {
            if (!names.empty()) names += ", ";
            names += project.filename().string();
        }
        return {false, {}, "multiple .vesperaproject files were found beside the Vespera player (" + names + "); pass --project <path> explicitly"};
    }
    return {true, absolute_normalized(projects.front()), "discovered packaged project: " + projects.front().filename().string()};
}

RuntimeRmlSelection select_runtime_rml_document(
    const VesperaProject& project,
    const AssetCatalog& catalog
) {
    RuntimeRmlSelection result;

    if (!project.startup_ui.empty() || !project.startup_ui_asset_id.empty()) {
        const auto resolved = catalog.resolve_reference(project.startup_ui_reference());
        if (!resolved || !resolved.record) {
            result.message = "project startup UI could not be resolved from its stable asset reference";
            return result;
        }
        if (resolved.record->kind != AssetKind::RmlDocument) {
            result.message = "project startup UI does not resolve to an RML document: "
                + resolved.record->relative_path.generic_string();
            return result;
        }
        result.document = resolved.record;
        result.candidate_count = 1u;
        result.message = "startup RML resolved from project startup_ui: "
            + result.document->relative_path.generic_string();
        return result;
    }

    // Compatibility fallback for projects authored before project v8. Keep this
    // path readable during migration, but new projects should use startup_ui.
    for (std::size_t i = 0; i < project.build_includes.size(); ++i) {
        const auto resolved = catalog.resolve_reference(project.build_include_reference(i));
        if (!resolved || !resolved.record || resolved.record->kind != AssetKind::RmlDocument) continue;
        ++result.candidate_count;
        if (!result.document) result.document = resolved.record;
    }

    if (!result.document) {
        result.message = "project has no startup_ui and no legacy RML build root; player will run without a startup UI";
    } else if (result.candidate_count == 1u) {
        result.message = "legacy project has no startup_ui; inferred startup RML from build_includes: "
            + result.document->relative_path.generic_string();
    } else {
        result.message = "legacy project has no startup_ui and multiple RML build roots; using the first for compatibility: "
            + result.document->relative_path.generic_string();
    }
    return result;
}

} // namespace vespera
