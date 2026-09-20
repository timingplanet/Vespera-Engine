#include <vespera/core/application.hpp>

#include <vespera/assets/texture_importer.hpp>
#include <vespera/core/game.hpp>
#include <vespera/core/log.hpp>
#include <vespera/core/version.hpp>
#include <vespera/render/render_backend.hpp>
#include <vespera/ui/ui_render.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <format>
#include <optional>
#include <string>
#include <thread>

namespace vespera {
namespace {


std::uint64_t smoke_frame_budget_from_environment() {
    const char* raw = std::getenv("VESPERA_SMOKE_FRAMES");
    if (!raw || !*raw) return 0;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(raw, &end, 10);
    if (end == raw || (end && *end != '\0')) return 0;
    return static_cast<std::uint64_t>(parsed);
}

std::optional<Key> map_scancode(SDL_Scancode scancode) {
    switch (scancode) {
        case SDL_SCANCODE_W: return Key::W;
        case SDL_SCANCODE_A: return Key::A;
        case SDL_SCANCODE_S: return Key::S;
        case SDL_SCANCODE_D: return Key::D;
        case SDL_SCANCODE_LSHIFT: return Key::LeftShift;
        case SDL_SCANCODE_ESCAPE: return Key::Escape;
        case SDL_SCANCODE_SPACE: return Key::Space;
        case SDL_SCANCODE_RETURN: return Key::Enter;
        case SDL_SCANCODE_TAB: return Key::Tab;
        case SDL_SCANCODE_UP: return Key::Up;
        case SDL_SCANCODE_DOWN: return Key::Down;
        case SDL_SCANCODE_LEFT: return Key::Left;
        case SDL_SCANCODE_RIGHT: return Key::Right;
        case SDL_SCANCODE_L: return Key::L;
        case SDL_SCANCODE_BACKSPACE: return Key::Backspace;
        default: return std::nullopt;
    }
}


std::optional<MouseButton> map_mouse_button(Uint8 button) {
    switch (button) {
        case SDL_BUTTON_LEFT: return MouseButton::Left;
        case SDL_BUTTON_RIGHT: return MouseButton::Right;
        case SDL_BUTTON_MIDDLE: return MouseButton::Middle;
        case SDL_BUTTON_X1: return MouseButton::X1;
        case SDL_BUTTON_X2: return MouseButton::X2;
        default: return std::nullopt;
    }
}

float normalize_stick_axis(Sint16 value) {
    if (value < 0) {
        return static_cast<float>(value) / 32768.0f;
    }
    return static_cast<float>(value) / 32767.0f;
}

float normalize_trigger_axis(Sint16 value) {
    return std::clamp(static_cast<float>(value) / 32767.0f, 0.0f, 1.0f);
}

void apply_window_icon(SDL_Window* window, const std::filesystem::path& path) {
    if (!window || path.empty()) return;
    const auto imported = import_texture(path, "Vespera window icon");
    if (!imported) {
        log::warn("Window icon skipped: " + imported.message);
        return;
    }
    const auto& texture = imported.texture;
    if (texture.width == 0 || texture.height == 0 || texture.rgba8.empty()) return;
    SDL_Surface* surface = SDL_CreateSurfaceFrom(
        static_cast<int>(texture.width),
        static_cast<int>(texture.height),
        SDL_PIXELFORMAT_RGBA32,
        const_cast<std::uint8_t*>(texture.rgba8.data()),
        static_cast<int>(texture.width * 4u));
    if (!surface) {
        log::warn(std::format("Could not create window icon surface: {}", SDL_GetError()));
        return;
    }
    if (!SDL_SetWindowIcon(window, surface)) {
        log::warn(std::format("Could not set window icon: {}", SDL_GetError()));
    }
    SDL_DestroySurface(surface);
}

UiRenderPacket make_splash_packet(const TextureData& texture, int viewport_width, int viewport_height) {
    UiRenderPacket packet;
    packet.viewport_width = std::max(viewport_width, 1);
    packet.viewport_height = std::max(viewport_height, 1);
    packet.atlas = &texture;
    packet.atlas_revision = 1;
    if (texture.width == 0 || texture.height == 0 || texture.rgba8.empty()) return packet;

    const float vw = static_cast<float>(packet.viewport_width);
    const float vh = static_cast<float>(packet.viewport_height);
    const float iw = static_cast<float>(texture.width);
    const float ih = static_cast<float>(texture.height);
    const float scale = std::min(vw / iw, vh / ih);
    const float w = iw * scale;
    const float h = ih * scale;
    const float x0 = (vw - w) * 0.5f;
    const float y0 = (vh - h) * 0.5f;
    const float x1 = x0 + w;
    const float y1 = y0 + h;
    constexpr std::uint32_t white = 0xFFFFFFFFu;
    packet.vertices = {
        {x0, y0, 0.0f, 0.0f, white}, {x1, y0, 1.0f, 0.0f, white}, {x1, y1, 1.0f, 1.0f, white},
        {x0, y0, 0.0f, 0.0f, white}, {x1, y1, 1.0f, 1.0f, white}, {x0, y1, 0.0f, 1.0f, white},
    };
    return packet;
}

#if defined(_WIN32)
struct ApplicationLiveResizeContext {
    SDL_Window* window = nullptr;
    RenderBackend* renderer = nullptr;
    Game* game = nullptr;
    GameContext* game_context = nullptr;
    std::chrono::steady_clock::time_point start_time{};
    bool frame_in_progress = false;
    bool live_redraw_in_progress = false;
    RuntimePerformanceCounters* performance = nullptr;
};

bool render_application_frame(ApplicationLiveResizeContext& context) {
    if (!context.renderer || !context.game || !context.game_context) return false;
    const auto now = std::chrono::steady_clock::now();
    const double total_seconds = std::chrono::duration<double>(now - context.start_time).count();
    if (!context.renderer->begin_frame()) return false;
    context.frame_in_progress = true;
    context.renderer->render_scene(context.game_context->scene, total_seconds, nullptr, nullptr);
    context.game->on_render(*context.game_context, *context.renderer, total_seconds);
    const bool presented = context.renderer->end_frame();
    context.frame_in_progress = false;
    return presented;
}

bool SDLCALL application_live_resize_event_watch(void* userdata, SDL_Event* event) {
    auto* context = static_cast<ApplicationLiveResizeContext*>(userdata);
    if (!context || !event || !context->window || !context->renderer
        || !context->game || !context->game_context) {
        return true;
    }

    // On Windows, moving/resizing a native window enters a modal OS loop and the
    // normal Application frame loop temporarily stops. Without an explicit redraw,
    // DWM stretches the last presented backbuffer until the drag is released. SDL
    // delivers WINDOW_EXPOSED from the main thread during this loop specifically so
    // applications can repaint live. The editor already uses the same mechanism.
    if (event->type != SDL_EVENT_WINDOW_EXPOSED
        || event->window.data1 != 1
        || event->window.windowID != SDL_GetWindowID(context->window)
        || context->frame_in_progress
        || context->live_redraw_in_progress) {
        return true;
    }

    int pixel_width = 0;
    int pixel_height = 0;
    if (!SDL_GetWindowSizeInPixels(context->window, &pixel_width, &pixel_height)
        || pixel_width <= 0 || pixel_height <= 0) {
        return true;
    }

    context->live_redraw_in_progress = true;
    if (context->performance) ++context->performance->live_resize_redraws;
    context->renderer->resize(pixel_width, pixel_height);
    (void)render_application_frame(*context);
    context->live_redraw_in_progress = false;
    return true;
}
#endif


} // namespace

Application::Application() = default;
Application::~Application() = default;

int Application::run(Game& game, const ApplicationConfig& config) {
    SDL_SetAppMetadata(config.title.c_str(), kEngineVersion.data(), "org.vespera.reference");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        log::error(std::format("SDL_Init failed: {}", SDL_GetError()));
        return 1;
    }

