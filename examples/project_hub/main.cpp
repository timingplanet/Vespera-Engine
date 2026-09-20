#include <vespera/core/application.hpp>
#include <vespera/core/game.hpp>
#include <vespera/core/log.hpp>
#include <vespera/core/version.hpp>
#include <vespera/project/project.hpp>
#include <vespera/project/project_templates.hpp>
#include <vespera/platform/process.hpp>
#include <vespera/render/render_backend.hpp>
#include <vespera/ui/rmlui_surface.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace {

std::filesystem::path default_projects_directory() {
#ifdef _WIN32
    // USERPROFILE\Documents is not reliable when Windows Known Folder Move /
    // OneDrive redirects Documents. Ask Windows for the real Documents folder
    // and request that the known folder itself exists before we append ours.
    PWSTR documents = nullptr;
    const HRESULT result = SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_CREATE, nullptr, &documents);
    if (SUCCEEDED(result) && documents) {
        std::filesystem::path path(documents);
        CoTaskMemFree(documents);
        return path / "Vespera Projects";
    }
    if (documents) CoTaskMemFree(documents);
    if (const char* user = std::getenv("USERPROFILE"); user && *user)
        return std::filesystem::path(user) / "Vespera Projects";
#endif
    if (const char* home = std::getenv("HOME"); home && *home)
        return std::filesystem::path(home) / "Vespera Projects";
    return std::filesystem::current_path() / "Vespera Projects";
}

std::filesystem::path path_from_ui_text(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.remove_prefix(1);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.remove_suffix(1);
    if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\''))) {
        value.remove_prefix(1);
        value.remove_suffix(1);
    }
    std::u8string utf8;
    utf8.reserve(value.size());
    for (unsigned char c : value) utf8.push_back(static_cast<char8_t>(c));
    return std::filesystem::path(utf8).lexically_normal();
}

std::filesystem::path settings_directory() {
#ifdef _WIN32
    if (const char* local = std::getenv("LOCALAPPDATA"); local && *local)
        return std::filesystem::path(local) / "Vespera";
#endif
#ifndef _WIN32
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        return std::filesystem::path(xdg) / "Vespera";
    if (const char* home = std::getenv("HOME"); home && *home)
        return std::filesystem::path(home) / ".config" / "Vespera";
#else
    if (const char* home = std::getenv("HOME"); home && *home)
        return std::filesystem::path(home) / ".vespera";
#endif
    return std::filesystem::current_path();
}

std::filesystem::path hub_runtime_root() {
    const auto executable = vespera::platform::current_executable_path();
    if (!executable.empty()) return executable.parent_path().lexically_normal();
    return std::filesystem::current_path();
}

std::filesystem::path default_hub_result_file() {
    return settings_directory() / "hub_result.txt";
}

std::vector<std::filesystem::path> read_recent_projects() {
    std::vector<std::filesystem::path> result;
    std::ifstream input(settings_directory() / "recent_projects.txt");
    std::string line;
    while (std::getline(input, line) && result.size() < 8u) {
        if (line.empty()) continue;
        std::error_code ec;
        const std::filesystem::path path = std::filesystem::path(line).lexically_normal();
        if (std::filesystem::is_regular_file(path, ec) && !ec && path.extension() == ".vesperaproject")
            result.push_back(path);
    }
    return result;
}

void remember_project(const std::filesystem::path& project) {
    auto recent = read_recent_projects();
    const auto normalized = std::filesystem::absolute(project).lexically_normal();
    recent.erase(std::remove_if(recent.begin(), recent.end(), [&](const auto& item) {
        return std::filesystem::absolute(item).lexically_normal() == normalized;
    }), recent.end());
    recent.insert(recent.begin(), normalized);
    if (recent.size() > 8u) recent.resize(8u);

    std::error_code ec;
    std::filesystem::create_directories(settings_directory(), ec);
    std::ofstream output(settings_directory() / "recent_projects.txt", std::ios::trunc);
    for (const auto& item : recent) output << item.string() << '\n';
}

