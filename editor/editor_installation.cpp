#include "editor_installation.hpp"

#include <vespera/assets/project_package.hpp>
#include <vespera/platform/process.hpp>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace vespera::editor {
namespace {

std::filesystem::path first_regular_file(
    const std::filesystem::path& root,
    std::initializer_list<std::filesystem::path> candidates
) {
    std::error_code ec;
    for (const auto& relative : candidates) {
        const auto candidate = (root / relative).lexically_normal();
        ec.clear();
        if (std::filesystem::is_regular_file(candidate, ec) && !ec) return candidate;
    }
    return {};
}

bool is_source_root(const std::filesystem::path& path) {
    std::error_code ec;
    const bool cmake = std::filesystem::is_regular_file(path / "CMakeLists.txt", ec) && !ec;
    ec.clear();
    const bool engine = std::filesystem::is_directory(path / "engine", ec) && !ec;
    ec.clear();
    const bool templates = std::filesystem::is_directory(path / "templates", ec) && !ec;
    return cmake && engine && templates;
}

bool is_distribution_root(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path / "Vespera.DistributionManifest.txt", ec) && !ec;
}

} // namespace

std::filesystem::path editor_installation_root() {
    for (const char* name : {"VESPERA_HOME", "VESPERA_SOURCE_ROOT"}) {
        if (const char* value = std::getenv(name); value && *value) {
            const std::filesystem::path candidate(value);
            if (is_distribution_root(candidate) || is_source_root(candidate)) {
                return candidate.lexically_normal();
            }
        }
    }

    const auto executable = vespera::platform::current_executable_path();
    if (executable.empty()) return {};
    std::filesystem::path cursor = executable.parent_path();
    for (int depth = 0; depth < 8 && !cursor.empty(); ++depth) {
        if (is_distribution_root(cursor) || is_source_root(cursor)) return cursor.lexically_normal();
        const auto parent = cursor.parent_path();
        if (parent == cursor) break;
        cursor = parent;
    }
    return {};
}

bool editor_is_distribution_installation() {
    const auto root = editor_installation_root();
    return !root.empty() && is_distribution_root(root);
}

std::filesystem::path editor_user_settings_directory() {
#if defined(_WIN32)
    if (const char* local = std::getenv("LOCALAPPDATA"); local && *local) {
        return (std::filesystem::path(local) / "Vespera").lexically_normal();
    }
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
        return (std::filesystem::path(xdg) / "Vespera").lexically_normal();
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        return (std::filesystem::path(home) / ".config" / "Vespera").lexically_normal();
    }
#endif
    std::error_code ec;
    auto fallback = std::filesystem::temp_directory_path(ec);
    if (ec) fallback = std::filesystem::current_path(ec);
    return (fallback / "Vespera").lexically_normal();
}

std::filesystem::path editor_find_builder_executable(std::string_view configuration) {
    const auto root = editor_installation_root();
    if (root.empty()) return {};
#if defined(_WIN32)
    if (auto packaged = first_regular_file(root, {"tools/VesperaBuilder.exe", "tools/vespera_builder.exe"}); !packaged.empty()) {
        return packaged;
    }
    for (const auto package_config : editor_builder_configuration_fallbacks(configuration)) {
        const std::string native_config(vespera::native_configuration_for_package(package_config));
        if (auto built = first_regular_file(root, {
                std::filesystem::path("build/tools/builder") / native_config / "vespera_builder.exe"
            }); !built.empty()) {
            return built;
        }
    }
    return {};
#else
    (void)configuration;
    if (auto packaged = first_regular_file(root, {"tools/VesperaBuilder", "tools/vespera_builder"}); !packaged.empty()) {
        return packaged;
    }
    return first_regular_file(root, {
        "build-linux/tools/builder/vespera_builder",
        "build/tools/builder/vespera_builder"
    });
#endif
}

