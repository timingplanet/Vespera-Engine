#include <vespera/assets/asset_catalog.hpp>
#include <vespera/assets/project_package.hpp>
#include <vespera/core/version.hpp>
#include <vespera/platform/process.hpp>
#include <vespera/project/project.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {

struct BuilderOptions {
    fs::path project;
    fs::path output;
    fs::path runtime;
    fs::path dotnet;
    fs::path sdk_project;
    fs::path script_tool_project;
    fs::path managed_output;
    std::string configuration = "Development";
    std::optional<vespera::ManagedDeploymentMode> managed_deployment;
    bool launch = false;
    bool clean = true;
    bool managed_only = false;
};

void usage() {
    std::cout
        << "Vespera standalone game builder\n"
        << "Usage: vespera_builder --project <file.vesperaproject> --output <dir> --runtime <player> [options]\n"
        << "Options:\n"
        << "  --configuration <name>      Debug, Development, or Release.\n"
        << "  --dotnet <path>             dotnet executable used to compile C# projects.\n"
        << "  --sdk-project <path>        Vespera.NET.csproj used by managed game projects.\n"
        << "  --script-tool-project <path> Vespera.ScriptTool.csproj for editor metadata.\n"
        << "  --managed-only              Compile/stage C# for editor Play Mode; do not package.\n"
        << "  --managed-output <dir>      Destination for --managed-only (normally editor managed/).\n"
        << "  --managed-deployment <mode> framework-dependent or portable.\n"
        << "  --launch                    Launch the packaged game after a successful build.\n"
        << "  --no-clean                  Preserve existing output before packaging.\n";
}

bool take_value(int& index, int argc, char** argv, std::string& value) {
    if (index + 1 >= argc) return false;
    value = argv[++index];
    return true;
}

bool parse_args(int argc, char** argv, BuilderOptions& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i] ? std::string_view(argv[i]) : std::string_view{};
        std::string value;
        if (arg == "--project") {
            if (!take_value(i, argc, argv, value)) return false;
            options.project = value;
        } else if (arg == "--output") {
            if (!take_value(i, argc, argv, value)) return false;
            options.output = value;
        } else if (arg == "--runtime") {
            if (!take_value(i, argc, argv, value)) return false;
            options.runtime = value;
        } else if (arg == "--dotnet") {
            if (!take_value(i, argc, argv, value)) return false;
            options.dotnet = value;
        } else if (arg == "--sdk-project") {
            if (!take_value(i, argc, argv, value)) return false;
            options.sdk_project = value;
        } else if (arg == "--script-tool-project") {
            if (!take_value(i, argc, argv, value)) return false;
            options.script_tool_project = value;
        } else if (arg == "--managed-output") {
            if (!take_value(i, argc, argv, value)) return false;
            options.managed_output = value;
        } else if (arg == "--managed-only") {
            options.managed_only = true;
        } else if (arg == "--configuration") {
            if (!take_value(i, argc, argv, value)) return false;
            options.configuration = value;
        } else if (arg == "--managed-deployment") {
            if (!take_value(i, argc, argv, value)) return false;
            const auto mode = vespera::managed_deployment_mode_from_name(value);
            if (!mode) {
                std::cerr << "--managed-deployment must be framework-dependent or portable\n";
                return false;
            }
            options.managed_deployment = *mode;
        } else if (arg == "--launch") {
            options.launch = true;
        } else if (arg == "--no-clean") {
            options.clean = false;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            return false;
        }
    }
    if (options.project.empty()) return false;
    if (options.managed_only) return !options.managed_output.empty();
    return !options.output.empty() && !options.runtime.empty();
}

std::string read_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::ostringstream out;
    out << input.rdbuf();
    return out.str();
}

std::optional<int> select_sdk_major(const fs::path& dotnet, const fs::path& scratch) {
    const fs::path log = scratch / "dotnet-sdks.txt";
    vespera::platform::ProcessOptions process;
    process.executable = dotnet;
    process.arguments = {"--list-sdks"};
    process.timeout_seconds = 30;
    const int result = vespera::platform::run_process_to_file(process, log);
    if (result != 0) return std::nullopt;
    const std::string text = read_file(log);
    const std::regex line(R"((?:^|\n)([0-9]+)\.[0-9]+\.[^\s]+\s+\[[^\]]+\])");
    int best = 0;
    for (std::sregex_iterator it(text.begin(), text.end(), line), end; it != end; ++it) {
        const int major = std::stoi((*it)[1].str());
        if (major >= 8) best = std::max(best, major);
    }
    if (best == 0) return std::nullopt;
    return best;
}

