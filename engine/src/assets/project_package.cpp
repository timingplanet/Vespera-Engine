#include <vespera/assets/project_package.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <cstdlib>
#include <map>
#include <cstdio>
#include <regex>
#include <sstream>
#include <tuple>
#include <system_error>

namespace vespera {

bool valid_package_configuration(std::string_view configuration) {
    return configuration == "Debug" || configuration == "Development" || configuration == "Release";
}

std::string_view native_configuration_for_package(std::string_view configuration) {
    if (configuration == "Release") return "Release";
    if (configuration == "Development") return "RelWithDebInfo";
    return "Debug";
}

namespace {

std::filesystem::path absolute_normalized(const std::filesystem::path& path) {
    std::error_code ec;
    const auto absolute = std::filesystem::absolute(path, ec);
    return (ec ? path : absolute).lexically_normal();
}

bool is_same_or_parent(const std::filesystem::path& parent, const std::filesystem::path& child) {
    const auto p = absolute_normalized(parent);
    const auto c = absolute_normalized(child);
    auto pit = p.begin();
    auto cit = c.begin();
    for (; pit != p.end(); ++pit, ++cit) {
        if (cit == c.end() || *pit != *cit) return false;
    }
    return true;
}

bool safe_output_directory(const VesperaProject& project, const std::filesystem::path& output, std::string& error) {
    if (output.empty()) {
        error = "package output directory is empty";
        return false;
    }
    const auto out = absolute_normalized(output);
    const auto root = absolute_normalized(project.root_directory);
    const auto assets = absolute_normalized(project.assets_root());
    if (out == root || out == assets || is_same_or_parent(out, root) || is_same_or_parent(out, assets)
        || is_same_or_parent(assets, out)) {
        error = "refusing package output that is the project/assets directory or one of its parents: " + out.string();
        return false;
    }
    return true;
}

bool copy_regular_file(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    std::string& error
) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(source, ec) || ec) {
        error = "source file does not exist: " + source.string();
        return false;
    }
    std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec) {
        error = "could not create package directory '" + destination.parent_path().string() + "': " + ec.message();
        return false;
    }
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        error = "could not copy '" + source.string() + "' -> '" + destination.string() + "': " + ec.message();
        return false;
    }
    return true;
}

std::size_t copy_directory_recursive(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    std::string& error
) {
    std::error_code ec;
    if (!std::filesystem::is_directory(source, ec) || ec) {
        error = "source directory does not exist: " + source.string();
        return 0;
    }
    std::size_t copied = 0;
    for (std::filesystem::recursive_directory_iterator it(source, ec), end; it != end && !ec; it.increment(ec)) {
        if (!it->is_regular_file(ec) || ec) continue;
        const auto relative = std::filesystem::relative(it->path(), source, ec);
        if (ec) break;
        std::string copy_error;
        if (!copy_regular_file(it->path(), destination / relative, copy_error)) {
            error = std::move(copy_error);
            return 0;
        }
        ++copied;
    }
    if (ec) {
        error = "failed while traversing directory '" + source.string() + "': " + ec.message();
        return 0;
    }
    return copied;
}

std::uintmax_t file_size_or_zero(const std::filesystem::path& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    return ec ? 0 : size;
}

std::uintmax_t directory_payload_bytes(const std::filesystem::path& root) {
    std::uintmax_t total = 0;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec)) {
        if (!it->is_regular_file(ec) || ec) continue;
        total += file_size_or_zero(it->path());
    }
    return total;
}

bool is_regular_file_clean(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec) && !ec;
}