    const RenderBackendType selected_backend = resolve_render_backend_type(config.renderer);
    if (config.renderer != RenderBackendType::Automatic && !render_backend_compiled(config.renderer)) {
        log::error(std::format(
            "Requested renderer backend '{}' is not compiled into this build.",
            render_backend_type_name(config.renderer)));
        SDL_Quit();
        return 1;
    }

    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (config.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (selected_backend == RenderBackendType::Vulkan) {
        flags |= SDL_WINDOW_VULKAN;
    }

    SDL_Window* window = SDL_CreateWindow(
        config.title.c_str(),
        std::max(config.width, 320),
        std::max(config.height, 200),
        flags
    );

    if (!window) {
        log::error(std::format("SDL_CreateWindow failed: {}", SDL_GetError()));
        SDL_Quit();
        return 1;
    }

    apply_window_icon(window, config.icon_path);

    if (config.relative_mouse && !SDL_SetWindowRelativeMouseMode(window, true)) {
        log::warn(std::format("Could not enable relative mouse mode: {}", SDL_GetError()));
    }
    if (!SDL_StartTextInput(window)) {
        log::warn(std::format("Could not enable text input events: {}", SDL_GetError()));
    }

    SDL_Gamepad* active_gamepad = nullptr;
    SDL_JoystickID active_gamepad_id = 0;

    const auto close_gamepad = [&]() {
        if (active_gamepad) {
            SDL_CloseGamepad(active_gamepad);
            active_gamepad = nullptr;
            active_gamepad_id = 0;
        }
        input_.set_gamepad_connected(false);
    };

    const auto open_gamepad = [&](SDL_JoystickID instance_id) {
        if (active_gamepad) {
            return true;
        }

        SDL_Gamepad* gamepad = SDL_OpenGamepad(instance_id);
        if (!gamepad) {
            log::warn(std::format("Could not open gamepad {}: {}", instance_id, SDL_GetError()));
            return false;
        }

        active_gamepad = gamepad;
        active_gamepad_id = SDL_GetGamepadID(gamepad);
        const char* name = SDL_GetGamepadName(gamepad);
        const std::string gamepad_name = name ? name : "Unnamed gamepad";
        input_.set_gamepad_connected(true, gamepad_name);
        log::info(std::format("Gamepad connected: {} (id {})", gamepad_name, active_gamepad_id));
        return true;
    };

    const auto open_first_available_gamepad = [&]() {
        if (active_gamepad) {
            return;
        }

        int count = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&count);
        if (!ids) {
            return;
        }

        for (int i = 0; i < count && !active_gamepad; ++i) {
            open_gamepad(ids[i]);
        }
        SDL_free(ids);
    };

