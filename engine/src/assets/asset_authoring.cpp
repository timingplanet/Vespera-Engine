#include <vespera/assets/asset_authoring.hpp>
#include <vespera/assets/rml_asset_references.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <unordered_map>

namespace vespera {
namespace {

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool is_safe_relative_path(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) return false;
    const auto normalized = path.lexically_normal();
    if (normalized.empty() || normalized == ".") return false;
    for (const auto& part : normalized) {
        if (part == "..") return false;
    }
    return true;
}

bool atomic_replace_text(const std::filesystem::path& path, const std::string& text, std::string& error) {
    const auto temp = std::filesystem::path(path.string() + ".vespera_tmp");
    const auto backup = std::filesystem::path(path.string() + ".vespera_bak");
    std::error_code ec;
    std::filesystem::remove(temp, ec);
    ec.clear();
    std::filesystem::remove(backup, ec);
    ec.clear();

    {
        std::ofstream output(temp, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "could not create temporary authored asset file: " + temp.string();
            return false;
        }
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!output) {
            error = "could not write temporary authored asset file: " + temp.string();
            std::filesystem::remove(temp, ec);
            return false;
        }
    }

    std::filesystem::rename(path, backup, ec);
    if (ec) {
        error = "could not stage authored asset backup: " + ec.message();
        std::filesystem::remove(temp, ec);
        return false;
    }
    ec.clear();
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        const std::string failure = ec.message();
        std::error_code restore_ec;
        std::filesystem::rename(backup, path, restore_ec);
        std::error_code cleanup_ec;
        std::filesystem::remove(temp, cleanup_ec);
        error = "could not commit authored asset rewrite: " + failure;
        if (restore_ec) error += "; backup restore also failed: " + restore_ec.message();
        return false;
    }
    std::filesystem::remove(backup, ec);
    return true;
}

bool repair_descriptor_source_asset(
    const AssetRecord& descriptor,
    const AssetCatalog& catalog,
    std::size_t& repaired_count,
    std::string& error
) {
    std::ifstream input(descriptor.absolute_path, std::ios::binary);
    if (!input) {
        error = "could not open descriptor: " + descriptor.relative_path.generic_string();
        return false;
    }
    std::ostringstream original_stream;
    original_stream << input.rdbuf();
    input.close(); // Windows: release the descriptor before atomic rename/replace.
    const std::string original = original_stream.str();

    std::istringstream lines(original);
    std::ostringstream rewritten;
    std::string line;
    bool changed = false;
    while (std::getline(lines, line)) {
        std::string output_line = line;
        const auto first = line.find_first_not_of(" \t\r");
        if (first != std::string::npos && line[first] != '#') {
            std::istringstream parse(line.substr(first));
            std::string command;
            parse >> command;
            if (command == "source_asset" || command == "base_texture_asset" || command == "image_asset" || command == "font_asset") {
                std::string id;
                std::string fallback;
                if (parse >> std::quoted(id) >> std::quoted(fallback)) {
                    AssetReference reference{id, fallback};
                    const auto resolved = catalog.resolve_reference(reference);
                    if (resolved && resolved.resolved_by_id && resolved.stale_fallback_path) {
                        const std::string indent = line.substr(0, first);
                        std::ostringstream replacement;
                        replacement << indent << command << " " << std::quoted(id) << " "
                            << std::quoted(resolved.record->relative_path.lexically_normal().generic_string());
                        output_line = replacement.str();
                        ++repaired_count;
                        changed = true;
                    }
                }
            }
        }
        rewritten << output_line;
        if (!lines.eof()) rewritten << '\n';
    }

    if (!changed) return true;
    return atomic_replace_text(descriptor.absolute_path, rewritten.str(), error);
}