bool patch_windows_pe_subsystem(
    const std::filesystem::path& executable,
    std::uint16_t subsystem,
    std::string& error
) {
    std::fstream io(executable, std::ios::binary | std::ios::in | std::ios::out);
    if (!io) {
        error = "could not open packaged executable for Windows subsystem patch: " + executable.string();
        return false;
    }

    std::array<unsigned char, 2> mz{};
    io.read(reinterpret_cast<char*>(mz.data()), static_cast<std::streamsize>(mz.size()));
    if (!io || mz[0] != 'M' || mz[1] != 'Z') {
        error = "packaged Release runtime is not a Windows PE executable: " + executable.string();
        return false;
    }

    io.seekg(0x3c, std::ios::beg);
    std::array<unsigned char, 4> offset_bytes{};
    io.read(reinterpret_cast<char*>(offset_bytes.data()), static_cast<std::streamsize>(offset_bytes.size()));
    if (!io) {
        error = "could not read PE header offset from packaged runtime: " + executable.string();
        return false;
    }
    const std::uint32_t pe_offset = static_cast<std::uint32_t>(offset_bytes[0])
        | (static_cast<std::uint32_t>(offset_bytes[1]) << 8u)
        | (static_cast<std::uint32_t>(offset_bytes[2]) << 16u)
        | (static_cast<std::uint32_t>(offset_bytes[3]) << 24u);

    io.seekg(static_cast<std::streamoff>(pe_offset), std::ios::beg);
    std::array<unsigned char, 4> signature{};
    io.read(reinterpret_cast<char*>(signature.data()), static_cast<std::streamsize>(signature.size()));
    if (!io || signature != std::array<unsigned char, 4>{'P', 'E', 0, 0}) {
        error = "packaged Release runtime has an invalid PE signature: " + executable.string();
        return false;
    }

    // PE signature (4) + COFF header (20) + Optional Header Subsystem offset
    // (68 for both PE32 and PE32+). 2 == IMAGE_SUBSYSTEM_WINDOWS_GUI.
    constexpr std::streamoff kSubsystemFromPe = 4 + 20 + 68;
    io.seekp(static_cast<std::streamoff>(pe_offset) + kSubsystemFromPe, std::ios::beg);
    const std::array<unsigned char, 2> value{{
        static_cast<unsigned char>(subsystem & 0xffu),
        static_cast<unsigned char>((subsystem >> 8u) & 0xffu),
    }};
    io.write(reinterpret_cast<const char*>(value.data()), static_cast<std::streamsize>(value.size()));
    io.flush();
    if (!io) {
        error = "could not write Windows subsystem to packaged Release runtime: " + executable.string();
        return false;
    }
    return true;
}

bool validate_managed_stage(
    const VesperaProject& project,
    const ProjectPackageOptions& options,
    std::string& error
) {
    if (project.managed_assembly.empty()) return true;
    if (options.managed_directory.empty()) {
        error = "project declares managed assembly '" + project.managed_assembly
            + "' but no staged managed runtime directory was supplied";
        return false;
    }
    const std::array<std::filesystem::path, 3> required{{
        options.managed_directory / "Vespera.Managed.runtimeconfig.json",
        options.managed_directory / "Vespera.NET.dll",
        options.managed_directory / (project.managed_assembly + ".dll"),
    }};
    for (const auto& path : required) {
        if (!is_regular_file_clean(path)) {
            error = "managed export stage is incomplete; required file is missing: " + path.string();
            return false;
        }
    }
    return true;
}

bool contains_hostfxr(const std::filesystem::path& dotnet_root) {
    std::error_code ec;
    const auto fxr_root = dotnet_root / "host" / "fxr";
    if (!std::filesystem::is_directory(fxr_root, ec) || ec) return false;
    for (std::filesystem::recursive_directory_iterator it(fxr_root, ec), end; it != end && !ec; it.increment(ec)) {
        if (it->is_regular_file(ec) && !ec && it->path().filename() == "hostfxr.dll") return true;
    }
    return false;
}

