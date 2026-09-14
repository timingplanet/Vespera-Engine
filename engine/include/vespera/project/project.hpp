#pragma once

#include <vespera/assets/asset_reference.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <optional>
#include <vector>

namespace vespera {

class InputMap;

struct ProjectInputBinding {
    std::string action;
    std::string device;
    std::string code;
    float scale = 1.0f;
    float deadzone = 0.0f;
};

enum class ManagedDeploymentMode {
    FrameworkDependent,
    Portable,
};

[[nodiscard]] std::string_view managed_deployment_mode_name(ManagedDeploymentMode mode);
[[nodiscard]] std::optional<ManagedDeploymentMode> managed_deployment_mode_from_name(std::string_view name);

struct VesperaProject {
    int format_version = 10;
    std::string name = "Untitled Project";
    std::filesystem::path project_file;
    std::filesystem::path root_directory;
    std::filesystem::path assets_directory = "assets";
    std::filesystem::path startup_scene = "scenes/main.slscene";
    std::string startup_scene_asset_id;
    // Optional project-owned startup RML document (project v8). This is a
    // first-class stable asset reference rather than inferring UI from build roots.
    std::filesystem::path startup_ui;
    std::string startup_ui_asset_id;
    // Optional project-level Lua entry script (project v9). Lua is the lightweight
    // runtime/mod scripting surface and can coexist with the primary C# assembly.
    std::filesystem::path lua_entry;
    std::string lua_entry_asset_id;
    std::filesystem::path managed_project;
    std::string managed_assembly;
    std::string game_target;

    // Shipping/build metadata (project v6). These are renderer/runtime agnostic
    // and feed package manifests/export defaults rather than platform handles.
    std::string company_name;
    std::string product_version = "0.1.0";
    std::string package_name;
    ManagedDeploymentMode managed_deployment = ManagedDeploymentMode::FrameworkDependent;

    // Shipping/build settings (project v7). These remain platform-neutral: the
    // exporter decides how a target platform maps them onto executable/package
    // metadata. game_icon uses the same stable-ID + readable-fallback model as
    // other project assets, while build_output_directory is project-relative
    // unless explicitly authored as an absolute path.
    std::string executable_name;
    std::filesystem::path build_output_directory = "builds";
    std::filesystem::path game_icon;
    std::string game_icon_asset_id;
    bool development_diagnostics = true;

    // Runtime window settings are project-owned in v2 so an exported game and
    // the reference runner do not need hardcoded presentation defaults.
    std::string window_title;
    int window_width = 1280;
    int window_height = 720;
    bool window_resizable = true;
    bool relative_mouse = false;
    bool escape_quits = false;
    bool vsync = true;

    // Project-owned input bindings (v5). Storing semantic device/code names in
    // project data keeps editor Play Mode and standalone hosts on one mapping.
    std::vector<ProjectInputBinding> input_bindings;

    // Explicit project assets that should be present in standalone builds even
    // when the startup-scene dependency graph cannot discover them (music, UI
    // atlases, dynamically loaded scenes, etc.).
    std::vector<std::filesystem::path> build_includes;
    // Parallel stable IDs for build_includes. Empty entries represent legacy
    // path-only roots and remain supported.
    std::vector<std::string> build_include_asset_ids;

    [[nodiscard]] std::filesystem::path assets_root() const;
    [[nodiscard]] std::filesystem::path startup_scene_path() const;
    [[nodiscard]] std::filesystem::path managed_project_path() const;
    [[nodiscard]] std::filesystem::path resolve_asset(const std::filesystem::path& relative) const;
    [[nodiscard]] AssetReference startup_scene_reference() const;
    [[nodiscard]] AssetReference startup_ui_reference() const;
    [[nodiscard]] AssetReference lua_entry_reference() const;
    [[nodiscard]] AssetReference game_icon_reference() const;
    [[nodiscard]] AssetReference build_include_reference(std::size_t index) const;
    [[nodiscard]] std::filesystem::path build_output_root() const;
};

enum class ProjectValidationSeverity {
    Warning,
    Error,
};

struct ProjectValidationIssue {
    ProjectValidationSeverity severity = ProjectValidationSeverity::Warning;
    std::string message;
};

struct ProjectValidationOptions {
    bool require_managed_source = true;
};

// Rebuilds an InputMap from project-authored bindings. Returns false only for
// unsupported/invalid binding records; the destination map is cleared first.
bool configure_project_input_map(
    const VesperaProject& project,
    InputMap& map,
    std::string* error = nullptr
);

[[nodiscard]] std::vector<ProjectValidationIssue> validate_vespera_project(
    const VesperaProject& project,
    ProjectValidationOptions options = {}
);

struct ProjectIoResult {
    bool ok = false;
    std::string message;
    explicit operator bool() const { return ok; }
};

[[nodiscard]] ProjectIoResult load_vespera_project(
    VesperaProject& project,
    const std::filesystem::path& path
);

[[nodiscard]] ProjectIoResult save_vespera_project(
    const VesperaProject& project,
    const std::filesystem::path& path
);

} // namespace vespera