    open_first_available_gamepad();

    const auto poll_active_gamepad = [&]() {
        if (!active_gamepad || !SDL_GamepadConnected(active_gamepad)) {
            return;
        }

        input_.set_gamepad_axis(GamepadAxis::LeftX, normalize_stick_axis(SDL_GetGamepadAxis(active_gamepad, SDL_GAMEPAD_AXIS_LEFTX)));
        input_.set_gamepad_axis(GamepadAxis::LeftY, normalize_stick_axis(SDL_GetGamepadAxis(active_gamepad, SDL_GAMEPAD_AXIS_LEFTY)));
        input_.set_gamepad_axis(GamepadAxis::RightX, normalize_stick_axis(SDL_GetGamepadAxis(active_gamepad, SDL_GAMEPAD_AXIS_RIGHTX)));
        input_.set_gamepad_axis(GamepadAxis::RightY, normalize_stick_axis(SDL_GetGamepadAxis(active_gamepad, SDL_GAMEPAD_AXIS_RIGHTY)));
        input_.set_gamepad_axis(GamepadAxis::LeftTrigger, normalize_trigger_axis(SDL_GetGamepadAxis(active_gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER)));
        input_.set_gamepad_axis(GamepadAxis::RightTrigger, normalize_trigger_axis(SDL_GetGamepadAxis(active_gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)));

        input_.set_gamepad_button(GamepadButton::South, SDL_GetGamepadButton(active_gamepad, SDL_GAMEPAD_BUTTON_SOUTH));
        input_.set_gamepad_button(GamepadButton::East, SDL_GetGamepadButton(active_gamepad, SDL_GAMEPAD_BUTTON_EAST));
        input_.set_gamepad_button(GamepadButton::West, SDL_GetGamepadButton(active_gamepad, SDL_GAMEPAD_BUTTON_WEST));
        input_.set_gamepad_button(GamepadButton::North, SDL_GetGamepadButton(active_gamepad, SDL_GAMEPAD_BUTTON_NORTH));
        input_.set_gamepad_button(GamepadButton::Back, SDL_GetGamepadButton(active_gamepad, SDL_GAMEPAD_BUTTON_BACK));
        input_.set_gamepad_button(GamepadButton::Guide, SDL_GetGamepadButton(active_gamepad, SDL_GAMEPAD_BUTTON_GUIDE));
        input_.set_gamepad_button(GamepadButton::Start, SDL_GetGamepadButton(active_gamepad, SDL_GAMEPAD_BUTTON_START));
        input_.set_gamepad_button(GamepadButton::LeftStick, SDL_GetGamepadButton(active_gamepad, SDL_GAMEPAD_BUTTON_LEFT_STICK));
        input_.set_gamepad_button(GamepadButton::RightStick, SDL_GetGamepadButton(active_gamepad, SDL_GAMEPAD_BUTTON_RIGHT_STICK));
        input_.set_gamepad_button(GamepadButton::LeftShoulder, SDL_GetGamepadButton(active_gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));
        input_.set_gamepad_button(GamepadButton::RightShoulder, SDL_GetGamepadButton(active_gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER));
        input_.set_gamepad_button(GamepadButton::DpadUp, SDL_GetGamepadButton(active_gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP));
        input_.set_gamepad_button(GamepadButton::DpadDown, SDL_GetGamepadButton(active_gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN));
        input_.set_gamepad_button(GamepadButton::DpadLeft, SDL_GetGamepadButton(active_gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT));
        input_.set_gamepad_button(GamepadButton::DpadRight, SDL_GetGamepadButton(active_gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT));
    };