bool repair_scene_prefab_sources(
    const AssetRecord& scene,
    const AssetCatalog& catalog,
    std::size_t& repaired_count,
    std::string& error
) {
    std::ifstream input(scene.absolute_path, std::ios::binary);
    if (!input) {
        error = "could not open scene: " + scene.relative_path.generic_string();
        return false;
    }
    std::ostringstream original_stream;
    original_stream << input.rdbuf();
    input.close(); // Windows: release the descriptor before atomic rename/replace.
    const std::string original = original_stream.str();

    std::istringstream lines(original);
    std::ostringstream rewritten;
    std::string line;
    bool changed = false;
    while (std::getline(lines, line)) {
        std::string output_line = line;
        const auto first = line.find_first_not_of(" \t\r");
        if (first != std::string::npos && line[first] != '#') {
            std::istringstream parse(line.substr(first));
            std::string command;
            parse >> command;
            if (command == "prefab_source_asset" || command == "mesh_material_asset") {
                std::string id;
                std::string fallback;
                if (parse >> std::quoted(id) >> std::quoted(fallback)) {
                    const auto resolved = catalog.resolve_reference(AssetReference{id, fallback});
                    if (resolved && resolved.resolved_by_id && resolved.stale_fallback_path) {
                        const std::string indent = line.substr(0, first);
                        std::ostringstream replacement;
                        replacement << indent << command << " " << std::quoted(id) << " "
                            << std::quoted(resolved.record->relative_path.lexically_normal().generic_string());
                        output_line = replacement.str();
                        ++repaired_count;
                        changed = true;
                    }
                }
            }
        }
        rewritten << output_line;
        if (!lines.eof()) rewritten << '\n';
    }

    if (!changed) return true;
    return atomic_replace_text(scene.absolute_path, rewritten.str(), error);
}

} // namespace

