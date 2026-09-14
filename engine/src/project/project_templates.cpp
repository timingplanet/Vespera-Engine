#include <vespera/project/project_templates.hpp>

#include <vespera/project/project.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <string>

namespace vespera {
namespace {

constexpr std::array<ProjectTemplateInfo, 3> kTemplates{{
    {ProjectTemplateKind::Game2D, "2d", "2D / UI Foundation (Experimental)", "Playable screen-space 2D starter using project-owned C# + RmlUi; dedicated orthographic world tooling is not advertised for 1.0.", "2d"},
    {ProjectTemplateKind::Game3D, "3d", "3D / 2.5D Game", "Sector-world starter with project-owned C# movement, room geometry, and lighting.", "3d"},
    {ProjectTemplateKind::Empty, "empty", "Empty Project", "Minimal Vespera project with a blank scene and project-owned C# bootstrap.", "empty"},
}};

std::string trim_copy(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c) != 0; });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c) != 0; }).base();
    if (first >= last) return {};
    return std::string(first, last);
}

std::string safe_stem(std::string_view name) {
    std::string result;
    result.reserve(name.size());
    bool pending_separator = false;
    for (unsigned char c : name) {
        if (std::isalnum(c)) {
            if (pending_separator && !result.empty()) result.push_back('-');
            result.push_back(static_cast<char>(c));
            pending_separator = false;
        } else if (c == '-' || c == '_') {
            if (!result.empty() && result.back() != '-' && result.back() != '_') result.push_back(static_cast<char>(c));
            pending_separator = false;
        } else if (std::isspace(c)) {
            pending_separator = !result.empty();
        }
    }
    while (!result.empty() && (result.back() == '-' || result.back() == '_')) result.pop_back();
    if (result.empty()) result = "VesperaProject";
    return result;
}

bool directory_empty_or_missing(const std::filesystem::path& path, std::error_code& ec) {
    if (!std::filesystem::exists(path, ec)) return !ec;
    if (ec || !std::filesystem::is_directory(path, ec) || ec) return false;
    return std::filesystem::directory_iterator(path, ec) == std::filesystem::directory_iterator{} && !ec;
}

} // namespace

std::span<const ProjectTemplateInfo> project_template_catalog() { return kTemplates; }

const ProjectTemplateInfo* project_template_info(ProjectTemplateKind kind) {
    for (const auto& info : kTemplates) if (info.kind == kind) return &info;
    return nullptr;
}

const ProjectTemplateInfo* project_template_info(std::string_view id) {
    for (const auto& info : kTemplates) if (info.id == id) return &info;
    return nullptr;
}

ProjectCreateResult create_project_from_template(const ProjectCreateOptions& options) {
    ProjectCreateResult result;
    const auto* info = project_template_info(options.kind);
    if (!info) {
        result.message = "unknown Vespera project template";
        return result;
    }

    const std::string project_name = trim_copy(options.project_name);
    if (project_name.empty()) {
        result.message = "project name cannot be empty";
        return result;
    }
    if (options.destination_directory.empty()) {
        result.message = "project destination cannot be empty";
        return result;
    }
    if (options.template_root.empty()) {
        result.message = "template root cannot be empty";
        return result;
    }

    std::error_code ec;
    const auto source = (options.template_root / std::string(info->folder_name)).lexically_normal();
    if (!std::filesystem::is_directory(source, ec) || ec) {
        result.message = "template folder is missing: " + source.string();
        return result;
    }

    const std::string stem = safe_stem(project_name);
    result.project_root = (options.destination_directory / stem).lexically_normal();
    if (!directory_empty_or_missing(result.project_root, ec)) {
        result.message = ec ? ("could not inspect destination: " + ec.message())
                            : ("destination already exists and is not empty: " + result.project_root.string());
        return result;
    }
    if (ec) {
        result.message = "could not inspect destination: " + ec.message();
        return result;
    }

    std::filesystem::create_directories(options.destination_directory, ec);
    if (ec) {
        result.message = "could not create project parent directory '" + options.destination_directory.string() + "': " + ec.message();
        return result;
    }
    std::filesystem::create_directories(result.project_root, ec);
    if (ec) {
        result.message = "could not create project directory '" + result.project_root.string() + "': " + ec.message();
        return result;
    }

    // Copy the template *contents* into the new project root. Copying the
    // source directory itself is implementation-sensitive when the destination
    // already exists and can accidentally create <project>/<template>/... .
    for (std::filesystem::directory_iterator it(source, ec), end; !ec && it != end; it.increment(ec)) {
        const auto destination = result.project_root / it->path().filename();
        std::filesystem::copy(it->path(), destination,
            std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) break;
    }
    if (ec) {
        result.message = "could not copy project template: " + ec.message();
        return result;
    }

    const auto template_project = result.project_root / "Template.vesperaproject";
    VesperaProject project;
    const auto loaded = load_vespera_project(project, template_project);
    if (!loaded) {
        result.message = "template project is invalid: " + loaded.message;
        return result;
    }

    project.name = project_name;
    project.window_title = project_name;
    project.product_version = "0.1.0";
    project.package_name = stem;
    project.executable_name = stem;
    project.root_directory = result.project_root;
    result.project_file = result.project_root / (stem + ".vesperaproject");
    project.project_file = result.project_file;

    const auto saved = save_vespera_project(project, result.project_file);
    if (!saved) {
        result.message = "could not finalize project file: " + saved.message;
        return result;
    }
    if (template_project != result.project_file) {
        std::filesystem::remove(template_project, ec);
        ec.clear();
    }

    result.ok = true;
    result.message = "Created " + project_name + " from the " + std::string(info->display_name) + " template.";
    return result;
}

} // namespace vespera
