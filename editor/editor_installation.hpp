#pragma once

#include <array>
#include <filesystem>
#include <string_view>

namespace vespera::editor {

// Resolves either the root of a portable/installed Vespera distribution or the
// root of a source checkout when the editor is launched from a developer build.
[[nodiscard]] std::filesystem::path editor_installation_root();
[[nodiscard]] bool editor_is_distribution_installation();
[[nodiscard]] std::filesystem::path editor_user_settings_directory();

[[nodiscard]] constexpr std::array<std::string_view, 3> editor_builder_configuration_fallbacks(std::string_view configuration) {
    if (configuration == "Release") return {"Release", "Development", "Debug"};
    if (configuration == "Debug") return {"Debug", "Development", "Release"};
    return {"Development", "Release", "Debug"};
}

[[nodiscard]] std::filesystem::path editor_find_builder_executable(std::string_view configuration = "Development");
[[nodiscard]] std::filesystem::path editor_find_dotnet_executable();
[[nodiscard]] std::filesystem::path editor_find_sdk_project();
[[nodiscard]] std::filesystem::path editor_find_script_tool_project();
[[nodiscard]] std::filesystem::path editor_find_runtime_executable(
    std::string_view target_name,
    std::string_view configuration = "Development"
);

} // namespace vespera::editor