std::filesystem::path editor_find_dotnet_executable() {
    const auto root = editor_installation_root();
    if (!root.empty()) {
#if defined(_WIN32)
        if (auto bundled = first_regular_file(root, {"dotnet/dotnet.exe"}); !bundled.empty()) return bundled;
#else
        if (auto bundled = first_regular_file(root, {"dotnet/dotnet"}); !bundled.empty()) return bundled;
#endif
    }

#if defined(_WIN32)
    for (const char* name : {"DOTNET_ROOT", "DOTNET_ROOT_X64", "ProgramW6432", "ProgramFiles"}) {
        const char* value = std::getenv(name);
        if (!value || !*value) continue;
        std::filesystem::path base(value);
        if (std::string_view(name).starts_with("Program")) base /= "dotnet";
        const auto candidate = base / "dotnet.exe";
        std::error_code ec;
        if (std::filesystem::is_regular_file(candidate, ec) && !ec) return candidate;
    }
#else
    for (const char* name : {"DOTNET_ROOT", "DOTNET_ROOT_X64"}) {
        if (const char* value = std::getenv(name); value && *value) {
            const auto candidate = std::filesystem::path(value) / "dotnet";
            std::error_code ec;
            if (std::filesystem::is_regular_file(candidate, ec) && !ec) return candidate;
        }
    }
    for (const std::filesystem::path candidate : {
             "/usr/bin/dotnet", "/usr/share/dotnet/dotnet", "/usr/local/share/dotnet/dotnet"}) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(candidate, ec) && !ec) return candidate;
    }
#endif
    return {};
}

std::filesystem::path editor_find_sdk_project() {
    const auto root = editor_installation_root();
    if (root.empty()) return {};
    return first_regular_file(root, {
        "sdk/Vespera.NET/Vespera.NET.csproj",
        "managed/Vespera.NET/Vespera.NET.csproj"
    });
}

std::filesystem::path editor_find_script_tool_project() {
    const auto root = editor_installation_root();
    if (root.empty()) return {};
    return first_regular_file(root, {
        "sdk/Vespera.ScriptTool/Vespera.ScriptTool.csproj",
        "managed/Vespera.ScriptTool/Vespera.ScriptTool.csproj"
    });
}

std::filesystem::path editor_find_runtime_executable(
    std::string_view target_name,
    std::string_view configuration
) {
    const auto root = editor_installation_root();
    if (root.empty()) return {};
    const std::string requested = target_name.empty() ? "vespera_player" : std::string(target_name);

#if defined(_WIN32)
    if (requested == "vespera_player") {
        if (auto packaged = first_regular_file(root, {"runtime/VesperaPlayer.exe", "runtime/vespera_player.exe"}); !packaged.empty()) {
            return packaged;
        }
    }
    const std::string filename = requested + ".exe";
    const std::string native_config(vespera::native_configuration_for_package(configuration));
    const auto build_root = root / "build";
    std::error_code ec;
    if (!std::filesystem::is_directory(build_root, ec) || ec) return {};
    for (std::filesystem::recursive_directory_iterator it(build_root, ec), end; it != end && !ec; it.increment(ec)) {
        if (!it->is_regular_file(ec) || ec || it->path().filename() != filename) continue;
        bool config_match = false;
        for (const auto& part : it->path().parent_path()) {
            if (part.string() == native_config) { config_match = true; break; }
        }
        if (config_match) return it->path().lexically_normal();
    }
#else
    (void)configuration;
    if (requested == "vespera_player") {
        if (auto packaged = first_regular_file(root, {"runtime/VesperaPlayer", "runtime/vespera_player"}); !packaged.empty()) {
            return packaged;
        }
    }
    for (const auto& build_root : {root / "build-linux", root / "build"}) {
        std::error_code ec;
        if (!std::filesystem::is_directory(build_root, ec) || ec) continue;
        for (std::filesystem::recursive_directory_iterator it(build_root, ec), end; it != end && !ec; it.increment(ec)) {
            if (it->is_regular_file(ec) && !ec && it->path().filename() == requested) {
                return it->path().lexically_normal();
            }
        }
    }
#endif
    return {};
}

} // namespace vespera::editor
