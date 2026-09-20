#include <vespera/assets/asset_catalog.hpp>
#include <vespera/assets/material_asset.hpp>
#include <vespera/assets/texture_importer.hpp>
#include <vespera/core/application.hpp>
#include <vespera/core/game.hpp>
#include <vespera/core/log.hpp>
#include <vespera/core/version.hpp>
#include <vespera/project/project.hpp>
#include <vespera/render/render_backend.hpp>
#include <vespera/runtime/player_project.hpp>
#include <vespera/scene/scene_io.hpp>
#include <vespera/scripting/managed_script_host.hpp>
#ifdef VESPERA_HAS_LUA
#include <vespera/scripting/lua_script_host.hpp>
#endif
#include <vespera/ui/ui_surface.hpp>
#ifdef VESPERA_HAS_RMLUI
#include <vespera/ui/rmlui_surface.hpp>
#endif

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

#ifdef _WIN32
std::wstring utf8_to_wide(std::string_view text) {
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) return std::wstring(text.begin(), text.end());
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), length);
    return result;
}

void show_runtime_error_dialog(std::string_view message) {
    // Source-tree Debug/Development/Release runs normally have a console and
    // should stay non-modal. Packaged Release shared-player executables are
    // patched to GUI subsystem and have no console, so surface fatal startup
    // failures instead of appearing to close silently.
    if (GetConsoleWindow() != nullptr) return;
    const auto wide = utf8_to_wide(message);
    MessageBoxW(nullptr, wide.c_str(), L"Vespera Runtime Error", MB_OK | MB_ICONERROR | MB_TASKMODAL);
}

std::wstring g_runtime_crash_log_path;

LONG WINAPI vespera_unhandled_exception_filter(EXCEPTION_POINTERS* exception) {
    if (!g_runtime_crash_log_path.empty()) {
        HANDLE file = CreateFileW(
            g_runtime_crash_log_path.c_str(),
            FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            const DWORD code = exception && exception->ExceptionRecord
                ? exception->ExceptionRecord->ExceptionCode
                : 0u;
            const void* address = exception && exception->ExceptionRecord
                ? exception->ExceptionRecord->ExceptionAddress
                : nullptr;
            char message[256]{};
            const int length = std::snprintf(
                message,
                sizeof(message),
                "\r\n[FATAL] Unhandled Windows exception 0x%08lX at %p\r\n",
                static_cast<unsigned long>(code),
                address);
            if (length > 0 && static_cast<std::size_t>(length) < sizeof(message)) {
                DWORD written = 0;
                WriteFile(file, message, static_cast<DWORD>(length), &written, nullptr);
                FlushFileBuffers(file);
            }
            CloseHandle(file);
        }
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
#else
void show_runtime_error_dialog(std::string_view) {}
#endif

struct PlayerArguments {
    std::filesystem::path project;
    std::filesystem::path managed_directory;
    vespera::RenderBackendType renderer = vespera::RenderBackendType::Automatic;
};

bool read_path_argument(int& index, int argc, char** argv, std::string_view name, std::filesystem::path& out) {
    const std::string_view arg = argv[index] ? std::string_view(argv[index]) : std::string_view{};
    const std::string prefix = std::string(name) + "=";
    if (arg.starts_with(prefix)) {
        out = std::string(arg.substr(prefix.size()));
        return true;
    }
    if (arg == name) {
        if (index + 1 >= argc) return false;
        out = argv[++index];
        return true;
    }
    return false;
}

std::optional<PlayerArguments> parse_arguments(int argc, char** argv, std::string& error) {
    PlayerArguments result;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i] ? std::string_view(argv[i]) : std::string_view{};
        if (arg == "--help" || arg == "-h") {
            error = "usage: vespera_player [--project <file.vesperaproject>] [--managed-dir <dir>] [--renderer <auto|d3d12|vulkan|null>]";
            return std::nullopt;
        }
        if (arg == "--project" && i + 1 >= argc) {
            error = "--project requires a path";
            return std::nullopt;
        }
        if (arg == "--managed-dir" && i + 1 >= argc) {
            error = "--managed-dir requires a path";
            return std::nullopt;
        }
        if (arg == "--renderer" && i + 1 >= argc) {
            error = "--renderer requires auto, d3d12, vulkan, or null";
            return std::nullopt;
        }
        if (read_path_argument(i, argc, argv, "--project", result.project)) continue;
        if (read_path_argument(i, argc, argv, "--managed-dir", result.managed_directory)) continue;
        if (arg == "--renderer" || arg.starts_with("--renderer=")) {
            std::string_view value;
            if (arg == "--renderer") value = argv[++i] ? std::string_view(argv[i]) : std::string_view{};
            else value = arg.substr(std::string_view("--renderer=").size());
            const auto parsed = vespera::parse_render_backend_type(value);
            if (!parsed) {
                error = "unknown renderer backend: " + std::string(value) + " (expected auto, d3d12, vulkan, or null)";
                return std::nullopt;
            }
            result.renderer = *parsed;
            continue;
        }
        error = "unknown Vespera player argument: " + std::string(arg);
        return std::nullopt;
    }
    return result;
}