AssetMoveResult move_project_asset(
    const AssetCatalog& catalog,
    std::string_view asset_id,
    const std::filesystem::path& destination_relative_path
) {
    AssetMoveResult result;
    const auto* source = catalog.find_by_id(asset_id);
    if (!source) {
        result.message = "asset ID is not present in the current project catalog";
        return result;
    }

    const auto destination_relative = destination_relative_path.lexically_normal();
    if (!is_safe_relative_path(destination_relative)) {
        result.message = "destination must be a project-relative path inside Assets";
        return result;
    }
    if (lowercase(destination_relative.extension().string()) != lowercase(source->relative_path.extension().string())) {
        result.message = "move/rename cannot change an asset file extension";
        return result;
    }
    if (!asset_kind_from_path(destination_relative)) {
        result.message = "destination does not have a recognized Vespera asset extension";
        return result;
    }

    const auto old_absolute = source->absolute_path.lexically_normal();
    const auto new_absolute = (catalog.root() / destination_relative).lexically_normal();
    const auto old_metadata = source->metadata_path.lexically_normal();
    const auto new_metadata = std::filesystem::path(new_absolute.string() + ".vmeta");
    result.asset_id = source->asset_id;
    result.old_path = source->relative_path;
    result.new_path = destination_relative;

    if (old_absolute == new_absolute) {
        result.ok = true;
        result.message = "asset already has the requested path";
        return result;
    }

    std::error_code ec;
    if (!std::filesystem::is_regular_file(old_absolute, ec) || ec) {
        result.message = "source asset file is missing";
        return result;
    }
    ec.clear();
    if (!std::filesystem::is_regular_file(old_metadata, ec) || ec) {
        result.message = "source .vmeta is missing; refresh/import the asset before moving it";
        return result;
    }
    ec.clear();
    if (std::filesystem::exists(new_absolute, ec) && !ec) {
        result.message = "destination asset already exists";
        return result;
    }
    ec.clear();
    if (std::filesystem::exists(new_metadata, ec) && !ec) {
        result.message = "destination .vmeta already exists";
        return result;
    }

    const auto is_rml_kind = [](AssetKind kind) {
        return kind == AssetKind::RmlDocument || kind == AssetKind::RmlStyleSheet;
    };

    // Most legacy path-only references still block a move. RML/RCSS is the
    // deliberate exception: the controlled Vespera move path can rewrite those
    // ordinary relative paths while it still knows the moved asset's stable ID.
    const bool same_stem = lowercase(destination_relative.stem().string())
        == lowercase(source->relative_path.stem().string());
    std::unordered_map<std::string, std::size_t> expected_rml_rewrites;
    for (const auto* dependency : catalog.dependents_of(source->asset_id)) {
        if (!dependency || !dependency->requested_asset_id.empty()) continue;
        const auto* dependent = catalog.find_by_id(dependency->source_asset_id);
        if (dependent && is_rml_kind(dependent->kind)) {
            ++expected_rml_rewrites[dependent->asset_id];
            continue;
        }
        const bool texture_name_reference = dependency->reason == "Material texture"
            || dependency->reason == "Embedded sprite clip texture"
            || dependency->reason == "Sprite renderer texture";
        if (texture_name_reference && same_stem) continue;
        result.message = "move/rename blocked by path-only dependent reference";
        if (dependent) result.message += ": " + dependent->relative_path.generic_string();
        if (!dependency->reason.empty()) result.message += " (" + dependency->reason + ")";
        result.message += "; migrate that reference to a stable asset ID first";
        return result;
    }

    struct RmlRewritePlan {
        const AssetRecord* record = nullptr;
        std::filesystem::path write_path;
        std::string original;
        std::string rewritten;
        std::size_t references_rewritten = 0;
    };
    std::vector<RmlRewritePlan> rml_rewrites;
    const std::array<RmlAssetPathMove, 1> moved_assets{{
        {source->relative_path.lexically_normal(), destination_relative}
    }};

    for (const auto& record : catalog.records()) {
        if (!is_rml_kind(record.kind)) continue;
        const bool source_itself_moves = record.asset_id == source->asset_id;
        const auto expected_it = expected_rml_rewrites.find(record.asset_id);
        const std::size_t expected = expected_it == expected_rml_rewrites.end() ? 0u : expected_it->second;
        if (!source_itself_moves && expected == 0u) continue;

        std::ifstream input(record.absolute_path, std::ios::binary);
        if (!input) {
            result.message = "could not open RML/RCSS dependent before asset move: " + record.relative_path.generic_string();
            return result;
        }
        std::ostringstream stream;
        stream << input.rdbuf();
        input.close();
        const std::string original = stream.str();
        if (source_itself_moves) {
            const auto source_references = scan_rml_asset_path_references(
                original, record.relative_path, record.kind == AssetKind::RmlStyleSheet);
            if (std::any_of(source_references.begin(), source_references.end(), [](const auto& reference) {
                    return reference.root_relative;
                })) {
                result.message = "move/rename cannot safely rebase a leading-slash RML/RCSS reference in "
                    + record.relative_path.generic_string()
                    + "; use an ordinary relative path before moving this UI asset";
                return result;
            }
        }
        const auto new_source_path = source_itself_moves ? destination_relative : record.relative_path;
        const auto rewritten = rewrite_rml_asset_path_references(
            original,
            record.relative_path,
            new_source_path,
            moved_assets,
            record.kind == AssetKind::RmlStyleSheet
        );
        if (rewritten.references_rewritten < expected) {
            result.message = "move/rename could not safely rewrite every RML/RCSS dependent reference in "
                + record.relative_path.generic_string();
            return result;
        }
        if (rewritten.references_rewritten != 0u) {
            rml_rewrites.push_back({
                &record,
                source_itself_moves ? new_absolute : record.absolute_path,
                original,
                rewritten.text,
                rewritten.references_rewritten,
            });
        }
    }

    std::filesystem::create_directories(new_absolute.parent_path(), ec);
    if (ec) {
        result.message = "could not create destination folder: " + ec.message();
        return result;
    }

    std::filesystem::rename(old_absolute, new_absolute, ec);
    if (ec) {
        result.message = "could not move asset file: " + ec.message();
        return result;
    }
    ec.clear();
    std::filesystem::rename(old_metadata, new_metadata, ec);
    if (ec) {
        const std::string metadata_failure = ec.message();
        std::error_code rollback_ec;
        std::filesystem::rename(new_absolute, old_absolute, rollback_ec);
        if (rollback_ec) {
            result.message = "could not move .vmeta sidecar and asset rollback also failed: "
                + metadata_failure + "; rollback: " + rollback_ec.message();
        } else {
            result.message = "could not move .vmeta sidecar; source asset was rolled back: " + metadata_failure;
        }
        return result;
    }

    std::vector<std::size_t> applied_rewrites;
    std::size_t rewritten_references = 0;
    for (std::size_t i = 0; i < rml_rewrites.size(); ++i) {
        std::string rewrite_error;
        if (!atomic_replace_text(rml_rewrites[i].write_path, rml_rewrites[i].rewritten, rewrite_error)) {
            std::string rollback_failures;
            for (auto it = applied_rewrites.rbegin(); it != applied_rewrites.rend(); ++it) {
                std::string restore_error;
                if (!atomic_replace_text(rml_rewrites[*it].write_path, rml_rewrites[*it].original, restore_error)) {
                    if (!rollback_failures.empty()) rollback_failures += "; ";
                    rollback_failures += restore_error;
                }
            }

            std::error_code metadata_rollback_ec;
            std::filesystem::rename(new_metadata, old_metadata, metadata_rollback_ec);
            std::error_code asset_rollback_ec;
            std::filesystem::rename(new_absolute, old_absolute, asset_rollback_ec);

            result.message = "asset move RML/RCSS rewrite failed and the move was rolled back: " + rewrite_error;
            if (!rollback_failures.empty()) result.message += "; dependent restore failure(s): " + rollback_failures;
            if (metadata_rollback_ec) result.message += "; .vmeta rollback failed: " + metadata_rollback_ec.message();
            if (asset_rollback_ec) result.message += "; asset rollback failed: " + asset_rollback_ec.message();
            return result;
        }
        applied_rewrites.push_back(i);
        rewritten_references += rml_rewrites[i].references_rewritten;
    }

    result.rml_references_rewritten = rewritten_references;
    result.rml_files_rewritten = rml_rewrites.size();
    result.ok = true;
    result.message = "asset moved with stable ID preserved: " + result.old_path.generic_string()
        + " -> " + result.new_path.generic_string();
    if (result.rml_references_rewritten != 0u) {
        result.message += "; rewrote " + std::to_string(result.rml_references_rewritten)
            + " RML/RCSS path reference(s) across " + std::to_string(result.rml_files_rewritten) + " file(s)";
    }
    return result;
}