bool validate_packaged_payload(
    const VesperaProject& project,
    const ProjectPackageOptions& options,
    const ProjectPackageResult& result,
    std::string& error
) {
    if (!is_regular_file_clean(result.packaged_project)) {
        error = "package self-check failed: packaged .vesperaproject is missing";
        return false;
    }
    if (!options.runtime_executable.empty() && !is_regular_file_clean(result.packaged_runtime)) {
        error = "package self-check failed: packaged runtime executable is missing";
        return false;
    }
    if (!project.managed_assembly.empty()) {
        const auto managed = result.package_directory / "managed";
        const std::array<std::filesystem::path, 3> required{{
            managed / "Vespera.Managed.runtimeconfig.json",
            managed / "Vespera.NET.dll",
            managed / (project.managed_assembly + ".dll"),
        }};
        for (const auto& path : required) {
            if (!is_regular_file_clean(path)) {
                error = "package self-check failed: managed payload is missing " + path.filename().string();
                return false;
            }
        }
        if (options.managed_deployment == ManagedDeploymentMode::Portable) {
            const auto dotnet = result.package_directory / "dotnet";
            if (!contains_hostfxr(dotnet)) {
                error = "package self-check failed: portable managed payload has no host/fxr/*/hostfxr.dll";
                return false;
            }
            const auto runtime = dotnet / "shared" / "Microsoft.NETCore.App" / result.dotnet_runtime_version;
            std::error_code ec;
            if (result.dotnet_runtime_version.empty() || !std::filesystem::is_directory(runtime, ec) || ec) {
                error = "package self-check failed: portable managed payload has no selected Microsoft.NETCore.App runtime";
                return false;
            }
        }
    }

    if (!options.runtime_executable.empty() && options.runtime_executable.stem() == "vespera_player") {
        const auto branding = result.package_directory / "branding";
        for (const char* filename : {"vespera_icon_window.png", "vespera_splash.png", "vespera_logo_sting.wav"}) {
            if (!is_regular_file_clean(branding / filename)) {
                error = std::string("package self-check failed: shared player branding payload is missing ") + filename;
                return false;
            }
        }
        const auto legal = result.package_directory / "legal";
        for (const char* filename : {"Vespera.LICENSE.txt", "Vespera.ThirdPartyNotices.md"}) {
            if (!is_regular_file_clean(legal / filename)) {
                error = std::string("package self-check failed: shared player legal payload is missing ") + filename;
                return false;
            }
        }
    }
    return true;
}

std::filesystem::path packaged_runtime_filename(const VesperaProject& project, const std::filesystem::path& source) {
    std::string name = project.executable_name.empty() ? project.name : project.executable_name;
    for (char& c : name) {
        const bool allowed = std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-';
        if (!allowed) c = '-';
    }
    while (!name.empty() && name.back() == '-') name.pop_back();
    if (name.empty()) name = "VesperaGame";
    std::filesystem::path filename(name);
    if (filename.extension().empty() && !source.extension().empty()) filename.replace_extension(source.extension());
    return filename.filename();
}

std::tuple<int,int,int> version_triplet(std::string text) {
    int a=0,b=0,c=0;
    std::sscanf(text.c_str(), "%d.%d.%d", &a, &b, &c);
    return {a,b,c};
}