bool write_result(const std::filesystem::path& result_file, const std::filesystem::path& project, std::string& error) {
    std::error_code ec;
    if (!result_file.parent_path().empty()) std::filesystem::create_directories(result_file.parent_path(), ec);
    std::ofstream output(result_file, std::ios::trunc);
    if (!output) {
        error = "Could not write Project Hub result file: " + result_file.string();
        return false;
    }
    output << std::filesystem::absolute(project).lexically_normal().string() << '\n';
    return true;
}


std::filesystem::path find_editor_executable(const std::filesystem::path& explicit_path = {}) {
    std::error_code ec;
    if (!explicit_path.empty() && std::filesystem::is_regular_file(explicit_path, ec) && !ec) {
        return std::filesystem::absolute(explicit_path).lexically_normal();
    }
    if (const char* env = std::getenv("VESPERA_EDITOR_PATH"); env && *env) {
        std::filesystem::path path(env);
        ec.clear();
        if (std::filesystem::is_regular_file(path, ec) && !ec) return std::filesystem::absolute(path).lexically_normal();
    }

    const auto self = vespera::platform::current_executable_path();
    if (self.empty()) return {};
    const auto dir = self.parent_path();
#ifdef _WIN32
    const std::vector<std::filesystem::path> sibling_candidates{
        dir / "VesperaEditor.exe",
        dir / "vespera_editor.exe",
    };
#else
    const std::vector<std::filesystem::path> sibling_candidates{
        dir / "VesperaEditor",
        dir / "vespera_editor",
    };
#endif
    for (const auto& candidate : sibling_candidates) {
        ec.clear();
        if (std::filesystem::is_regular_file(candidate, ec) && !ec) return candidate.lexically_normal();
    }

    // Source-build fallback. Windows multi-config output lives one directory
    // deeper than Linux/Ninja output, but both can be derived from the Hub path.
#ifdef _WIN32
    const auto config = dir.filename();
    const auto build_root = dir.parent_path().parent_path().parent_path();
    const auto source_candidate = build_root / "editor" / config / "vespera_editor.exe";
#else
    const auto build_root = dir.parent_path().parent_path();
    const auto source_candidate = build_root / "editor" / "vespera_editor";
#endif
    ec.clear();
    if (std::filesystem::is_regular_file(source_candidate, ec) && !ec) return source_candidate.lexically_normal();
    return {};
}

bool launch_editor_for_project(
    const std::filesystem::path& editor,
    const std::filesystem::path& project,
    std::string& error
) {
    if (editor.empty()) {
        error = "Vespera Editor could not be located beside the Project Hub.";
        return false;
    }
    vespera::platform::ProcessOptions process;
    process.executable = editor;
    process.arguments = {std::filesystem::absolute(project).lexically_normal().string()};
    process.working_directory = editor.parent_path();
    return vespera::platform::launch_process(process, &error);
}

class ProjectHubGame final : public vespera::Game {
public:
    ProjectHubGame(
        std::filesystem::path templates,
        std::filesystem::path result,
        std::filesystem::path editor,
        std::filesystem::path ui_document,
        bool launch_editor)
        : template_root_(std::move(templates)),
          result_file_(std::move(result)),
          editor_path_(std::move(editor)),
          ui_document_(std::move(ui_document)),
          launch_editor_(launch_editor) {}

    void on_start(vespera::GameContext&) override {
        const auto loaded = ui_.initialize(ui_document_, 1180, 760);
        if (!loaded) {
            failed_ = true;
            vespera::log::error("Project Hub UI failed: " + loaded.message);
            return;
        }
        (void)ui_.set_text("engine-version", std::string("Vespera Engine ") + std::string(vespera::kEngineVersion));
        (void)ui_.set_value("project-name", "My Vespera Game");
        (void)ui_.set_value("project-location", default_projects_directory().string());
        select_template(vespera::ProjectTemplateKind::Game3D);
        refresh_recent();
        set_status("Choose a template or open an existing project.", false);
    }

