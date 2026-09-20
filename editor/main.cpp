#include <vespera/core/version.hpp>

#include "editor_installation.hpp"
#include "editor_automation.hpp"
#include "editor_console.hpp"
#include "editor_extensions.hpp"
#include "editor_frame_ui.hpp"
#include "editor_play_controls.hpp"
#include "editor_project_session.hpp"
#include "editor_session_dialogs.hpp"
#include "editor_state.hpp"
#include "editor_style.hpp"
#include "editor_window_identity.hpp"
#if defined(VESPERA_EDITOR_D3D12)
#include "editor_windows_d3d12.hpp"
#endif
#if defined(VESPERA_EDITOR_VULKAN)
#include "editor_vulkan.hpp"
#endif

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#if defined(VESPERA_EDITOR_VULKAN)
#include <imgui_impl_vulkan.h>
#endif
#if !defined(VESPERA_EDITOR_D3D12) && !defined(VESPERA_EDITOR_VULKAN)
#include <imgui_impl_sdlrenderer3.h>
#endif

#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <system_error>

namespace {

using vespera::editor::ConsoleEntry;
using vespera::editor::EditorState;
using vespera::editor::apply_editor_style;
using vespera::editor::draw_editor_ui_frame;
using vespera::editor::editor_is_playing;
using vespera::editor::open_project;
using vespera::editor::open_scene;
using vespera::editor::poll_automation_server;
using vespera::editor::push_console;
using vespera::editor::register_core_extension_capabilities;
using vespera::editor::request_exit;
using vespera::editor::set_game_input_capture;
using vespera::editor::start_automation_server;
using vespera::editor::stop_automation_server;
using vespera::editor::stop_play_mode;
using vespera::editor::window_title;
#if defined(VESPERA_EDITOR_D3D12)
using vespera::editor::EditorWindowsD3D12Host;
using vespera::editor::editor_startup_trace;
#endif
#if defined(VESPERA_EDITOR_VULKAN)
using vespera::editor::EditorVulkanHost;
#endif

enum class EditorRendererMode : std::uint8_t {
    D3D12,
    Vulkan,
    Sdl,
};

constexpr EditorRendererMode default_editor_renderer() {
#if defined(VESPERA_EDITOR_D3D12)
    return EditorRendererMode::D3D12;
#elif defined(VESPERA_EDITOR_VULKAN)
    return EditorRendererMode::Vulkan;
#else
    return EditorRendererMode::Sdl;
#endif
}

bool renderer_supported(EditorRendererMode mode) {
    switch (mode) {
        case EditorRendererMode::D3D12:
#if defined(VESPERA_EDITOR_D3D12)
            return true;
#else
            return false;
#endif
        case EditorRendererMode::Vulkan:
#if defined(VESPERA_EDITOR_VULKAN)
            return true;
#else
            return false;
#endif
        case EditorRendererMode::Sdl:
#if !defined(VESPERA_EDITOR_D3D12) && !defined(VESPERA_EDITOR_VULKAN)
            return true;
#else
            return false;
#endif
    }
    return false;
}

const char* renderer_mode_name(EditorRendererMode mode) {
    switch (mode) {
        case EditorRendererMode::D3D12: return "d3d12";
        case EditorRendererMode::Vulkan: return "vulkan";
        case EditorRendererMode::Sdl: return "sdl";
    }
    return "unknown";
}

EditorRendererMode requested_editor_renderer(int argc, char** argv, bool& valid) {
    EditorRendererMode mode = default_editor_renderer();
    valid = true;
    constexpr std::string_view prefix = "--renderer=";
    const auto apply = [&](std::string_view value) {
        if (value == "d3d12") mode = EditorRendererMode::D3D12;
        else if (value == "vulkan") mode = EditorRendererMode::Vulkan;
        else if (value == "sdl") mode = EditorRendererMode::Sdl;
        else valid = false;
    };
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i] ? std::string_view(argv[i]) : std::string_view{};
        if (arg.starts_with(prefix)) {
            apply(arg.substr(prefix.size()));
            continue;
        }
        if (arg == "--renderer") {
            if (i + 1 >= argc || !argv[i + 1]) {
                valid = false;
                continue;
            }
            apply(std::string_view(argv[++i]));
        }
    }
    if (!renderer_supported(mode)) valid = false;
    return mode;
}