bool write_runtime_config(const fs::path& stage, int major, std::string& error) {
    std::ofstream out(stage / "Vespera.Managed.runtimeconfig.json", std::ios::trunc);
    if (!out) {
        error = "could not create Vespera.Managed.runtimeconfig.json";
        return false;
    }
    out << "{\n"
        << "  \"runtimeOptions\": {\n"
        << "    \"tfm\": \"net" << major << ".0\",\n"
        << "    \"framework\": { \"name\": \"Microsoft.NETCore.App\", \"version\": \"" << major << ".0.0\" },\n"
        << "    \"rollForward\": \"LatestMajor\"\n"
        << "  }\n"
        << "}\n";
    return static_cast<bool>(out);
}

bool files_equal(const fs::path& left, const fs::path& right) {
    std::error_code ec;
    const auto left_size = fs::file_size(left, ec);
    if (ec) return false;
    ec.clear();
    const auto right_size = fs::file_size(right, ec);
    if (ec || left_size != right_size) return false;
    std::ifstream a(left, std::ios::binary);
    std::ifstream b(right, std::ios::binary);
    std::array<char, 64 * 1024> ab{};
    std::array<char, 64 * 1024> bb{};
    while (a && b) {
        a.read(ab.data(), static_cast<std::streamsize>(ab.size()));
        b.read(bb.data(), static_cast<std::streamsize>(bb.size()));
        const auto ac = a.gcount();
        const auto bc = b.gcount();
        if (ac != bc || !std::equal(ab.begin(), ab.begin() + ac, bb.begin())) return false;
        if (ac == 0) break;
    }
    return true;
}

