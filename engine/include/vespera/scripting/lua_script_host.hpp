#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace vespera {

class Scene;
class InputSystem;
class AudioSystem;
class AssetCatalog;
class UiSurface;

struct LuaScriptHostConfig {
    std::filesystem::path entry_script;
};

struct LuaSceneLoadRequest {
    std::filesystem::path path;
    bool force_reload = false;
};

struct LuaScriptHostStatus {
    bool available = false;
    bool initialized = false;
    bool running = false;
    std::filesystem::path entry_script;
    std::string message;
};

// Lightweight project-level Lua runtime. C# remains Vespera's primary full-game
// scripting path; Lua deliberately provides a small runtime/mod surface over the
// same Scene/Input/Audio/Assets/UiSurface semantics rather than a second engine.
//
// One project entry script may define optional global lifecycle functions:
//   Start()
//   Update(delta_seconds)
//   Stop()
//
// The script is re-created on reload/scene switches, keeping authored Scene data
// independent from Lua VM state.
class LuaScriptHost {
public:
    LuaScriptHost();
    ~LuaScriptHost();

    LuaScriptHost(const LuaScriptHost&) = delete;
    LuaScriptHost& operator=(const LuaScriptHost&) = delete;

    bool initialize(Scene& scene, InputSystem& input, AudioSystem& audio, AssetCatalog& assets,
                    const LuaScriptHostConfig& config, UiSurface* ui_surface = nullptr);
    bool start();
    bool update(double delta_seconds);
    bool reload();
    void shutdown();

    void set_current_scene_path(const std::filesystem::path& path);
    [[nodiscard]] std::optional<LuaSceneLoadRequest> take_scene_load_request();
    [[nodiscard]] const LuaScriptHostStatus& status() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vespera
