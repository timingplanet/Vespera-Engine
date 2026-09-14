#include <vespera/project/project.hpp>
#include <vespera/input/input.hpp>
#include <utility>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <string_view>

#include <fstream>
#include <iomanip>
#include <sstream>

namespace vespera {
namespace {

ProjectIoResult fail(std::size_t line, std::string message) {
    if (line > 0) {
        message = "project line " + std::to_string(line) + ": " + message;
    }
    return {false, std::move(message)};
}

std::filesystem::path absolute_normalized(const std::filesystem::path& value) {
    std::error_code ec;
    const auto result = std::filesystem::absolute(value, ec);
    return ec ? value.lexically_normal() : result.lexically_normal();
}


std::string lowercase_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::optional<Key> project_key(std::string_view code) {
    if (code == "W") return Key::W;
    if (code == "A") return Key::A;
    if (code == "S") return Key::S;
    if (code == "D") return Key::D;
    if (code == "LeftShift") return Key::LeftShift;
    if (code == "Escape") return Key::Escape;
    if (code == "Space") return Key::Space;
    if (code == "Enter") return Key::Enter;
    if (code == "Up") return Key::Up;
    if (code == "Down") return Key::Down;
    if (code == "Left") return Key::Left;
    if (code == "Right") return Key::Right;
    if (code == "L") return Key::L;
    return std::nullopt;
}

std::optional<GamepadButton> project_gamepad_button(std::string_view code) {
    if (code == "South") return GamepadButton::South;
    if (code == "East") return GamepadButton::East;
    if (code == "West") return GamepadButton::West;
    if (code == "North") return GamepadButton::North;
    if (code == "Back") return GamepadButton::Back;
    if (code == "Guide") return GamepadButton::Guide;
    if (code == "Start") return GamepadButton::Start;
    if (code == "LeftStick") return GamepadButton::LeftStick;
    if (code == "RightStick") return GamepadButton::RightStick;
    if (code == "LeftShoulder") return GamepadButton::LeftShoulder;
    if (code == "RightShoulder") return GamepadButton::RightShoulder;
    if (code == "DpadUp") return GamepadButton::DpadUp;
    if (code == "DpadDown") return GamepadButton::DpadDown;
    if (code == "DpadLeft") return GamepadButton::DpadLeft;
    if (code == "DpadRight") return GamepadButton::DpadRight;
    return std::nullopt;
}

std::optional<GamepadAxis> project_gamepad_axis(std::string_view code) {
    if (code == "LeftX") return GamepadAxis::LeftX;
    if (code == "LeftY") return GamepadAxis::LeftY;
    if (code == "RightX") return GamepadAxis::RightX;
    if (code == "RightY") return GamepadAxis::RightY;
    if (code == "LeftTrigger") return GamepadAxis::LeftTrigger;
    if (code == "RightTrigger") return GamepadAxis::RightTrigger;
    return std::nullopt;
}

bool valid_project_binding(const ProjectInputBinding& binding, std::string* error) {
    const auto fail_binding = [&](std::string message) {
        if (error) *error = std::move(message);
        return false;
    };
    if (binding.action.empty()) return fail_binding("input binding action cannot be empty");
    if (!std::isfinite(binding.scale) || std::abs(binding.scale) > 100.0f) {
        return fail_binding("input binding scale must be finite and within +/-100");
    }
    if (!std::isfinite(binding.deadzone) || binding.deadzone < 0.0f || binding.deadzone > 0.95f) {
        return fail_binding("input binding deadzone must be between 0 and 0.95");
    }
    if (binding.device == "key") {
        if (!project_key(binding.code)) return fail_binding("unsupported key code '" + binding.code + "'");
        return true;
    }
    if (binding.device == "gamepad_button") {
        if (!project_gamepad_button(binding.code)) return fail_binding("unsupported gamepad button '" + binding.code + "'");
        return true;
    }
    if (binding.device == "gamepad_axis") {
        if (!project_gamepad_axis(binding.code)) return fail_binding("unsupported gamepad axis '" + binding.code + "'");
        return true;
    }
    return fail_binding("unsupported input device '" + binding.device + "'");
}

} // namespace

std::filesystem::path VesperaProject::assets_root() const {
    return (root_directory / assets_directory).lexically_normal();
}

std::filesystem::path VesperaProject::startup_scene_path() const {
    return resolve_asset(startup_scene);
}

std::filesystem::path VesperaProject::managed_project_path() const {
    if (managed_project.empty()) return {};
    return (root_directory / managed_project).lexically_normal();
}

std::filesystem::path VesperaProject::resolve_asset(const std::filesystem::path& relative) const {
    if (relative.is_absolute()) return relative.lexically_normal();
    return (assets_root() / relative).lexically_normal();
}

AssetReference VesperaProject::startup_scene_reference() const {
    return {startup_scene_asset_id, startup_scene};
}

AssetReference VesperaProject::startup_ui_reference() const {
    return {startup_ui_asset_id, startup_ui};
}

AssetReference VesperaProject::lua_entry_reference() const {
    return {lua_entry_asset_id, lua_entry};
}

AssetReference VesperaProject::game_icon_reference() const {
    return {game_icon_asset_id, game_icon};
}

std::filesystem::path VesperaProject::build_output_root() const {
    if (build_output_directory.empty()) return (root_directory / "builds").lexically_normal();
    if (build_output_directory.is_absolute()) return build_output_directory.lexically_normal();
    return (root_directory / build_output_directory).lexically_normal();
}

AssetReference VesperaProject::build_include_reference(std::size_t index) const {
    if (index >= build_includes.size()) return {};
    const std::string asset_id = index < build_include_asset_ids.size() ? build_include_asset_ids[index] : std::string{};
    return {asset_id, build_includes[index]};
}


std::string_view managed_deployment_mode_name(ManagedDeploymentMode mode) {
    return mode == ManagedDeploymentMode::Portable ? "portable" : "framework-dependent";
}

std::optional<ManagedDeploymentMode> managed_deployment_mode_from_name(std::string_view name) {
    std::string value = lowercase_ascii(std::string(name));
    if (value == "framework-dependent" || value == "framework_dependent" || value == "framework")
        return ManagedDeploymentMode::FrameworkDependent;
    if (value == "portable" || value == "self-contained" || value == "self_contained")
        return ManagedDeploymentMode::Portable;
    return std::nullopt;
}

bool configure_project_input_map(
    const VesperaProject& project,
    InputMap& map,
    std::string* error
) {
    map.clear();
    for (std::size_t i = 0; i < project.input_bindings.size(); ++i) {
        const auto& binding = project.input_bindings[i];
        std::string binding_error;
        if (!valid_project_binding(binding, &binding_error)) {
            if (error) *error = "input binding #" + std::to_string(i + 1) + ": " + binding_error;
            map.clear();
            return false;
        }
        if (binding.device == "key") {
            map.bind_key(binding.action, *project_key(binding.code), binding.scale);
        } else if (binding.device == "gamepad_button") {
            map.bind_gamepad_button(binding.action, *project_gamepad_button(binding.code), binding.scale);
        } else if (binding.device == "gamepad_axis") {
            map.bind_gamepad_axis(binding.action, *project_gamepad_axis(binding.code), binding.scale, binding.deadzone);
        }
    }
    if (error) error->clear();
    return true;
}

std::vector<ProjectValidationIssue> validate_vespera_project(
    const VesperaProject& project,
    ProjectValidationOptions options
) {
    std::vector<ProjectValidationIssue> issues;
    const auto add = [&](ProjectValidationSeverity severity, std::string message) {
        issues.push_back({severity, std::move(message)});
    };

    std::error_code ec;
    if (project.root_directory.empty() || !std::filesystem::is_directory(project.root_directory, ec) || ec) {
        add(ProjectValidationSeverity::Error, "project root directory does not exist");
        ec.clear();
    }
    if (!std::filesystem::is_directory(project.assets_root(), ec) || ec) {
        add(ProjectValidationSeverity::Error, "assets directory does not exist: " + project.assets_root().string());
        ec.clear();
    }
    if (!project.startup_scene.empty()) {
        if (!std::filesystem::is_regular_file(project.startup_scene_path(), ec) || ec) {
            add(project.startup_scene_asset_id.empty() ? ProjectValidationSeverity::Error : ProjectValidationSeverity::Warning,
                "startup scene fallback path does not exist: " + project.startup_scene_path().string()
                + (project.startup_scene_asset_id.empty() ? std::string{} : " (stable asset ID will be resolved from the catalog)"));
            ec.clear();
        } else if (project.startup_scene_path().extension() != ".slscene") {
            add(ProjectValidationSeverity::Warning, "startup scene does not use the .slscene extension");
        }
    } else if (project.startup_scene_asset_id.empty()) {
        add(ProjectValidationSeverity::Error, "startup scene reference is empty");
    }
    if (!project.startup_ui.empty() || !project.startup_ui_asset_id.empty()) {
        if (project.startup_ui.empty()) {
            add(ProjectValidationSeverity::Error, "startup UI stable reference requires a readable fallback path");
        } else {
            const auto startup_ui_path = project.resolve_asset(project.startup_ui);
            if (!std::filesystem::is_regular_file(startup_ui_path, ec) || ec) {
                add(project.startup_ui_asset_id.empty() ? ProjectValidationSeverity::Error : ProjectValidationSeverity::Warning,
                    "startup UI fallback path does not exist: " + startup_ui_path.string()
                    + (project.startup_ui_asset_id.empty() ? std::string{} : " (stable asset ID will be resolved from the catalog)"));
                ec.clear();
            } else if (startup_ui_path.extension() != ".rml") {
                add(ProjectValidationSeverity::Error, "startup UI must use the .rml extension");
            }
        }
    }
    if (!project.lua_entry.empty() || !project.lua_entry_asset_id.empty()) {
        if (project.lua_entry.empty()) {
            add(ProjectValidationSeverity::Warning, "Lua entry has a stable asset ID but no readable fallback path");
        } else {
            const auto lua_path = project.resolve_asset(project.lua_entry);
            if (!std::filesystem::is_regular_file(lua_path, ec) || ec) {
                add(project.lua_entry_asset_id.empty() ? ProjectValidationSeverity::Error : ProjectValidationSeverity::Warning,
                    "Lua entry fallback path does not exist: " + lua_path.string()
                    + (project.lua_entry_asset_id.empty() ? std::string{} : " (stable asset ID will be resolved from the catalog)"));
                ec.clear();
            } else if (lua_path.extension() != ".lua") {
                add(ProjectValidationSeverity::Error, "Lua entry must reference a .lua asset: " + lua_path.string());
            }
        }
    }

    if (!project.managed_project.empty()) {
        if (!std::filesystem::is_regular_file(project.managed_project_path(), ec) || ec) {
            if (options.require_managed_source) {
                add(ProjectValidationSeverity::Error,
                    "managed C# project does not exist: " + project.managed_project_path().string());
            }
            ec.clear();
        }
        if (project.managed_assembly.empty()) {
            add(ProjectValidationSeverity::Error, "managed_project is set but managed_assembly is empty");
        }
    }
    if (project.game_target.empty()) {
        add(ProjectValidationSeverity::Warning, "game_target is empty; export will use the shared vespera_player runtime");
    }
    if (project.product_version.empty()) {
        add(ProjectValidationSeverity::Error, "product_version cannot be empty");
    }
    if (project.executable_name.empty()) {
        add(ProjectValidationSeverity::Warning, "executable_name is empty; export will derive a safe name from the project name");
    } else {
        const bool safe_executable = std::all_of(project.executable_name.begin(), project.executable_name.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '.' || c == '_' || c == '-';
        });
        if (!safe_executable || project.executable_name == "." || project.executable_name == "..")
            add(ProjectValidationSeverity::Error, "executable_name may contain only letters, numbers, '.', '_' and '-' and cannot be '.' or '..'");
    }
    if (project.build_output_directory.empty()) {
        add(ProjectValidationSeverity::Warning, "build_output_directory is empty; export will use a 'builds' folder beside the project");
    }
    if (!project.game_icon.empty()) {
        if (project.game_icon.is_absolute()) {
            add(ProjectValidationSeverity::Error, "game_icon must be a project-relative asset path");
        }
        for (const auto& part : project.game_icon.lexically_normal()) {
            if (part == "..") {
                add(ProjectValidationSeverity::Error, "game_icon cannot escape the project Assets root");
                break;
            }
        }
        std::error_code icon_ec;
        const auto icon_path = project.resolve_asset(project.game_icon);
        if (!std::filesystem::is_regular_file(icon_path, icon_ec) || icon_ec) {
            add(ProjectValidationSeverity::Warning,
                "game icon fallback path does not exist: " + icon_path.string()
                + (project.game_icon_asset_id.empty() ? std::string{} : " (stable asset ID will be resolved from the catalog)"));
        }
    }
    if (!project.package_name.empty()) {
        const bool safe = std::all_of(project.package_name.begin(), project.package_name.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '.' || c == '_' || c == '-';
        });
        if (!safe) add(ProjectValidationSeverity::Error, "package_name may contain only letters, numbers, '.', '_' and '-'");
    }
    if (project.managed_deployment == ManagedDeploymentMode::Portable && project.managed_assembly.empty()) {
        add(ProjectValidationSeverity::Warning, "portable managed deployment is selected but this project has no managed assembly");
    }
    if (project.window_width < 320 || project.window_width > 16384) {
        add(ProjectValidationSeverity::Error, "window_width must be between 320 and 16384");
    }
    if (project.window_height < 200 || project.window_height > 16384) {
        add(ProjectValidationSeverity::Error, "window_height must be between 200 and 16384");
    }
    for (std::size_t i = 0; i < project.input_bindings.size(); ++i) {
        std::string binding_error;
        if (!valid_project_binding(project.input_bindings[i], &binding_error)) {
            add(ProjectValidationSeverity::Error,
                "input binding #" + std::to_string(i + 1) + ": " + binding_error);
        }
    }
    std::vector<std::string> seen_build_includes;
    std::vector<std::string> seen_build_ids;
    for (std::size_t i = 0; i < project.build_includes.size(); ++i) {
        const auto& include = project.build_includes[i];
        const std::string asset_id = i < project.build_include_asset_ids.size() ? project.build_include_asset_ids[i] : std::string{};
        if (include.empty() && asset_id.empty()) {
            add(ProjectValidationSeverity::Warning, "build include contains an empty reference");
            continue;
        }
        if (include.is_absolute()) {
            add(ProjectValidationSeverity::Error, "build include fallback path must be project-asset relative: " + include.string());
            continue;
        }
        if (!asset_id.empty()) {
            if (std::find(seen_build_ids.begin(), seen_build_ids.end(), asset_id) != seen_build_ids.end()) {
                add(ProjectValidationSeverity::Warning, "duplicate build include asset ID: " + asset_id);
                continue;
            }
            seen_build_ids.push_back(asset_id);
        }
        if (!include.empty()) {
            const auto normalized = include.lexically_normal().generic_string();
            if (std::find(seen_build_includes.begin(), seen_build_includes.end(), normalized) != seen_build_includes.end()) {
                add(ProjectValidationSeverity::Warning, "duplicate build include fallback path: " + normalized);
                continue;
            }
            seen_build_includes.push_back(normalized);
            if (!std::filesystem::is_regular_file(project.resolve_asset(include), ec) || ec) {
                add(ProjectValidationSeverity::Warning, "build include fallback path does not exist: " + normalized
                    + (asset_id.empty() ? std::string{} : " (stable asset ID will be resolved from the catalog)"));
                ec.clear();
            }
        }
    }
    return issues;
}

