#pragma once

#include <vespera/assets/asset_catalog.hpp>
#include <vespera/assets/build_manifest.hpp>
#include <vespera/project/project.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace vespera {

[[nodiscard]] bool valid_package_configuration(std::string_view configuration);
[[nodiscard]] std::string_view native_configuration_for_package(std::string_view configuration);

struct ProjectPackageOptions {
    std::filesystem::path output_directory;
    std::filesystem::path runtime_executable;
    std::filesystem::path managed_directory;
    std::string configuration = "Development";
    ManagedDeploymentMode managed_deployment = ManagedDeploymentMode::FrameworkDependent;
    // Optional explicit dotnet root. When empty, portable export discovers an
    // installed x64 runtime and copies only the host/runtime files it needs.
    std::filesystem::path dotnet_root;
    bool clean_output = true;
    bool include_metadata = true;
    bool include_debug_symbols = false;
};

struct ProjectPackageResult {
    bool ok = false;
    std::string message;
    BuildAssetManifest manifest;
    std::size_t copied_assets = 0;
    std::size_t copied_metadata = 0;
    std::size_t copied_managed_files = 0;
    std::size_t copied_runtime_files = 0;
    std::size_t copied_dotnet_files = 0;
    std::uintmax_t asset_bytes = 0;
    std::uintmax_t payload_bytes = 0;
    std::string dotnet_runtime_version;
    std::filesystem::path package_directory;
    std::filesystem::path packaged_project;
    std::filesystem::path packaged_runtime;
    std::filesystem::path packaged_game_icon;
    std::filesystem::path package_report;
    std::vector<std::string> warnings;

    explicit operator bool() const { return ok; }
};

// Creates a deterministic standalone package directory from the project's
// transitive build-asset closure. The operation copies source/runtime files
// only; it never mutates project source assets or renderer/runtime state.
[[nodiscard]] ProjectPackageResult export_project_package(
    const VesperaProject& project,
    const AssetCatalog& catalog,
    const ProjectPackageOptions& options
);

} // namespace vespera