    renderer_ = create_render_backend(selected_backend);
    if (!renderer_ || !renderer_->initialize(window)) {
        log::error("Renderer initialization failed.");
        renderer_.reset();
        close_gamepad();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    renderer_->set_vsync_enabled(config.vsync);
    log::info(std::format(
        "Renderer: {} | backend {} | VSync: {}",
        renderer_->name(),
        render_backend_type_name(selected_backend),
        config.vsync ? "on" : "off"));

    int pixel_width = 0;
    int pixel_height = 0;
    if (SDL_GetWindowSizeInPixels(window, &pixel_width, &pixel_height)) {
        renderer_->resize(pixel_width, pixel_height);
    }

    bool running = true;
    const std::uint64_t smoke_frame_budget = smoke_frame_budget_from_environment();
    std::uint64_t rendered_frame_count = 0;
    if (smoke_frame_budget > 0) {
        log::info(std::format("Smoke mode: exit automatically after {} rendered frame(s).", smoke_frame_budget));
    }
    const auto start_time = std::chrono::steady_clock::now();
    auto previous_time = start_time;
    if (audio_.initialize()) {
        log::info("Audio: " + audio_.status_message());
    } else {
        log::warn("Audio unavailable: " + audio_.status_message());
    }

    // Present the engine/project startup branding before synchronous Game::on_start
    // work begins. Loading time counts toward the configured minimum so slower
    // projects are never delayed just to satisfy branding, while fast projects do
    // not collapse the splash to an imperceptible single-frame flash.
    std::optional<std::chrono::steady_clock::time_point> splash_presented_at;
    if (!config.startup_splash_image.empty()) {
        const auto splash = import_texture(config.startup_splash_image, "Vespera startup splash");
        if (splash) {
            int splash_width = std::max(renderer_->target_width(), 1);
            int splash_height = std::max(renderer_->target_height(), 1);
            if (renderer_->begin_frame()) {
                auto packet = make_splash_packet(splash.texture, splash_width, splash_height);
                renderer_->render_ui(packet);
                if (!renderer_->end_frame()) {
                    log::warn("Startup splash presentation failed.");
                } else {
                    splash_presented_at = std::chrono::steady_clock::now();
                }
            }
        } else {
            log::warn("Startup splash skipped: " + splash.message);
        }
    }
    if (audio_.initialized() && !config.startup_sound.empty()) {
        if (!audio_.play_one_shot(config.startup_sound, std::clamp(config.startup_sound_volume, 0.0f, 1.0f))) {
            log::warn("Startup logo sting could not be played: " + config.startup_sound.string());
        }
    }

    performance_ = {};
    GameContext context{scene_, input_, audio_, performance_};
#if defined(_WIN32)
    ApplicationLiveResizeContext live_resize_context{
        window,
        renderer_.get(),
        &game,
        &context,
        start_time,
        false,
        false,
        &performance_
    };
    bool live_resize_watch_installed = false;
#endif

    try {
        game.on_start(context);

        if (splash_presented_at && config.startup_splash_minimum_seconds > 0.0f) {
            const auto minimum = std::chrono::duration<double>(config.startup_splash_minimum_seconds);
            const auto visible_for = std::chrono::steady_clock::now() - *splash_presented_at;
            if (visible_for < minimum) {
                const auto deadline = std::chrono::steady_clock::now() + (minimum - visible_for);
                // A four-second branding window is long enough that a single blocking
                // sleep can make Windows consider the new window unresponsive. Keep
                // pumping platform events while the splash remains on screen.
                while (std::chrono::steady_clock::now() < deadline) {
                    SDL_PumpEvents();
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }
        }

#if defined(_WIN32)
        live_resize_watch_installed = config.resizable
            && SDL_AddEventWatch(application_live_resize_event_watch, &live_resize_context);
        if (config.resizable && !live_resize_watch_installed) {
            log::warn(std::format("Live-resize redraw watcher unavailable: {}", SDL_GetError()));
        }
#endif

        while (running) {
            const auto frame_begin = std::chrono::steady_clock::now();
            input_.begin_frame();

            SDL_Event event{};
            while (SDL_PollEvent(&event)) {
                switch (event.type) {
                    case SDL_EVENT_QUIT:
                        running = false;
                        break;
                    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                        renderer_->resize(event.window.data1, event.window.data2);
                        break;
                    case SDL_EVENT_KEY_DOWN:
                    case SDL_EVENT_KEY_UP: {
                        const auto key = map_scancode(event.key.scancode);
                        if (key) {
                            input_.set_key(*key, event.type == SDL_EVENT_KEY_DOWN);
                            if (event.type == SDL_EVENT_KEY_DOWN && event.key.repeat) {
                                input_.add_key_repeat(*key);
                            }
                        }
                        if (config.escape_quits
                            && event.type == SDL_EVENT_KEY_DOWN
                            && event.key.scancode == SDL_SCANCODE_ESCAPE) {
                            running = false;
                        }
                        break;
                    }
                    case SDL_EVENT_TEXT_INPUT:
                        if (event.text.text) input_.add_text_input(event.text.text);
                        break;
                    case SDL_EVENT_MOUSE_MOTION:
                        input_.add_mouse_delta(event.motion.xrel, event.motion.yrel);
                        input_.set_mouse_position(event.motion.x, event.motion.y);
                        break;
                    case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    case SDL_EVENT_MOUSE_BUTTON_UP: {
                        if (const auto button = map_mouse_button(event.button.button)) {
                            input_.set_mouse_position(event.button.x, event.button.y);
                            input_.set_mouse_button(*button, event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                        }
                        break;
                    }
                    case SDL_EVENT_GAMEPAD_ADDED:
                        if (!active_gamepad) {
                            open_gamepad(event.gdevice.which);
                        }
                        break;
                    case SDL_EVENT_GAMEPAD_REMOVED:
                        if (active_gamepad && event.gdevice.which == active_gamepad_id) {
                            log::info(std::format("Gamepad disconnected: id {}", active_gamepad_id));
                            close_gamepad();
                            open_first_available_gamepad();
                        }
                        break;
                    default:
                        break;
                }
            }

            poll_active_gamepad();

            const auto now = std::chrono::steady_clock::now();
            const double delta_seconds = std::chrono::duration<double>(now - previous_time).count();
            const double total_seconds = std::chrono::duration<double>(now - start_time).count();
            previous_time = now;

            const auto update_begin = std::chrono::steady_clock::now();
            game.on_update(context, delta_seconds);
            const auto update_end = std::chrono::steady_clock::now();
            if (game.wants_quit()) {
                running = false;
                continue;
            }
            audio_.set_camera_listener_position(scene_.camera.position);
            audio_.update();
#if defined(_WIN32)
            live_resize_context.frame_in_progress = true;
#endif
            const auto render_begin = std::chrono::steady_clock::now();
            if (renderer_->begin_frame()) {
                renderer_->render_scene(scene_, total_seconds, nullptr, nullptr);
                game.on_render(context, *renderer_, total_seconds);
                if (renderer_->end_frame()) {
                    ++rendered_frame_count;
                    if (smoke_frame_budget > 0 && rendered_frame_count >= smoke_frame_budget) {
                        log::info(std::format("Smoke mode complete after {} rendered frame(s).", rendered_frame_count));
                        running = false;
                    }
                }
            }
            const auto render_end = std::chrono::steady_clock::now();
            performance_.render = renderer_->frame_stats();
#if defined(_WIN32)
            live_resize_context.frame_in_progress = false;
#endif
            const auto frame_end = std::chrono::steady_clock::now();
            const auto to_ms = [](auto duration) {
                return std::chrono::duration<double, std::milli>(duration).count();
            };
            performance_.record_frame(
                to_ms(frame_end - frame_begin),
                to_ms(update_end - update_begin),
                to_ms(render_end - render_begin));
        }

#if defined(_WIN32)
        if (live_resize_watch_installed) {
            SDL_RemoveEventWatch(application_live_resize_event_watch, &live_resize_context);
            live_resize_watch_installed = false;
        }
#endif

        game.on_stop(context);
    } catch (const std::exception& ex) {
        log::error(std::format("Unhandled exception: {}", ex.what()));
        running = false;
    } catch (...) {
        log::error("Unhandled non-standard exception.");
        running = false;
    }

#if defined(_WIN32)
    if (live_resize_watch_installed) {
        SDL_RemoveEventWatch(application_live_resize_event_watch, &live_resize_context);
    }
#endif

    audio_.shutdown();
    renderer_->shutdown();
    renderer_.reset();
    close_gamepad();
    SDL_SetWindowRelativeMouseMode(window, false);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return game.exit_code();
}

} // namespace vespera