std::filesystem::path executable_path_from_args(int argc, char** argv) {
    std::error_code ec;
    std::filesystem::path path = argc > 0 && argv[0] && *argv[0]
        ? std::filesystem::path(argv[0])
        : std::filesystem::path("vespera_player");
    if (!path.is_absolute()) {
        const auto absolute = std::filesystem::absolute(path, ec);
        if (!ec) path = absolute;
    }
    return path.lexically_normal();
}

std::filesystem::path default_runtime_log_path(const std::filesystem::path& executable_path) {
    std::filesystem::path root;
#ifdef _WIN32
    if (const char* local = std::getenv("LOCALAPPDATA"); local && *local) {
        root = std::filesystem::path(local) / "Vespera" / "Logs";
    }
#else
    if (const char* state = std::getenv("XDG_STATE_HOME"); state && *state) {
        root = std::filesystem::path(state) / "vespera" / "logs";
    } else if (const char* home = std::getenv("HOME"); home && *home) {
        root = std::filesystem::path(home) / ".local" / "state" / "vespera" / "logs";
    }
#endif
    if (root.empty()) root = executable_path.parent_path() / "logs";
    std::string stem = executable_path.stem().string();
    if (stem.empty()) stem = "VesperaRuntime";
    return root / (stem + ".log");
}

std::filesystem::path resolve_scene_request(
    const vespera::VesperaProject& project,
    const std::filesystem::path& requested
) {
    if (requested.empty()) return {};
    std::error_code ec;
    if (requested.is_absolute() && std::filesystem::is_regular_file(requested, ec) && !ec) return requested;
    ec.clear();
    const auto project_relative = (project.root_directory / requested).lexically_normal();
    if (std::filesystem::is_regular_file(project_relative, ec) && !ec) return project_relative;
    ec.clear();
    const auto assets_relative = (project.assets_root() / requested).lexically_normal();
    if (std::filesystem::is_regular_file(assets_relative, ec) && !ec) return assets_relative;
    std::string generic = requested.generic_string();
    if (generic.starts_with("assets/")) {
        const auto stripped = (project.assets_root() / std::filesystem::path(generic.substr(7))).lexically_normal();
        ec.clear();
        if (std::filesystem::is_regular_file(stripped, ec) && !ec) return stripped;
    }
    return requested;
}

class VesperaPlayerGame final : public vespera::Game {
public:
    VesperaPlayerGame(vespera::VesperaProject project, std::filesystem::path managed_directory)
        : project_(std::move(project)), managed_directory_(std::move(managed_directory)) {}

