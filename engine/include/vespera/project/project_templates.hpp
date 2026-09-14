#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace vespera {

enum class ProjectTemplateKind {
    Empty,
    Game2D,
    Game3D,
};

struct ProjectTemplateInfo {
    ProjectTemplateKind kind{};
    std::string_view id;
    std::string_view display_name;
    std::string_view description;
    std::string_view folder_name;
};

[[nodiscard]] std::span<const ProjectTemplateInfo> project_template_catalog();
[[nodiscard]] const ProjectTemplateInfo* project_template_info(ProjectTemplateKind kind);
[[nodiscard]] const ProjectTemplateInfo* project_template_info(std::string_view id);

struct ProjectCreateOptions {
    ProjectTemplateKind kind = ProjectTemplateKind::Game3D;
    std::string project_name;
    // Parent folder. The creator makes a sanitized child folder for project_name.
    std::filesystem::path destination_directory;
    // Folder containing empty/, 2d/, and 3d/ template directories.
    std::filesystem::path template_root;
};

struct ProjectCreateResult {
    bool ok = false;
    std::string message;
    std::filesystem::path project_root;
    std::filesystem::path project_file;
    explicit operator bool() const { return ok; }
};

[[nodiscard]] ProjectCreateResult create_project_from_template(const ProjectCreateOptions& options);

} // namespace vespera