    void on_update(vespera::GameContext& context, double) override {
        if (failed_) return;
        ui_.process_input(context.input);

        if (ui_.consume_clicks("template-2d") > 0) select_template(vespera::ProjectTemplateKind::Game2D);
        if (ui_.consume_clicks("template-3d") > 0) select_template(vespera::ProjectTemplateKind::Game3D);
        if (ui_.consume_clicks("template-empty") > 0) select_template(vespera::ProjectTemplateKind::Empty);
        if (ui_.consume_clicks("create-project") > 0) create_project();
        if (ui_.consume_clicks("open-project") > 0) open_project(ui_.value("open-path"));

        for (std::size_t i = 0; i < recent_.size() && i < 5u; ++i) {
            if (ui_.consume_clicks(std::format("recent-{}", i + 1)) > 0) {
                open_project(recent_[i].string());
                break;
            }
        }
    }

    void on_render(vespera::GameContext&, vespera::RenderBackend& renderer, double) override {
        if (failed_) return;
        ui_.resize(std::max(renderer.target_width(), 1), std::max(renderer.target_height(), 1));
        auto packet = ui_.build_packet();
        renderer.render_ui(packet);
        if (!reported_warnings_ && !packet.warnings.empty()) {
            reported_warnings_ = true;
            for (const auto& warning : packet.warnings) vespera::log::warn("Project Hub UI: " + warning);
        }
    }

    void on_stop(vespera::GameContext&) override { ui_.shutdown(); }
    bool wants_quit() const override { return quit_; }

private:
    void select_template(vespera::ProjectTemplateKind kind) {
        selected_ = kind;
        (void)ui_.set_class("template-2d", "selected", kind == vespera::ProjectTemplateKind::Game2D);
        (void)ui_.set_class("template-3d", "selected", kind == vespera::ProjectTemplateKind::Game3D);
        (void)ui_.set_class("template-empty", "selected", kind == vespera::ProjectTemplateKind::Empty);
        if (const auto* info = vespera::project_template_info(kind))
            (void)ui_.set_text("template-summary", std::string(info->display_name) + " — " + std::string(info->description));
    }

    void refresh_recent() {
        recent_ = read_recent_projects();
        for (std::size_t i = 0; i < 5u; ++i) {
            const std::string id = std::format("recent-{}", i + 1);
            if (i < recent_.size()) {
                (void)ui_.set_text(id, recent_[i].stem().string() + "\n" + recent_[i].parent_path().string());
                (void)ui_.set_visible(id, true);
            } else {
                (void)ui_.set_visible(id, false);
            }
        }
        (void)ui_.set_visible("recent-empty", recent_.empty());
    }

    void set_status(std::string message, bool error) {
        (void)ui_.set_text("status", message);
        (void)ui_.set_class("status", "error", error);
    }

    void create_project() {
        vespera::ProjectCreateOptions options;
        options.kind = selected_;
        options.project_name = ui_.value("project-name");
        options.destination_directory = path_from_ui_text(ui_.value("project-location"));
        options.template_root = template_root_;
        const auto created = vespera::create_project_from_template(options);
        if (!created) {
            set_status(created.message, true);
            return;
        }
        remember_project(created.project_file);
        std::string error;
        if (!write_result(result_file_, created.project_file, error)) {
            set_status(error, true);
            return;
        }
        if (launch_editor_ && !launch_editor_for_project(editor_path_, created.project_file, error)) {
            set_status("Project created, but the editor could not be opened: " + error, true);
            return;
        }
        set_status(launch_editor_ ? created.message + " Opening editor..." : created.message, false);
        quit_ = true;
    }

    void open_project(std::string raw_path) {
        if (raw_path.empty()) {
            set_status("Enter a .vesperaproject path first.", true);
            return;
        }
        std::filesystem::path path = path_from_ui_text(raw_path);
        if (path.extension() != ".vesperaproject") {
            set_status("Open Existing expects a .vesperaproject file.", true);
            return;
        }
        vespera::VesperaProject project;
        const auto loaded = vespera::load_vespera_project(project, path);
        if (!loaded) {
            set_status("Could not open project: " + loaded.message, true);
            return;
        }
        remember_project(path);
        std::string error;
        if (!write_result(result_file_, path, error)) {
            set_status(error, true);
            return;
        }
        if (launch_editor_ && !launch_editor_for_project(editor_path_, path, error)) {
            set_status("Project opened, but the editor could not be launched: " + error, true);
            return;
        }
        quit_ = true;
    }