    void on_start(vespera::GameContext& context) override {
        using namespace vespera;

        bool validation_failed = false;
        ProjectValidationOptions runtime_validation;
        runtime_validation.require_managed_source = false;
        for (const auto& issue : validate_vespera_project(project_, runtime_validation)) {
            if (issue.severity == ProjectValidationSeverity::Error) {
                log::error("Project validation: " + issue.message);
                validation_failed = true;
            } else {
                log::warn("Project validation: " + issue.message);
            }
        }
        if (validation_failed) return fail("project validation failed");

        std::string input_error;
        if (!configure_project_input_map(project_, context.input.map(), &input_error)) {
            return fail("project input map failed: " + input_error);
        }

        AssetCatalogRefreshOptions runtime_catalog_options;
        runtime_catalog_options.write_metadata = false;
        AssetCatalogRefreshReport report;
        std::string catalog_error;
        if (!assets_.refresh(project_.assets_root(), runtime_catalog_options, &report, &catalog_error)) {
            return fail("asset catalog refresh failed: " + catalog_error);
        }
        log::info(std::format(
            "Player assets: {} record(s) | {} dependency edge(s) | {} broken | {} stale fallback(s)",
            assets_.records().size(), report.dependency_edges, report.broken_dependencies, report.stale_fallback_paths));

        std::size_t textures_loaded = 0;
        std::size_t texture_fallbacks = 0;
        for (const auto* texture_asset : assets_.records_of_kind(AssetKind::Texture)) {
            if (!texture_asset) continue;
            const auto imported = import_texture(texture_asset->absolute_path, texture_asset->display_name);
            if (!imported || !imported.texture.valid()) {
                context.scene.world.add_texture(make_missing_texture_placeholder(texture_asset->display_name));
                ++texture_fallbacks;
                log::warn("Player texture fallback '" + texture_asset->relative_path.generic_string() + "': " + imported.message);
                continue;
            }
            context.scene.world.add_texture(imported.texture);
            ++textures_loaded;
        }
        if (textures_loaded > 0 || texture_fallbacks > 0) {
            log::info(std::format("Player textures: {} imported | {} fallback placeholder(s)", textures_loaded, texture_fallbacks));
        }

        const auto startup = assets_.resolve_reference(project_.startup_scene_reference());
        if (!startup || !startup.record || startup.record->kind != AssetKind::Scene) {
            return fail("startup scene stable reference could not be resolved");
        }
        current_scene_path_ = startup.record->absolute_path;
        if (!load_scene(context, current_scene_path_)) return;

#ifdef VESPERA_HAS_RMLUI
        const auto ui = select_runtime_rml_document(project_, assets_);
        if (ui.document) {
            const auto loaded = rml_ui_.initialize(
                ui.document->absolute_path,
                std::max(project_.window_width, 1),
                std::max(project_.window_height, 1));
            if (!loaded) {
                log::warn("Player RML UI failed to initialize: " + loaded.message);
            } else {
                rml_ui_loaded_ = true;
                log::info("Player UI: " + ui.message);
            }
        } else {
            log::info("Player UI: " + ui.message);
        }
#else
        log::warn("Player was built without RmlUi support; RML startup UI is unavailable.");
#endif

        initialize_managed(context);
#ifdef VESPERA_HAS_LUA
        initialize_lua(context);
#endif
        log::info(std::format(
            "Vespera Player {} started project '{}' from {}",
            vespera::kEngineVersion, project_.name, project_.project_file.string()));
    }

    void on_update(vespera::GameContext& context, double delta_seconds) override {
        if (failed_) return;
#ifdef VESPERA_HAS_RMLUI
        if (rml_ui_loaded_) rml_ui_.process_input(context.input);
#endif
        std::optional<std::pair<std::filesystem::path, bool>> scene_request;
        if (managed_ready_) {
            managed_.update(std::clamp(delta_seconds, 0.0, 0.05));
            if (const auto request = managed_.take_scene_load_request()) scene_request = {{request->path, request->force_reload}};
        }
#ifdef VESPERA_HAS_LUA
        if (lua_ready_) {
            lua_.update(std::clamp(delta_seconds, 0.0, 0.05));
            if (const auto request = lua_.take_scene_load_request()) {
                if (!scene_request) scene_request = {{request->path, request->force_reload}};
                else vespera::log::warn("Lua Scene.Load request ignored because another scripting runtime already requested a scene this frame.");
            }
        }
#endif
        if (scene_request) {
            const auto resolved = resolve_scene_request(project_, scene_request->first);
            if (!same_scene(resolved) || scene_request->second) {
                shutdown_scripts();
                if (load_scene(context, resolved)) initialize_scripts(context);
            } else {
                vespera::log::warn("Player coalesced same-scene Scene.Load request; use Scene.Reload() for an intentional reload.");
            }
        }
    }

    void on_render(vespera::GameContext&, vespera::RenderBackend& renderer, double) override {
#ifdef VESPERA_HAS_RMLUI
        if (!rml_ui_loaded_) return;
        rml_ui_.resize(std::max(renderer.target_width(), 1), std::max(renderer.target_height(), 1));
        auto packet = rml_ui_.build_packet();
        renderer.render_ui(packet);
        if (!reported_ui_warnings_ && !packet.warnings.empty()) {
            reported_ui_warnings_ = true;
            for (const auto& warning : packet.warnings) vespera::log::warn("Player RML UI: " + warning);
        }
#else
        (void)renderer;
#endif
    }