std::uint64_t editor_smoke_frame_budget() {
    const char* raw = std::getenv("VESPERA_SMOKE_FRAMES");
    if (!raw || !*raw) return 0;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(raw, &end, 10);
    if (end == raw || (end && *end != '\0')) return 0;
    return static_cast<std::uint64_t>(parsed);
}

} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i] ? std::string_view(argv[i]) : std::string_view{};
        if (arg == "--version") {
            std::printf("Vespera Editor %s\n", vespera::kEngineVersion.data());
            return 0;
        }
    }

    bool renderer_request_valid = false;
    const EditorRendererMode renderer_mode = requested_editor_renderer(argc, argv, renderer_request_valid);
    if (!renderer_request_valid) {
        std::fprintf(stderr,
            "Unsupported editor renderer request. Use --renderer=d3d12 or --renderer=vulkan when that backend is built.\n");
        return 2;
    }

#if defined(VESPERA_EDITOR_D3D12)
    if (renderer_mode == EditorRendererMode::D3D12) {
        editor_startup_trace(std::format("Vespera Editor {} startup", vespera::kEngineVersion), true);
    }
#endif
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (renderer_mode == EditorRendererMode::Vulkan) window_flags |= SDL_WINDOW_VULKAN;
    SDL_Window* window = SDL_CreateWindow(
        std::format("Vespera Editor {}", vespera::kEngineVersion).c_str(),
        1440,
        900,
        window_flags
    );
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Resolve branding from the installation rather than the current working
    // directory so file-association and launcher-driven starts behave equally.
    const auto installation_root = vespera::editor::editor_installation_root();
    const auto icon_path = installation_root.empty()
        ? std::filesystem::path("branding/vespera_icon_window.bmp")
        : installation_root / "branding" / "vespera_icon_window.bmp";
    if (SDL_Surface* icon = SDL_LoadBMP(icon_path.string().c_str())) {
        SDL_SetWindowIcon(window, icon);
        SDL_DestroySurface(icon);
    }

#if defined(VESPERA_EDITOR_D3D12)
    constexpr double kEditorStartupSplashMinimumSeconds = 4.0;
    EditorWindowsD3D12Host windows_host;
#endif
#if defined(VESPERA_EDITOR_VULKAN)
    EditorVulkanHost vulkan_host;
#endif
#if !defined(VESPERA_EDITOR_D3D12) && !defined(VESPERA_EDITOR_VULKAN)
    SDL_Renderer* sdl_renderer = nullptr;
#endif

    bool renderer_initialized = false;
#if defined(VESPERA_EDITOR_D3D12)
    if (renderer_mode == EditorRendererMode::D3D12) {
        renderer_initialized = windows_host.initialize_renderer(window);
    }
#endif
#if defined(VESPERA_EDITOR_VULKAN)
    if (renderer_mode == EditorRendererMode::Vulkan) {
        renderer_initialized = vulkan_host.initialize_renderer(window);
    }
#endif
#if !defined(VESPERA_EDITOR_D3D12) && !defined(VESPERA_EDITOR_VULKAN)
    if (renderer_mode == EditorRendererMode::Sdl) {
        sdl_renderer = SDL_CreateRenderer(window, nullptr);
        renderer_initialized = sdl_renderer != nullptr;
        if (sdl_renderer) SDL_SetRenderVSync(sdl_renderer, 1);
    }