ProjectIoResult load_vespera_project(VesperaProject& project, const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) return {false, "could not open Vespera project: " + path.string()};

    VesperaProject candidate;
    candidate.project_file = absolute_normalized(path);
    candidate.root_directory = candidate.project_file.parent_path();

    std::string line;
    std::size_t line_number = 0;
    bool saw_header = false;
    while (std::getline(input, line)) {
        ++line_number;
        const auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos || line[first] == '#') continue;

        std::istringstream stream(line.substr(first));
        std::string command;
        stream >> command;
        if (!saw_header) {
            if (command != "vespera_project") return fail(line_number, "expected 'vespera_project' header");
            int version = 0;
            if (!(stream >> version) || version < 1 || version > 10) return fail(line_number, "unsupported project format version");
            candidate.format_version = version;
            saw_header = true;
            continue;
        }

        if (command == "name") {
            if (!(stream >> std::quoted(candidate.name))) return fail(line_number, "invalid project name");
        } else if (command == "assets") {
            std::string value;
            if (!(stream >> std::quoted(value))) return fail(line_number, "invalid assets path");
            candidate.assets_directory = value;
        } else if (command == "startup_scene") {
            std::string value;
            if (!(stream >> std::quoted(value))) return fail(line_number, "invalid startup scene");
            candidate.startup_scene = value;
            candidate.startup_scene_asset_id.clear();
        } else if (command == "startup_scene_asset") {
            if (candidate.format_version < 4) return fail(line_number, "startup_scene_asset requires project format 4");
            std::string id;
            std::string value;
            if (!(stream >> std::quoted(id) >> std::quoted(value)) || id.empty()) return fail(line_number, "invalid startup scene asset reference");
            candidate.startup_scene_asset_id = std::move(id);
            candidate.startup_scene = value;
        } else if (command == "startup_ui") {
            if (candidate.format_version < 8) return fail(line_number, "startup_ui requires project format 8");
            std::string value;
            if (!(stream >> std::quoted(value)) || value.empty()) return fail(line_number, "invalid startup UI path");
            candidate.startup_ui = value;
            candidate.startup_ui_asset_id.clear();
        } else if (command == "startup_ui_asset") {
            if (candidate.format_version < 8) return fail(line_number, "startup_ui_asset requires project format 8");
            std::string id;
            std::string value;
            if (!(stream >> std::quoted(id) >> std::quoted(value)) || id.empty() || value.empty())
                return fail(line_number, "invalid startup UI asset reference");
            candidate.startup_ui_asset_id = std::move(id);
            candidate.startup_ui = value;
        } else if (command == "lua_entry") {
            if (candidate.format_version < 9) return fail(line_number, "lua_entry requires project format 9");
            std::string value;
            if (!(stream >> std::quoted(value)) || value.empty()) return fail(line_number, "invalid Lua entry path");
            candidate.lua_entry = value;
            candidate.lua_entry_asset_id.clear();
        } else if (command == "lua_entry_asset") {
            if (candidate.format_version < 9) return fail(line_number, "lua_entry_asset requires project format 9");
            std::string id;
            std::string value;
            if (!(stream >> std::quoted(id) >> std::quoted(value)) || id.empty() || value.empty())
                return fail(line_number, "invalid Lua entry asset reference");
            candidate.lua_entry_asset_id = std::move(id);
            candidate.lua_entry = value;
        } else if (command == "managed_project") {
            std::string value;
            if (!(stream >> std::quoted(value))) return fail(line_number, "invalid managed project path");
            candidate.managed_project = value;
        } else if (command == "managed_assembly") {
            if (!(stream >> std::quoted(candidate.managed_assembly))) return fail(line_number, "invalid managed assembly name");
        } else if (command == "game_target") {
            if (!(stream >> std::quoted(candidate.game_target))) return fail(line_number, "invalid game target");
        } else if (command == "company_name") {
            if (candidate.format_version < 6) return fail(line_number, "company_name requires project format 6");
            if (!(stream >> std::quoted(candidate.company_name))) return fail(line_number, "invalid company name");
        } else if (command == "product_version") {
            if (candidate.format_version < 6) return fail(line_number, "product_version requires project format 6");
            if (!(stream >> std::quoted(candidate.product_version)) || candidate.product_version.empty()) return fail(line_number, "invalid product version");
        } else if (command == "package_name") {
            if (candidate.format_version < 6) return fail(line_number, "package_name requires project format 6");
            if (!(stream >> std::quoted(candidate.package_name))) return fail(line_number, "invalid package name");
        } else if (command == "managed_deployment") {
            if (candidate.format_version < 6) return fail(line_number, "managed_deployment requires project format 6");
            std::string value;
            if (!(stream >> std::quoted(value))) return fail(line_number, "invalid managed deployment mode");
            const auto mode = managed_deployment_mode_from_name(value);
            if (!mode) return fail(line_number, "managed_deployment must be framework-dependent or portable");
            candidate.managed_deployment = *mode;
        } else if (command == "executable_name") {
            if (candidate.format_version < 7) return fail(line_number, "executable_name requires project format 7");
            if (!(stream >> std::quoted(candidate.executable_name))) return fail(line_number, "invalid executable name");
        } else if (command == "build_output_directory") {
            if (candidate.format_version < 7) return fail(line_number, "build_output_directory requires project format 7");
            std::string value;
            if (!(stream >> std::quoted(value))) return fail(line_number, "invalid build output directory");
            candidate.build_output_directory = value;
        } else if (command == "game_icon") {
            if (candidate.format_version < 7) return fail(line_number, "game_icon requires project format 7");
            std::string value;
            if (!(stream >> std::quoted(value))) return fail(line_number, "invalid game icon path");
            candidate.game_icon = value;
            candidate.game_icon_asset_id.clear();
        } else if (command == "game_icon_asset") {
            if (candidate.format_version < 7) return fail(line_number, "game_icon_asset requires project format 7");
            std::string id;
            std::string value;
            if (!(stream >> std::quoted(id) >> std::quoted(value)) || id.empty()) return fail(line_number, "invalid game icon asset reference");
            candidate.game_icon_asset_id = std::move(id);
            candidate.game_icon = value;
        } else if (command == "development_diagnostics") {
            if (candidate.format_version < 7) return fail(line_number, "development_diagnostics requires project format 7");
            int value = 0;
            if (!(stream >> value) || (value != 0 && value != 1)) return fail(line_number, "invalid development_diagnostics value");
            candidate.development_diagnostics = value != 0;
        } else if (command == "window_title") {
            if (!(stream >> std::quoted(candidate.window_title))) return fail(line_number, "invalid window title");
        } else if (command == "window_width") {
            if (!(stream >> candidate.window_width)) return fail(line_number, "invalid window width");
        } else if (command == "window_height") {
            if (!(stream >> candidate.window_height)) return fail(line_number, "invalid window height");
        } else if (command == "window_resizable") {
            int value = 0;
            if (!(stream >> value) || (value != 0 && value != 1)) return fail(line_number, "invalid window_resizable value");
            candidate.window_resizable = value != 0;
        } else if (command == "relative_mouse") {
            int value = 0;
            if (!(stream >> value) || (value != 0 && value != 1)) return fail(line_number, "invalid relative_mouse value");
            candidate.relative_mouse = value != 0;
        } else if (command == "escape_quits") {
            int value = 0;
            if (!(stream >> value) || (value != 0 && value != 1)) return fail(line_number, "invalid escape_quits value");
            candidate.escape_quits = value != 0;
        } else if (command == "vsync") {
            if (candidate.format_version < 10) return fail(line_number, "vsync requires project format 10");
            int value = 0;
            if (!(stream >> value) || (value != 0 && value != 1)) return fail(line_number, "invalid vsync value");
            candidate.vsync = value != 0;
        } else if (command == "input_bind") {
            if (candidate.format_version < 5) return fail(line_number, "input_bind requires project format 5");
            ProjectInputBinding binding;
            if (!(stream >> std::quoted(binding.action) >> std::quoted(binding.device) >> std::quoted(binding.code)
                    >> binding.scale >> binding.deadzone)) {
                return fail(line_number, "invalid input_bind record");
            }
            std::string binding_error;
            if (!valid_project_binding(binding, &binding_error)) return fail(line_number, binding_error);
            candidate.input_bindings.push_back(std::move(binding));
        } else if (command == "build_include") {
            std::string value;
            if (!(stream >> std::quoted(value)) || value.empty()) return fail(line_number, "invalid build_include path");
            candidate.build_includes.emplace_back(value);
            candidate.build_include_asset_ids.emplace_back();
        } else if (command == "build_include_asset") {
            if (candidate.format_version < 4) return fail(line_number, "build_include_asset requires project format 4");
            std::string id;
            std::string value;
            if (!(stream >> std::quoted(id) >> std::quoted(value)) || id.empty()) return fail(line_number, "invalid build include asset reference");
            candidate.build_include_asset_ids.push_back(std::move(id));
            candidate.build_includes.emplace_back(value);
        } else if (command == "end_project") {
            break;
        } else {
            return fail(line_number, "unknown project command '" + command + "'");
        }
    }

    if (!saw_header) return {false, "project file is empty or missing a header"};
    if (candidate.name.empty()) return {false, "project name cannot be empty"};
    if (candidate.assets_directory.empty()) return {false, "assets path cannot be empty"};
    if (candidate.startup_scene.empty() && candidate.startup_scene_asset_id.empty()) return {false, "startup scene reference cannot be empty"};

    // v1 projects predate runtime-window settings. Preserve their historical
    // behavior while upgrading in-memory representation to the current format.
    if (candidate.window_title.empty()) candidate.window_title = candidate.name;
    if (candidate.executable_name.empty()) {
        candidate.executable_name = candidate.name;
        for (char& c : candidate.executable_name) {
            const bool allowed = std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-';
            if (!allowed) c = '-';
        }
        while (!candidate.executable_name.empty() && candidate.executable_name.back() == '-') candidate.executable_name.pop_back();
        if (candidate.executable_name.empty()) candidate.executable_name = "VesperaGame";
    }
    if (candidate.build_output_directory.empty()) candidate.build_output_directory = "builds";
    candidate.format_version = 10;
    project = std::move(candidate);
    return {true, "loaded Vespera project '" + project.name + "'"};
}