std::string runtime_version_from_config(const std::filesystem::path& managed_directory) {
    const auto path = managed_directory / "Vespera.Managed.runtimeconfig.json";
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::ostringstream text; text << input.rdbuf();
    const std::regex pattern(R"re("version"\s*:\s*"([0-9]+\.[0-9]+\.[0-9]+[^"]*)")re");
    std::smatch match;
    const std::string value = text.str();
    return std::regex_search(value, match, pattern) && match.size() >= 2 ? match[1].str() : std::string{};
}

void append_env_root(std::vector<std::filesystem::path>& roots, const char* name) {
    if (const char* value = std::getenv(name); value && *value) roots.emplace_back(value);
}

std::vector<std::filesystem::path> candidate_dotnet_roots(const ProjectPackageOptions& options) {
    std::vector<std::filesystem::path> roots;
    if (!options.dotnet_root.empty()) roots.push_back(options.dotnet_root);
    append_env_root(roots, "DOTNET_ROOT");
    append_env_root(roots, "DOTNET_ROOT_X64");
#ifdef _WIN32
    if (const char* p = std::getenv("ProgramW6432"); p && *p) roots.emplace_back(std::filesystem::path(p) / "dotnet");
    if (const char* p = std::getenv("ProgramFiles"); p && *p) roots.emplace_back(std::filesystem::path(p) / "dotnet");
#else
    roots.emplace_back("/usr/share/dotnet");
    roots.emplace_back("/usr/local/share/dotnet");
#endif
    for (auto& root : roots) {
        if (root.filename() != "dotnet" && std::filesystem::is_directory(root / "dotnet")) root /= "dotnet";
        root = root.lexically_normal();
    }

    // Preserve caller intent: an explicit -DotnetRoot must outrank ambient
    // machine installations and environment fallbacks. Sorting these paths
    // made Windows machines prefer C:\Program Files\dotnet over a supplied
    // project/test root simply because it sorted earlier lexicographically.
    std::vector<std::filesystem::path> unique_roots;
    unique_roots.reserve(roots.size());
    for (const auto& root : roots) {
        if (std::find(unique_roots.begin(), unique_roots.end(), root) == unique_roots.end()) {
            unique_roots.push_back(root);
        }
    }
    return unique_roots;
}

struct DotnetSelection {
    std::filesystem::path root;
    std::filesystem::path fxr_dir;
    std::filesystem::path runtime_dir;
    std::string fxr_version;
    std::string runtime_version;
};

DotnetSelection select_dotnet_runtime(const ProjectPackageOptions& options) {
    const std::string requested = runtime_version_from_config(options.managed_directory);
    const int requested_major = std::get<0>(version_triplet(requested));
    for (const auto& root : candidate_dotnet_roots(options)) {
        std::error_code ec;
        const auto runtime_root = root / "shared" / "Microsoft.NETCore.App";
        const auto fxr_root = root / "host" / "fxr";
        if (!std::filesystem::is_directory(runtime_root, ec) || ec) continue;
        ec.clear();
        if (!std::filesystem::is_directory(fxr_root, ec) || ec) continue;

        std::vector<std::filesystem::path> runtimes, fxrs;
        for (const auto& entry : std::filesystem::directory_iterator(runtime_root, ec)) {
            if (entry.is_directory()) runtimes.push_back(entry.path());
        }
        ec.clear();
        for (const auto& entry : std::filesystem::directory_iterator(fxr_root, ec)) {
            if (entry.is_directory()) fxrs.push_back(entry.path());
        }
        const auto by_version = [](const auto& a, const auto& b) {
            return version_triplet(a.filename().string()) < version_triplet(b.filename().string());
        };
        std::sort(runtimes.begin(), runtimes.end(), by_version);
        std::sort(fxrs.begin(), fxrs.end(), by_version);
        if (runtimes.empty() || fxrs.empty()) continue;

        auto runtime_it = runtimes.end();
        if (!requested.empty()) {
            runtime_it = std::find_if(runtimes.begin(), runtimes.end(), [&](const auto& path) {
                return path.filename().string() == requested;
            });
        }
        if (runtime_it == runtimes.end()) {
            for (auto it = runtimes.rbegin(); it != runtimes.rend(); ++it) {
                const int major = std::get<0>(version_triplet(it->filename().string()));
                if ((requested_major == 0 && major >= 8) || major == requested_major) {
                    runtime_it = std::prev(it.base());
                    break;
                }
            }
        }
        if (runtime_it == runtimes.end()) continue;
        const int runtime_major = std::get<0>(version_triplet(runtime_it->filename().string()));
        auto fxr_it = fxrs.end();
        for (auto it = fxrs.rbegin(); it != fxrs.rend(); ++it) {
            if (std::get<0>(version_triplet(it->filename().string())) == runtime_major) {
                fxr_it = std::prev(it.base());
                break;
            }
        }
        if (fxr_it == fxrs.end()) fxr_it = std::prev(fxrs.end());
        return {root, *fxr_it, *runtime_it, fxr_it->filename().string(), runtime_it->filename().string()};
    }
    return {};
}

std::size_t copy_portable_dotnet_runtime(
    const ProjectPackageOptions& options,
    const std::filesystem::path& destination,
    std::string& runtime_version,
    std::string& error
) {
    const auto selected = select_dotnet_runtime(options);
    if (selected.root.empty()) {
        error = "portable managed deployment requested, but a compatible .NET 8+ runtime could not be located";
        return 0;
    }
    std::size_t copied = 0;
    const auto fxr_target = destination / "host" / "fxr" / selected.fxr_version;
    const auto runtime_target = destination / "shared" / "Microsoft.NETCore.App" / selected.runtime_version;
    std::string copy_error;
    const auto copy_tree = [&](const auto& src, const auto& dst, std::size_t& count, std::string& err) -> bool {
        std::error_code ec;
        for (std::filesystem::recursive_directory_iterator it(src, ec), end; it != end && !ec; it.increment(ec)) {
            if (!it->is_regular_file(ec) || ec) continue;
            const auto rel = std::filesystem::relative(it->path(), src, ec);
            if (ec) break;
            if (!copy_regular_file(it->path(), dst / rel, err)) return false;
            ++count;
        }
        if (ec) { err = "failed while traversing .NET runtime '" + src.string() + "': " + ec.message(); return false; }
        return true;
    };
    if (!copy_tree(selected.fxr_dir, fxr_target, copied, copy_error)
        || !copy_tree(selected.runtime_dir, runtime_target, copied, copy_error)) {
        error = std::move(copy_error);
        return 0;
    }
    for (const char* name : {"LICENSE.txt", "ThirdPartyNotices.txt"}) {
        const auto src = selected.root / name;
        std::error_code ec;
        if (std::filesystem::is_regular_file(src, ec) && !ec) {
            if (!copy_regular_file(src, destination / name, copy_error)) { error = std::move(copy_error); return 0; }
            ++copied;
        }
    }
    runtime_version = selected.runtime_version;
    return copied;
}

bool write_package_manifest(
    const VesperaProject& project,
    const ProjectPackageResult& result,
    const ProjectPackageOptions& options,
    std::string& error
) {
    const auto path = result.package_directory / "Vespera.PackageManifest.txt";
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        error = "could not create package manifest: " + path.string();
        return false;
    }
    out << "vespera_package_manifest 3\n";
    out << "project " << std::quoted(project.name) << "\n";
    out << "configuration " << std::quoted(options.configuration) << "\n";
    out << "company " << std::quoted(project.company_name) << "\n";
    out << "product_version " << std::quoted(project.product_version) << "\n";
    out << "package_name " << std::quoted(project.package_name) << "\n";
    out << "executable_name " << std::quoted(project.executable_name) << "\n";
    out << "development_diagnostics " << (project.development_diagnostics ? 1 : 0) << "\n";
    out << "managed_deployment " << std::quoted(std::string(managed_deployment_mode_name(options.managed_deployment))) << "\n";
    if (!result.dotnet_runtime_version.empty()) out << "dotnet_runtime " << std::quoted(result.dotnet_runtime_version) << "\n";
    out << "project_file " << std::quoted(result.packaged_project.filename().generic_string()) << "\n";
    if (!result.packaged_runtime.empty()) {
        out << "runtime " << std::quoted(result.packaged_runtime.filename().generic_string()) << "\n";
    }
    if (!result.packaged_game_icon.empty()) {
        out << "game_icon " << std::quoted(std::filesystem::relative(result.packaged_game_icon, result.package_directory).generic_string()) << "\n";
        out << "game_icon_asset_id " << std::quoted(project.game_icon_asset_id) << "\n";
        out << "game_icon_fallback " << std::quoted(project.game_icon.generic_string()) << "\n";
    }
    if (!result.package_report.empty()) {
        out << "package_report " << std::quoted(result.package_report.filename().generic_string()) << "\n";
    }
    out << "asset_count " << result.copied_assets << "\n";
    out << "metadata_count " << result.copied_metadata << "\n";
    out << "managed_file_count " << result.copied_managed_files << "\n";
    out << "runtime_file_count " << result.copied_runtime_files << "\n";
    out << "dotnet_file_count " << result.copied_dotnet_files << "\n";
    out << "asset_bytes " << result.asset_bytes << "\n";
    out << "payload_bytes " << result.payload_bytes << "\n";
    for (const auto* asset : result.manifest.assets) {
        if (!asset) continue;
        out << "asset " << std::quoted(asset->asset_id) << " "
            << std::quoted(asset->relative_path.generic_string()) << " "
            << std::quoted(std::string(asset_kind_name(asset->kind))) << "\n";
    }
    out << "end_package_manifest\n";
    if (!out) {
        error = "failed while writing package manifest";
        return false;
    }
    return true;
}

bool write_package_report(
    const VesperaProject& project,
    const ProjectPackageResult& result,
    const ProjectPackageOptions& options,
    std::string& error
) {
    const auto path = result.package_directory / "Vespera.PackageReport.txt";
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        error = "could not create package report: " + path.string();
        return false;
    }

    std::map<std::string, std::pair<std::size_t, std::uintmax_t>> kinds;
    for (const auto* asset : result.manifest.assets) {
        if (!asset) continue;
        auto& stats = kinds[std::string(asset_kind_name(asset->kind))];
        ++stats.first;
        stats.second += file_size_or_zero(asset->absolute_path);
    }

    out << "Vespera Package Report\n";
    out << "======================\n";
    out << "Project: " << project.name << "\n";
    out << "Configuration: " << options.configuration << "\n";
    out << "Native configuration: " << native_configuration_for_package(options.configuration) << "\n";
    out << "Company: " << project.company_name << "\n";
    out << "Product version: " << project.product_version << "\n";
    out << "Package identifier: " << project.package_name << "\n";
    out << "Executable: " << (result.packaged_runtime.empty() ? std::string("<content-only>") : result.packaged_runtime.filename().string()) << "\n";
    if (!result.packaged_runtime.empty() && result.packaged_runtime.extension() == ".exe") {
        const bool shared_player = !options.runtime_executable.empty()
            && options.runtime_executable.stem() == "vespera_player";
        if (shared_player) {
            out << "Windows subsystem: " << (options.configuration == "Release" ? "GUI (no console)" : "Console/diagnostic") << "\n";
            out << "Runtime log: %LOCALAPPDATA%\\Vespera\\Logs\\" << result.packaged_runtime.stem().string() << ".log\n";
            out << "Legal notices: legal/Vespera.LICENSE.txt; legal/Vespera.ThirdPartyNotices.md\n";
        } else {
            out << "Windows subsystem: custom runtime target-defined\n";
            out << "Runtime log: custom runtime target-defined\n";
        }
    }
    out << "Managed deployment: " << managed_deployment_mode_name(options.managed_deployment) << "\n";
    out << "Development diagnostics: " << (project.development_diagnostics ? "enabled" : "disabled") << "\n";
    out << "Required assets: " << result.copied_assets << "\n";
    out << "Asset bytes: " << result.asset_bytes << "\n";
    out << "Payload bytes before report/manifest: " << result.payload_bytes << "\n";
    out << "Metadata sidecars: " << result.copied_metadata << "\n";
    out << "Managed files: " << result.copied_managed_files << "\n";
    out << "Runtime files: " << result.copied_runtime_files << "\n";
    out << "Bundled .NET files: " << result.copied_dotnet_files << "\n";
    if (!result.dotnet_runtime_version.empty()) out << "Bundled .NET runtime: " << result.dotnet_runtime_version << "\n";
    if (!result.packaged_game_icon.empty()) {
        out << "Game icon asset: " << std::filesystem::relative(result.packaged_game_icon, result.package_directory).generic_string() << "\n";
    }

    out << "\nAsset kinds\n-----------\n";
    for (const auto& [kind, stats] : kinds) {
        out << kind << ": " << stats.first << " file(s), " << stats.second << " bytes\n";
    }

    if (!result.warnings.empty()) {
        out << "\nWarnings\n--------\n";
        for (const auto& warning : result.warnings) out << "- " << warning << "\n";
    }

    out << "\nDeterministic asset closure\n---------------------------\n";
    for (const auto* asset : result.manifest.assets) {
        if (!asset) continue;
        out << asset->relative_path.generic_string() << " | " << asset_kind_name(asset->kind)
            << " | " << asset->asset_id << " | " << file_size_or_zero(asset->absolute_path) << " bytes\n";
    }
    if (!out) {
        error = "failed while writing package report";
        return false;
    }
    return true;
}

} // namespace