bool copy_atomic_file(const fs::path& source, const fs::path& destination, std::string& error) {
    std::error_code ec;
    fs::create_directories(destination.parent_path(), ec);
    if (ec) {
        error = "could not create managed output directory: " + ec.message();
        return false;
    }
    const fs::path pending = destination.string() + ".vespera-new";
    fs::remove(pending, ec);
    ec.clear();
    if (!fs::copy_file(source, pending, fs::copy_options::overwrite_existing, ec) || ec) {
        error = "could not stage " + source.filename().string() + ": " + ec.message();
        fs::remove(pending, ec);
        return false;
    }
#if defined(_WIN32)
    if (fs::exists(destination)) {
        const fs::path backup = destination.string() + ".vespera-old";
        fs::remove(backup, ec);
        if (!ReplaceFileW(destination.wstring().c_str(), pending.wstring().c_str(), backup.wstring().c_str(),
                REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) {
            const DWORD code = GetLastError();
            fs::remove(pending, ec);
            fs::remove(backup, ec);
            error = "could not atomically replace " + destination.filename().string()
                + " (Windows error " + std::to_string(code) + ")";
            return false;
        }
        fs::remove(backup, ec);
    } else if (!MoveFileExW(pending.wstring().c_str(), destination.wstring().c_str(), MOVEFILE_WRITE_THROUGH)) {
        const DWORD code = GetLastError();
        fs::remove(pending, ec);
        error = "could not commit " + destination.filename().string()
            + " (Windows error " + std::to_string(code) + ")";
        return false;
    }
#else
    fs::rename(pending, destination, ec);
    if (ec) {
        fs::remove(pending, ec);
        error = "could not commit " + destination.filename().string() + ": " + ec.message();
        return false;
    }
#endif
    return true;
}

bool generate_script_metadata(
    const vespera::VesperaProject& project,
    const BuilderOptions& options,
    const fs::path& stage,
    const fs::path& scratch,
    int major
) {
    if (options.script_tool_project.empty()) return true;
    if (!fs::is_regular_file(options.script_tool_project)) {
        std::cerr << "ERROR: Vespera.ScriptTool project not found: " << options.script_tool_project << "\n";
        return false;
    }
    const std::string tfm = "net" + std::to_string(major) + ".0";
    const fs::path tool_stage = scratch / "script-tool";
    fs::create_directories(tool_stage);

    std::cout << "Stage: Generating C# metadata\n" << std::flush;
    vespera::platform::ProcessOptions build;
    build.executable = options.dotnet;
    build.arguments = {
        "build", options.script_tool_project.string(), "-c", "Release", "-f", tfm,
        "-o", tool_stage.string(), "-p:VesperaTargetFramework=" + tfm, "--nologo", "-v:minimal"
    };
    build.timeout_seconds = 600;
    const fs::path build_log = scratch / "script-tool-build.log";
    std::string process_error;
    const int build_result = vespera::platform::run_process_to_file(build, build_log, &process_error);
    const std::string build_text = read_file(build_log);
    if (!build_text.empty()) std::cout << build_text;
    if (build_result != 0) {
        std::cerr << "ERROR: Vespera.ScriptTool build failed with exit code " << build_result << "\n";
        return false;
    }

    const fs::path tool = tool_stage / "Vespera.ScriptTool.dll";
    if (!fs::is_regular_file(tool)) {
        std::cerr << "ERROR: Vespera.ScriptTool.dll was not produced.\n";
        return false;
    }
    vespera::platform::ProcessOptions run;
    run.executable = options.dotnet;
    run.arguments = {
        tool.string(),
        (stage / (project.managed_assembly + ".dll")).string(),
        (stage / "Vespera.ScriptMetadata.txt").string()
    };
    run.timeout_seconds = 120;
    const fs::path metadata_log = scratch / "script-metadata.log";
    const int metadata_result = vespera::platform::run_process_to_file(run, metadata_log, &process_error);
    const std::string metadata_text = read_file(metadata_log);
    if (!metadata_text.empty()) std::cout << metadata_text;
    if (metadata_result != 0 || !fs::is_regular_file(stage / "Vespera.ScriptMetadata.txt")) {
        std::cerr << "ERROR: C# metadata generation failed.\n";
        return false;
    }
    return true;
}

bool commit_managed_stage(
    const vespera::VesperaProject& project,
    const fs::path& stage,
    const fs::path& destination
) {
    std::error_code ec;
    fs::create_directories(destination, ec);
    if (ec) {
        std::cerr << "ERROR: could not create managed output: " << ec.message() << "\n";
        return false;
    }

    const std::string game_dll = project.managed_assembly + ".dll";
    const std::array<std::string, 2> bridge_files{"Vespera.NET.dll", "Vespera.NET.pdb"};
    for (const auto& bridge_name : bridge_files) {
        const fs::path source = stage / bridge_name;
        if (!fs::is_regular_file(source)) continue;
        const fs::path target = destination / bridge_name;
        if (fs::is_regular_file(target)) {
            if (!files_equal(source, target)) {
                std::cout << "WARNING: " << bridge_name
                          << " changed; restart the editor before using the new bridge. Existing live bridge preserved.\n";
            }
            continue;
        }
        std::string error;
        if (!copy_atomic_file(source, target, error)) {
            std::cerr << "ERROR: " << error << "\n";
            return false;
        }
    }

    // Commit dependencies/metadata before the game assembly. The runtime watches
    // the game DLL as the last-good reload marker.
    for (const auto& entry : fs::directory_iterator(stage, ec)) {
        if (ec || !entry.is_regular_file()) continue;
        const std::string name = entry.path().filename().string();
        if (name == game_dll || name == "Vespera.NET.dll" || name == "Vespera.NET.pdb") continue;
        std::string error;
        if (!copy_atomic_file(entry.path(), destination / name, error)) {
            std::cerr << "ERROR: " << error << "\n";
            return false;
        }
    }
    std::string error;
    if (!copy_atomic_file(stage / game_dll, destination / game_dll, error)) {
        std::cerr << "ERROR: " << error << "\n";
        return false;
    }
    std::cout << "Managed output committed: " << destination << "\n";
    return true;
}

struct ScopedDirectoryCleanup {
    fs::path path;
    ~ScopedDirectoryCleanup() {
        if (path.empty()) return;
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

std::optional<fs::path> build_managed_project(
    const vespera::VesperaProject& project,
    const BuilderOptions& options,
    const fs::path& scratch
) {
    if (project.managed_assembly.empty()) return fs::path{};
    if (project.managed_project.empty()) {
        std::cerr << "ERROR: project declares managed_assembly but no managed_project.\n";
        return std::nullopt;
    }
    if (options.dotnet.empty() || !fs::is_regular_file(options.dotnet)) {
        std::cerr << "ERROR: this project uses C#, but a usable dotnet SDK executable was not supplied.\n";
        return std::nullopt;
    }
    const fs::path managed_project = project.managed_project_path();
    if (!fs::is_regular_file(managed_project)) {
        std::cerr << "ERROR: managed project not found: " << managed_project << "\n";
        return std::nullopt;
    }
    if (options.sdk_project.empty() || !fs::is_regular_file(options.sdk_project)) {
        std::cerr << "ERROR: Vespera.NET SDK project not found: " << options.sdk_project << "\n";
        return std::nullopt;
    }

    const auto major = select_sdk_major(options.dotnet, scratch);
    if (!major) {
        std::cerr << "ERROR: dotnet is present, but no .NET 8+ SDK was found.\n";
        return std::nullopt;
    }
    const std::string tfm = "net" + std::to_string(*major) + ".0";
    const fs::path stage = scratch / "managed";
    std::error_code ec;
    fs::create_directories(stage, ec);
    if (ec) {
        std::cerr << "ERROR: could not create managed build stage: " << ec.message() << "\n";
        return std::nullopt;
    }

    std::cout << "Stage: Building C# scripts (" << tfm << ")\n" << std::flush;
    vespera::platform::ProcessOptions build;
    build.executable = options.dotnet;
    build.working_directory = project.root_directory;
    build.arguments = {
        "build", managed_project.string(),
        "-c", options.configuration == "Debug" ? "Debug" : "Release",
        "-f", tfm,
        "-o", stage.string(),
        "-p:VesperaTargetFramework=" + tfm,
        "-p:VesperaSdkProject=" + options.sdk_project.string(),
        "--nologo", "-v:minimal",
    };
    build.timeout_seconds = 900;
    std::string process_error;
    const fs::path build_log = scratch / "managed-build.log";
    const int result = vespera::platform::run_process_to_file(build, build_log, &process_error);
    const std::string build_text = read_file(build_log);
    if (!build_text.empty()) std::cout << build_text;
    if (result != 0) {
        std::cerr << "ERROR: C# build failed with exit code " << result;
        if (!process_error.empty()) std::cerr << ": " << process_error;
        std::cerr << "\n";
        return std::nullopt;
    }

    const fs::path bridge = stage / "Vespera.NET.dll";
    const fs::path game = stage / (project.managed_assembly + ".dll");
    if (!fs::is_regular_file(bridge) || !fs::is_regular_file(game)) {
        std::cerr << "ERROR: C# build succeeded but the expected Vespera.NET/game assemblies were not staged.\n";
        return std::nullopt;
    }
    std::string config_error;
    if (!write_runtime_config(stage, *major, config_error)) {
        std::cerr << "ERROR: " << config_error << "\n";
        return std::nullopt;
    }
    if (!generate_script_metadata(project, options, stage, scratch, *major)) return std::nullopt;
    std::cout << "Vespera C# build succeeded\n" << std::flush;
    return stage;
}

fs::path resolved_dotnet_root(const fs::path& dotnet) {
    if (dotnet.empty()) return {};
    std::error_code ec;
    const auto canonical = fs::weakly_canonical(dotnet, ec);
    const fs::path exe = ec ? dotnet : canonical;
    return exe.parent_path();
}

} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i] ? std::string_view(argv[i]) : std::string_view{};
        if (arg == "--help" || arg == "-h") {
            usage();
            return 0;
        }
        if (arg == "--version") {
            std::cout << "Vespera Builder " << vespera::kEngineVersion << "\n";
            return 0;
        }
    }

    BuilderOptions options;
    if (!parse_args(argc, argv, options)) {
        usage();
        return 2;
    }
    if (!vespera::valid_package_configuration(options.configuration)) {
        std::cerr << "--configuration must be Debug, Development, or Release\n";
        return 2;
    }

    std::cout << "Vespera Builder " << vespera::kEngineVersion << "\n";
    std::cout << "Stage: Validating project\n" << std::flush;

    vespera::VesperaProject project;
    const auto loaded = vespera::load_vespera_project(project, options.project);
    if (!loaded) {
        std::cerr << "ERROR: Project load failed: " << loaded.message << "\n";
        return 3;
    }
    bool validation_failed = false;
    vespera::ProjectValidationOptions validation_options;
    validation_options.require_managed_source = false;
    for (const auto& issue : vespera::validate_vespera_project(project, validation_options)) {
        const bool is_error = issue.severity == vespera::ProjectValidationSeverity::Error;
        std::cerr << (is_error ? "ERROR: " : "WARNING: ") << issue.message << "\n";
        validation_failed = validation_failed || is_error;
    }
    if (validation_failed) return 4;

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path scratch = fs::temp_directory_path() / ("vespera-builder-" + std::to_string(stamp));
    std::error_code ec;
    fs::create_directories(scratch, ec);
    if (ec) {
        std::cerr << "ERROR: Could not create build scratch directory: " << ec.message() << "\n";
        return 5;
    }
    ScopedDirectoryCleanup cleanup{scratch};

    const auto managed = build_managed_project(project, options, scratch);
    if (!managed) return 6;
    if (options.managed_only) {
        if (managed->empty()) {
            std::cerr << "ERROR: project does not declare a managed C# assembly.\n";
            return 6;
        }
        std::cout << "Stage: Committing C# scripts\n" << std::flush;
        if (!commit_managed_stage(project, *managed, options.managed_output)) return 6;
        return 0;
    }

    std::cout << "Stage: Resolving assets\n" << std::flush;
    vespera::AssetCatalog catalog;
    vespera::AssetCatalogRefreshOptions refresh_options;
    refresh_options.write_metadata = false;
    vespera::AssetCatalogRefreshReport report;
    std::string catalog_error;
    if (!catalog.refresh(project.assets_root(), refresh_options, &report, &catalog_error)) {
        std::cerr << "ERROR: Asset catalog refresh failed: " << catalog_error << "\n";
        return 7;
    }

    vespera::ProjectPackageOptions package;
    package.output_directory = options.output;
    package.runtime_executable = options.runtime;
    package.configuration = options.configuration;
    package.managed_deployment = options.managed_deployment.value_or(project.managed_deployment);
    package.clean_output = options.clean;
    package.include_debug_symbols = options.configuration == "Debug"
        || (options.configuration == "Development" && project.development_diagnostics);
    if (managed && !managed->empty()) package.managed_directory = *managed;
    if (package.managed_deployment == vespera::ManagedDeploymentMode::Portable && !options.dotnet.empty()) {
        package.dotnet_root = resolved_dotnet_root(options.dotnet);
    }

    std::cout << "Stage: Packaging game\n" << std::flush;
    const auto packaged = vespera::export_project_package(project, catalog, package);
    if (!packaged) {
        std::cerr << "ERROR: Package failed: " << packaged.message << "\n";
        return 8;
    }
    for (const auto& warning : packaged.warnings) std::cout << "WARNING: " << warning << "\n";
    std::cout << "Export succeeded\n";
    std::cout << "Package: " << packaged.package_directory << "\n";
    std::cout << "Runtime: " << packaged.packaged_runtime << "\n" << std::flush;

    if (options.launch && !packaged.packaged_runtime.empty()) {
        std::cout << "Stage: Launching game\n" << std::flush;
        vespera::platform::ProcessOptions launch;
        launch.executable = packaged.packaged_runtime;
        launch.working_directory = packaged.package_directory;
        std::string launch_error;
        if (!vespera::platform::launch_process(launch, &launch_error)) {
            std::cerr << "ERROR: Package succeeded but launch failed: " << launch_error << "\n";
            return 9;
        }
        std::cout << "Launched: " << packaged.packaged_runtime << "\n";
    }
    return 0;
}
