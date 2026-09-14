#include <vespera/assets/asset_catalog.hpp>
#include <vespera/assets/project_package.hpp>
#include <vespera/project/project.hpp>

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void print_usage() {
    std::cout
        << "Vespera project packager\n"
        << "Usage: vespera_packager --project <file.vesperaproject> --output <dir> [options]\n"
        << "Options:\n"
        << "  --runtime <exe>          Copy a built runtime executable into the package.\n"
        << "  --managed <dir>          Copy staged Vespera.NET/game managed runtime files.\n"
        << "  --configuration <name>  Debug, Development, or Release (default Development).\n"
        << "  --managed-deployment <mode>  framework-dependent or portable.\n"
        << "  --dotnet-root <dir>      Optional explicit dotnet root for portable deployment.\n"
        << "  --no-clean               Do not delete the existing output directory first.\n"
        << "  --no-metadata            Do not copy .vmeta sidecars.\n"
        << "  --debug-symbols          Force-copy an adjacent .pdb when present.\n"
        << "  --no-debug-symbols       Do not copy debug symbols for this package.\n";
}

bool next_arg(int& index, int argc, char** argv, std::string& out) {
    if (index + 1 >= argc) return false;
    out = argv[++index];
    return true;
}

} // namespace

int main(int argc, char** argv) {
    std::filesystem::path project_path;
    vespera::ProjectPackageOptions options;
    bool managed_deployment_overridden = false;
    bool debug_symbols_overridden = false;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        std::string value;
        if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        }
        if (arg == "--project") {
            if (!next_arg(i, argc, argv, value)) { std::cerr << "--project requires a value\n"; return 2; }
            project_path = value;
        } else if (arg == "--output") {
            if (!next_arg(i, argc, argv, value)) { std::cerr << "--output requires a value\n"; return 2; }
            options.output_directory = value;
        } else if (arg == "--runtime") {
            if (!next_arg(i, argc, argv, value)) { std::cerr << "--runtime requires a value\n"; return 2; }
            options.runtime_executable = value;
        } else if (arg == "--managed") {
            if (!next_arg(i, argc, argv, value)) { std::cerr << "--managed requires a value\n"; return 2; }
            options.managed_directory = value;
        } else if (arg == "--configuration") {
            if (!next_arg(i, argc, argv, value)) { std::cerr << "--configuration requires a value\n"; return 2; }
            options.configuration = value;
        } else if (arg == "--managed-deployment") {
            if (!next_arg(i, argc, argv, value)) { std::cerr << "--managed-deployment requires a value\n"; return 2; }
            const auto mode = vespera::managed_deployment_mode_from_name(value);
            if (!mode) { std::cerr << "--managed-deployment must be framework-dependent or portable\n"; return 2; }
            options.managed_deployment = *mode;
            managed_deployment_overridden = true;
        } else if (arg == "--dotnet-root") {
            if (!next_arg(i, argc, argv, value)) { std::cerr << "--dotnet-root requires a value\n"; return 2; }
            options.dotnet_root = value;
        } else if (arg == "--no-clean") {
            options.clean_output = false;
        } else if (arg == "--no-metadata") {
            options.include_metadata = false;
        } else if (arg == "--debug-symbols") {
            options.include_debug_symbols = true;
            debug_symbols_overridden = true;
        } else if (arg == "--no-debug-symbols") {
            options.include_debug_symbols = false;
            debug_symbols_overridden = true;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            print_usage();
            return 2;
        }
    }

    if (project_path.empty() || options.output_directory.empty()) {
        print_usage();
        return 2;
    }

    vespera::VesperaProject project;
    const auto loaded = vespera::load_vespera_project(project, project_path);
    if (!loaded) {
        std::cerr << "Project load failed: " << loaded.message << "\n";
        return 3;
    }
    if (!managed_deployment_overridden) {
        options.managed_deployment = project.managed_deployment;
    }
    if (!vespera::valid_package_configuration(options.configuration)) {
        std::cerr << "--configuration must be Debug, Development, or Release\n";
        return 2;
    }
    if (!debug_symbols_overridden) {
        options.include_debug_symbols = options.configuration == "Debug"
            || (options.configuration == "Development" && project.development_diagnostics);
    }

    bool project_errors = false;
    vespera::ProjectValidationOptions validation_options;
    validation_options.require_managed_source = false;
    for (const auto& issue : vespera::validate_vespera_project(project, validation_options)) {
        const bool error = issue.severity == vespera::ProjectValidationSeverity::Error;
        std::cerr << (error ? "ERROR: " : "WARNING: ") << issue.message << "\n";
        project_errors = project_errors || error;
    }
    if (project_errors) return 4;

    vespera::AssetCatalog catalog;
    vespera::AssetCatalogRefreshOptions refresh_options;
    refresh_options.write_metadata = false;
    vespera::AssetCatalogRefreshReport report;
    std::string catalog_error;
    if (!catalog.refresh(project.assets_root(), refresh_options, &report, &catalog_error)) {
        std::cerr << "Asset catalog refresh failed: " << catalog_error << "\n";
        return 5;
    }

    const auto packaged = vespera::export_project_package(project, catalog, options);
    if (!packaged) {
        std::cerr << "Package failed: " << packaged.message << "\n";
        if (!packaged.manifest.missing_roots.empty()) {
            for (const auto& root : packaged.manifest.missing_roots) std::cerr << "  missing root: " << root << "\n";
        }
        if (!packaged.manifest.broken_dependencies.empty()) {
            for (const auto* dependency : packaged.manifest.broken_dependencies) {
                if (!dependency) continue;
                const auto* source = catalog.find_by_id(dependency->source_asset_id);
                std::cerr << "  broken dependency: "
                          << (source ? source->relative_path.generic_string() : dependency->source_asset_id)
                          << " -> " << dependency->reference;
                if (!dependency->reason.empty()) std::cerr << " (" << dependency->reason << ")";
                std::cerr << "\n";
            }
        }
        return 6;
    }

    std::cout << packaged.message << "\n";
    std::cout << "Assets: " << packaged.copied_assets
              << " | metadata: " << packaged.copied_metadata
              << " | managed files: " << packaged.copied_managed_files
              << " | runtime files: " << packaged.copied_runtime_files
              << " | dotnet files: " << packaged.copied_dotnet_files << "\n";
    for (const auto& warning : packaged.warnings) std::cout << "WARNING: " << warning << "\n";
    std::cout << "Manifest: " << (packaged.package_directory / "Vespera.PackageManifest.txt").string() << "\n";
    std::cout << "Report:   " << packaged.package_report.string() << "\n";
    if (!packaged.packaged_runtime.empty()) std::cout << "Runtime:  " << packaged.packaged_runtime.string() << "\n";
    return 0;
}