    void on_stop(vespera::GameContext&) override {
        shutdown_scripts();
#ifdef VESPERA_HAS_RMLUI
        rml_ui_.shutdown();
#endif
    }

    [[nodiscard]] bool wants_quit() const override { return failed_; }
    [[nodiscard]] int exit_code() const override { return failed_ ? 1 : 0; }

private:
    void fail(std::string message) {
        const std::string full = "Vespera Player: " + message;
        vespera::log::error(full);
        show_runtime_error_dialog(full);
        failed_ = true;
    }

    bool load_scene(vespera::GameContext& context, const std::filesystem::path& path) {
        std::vector<std::string> texture_warnings;
        const auto loaded = vespera::load_scene_text_resilient(context.scene, path, &texture_warnings);
        if (!loaded) {
            fail("scene load failed: " + loaded.message);
            return false;
        }
        for (const auto& warning : texture_warnings) vespera::log::warn("Player: " + warning);
        const auto material_report = vespera::hydrate_scene_materials(context.scene, assets_);
        if (!material_report.message.empty()) vespera::log::warn("Material hydration: " + material_report.message);
        current_scene_path_ = path;
        vespera::log::info("Player scene: " + loaded.message);
        return true;
    }

    bool same_scene(const std::filesystem::path& path) const {
        if (current_scene_path_.empty() || path.empty()) return false;
        std::error_code ec;
        if (std::filesystem::exists(path, ec) && !ec) {
            ec.clear();
            if (std::filesystem::exists(current_scene_path_, ec) && !ec) {
                ec.clear();
                if (std::filesystem::equivalent(path, current_scene_path_, ec) && !ec) return true;
            }
        }
        return std::filesystem::absolute(path).lexically_normal()
            == std::filesystem::absolute(current_scene_path_).lexically_normal();
    }

    void shutdown_scripts() {
        managed_.shutdown();
        managed_ready_ = false;
#ifdef VESPERA_HAS_LUA
        lua_.shutdown();
        lua_ready_ = false;
#endif
    }

    void initialize_scripts(vespera::GameContext& context) {
        initialize_managed(context);
#ifdef VESPERA_HAS_LUA
        initialize_lua(context);
#endif
    }

#ifdef VESPERA_HAS_LUA
    void initialize_lua(vespera::GameContext& context) {
        if (project_.lua_entry.empty() && project_.lua_entry_asset_id.empty()) return;
        const auto resolved = assets_.resolve_reference(project_.lua_entry_reference());
        if (!resolved || !resolved.record || resolved.record->kind != vespera::AssetKind::LuaScript) {
            vespera::log::warn("Lua scripting unavailable: project lua_entry could not be resolved to a Lua script.");
            return;
        }
        vespera::LuaScriptHostConfig config;
        config.entry_script = resolved.record->absolute_path;
        vespera::UiSurface* ui = nullptr;
#ifdef VESPERA_HAS_RMLUI
        if (rml_ui_loaded_) ui = &rml_ui_;
#endif
        if (!lua_.initialize(context.scene, context.input, context.audio, assets_, config, ui)) {
            vespera::log::warn("Lua scripting unavailable: " + lua_.status().message);
            return;
        }
        lua_.set_current_scene_path(current_scene_path_);
        lua_ready_ = lua_.start();
        if (lua_ready_) vespera::log::info("Lua scripting: " + lua_.status().message);
    }
#endif

    void initialize_managed(vespera::GameContext& context) {
        if (project_.managed_assembly.empty()) {
            vespera::log::info("Managed scripting: project has no managed assembly; native player path only.");
            return;
        }
        std::filesystem::path root = managed_directory_.empty() ? project_.root_directory / "managed" : managed_directory_;
        if (!root.is_absolute()) root = std::filesystem::absolute(root).lexically_normal();
        vespera::ManagedScriptHostConfig config;
        config.runtime_config = root / "Vespera.Managed.runtimeconfig.json";
        config.bridge_assembly = root / "Vespera.NET.dll";
        config.game_assembly = root / (project_.managed_assembly + ".dll");
        config.auto_reload = false;

        vespera::UiSurface* ui = nullptr;
#ifdef VESPERA_HAS_RMLUI
        if (rml_ui_loaded_) ui = &rml_ui_;
#endif
        if (!managed_.initialize(context.scene, context.input, context.audio, assets_, config, ui, &context.performance)) {
            vespera::log::warn("Managed scripting unavailable: " + managed_.status().message);
            return;
        }
        managed_.set_current_scene_path(current_scene_path_);
        managed_.start();
        managed_ready_ = true;
        vespera::log::info("Managed scripting: " + managed_.status().message);
    }