AssetDeletePreflight preflight_delete_project_asset(
    const AssetCatalog& catalog,
    const VesperaProject& project,
    std::string_view asset_id
) {
    AssetDeletePreflight result;
    result.asset_id = std::string(asset_id);
    const auto* record = catalog.find_by_id(asset_id);
    if (!record) {
        result.blockers.push_back("asset ID is not present in the current project catalog");
        return result;
    }
    result.path = record->relative_path;

    const auto startup = catalog.resolve_reference(project.startup_scene_reference());
    if (startup && startup.record->asset_id == record->asset_id)
        result.blockers.push_back("asset is the project startup scene");

    const auto startup_ui = catalog.resolve_reference(project.startup_ui_reference());
    if (startup_ui && startup_ui.record->asset_id == record->asset_id)
        result.blockers.push_back("asset is the project startup UI");

    const auto lua_entry = catalog.resolve_reference(project.lua_entry_reference());
    if (lua_entry && lua_entry.record->asset_id == record->asset_id)
        result.blockers.push_back("asset is the project Lua entry script");

    const auto game_icon = catalog.resolve_reference(project.game_icon_reference());
    if (game_icon && game_icon.record->asset_id == record->asset_id)
        result.blockers.push_back("asset is the project game icon");

    const std::size_t build_count = (std::max)(project.build_includes.size(), project.build_include_asset_ids.size());
    for (std::size_t i = 0; i < build_count; ++i) {
        const auto resolved = catalog.resolve_reference(project.build_include_reference(i));
        if (resolved && resolved.record->asset_id == record->asset_id) {
            result.blockers.push_back("asset is an explicit standalone-build root");
            break;
        }
    }

    for (const auto* dependency : catalog.dependents_of(record->asset_id)) {
        if (!dependency) continue;
        std::string blocker = "referenced by ";
        if (const auto* source = catalog.find_by_id(dependency->source_asset_id))
            blocker += source->relative_path.generic_string();
        else blocker += dependency->source_asset_id;
        if (!dependency->reason.empty()) blocker += " (" + dependency->reason + ")";
        result.blockers.push_back(std::move(blocker));
    }

    std::sort(result.blockers.begin(), result.blockers.end());
    result.blockers.erase(std::unique(result.blockers.begin(), result.blockers.end()), result.blockers.end());
    result.allowed = result.blockers.empty();
    return result;
}