#endif
    if (!renderer_initialized) {
        std::fprintf(stderr, "Vespera editor %s renderer initialization failed: %s\n",
            renderer_mode_name(renderer_mode), SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    const auto settings_directory = vespera::editor::editor_user_settings_directory();
    std::error_code settings_error;
    std::filesystem::create_directories(settings_directory, settings_error);
    const std::string imgui_ini_path = (settings_directory / "vespera_editor_v2.ini").string();
    io.IniFilename = imgui_ini_path.c_str();
    apply_editor_style();

    bool imgui_initialized = false;
#if defined(VESPERA_EDITOR_D3D12)
    if (renderer_mode == EditorRendererMode::D3D12) {
        imgui_initialized = windows_host.initialize_imgui(window);
    }
#endif
#if defined(VESPERA_EDITOR_VULKAN)
    if (renderer_mode == EditorRendererMode::Vulkan) {
        imgui_initialized = vulkan_host.initialize_imgui(window);
    }
#endif
#if !defined(VESPERA_EDITOR_D3D12) && !defined(VESPERA_EDITOR_VULKAN)
    if (renderer_mode == EditorRendererMode::Sdl) {
        imgui_initialized = ImGui_ImplSDL3_InitForSDLRenderer(window, sdl_renderer)
            && ImGui_ImplSDLRenderer3_Init(sdl_renderer);
    }
#endif
    if (!imgui_initialized) {
        std::fprintf(stderr, "ImGui %s backend initialization failed.\n", renderer_mode_name(renderer_mode));
        ImGui::DestroyContext();
#if defined(VESPERA_EDITOR_D3D12)
        if (renderer_mode == EditorRendererMode::D3D12) windows_host.shutdown_renderer();
#endif
#if defined(VESPERA_EDITOR_VULKAN)
        if (renderer_mode == EditorRendererMode::Vulkan) vulkan_host.shutdown_renderer();
#endif
#if !defined(VESPERA_EDITOR_D3D12) && !defined(VESPERA_EDITOR_VULKAN)
        if (sdl_renderer) SDL_DestroyRenderer(sdl_renderer);
#endif
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    EditorState state;
    register_core_extension_capabilities(state);
    push_console(state, ConsoleEntry::Level::Info, std::format("Vespera Editor {} started.", vespera::kEngineVersion));
#if defined(VESPERA_EDITOR_D3D12)
    if (renderer_mode == EditorRendererMode::D3D12) {
        push_console(state, ConsoleEntry::Level::Info, std::format("Editor renderer: {}", windows_host.renderer_name()));
        push_console(state, ConsoleEntry::Level::Info,
            "Scene view ready. RMB + WASD/QE to navigate; Sector view provides top-down editing.");
    }
#endif
#if defined(VESPERA_EDITOR_VULKAN)
    if (renderer_mode == EditorRendererMode::Vulkan) {
        push_console(state, ConsoleEntry::Level::Info, std::format("Editor renderer: {}", vulkan_host.renderer_name()));
        push_console(state, ConsoleEntry::Level::Info,
            "Vulkan Scene/Game previews active. RMB + WASD/QE to navigate the Scene view.");
    }
#endif

    std::filesystem::path initial_target;
    bool startup_target_set = false;
    if (!vespera::editor::editor_is_distribution_installation()) {
        initial_target = "VesperaReference.vesperaproject";
    }
    bool start_automation = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i] ? std::string_view(argv[i]) : std::string_view{};
        if (arg == "--automation") {
            start_automation = true;
            continue;
        }
        if (arg.starts_with("--automation-port=")) {
            const auto port_text = arg.substr(std::string_view("--automation-port=").size());
            unsigned int port = 0;
            const auto parsed = std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
            if (parsed.ec == std::errc{} && parsed.ptr == port_text.data() + port_text.size() && port > 0 && port <= 65535) {
                state.automation_port = static_cast<std::uint16_t>(port);
                start_automation = true;
            }
            continue;
        }
        if (arg.starts_with("--renderer=")) continue;
        if (arg == "--renderer") {
            if (i + 1 < argc) ++i;
            continue;
        }
        if (!startup_target_set && !arg.empty() && !arg.starts_with("--")) {
            initial_target = std::filesystem::path(arg);
            startup_target_set = true;
        }
    }
    bool initial_loaded = false;
    if (!initial_target.empty()) {
        const bool initial_is_project = initial_target.extension() == ".vesperaproject";
        initial_loaded = initial_is_project
            ? open_project(state, initial_target)
            : open_scene(state, initial_target);
    }
    if (!initial_loaded) {
        if (!initial_target.empty()) {
            state.open_path_text = initial_target.string();
            state.save_path_text = initial_target.string();
        }
        push_console(state, initial_target.empty() ? ConsoleEntry::Level::Info : ConsoleEntry::Level::Warning,
            "Editor is ready. Open a .vesperaproject or create/open a project from Vespera Hub.");
    }
    if (start_automation) start_automation_server(state, state.automation_port);

#if defined(VESPERA_EDITOR_D3D12)
    if (renderer_mode == EditorRendererMode::D3D12) {
        windows_host.wait_for_startup_splash(kEditorStartupSplashMinimumSeconds);
        editor_startup_trace("Initial scene load complete; entering frame loop");
    }
#endif

    bool running = true;
    const std::uint64_t smoke_frame_budget = editor_smoke_frame_budget();
    std::uint64_t smoke_frame_count = 0;
    if (smoke_frame_budget > 0) {
        push_console(state, ConsoleEntry::Level::Info,
            std::format("Smoke mode: editor will exit after {} frame(s).", smoke_frame_budget));
    }
    const auto start_time = std::chrono::steady_clock::now();
#if defined(VESPERA_EDITOR_D3D12)
    if (renderer_mode == EditorRendererMode::D3D12) {
        const bool live_resize_watch_installed = windows_host.attach_frame(state, running, start_time);
        if (!live_resize_watch_installed) {
            push_console(state, ConsoleEntry::Level::Warning,
                std::format("Live-resize redraw watcher unavailable: {}", SDL_GetError()));
        }
    }
#endif
#if defined(VESPERA_EDITOR_VULKAN)
    if (renderer_mode == EditorRendererMode::Vulkan && !vulkan_host.attach_frame(state, running, start_time)) {
        push_console(state, ConsoleEntry::Level::Error, "Vulkan editor frame host initialization failed.");
        running = false;
    }
#endif

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) request_exit(state);
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window)) {
                request_exit(state);
            }
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST
                && (state.game_view.input_captured || state.game_view.capture_window_id != 0)) {
                set_game_input_capture(state, false);
            }
            if (editor_is_playing(state) && state.game_view.focused && !state.game_view.input_captured) {
                if (event.type == SDL_EVENT_TEXT_INPUT && event.text.text) state.game_view_text_input += event.text.text;
                if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_BACKSPACE) {
                    state.game_view_backspace_pending = true;
                }
            }
            if (event.type == SDL_EVENT_KEY_DOWN
                && event.key.scancode == SDL_SCANCODE_ESCAPE
                && state.game_view.input_captured) {
                set_game_input_capture(state, false);
            }
            if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED
                && event.window.windowID == SDL_GetWindowID(window)) {
#if defined(VESPERA_EDITOR_D3D12)
                if (renderer_mode == EditorRendererMode::D3D12) windows_host.resize(event.window.data1, event.window.data2);
#endif
#if defined(VESPERA_EDITOR_VULKAN)
                if (renderer_mode == EditorRendererMode::Vulkan) vulkan_host.resize(event.window.data1, event.window.data2);
#endif
            }
        }

        // Automation transport is polled from the editor/main thread only. Socket I/O
        // never mutates Scene, renderer, audio, managed state or undo history directly.
        poll_automation_server(state);