ProjectPackageResult export_project_package(
    const VesperaProject& project,
    const AssetCatalog& catalog,
    const ProjectPackageOptions& options
) {
    ProjectPackageResult result;
    result.package_directory = absolute_normalized(options.output_directory);
    if (!valid_package_configuration(options.configuration)) {
        result.message = "configuration must be Debug, Development, or Release";
        return result;
    }

    std::string safety_error;
    if (!safe_output_directory(project, result.package_directory, safety_error)) {
        result.message = std::move(safety_error);
        return result;
    }

    result.manifest = build_project_asset_manifest(project, catalog);
    if (!result.manifest.valid()) {
        if (!result.manifest.missing_roots.empty()) {
            result.message = "project build manifest is invalid: missing build root '" + result.manifest.missing_roots.front() + "'";
        } else if (!result.manifest.broken_dependencies.empty() && result.manifest.broken_dependencies.front()) {
            const auto* dependency = result.manifest.broken_dependencies.front();
            std::string source = dependency->source_asset_id;
            if (const auto* source_record = catalog.find_by_id(dependency->source_asset_id)) {
                source = source_record->relative_path.generic_string();
            }
            result.message = "project build manifest is invalid: '" + source + "' has broken reference '"
                + dependency->reference + "'" + (dependency->reason.empty() ? std::string{} : " (" + dependency->reason + ")");
        } else {
            result.message = "project build manifest is invalid (missing roots or broken dependencies)";
        }
        return result;
    }
    if (!result.manifest.stale_root_paths.empty()) {
        result.warnings.push_back("build manifest contains stale readable root fallback paths; stable IDs resolved them");
    }

    std::string managed_stage_error;
    if (!validate_managed_stage(project, options, managed_stage_error)) {
        result.message = std::move(managed_stage_error);
        return result;
    }

    std::error_code ec;
    if (options.clean_output && std::filesystem::exists(result.package_directory, ec) && !ec) {
        std::filesystem::remove_all(result.package_directory, ec);
        if (ec) {
            result.message = "could not clean package output directory: " + ec.message();
            return result;
        }
    }
    std::filesystem::create_directories(result.package_directory / "assets", ec);
    if (ec) {
        result.message = "could not create package output directory: " + ec.message();
        return result;
    }

    for (const auto* asset : result.manifest.assets) {
        if (!asset) continue;
        std::string copy_error;
        const auto target = result.package_directory / "assets" / asset->relative_path;
        if (!copy_regular_file(asset->absolute_path, target, copy_error)) {
            result.message = std::move(copy_error);
            return result;
        }
        ++result.copied_assets;
        result.asset_bytes += file_size_or_zero(asset->absolute_path);
        if ((!project.game_icon_asset_id.empty() && asset->asset_id == project.game_icon_asset_id)
            || (project.game_icon_asset_id.empty() && !project.game_icon.empty()
                && asset->relative_path.lexically_normal() == project.game_icon.lexically_normal())) {
            result.packaged_game_icon = target;
        }

        if (options.include_metadata) {
            const auto metadata = asset->metadata_path;
            if (!metadata.empty() && std::filesystem::is_regular_file(metadata, ec) && !ec) {
                const auto metadata_target = std::filesystem::path(target.string() + ".vmeta");
                if (!copy_regular_file(metadata, metadata_target, copy_error)) {
                    result.message = std::move(copy_error);
                    return result;
                }
                ++result.copied_metadata;
            }
            ec.clear();
        }
    }

    const auto project_name = project.project_file.empty()
        ? std::filesystem::path("Game.vesperaproject")
        : project.project_file.filename();
    result.packaged_project = result.package_directory / project_name;
    std::string copy_error;
    if (project.project_file.empty() || !copy_regular_file(project.project_file, result.packaged_project, copy_error)) {
        result.message = project.project_file.empty() ? "project file path is empty" : std::move(copy_error);
        return result;
    }

    if (!options.managed_directory.empty()) {
        std::string managed_error;
        result.copied_managed_files = copy_directory_recursive(
            options.managed_directory,
            result.package_directory / "managed",
            managed_error
        );
        if (!managed_error.empty()) {
            result.message = std::move(managed_error);
            return result;
        }
    } else if (!project.managed_assembly.empty()) {
        result.message = "managed package preflight passed without a managed directory; refusing an incomplete package";
        return result;
    }

    if (options.managed_deployment == ManagedDeploymentMode::Portable && !project.managed_assembly.empty()) {
        if (options.managed_directory.empty()) {
            result.message = "portable managed deployment requires the staged managed runtime directory";
            return result;
        }
        std::string dotnet_error;
        result.copied_dotnet_files = copy_portable_dotnet_runtime(
            options, result.package_directory / "dotnet", result.dotnet_runtime_version, dotnet_error);
        if (!dotnet_error.empty()) {
            result.message = std::move(dotnet_error);
            return result;
        }
        result.warnings.push_back("portable .NET runtime bundled; validate the package on a machine without a system .NET install before release");
    }

    if (!options.runtime_executable.empty()) {
        result.packaged_runtime = result.package_directory / packaged_runtime_filename(project, options.runtime_executable);
        if (!copy_regular_file(options.runtime_executable, result.packaged_runtime, copy_error)) {
            result.message = std::move(copy_error);
            return result;
        }
        ++result.copied_runtime_files;

        // Keep the source-tree Release player console-friendly for direct
        // run-project.ps1 diagnostics, but turn the copied shipping executable
        // into a normal Windows GUI application. This avoids a console window
        // without requiring a second native runtime target.
        if (options.configuration == "Release"
            && result.packaged_runtime.extension() == ".exe"
            && options.runtime_executable.stem() == "vespera_player") {
            std::string subsystem_error;
            if (!patch_windows_pe_subsystem(result.packaged_runtime, 2u, subsystem_error)) {
                result.message = std::move(subsystem_error);
                return result;
            }
        }

        if (options.include_debug_symbols) {
            auto pdb = options.runtime_executable;
            pdb.replace_extension(".pdb");
            if (std::filesystem::is_regular_file(pdb, ec) && !ec) {
                auto packaged_pdb = result.packaged_runtime;
                packaged_pdb.replace_extension(".pdb");
                if (!copy_regular_file(pdb, packaged_pdb, copy_error)) {
                    result.message = std::move(copy_error);
                    return result;
                }
                ++result.copied_runtime_files;
            }
            ec.clear();
        }

        // Runtime support payload belongs to the runtime, not export.ps1. This
        // keeps Build Game, MCP/direct packaging, and CLI export behavior
        // identical. The shared player build stages branding and distribution
        // notices beside the executable, so package both from that same runtime
        // directory.
        for (const char* directory : {"branding", "legal"}) {
            const auto support_source = options.runtime_executable.parent_path() / directory;
            if (std::filesystem::is_directory(support_source, ec) && !ec) {
                std::string support_error;
                const auto copied = copy_directory_recursive(
                    support_source, result.package_directory / directory, support_error);
                if (!support_error.empty()) {
                    result.message = std::move(support_error);
                    return result;
                }
                result.copied_runtime_files += copied;
            } else if (options.runtime_executable.stem() == "vespera_player") {
                result.message = std::string("shared vespera_player runtime is missing its staged ")
                    + directory + " directory: " + support_source.string();
                return result;
            }
            ec.clear();
        }
    } else {
        result.warnings.push_back("content package created without a runtime executable");
    }

    std::string package_check_error;
    if (!validate_packaged_payload(project, options, result, package_check_error)) {
        result.message = std::move(package_check_error);
        return result;
    }

    if (!write_build_asset_index(result.manifest, result.package_directory / "Vespera.BuildAssetIndex.txt", &copy_error)) {
        result.message = std::move(copy_error);
        return result;
    }
    result.payload_bytes = directory_payload_bytes(result.package_directory);
    result.package_report = result.package_directory / "Vespera.PackageReport.txt";
    if (!write_package_report(project, result, options, copy_error)) {
        result.message = std::move(copy_error);
        return result;
    }
    if (!write_package_manifest(project, result, options, copy_error)) {
        result.message = std::move(copy_error);
        return result;
    }

    result.ok = true;
    result.message = "packaged " + std::to_string(result.copied_assets) + " required assets to "
        + result.package_directory.string();
    return result;
}

} // namespace vespera