AssetReferenceRepairReport repair_stable_asset_fallbacks(
    VesperaProject& project,
    const AssetCatalog& catalog
) {
    AssetReferenceRepairReport report;

    const auto startup = catalog.resolve_reference(project.startup_scene_reference());
    if (startup && startup.resolved_by_id && startup.stale_fallback_path) {
        project.startup_scene = startup.record->relative_path;
        ++report.project_references_repaired;
    }

    const auto startup_ui = catalog.resolve_reference(project.startup_ui_reference());
    if (startup_ui && startup_ui.resolved_by_id && startup_ui.stale_fallback_path) {
        project.startup_ui = startup_ui.record->relative_path;
        ++report.project_references_repaired;
    }

    const auto lua_entry = catalog.resolve_reference(project.lua_entry_reference());
    if (lua_entry && lua_entry.resolved_by_id && lua_entry.stale_fallback_path) {
        project.lua_entry = lua_entry.record->relative_path;
        ++report.project_references_repaired;
    }

    const auto game_icon = catalog.resolve_reference(project.game_icon_reference());
    if (game_icon && game_icon.resolved_by_id && game_icon.stale_fallback_path) {
        project.game_icon = game_icon.record->relative_path;
        ++report.project_references_repaired;
    }

    const std::size_t build_count = (std::max)(project.build_includes.size(), project.build_include_asset_ids.size());
    if (project.build_includes.size() < build_count) project.build_includes.resize(build_count);
    if (project.build_include_asset_ids.size() < build_count) project.build_include_asset_ids.resize(build_count);
    for (std::size_t i = 0; i < build_count; ++i) {
        const auto resolved = catalog.resolve_reference(project.build_include_reference(i));
        if (resolved && resolved.resolved_by_id && resolved.stale_fallback_path) {
            project.build_includes[i] = resolved.record->relative_path;
            ++report.project_references_repaired;
        }
    }

    for (const auto& descriptor : catalog.records()) {
        if (descriptor.kind != AssetKind::SpriteSheet && descriptor.kind != AssetKind::AudioClip
            && descriptor.kind != AssetKind::Material && descriptor.kind != AssetKind::Scene
            && descriptor.kind != AssetKind::EntityPrefab && descriptor.kind != AssetKind::UiDocument) continue;
        std::size_t repaired_in_file = 0;
        std::string error;
        const bool repaired = (descriptor.kind == AssetKind::Scene || descriptor.kind == AssetKind::EntityPrefab)
            ? repair_scene_prefab_sources(descriptor, catalog, repaired_in_file, error)
            : repair_descriptor_source_asset(descriptor, catalog, repaired_in_file, error);
        if (!repaired) {
            report.warnings.push_back(std::move(error));
            continue;
        }
        if (repaired_in_file != 0) {
            report.descriptor_references_repaired += repaired_in_file;
            ++report.descriptor_files_rewritten;
            report.modified_descriptor_files.push_back(descriptor.relative_path);
        }
    }

    return report;
}

} // namespace vespera