    vespera::VesperaProject project_;
    std::filesystem::path managed_directory_;
    vespera::AssetCatalog assets_;
    std::filesystem::path current_scene_path_;
    vespera::ManagedScriptHost managed_;
    bool managed_ready_ = false;
#ifdef VESPERA_HAS_LUA
    vespera::LuaScriptHost lua_;
    bool lua_ready_ = false;
#endif
#ifdef VESPERA_HAS_RMLUI
    vespera::RmlUiSurface rml_ui_;
#endif
    bool rml_ui_loaded_ = false;
    bool reported_ui_warnings_ = false;
    bool failed_ = false;
};

} // namespace

int run_player(int argc, char** argv) {
    const std::filesystem::path executable_path = executable_path_from_args(argc, argv);
    const std::filesystem::path executable_root = executable_path.parent_path();
    const auto log_path = default_runtime_log_path(executable_path);
#ifdef _WIN32
    g_runtime_crash_log_path = std::filesystem::absolute(log_path).lexically_normal().wstring();
    SetUnhandledExceptionFilter(vespera_unhandled_exception_filter);
#endif
    if (vespera::log::set_file_sink(log_path)) {
        vespera::log::info("Runtime log: " + vespera::log::file_sink_path().string());
    }

    std::string argument_error;
    const auto arguments = parse_arguments(argc, argv, argument_error);
    if (!arguments) {
        vespera::log::error(argument_error);
        return argument_error.starts_with("usage:") ? 0 : 2;
    }

    const auto discovered = vespera::discover_player_project(executable_path, arguments->project);
    if (!discovered) {
        const std::string message = "Vespera Player: " + discovered.message;
        vespera::log::error(message);
        show_runtime_error_dialog(message);
        return 2;
    }

    vespera::VesperaProject project;
    const auto loaded = vespera::load_vespera_project(project, discovered.project_file);
    if (!loaded) {
        const std::string message = "Vespera Player project load failed: " + loaded.message;
        vespera::log::error(message);
        show_runtime_error_dialog(message);
        return 3;
    }
    vespera::log::info("Vespera Player: " + discovered.message);

    vespera::ApplicationConfig config;
    config.title = project.window_title.empty() ? project.name : project.window_title;
    config.width = project.window_width;
    config.height = project.window_height;
    config.resizable = project.window_resizable;
    config.relative_mouse = project.relative_mouse;
    config.escape_quits = project.escape_quits;
    config.vsync = project.vsync;
    config.renderer = arguments->renderer;
    std::filesystem::path project_icon;
    if (!project.game_icon.empty()) {
        project_icon = project.resolve_asset(project.game_icon);
        std::error_code icon_ec;
        if (!std::filesystem::is_regular_file(project_icon, icon_ec) || icon_ec) project_icon.clear();
    }
    config.icon_path = project_icon.empty() ? executable_root / "branding/vespera_icon_window.png" : project_icon;
    config.startup_splash_image = executable_root / "branding/vespera_splash.png";
    config.startup_sound = executable_root / "branding/vespera_logo_sting.wav";
    config.startup_sound_volume = 0.85f;

    VesperaPlayerGame game(std::move(project), arguments->managed_directory);
    vespera::Application application;
    return application.run(game, config);
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i] ? std::string_view(argv[i]) : std::string_view{};
        if (arg == "--version") {
            std::printf("Vespera Player %s\n", vespera::kEngineVersion.data());
            return 0;
        }
    }
    try {
        return run_player(argc, argv);
    } catch (const std::exception& exception) {
        const std::string message = std::string("Vespera Player fatal exception: ") + exception.what();
        vespera::log::error(message);
        show_runtime_error_dialog(message);
        return 70;
    } catch (...) {
        const std::string message = "Vespera Player fatal unknown exception.";
        vespera::log::error(message);
        show_runtime_error_dialog(message);
        return 71;
    }
}