#if defined(VESPERA_EDITOR_D3D12)
        if (renderer_mode == EditorRendererMode::D3D12) windows_host.render_frame(true);
#endif
#if defined(VESPERA_EDITOR_VULKAN)
        if (renderer_mode == EditorRendererMode::Vulkan) vulkan_host.render_frame(true);
#endif
#if !defined(VESPERA_EDITOR_D3D12) && !defined(VESPERA_EDITOR_VULKAN)
        if (renderer_mode == EditorRendererMode::Sdl) {
            ImGui_ImplSDLRenderer3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            draw_editor_ui_frame(state, running, true);
            ImGui::Render();
            SDL_SetWindowTitle(window, window_title(state).c_str());
            SDL_SetRenderDrawColor(sdl_renderer, 18, 20, 24, 255);
            SDL_RenderClear(sdl_renderer);
            ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), sdl_renderer);
            SDL_RenderPresent(sdl_renderer);
        }
#endif
        if (smoke_frame_budget > 0 && ++smoke_frame_count >= smoke_frame_budget) {
            const auto& render_stats = state.performance.render;
            std::printf("Smoke mode complete after %llu rendered frame(s).\n",
                static_cast<unsigned long long>(smoke_frame_count));
            std::printf(
                "Smoke renderer stats: scene_passes=%llu world_draw_calls=%llu mesh_draw_calls=%llu "
                "sprite_draw_calls=%llu ui_draw_calls=%llu\n",
                static_cast<unsigned long long>(render_stats.scene_passes),
                static_cast<unsigned long long>(render_stats.world_draw_calls),
                static_cast<unsigned long long>(render_stats.mesh_draw_calls),
                static_cast<unsigned long long>(render_stats.sprite_draw_calls),
                static_cast<unsigned long long>(render_stats.ui_draw_calls));
            std::fflush(stdout);
            running = false;
        }
    }

    // Tear editor-owned play services down while SDL/.NET host dependencies are
    // still alive. Release relative mouse mode even if focus was lost mid-play.
    set_game_input_capture(state, false);
    if (editor_is_playing(state)) stop_play_mode(state);
    stop_automation_server(state);

#if defined(VESPERA_EDITOR_D3D12)
    if (renderer_mode == EditorRendererMode::D3D12) {
        windows_host.detach_frame();
        windows_host.shutdown_imgui_backends();
        windows_host.shutdown_renderer();
    }
#endif
#if defined(VESPERA_EDITOR_VULKAN)
    if (renderer_mode == EditorRendererMode::Vulkan) {
        vulkan_host.detach_frame();
        vulkan_host.shutdown_imgui_backends();
        vulkan_host.shutdown_renderer();
    }
#endif
#if !defined(VESPERA_EDITOR_D3D12) && !defined(VESPERA_EDITOR_VULKAN)
    if (renderer_mode == EditorRendererMode::Sdl) {
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        SDL_DestroyRenderer(sdl_renderer);
    }
#endif
    ImGui::DestroyContext();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