ProjectIoResult save_vespera_project(const VesperaProject& project, const std::filesystem::path& path) {
    std::ofstream output(path, std::ios::trunc);
    if (!output) return {false, "could not write Vespera project: " + path.string()};

    output << "vespera_project 10\n";
    output << "name " << std::quoted(project.name) << "\n";
    output << "assets " << std::quoted(project.assets_directory.generic_string()) << "\n";
    if (!project.startup_scene_asset_id.empty()) {
        output << "startup_scene_asset " << std::quoted(project.startup_scene_asset_id) << " "
               << std::quoted(project.startup_scene.generic_string()) << "\n";
    } else {
        output << "startup_scene " << std::quoted(project.startup_scene.generic_string()) << "\n";
    }
    if (!project.startup_ui_asset_id.empty()) {
        output << "startup_ui_asset " << std::quoted(project.startup_ui_asset_id) << " "
               << std::quoted(project.startup_ui.generic_string()) << "\n";
    } else if (!project.startup_ui.empty()) {
        output << "startup_ui " << std::quoted(project.startup_ui.generic_string()) << "\n";
    }
    if (!project.lua_entry_asset_id.empty()) {
        output << "lua_entry_asset " << std::quoted(project.lua_entry_asset_id) << " "
               << std::quoted(project.lua_entry.generic_string()) << "\n";
    } else if (!project.lua_entry.empty()) {
        output << "lua_entry " << std::quoted(project.lua_entry.generic_string()) << "\n";
    }
    if (!project.managed_project.empty()) {
        output << "managed_project " << std::quoted(project.managed_project.generic_string()) << "\n";
    }
    if (!project.managed_assembly.empty()) {
        output << "managed_assembly " << std::quoted(project.managed_assembly) << "\n";
    }
    if (!project.game_target.empty()) {
        output << "game_target " << std::quoted(project.game_target) << "\n";
    }
    output << "company_name " << std::quoted(project.company_name) << "\n";
    output << "product_version " << std::quoted(project.product_version.empty() ? std::string("0.1.0") : project.product_version) << "\n";
    output << "package_name " << std::quoted(project.package_name) << "\n";
    output << "managed_deployment " << std::quoted(std::string(managed_deployment_mode_name(project.managed_deployment))) << "\n";
    output << "executable_name " << std::quoted(project.executable_name) << "\n";
    output << "build_output_directory " << std::quoted((project.build_output_directory.empty() ? std::filesystem::path("builds") : project.build_output_directory).generic_string()) << "\n";
    if (!project.game_icon_asset_id.empty()) {
        output << "game_icon_asset " << std::quoted(project.game_icon_asset_id) << " "
               << std::quoted(project.game_icon.generic_string()) << "\n";
    } else if (!project.game_icon.empty()) {
        output << "game_icon " << std::quoted(project.game_icon.generic_string()) << "\n";
    }
    output << "development_diagnostics " << (project.development_diagnostics ? 1 : 0) << "\n";
    output << "window_title " << std::quoted(project.window_title.empty() ? project.name : project.window_title) << "\n";
    output << "window_width " << project.window_width << "\n";
    output << "window_height " << project.window_height << "\n";
    output << "window_resizable " << (project.window_resizable ? 1 : 0) << "\n";
    output << "relative_mouse " << (project.relative_mouse ? 1 : 0) << "\n";
    output << "escape_quits " << (project.escape_quits ? 1 : 0) << "\n";
    output << "vsync " << (project.vsync ? 1 : 0) << "\n";
    for (const auto& binding : project.input_bindings) {
        output << "input_bind " << std::quoted(binding.action) << " "
               << std::quoted(binding.device) << " " << std::quoted(binding.code) << " "
               << binding.scale << " " << binding.deadzone << "\n";
    }
    for (std::size_t i = 0; i < project.build_includes.size(); ++i) {
        const auto& include = project.build_includes[i];
        const std::string id = i < project.build_include_asset_ids.size() ? project.build_include_asset_ids[i] : std::string{};
        if (!id.empty()) {
            output << "build_include_asset " << std::quoted(id) << " "
                   << std::quoted(include.lexically_normal().generic_string()) << "\n";
        } else if (!include.empty()) {
            output << "build_include " << std::quoted(include.lexically_normal().generic_string()) << "\n";
        }
    }
    output << "end_project\n";
    if (!output) return {false, "failed while writing Vespera project: " + path.string()};
    return {true, "saved Vespera project: " + path.string()};
}

} // namespace vespera