    vespera::RmlUiSurface ui_;
    vespera::ProjectTemplateKind selected_ = vespera::ProjectTemplateKind::Game3D;
    std::filesystem::path template_root_;
    std::filesystem::path result_file_;
    std::filesystem::path editor_path_;
    std::filesystem::path ui_document_;
    std::vector<std::filesystem::path> recent_;
    bool launch_editor_ = true;
    bool failed_ = false;
    bool reported_warnings_ = false;
    bool quit_ = false;
};

std::filesystem::path arg_value(int argc, char** argv, std::string_view prefix, std::filesystem::path fallback) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i] ? std::string_view(argv[i]) : std::string_view{};
        if (arg.starts_with(prefix)) return std::filesystem::path(std::string(arg.substr(prefix.size())));
    }
    return fallback;
}


bool has_arg(int argc, char** argv, std::string_view expected) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] && std::string_view(argv[i]) == expected) return true;
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i] ? std::string_view(argv[i]) : std::string_view{};
        if (arg == "--version") {
            std::printf("Vespera Hub %s\n", vespera::kEngineVersion.data());
            return 0;
        }
    }
    const auto runtime_root = hub_runtime_root();
    auto templates = arg_value(argc, argv, "--templates=", {});
    if (templates.empty()) templates = runtime_root / "templates";
    auto result = arg_value(argc, argv, "--result=", {});
    if (result.empty()) result = default_hub_result_file();
    const auto explicit_editor = arg_value(argc, argv, "--editor=", {});
    const bool launch_editor = !has_arg(argc, argv, "--no-launch-editor");
    const auto ui_document = runtime_root / "ui" / "project_hub.rml";

    // Release-gate/CI mode deliberately reuses the exact same public template
    // creator as the Hub UI without opening a window. This makes fresh-checkout
    // shipping tests deterministic while keeping normal users on the visual Hub.
    const auto create_template = arg_value(argc, argv, "--create-template=", {});
    if (!create_template.empty()) {
        const auto project_name = arg_value(argc, argv, "--project-name=", {});
        const auto project_location = arg_value(argc, argv, "--project-location=", {});
        const auto* info = vespera::project_template_info(create_template.string());
        if (!info) {
            vespera::log::error("Unknown Project Hub template id: " + create_template.string());
            return 2;
        }
        if (project_name.empty() || project_location.empty()) {
            vespera::log::error("Headless Project Hub creation requires --project-name=... and --project-location=...");
            return 2;
        }
        vespera::ProjectCreateOptions options;
        options.kind = info->kind;
        options.project_name = project_name.string();
        options.destination_directory = project_location;
        options.template_root = templates;
        const auto created = vespera::create_project_from_template(options);
        if (!created) {
            vespera::log::error(created.message);
            return 3;
        }
        // QA/CI creation should not pollute the user's Recent Projects list
        // with temporary release-gate paths. The visual Hub still remembers
        // projects created/opened through the normal user workflow.
        std::string error;
        if (!write_result(result, created.project_file, error)) {
            vespera::log::error(error);
            return 4;
        }
        vespera::log::info(created.message);
        return 0;
    }

    vespera::Application application;
    ProjectHubGame game(templates, result, find_editor_executable(explicit_editor), ui_document, launch_editor);
    vespera::ApplicationConfig config;
    config.title = std::string("Vespera Engine ") + std::string(vespera::kEngineVersion) + " - Project Hub";
    config.width = 1180;
    config.height = 760;
    config.resizable = true;
    config.relative_mouse = false;
    config.escape_quits = true;
    config.icon_path = runtime_root / "branding" / "vespera_icon_window.png";
    config.startup_splash_image = runtime_root / "branding" / "vespera_splash.png";
    config.startup_sound = runtime_root / "branding" / "vespera_logo_sting.wav";
    config.startup_sound_volume = 0.85f;
    return application.run(game, config);
}
