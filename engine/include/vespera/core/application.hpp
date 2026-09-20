#pragma once

#include <vespera/audio/audio.hpp>
#include <vespera/input/input.hpp>
#include <vespera/render/render_backend.hpp>
#include <vespera/core/game.hpp>
#include <vespera/scene/scene.hpp>

#include <filesystem>
#include <memory>
#include <string>

namespace vespera {

class Game;
struct ApplicationConfig {
    std::string title = "Vespera";
    int width = 1280;
    int height = 720;
    bool resizable = true;
    bool relative_mouse = false;
    bool escape_quits = false;
    bool vsync = true;
    RenderBackendType renderer = RenderBackendType::Automatic;
    std::filesystem::path icon_path;
    std::filesystem::path startup_splash_image;
    std::filesystem::path startup_sound;
    float startup_sound_volume = 0.85f;
    // Minimum time the startup artwork remains visible. Loading time counts toward
    // this duration; fast projects linger briefly instead of flashing for one frame.
    float startup_splash_minimum_seconds = 4.0f;
};

class Application {
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    int run(Game& game, const ApplicationConfig& config = {});

private:
    std::unique_ptr<RenderBackend> renderer_;
    Scene scene_;
    InputSystem input_;
    AudioSystem audio_;
    RuntimePerformanceCounters performance_;
};

} // namespace vespera
