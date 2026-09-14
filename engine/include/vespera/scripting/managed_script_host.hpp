#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <cstdint>
#include <vector>

namespace vespera {

class AudioSystem;
class AssetCatalog;
class Scene;
class InputSystem;
class UiSurface;
struct RuntimePerformanceCounters;

struct ManagedScriptHostConfig {
    std::filesystem::path runtime_config;
    std::filesystem::path bridge_assembly;
    std::filesystem::path game_assembly;
    // When enabled, the host watches the game assembly for a stable replacement
    // and reloads only the collectible game-code context. The bridge/runtime stay
    // process-lifetime. Manual reload remains available as a fallback.
    bool auto_reload = false;
    double auto_reload_poll_seconds = 0.25;
    double auto_reload_debounce_seconds = 0.35;
};



struct ManagedSceneLoadRequest {
    std::filesystem::path path;
    bool force_reload = false;
};

struct ManagedRuntimeEvent {
    std::uint64_t sequence = 0;
    std::uint64_t frame = 0;
    std::uint32_t assembly_generation = 0;
    std::uint64_t entity_id = 0;
    std::string component;
    std::string callback;
};

struct ManagedScriptHostStatus {
    bool available = false;
    bool initialized = false;
    std::size_t script_count = 0;
    std::string message;
};

// Hosts the .NET runtime inside the native Vespera process and creates one
// managed Component instance for each enabled ManagedScriptComponent in a
// scene. The CLR boundary is deliberately a tiny C ABI so the engine's C++
// object layout is never exposed to managed code.
class ManagedScriptHost {
public:
    ManagedScriptHost();
    ~ManagedScriptHost();

    ManagedScriptHost(const ManagedScriptHost&) = delete;
    ManagedScriptHost& operator=(const ManagedScriptHost&) = delete;

    bool initialize(Scene& scene, InputSystem& input, AudioSystem& audio, AssetCatalog& assets,
                    const ManagedScriptHostConfig& config,
                    UiSurface* ui_surface = nullptr,
                    RuntimePerformanceCounters* performance = nullptr);
    void start();
    void update(double delta_seconds);
    // True when managed gameplay explicitly wrote the scene camera during the
    // most recent Update(). Editor Play uses this to avoid immediately applying
    // its reference first-person controller over a scripted cinematic camera.
    [[nodiscard]] bool camera_was_written_last_update() const;
    // Reloads only the collectible game-code context while keeping CoreCLR and
    // Vespera.NET alive. Exposed fields are reapplied and Start() runs again.
    bool reload();
    // Dispatches native trigger overlap events to managed Components attached to
    // the trigger entity. `other_entity` is a stable SceneObjectId handle.
    void trigger_enter(std::uint64_t trigger_entity, std::uint64_t other_entity);
    void trigger_exit(std::uint64_t trigger_entity, std::uint64_t other_entity);
    void trigger_stay(std::uint64_t trigger_entity, std::uint64_t other_entity);

    // Scene.Load() from managed code is an end-of-frame request. The native game
    // owner consumes it at a safe point instead of mutating Scene while CLR
    // Update() is still on the stack.
    [[nodiscard]] std::optional<ManagedSceneLoadRequest> take_scene_load_request();
    void set_current_scene_path(const std::filesystem::path& path);

    // Runtime QA telemetry. Lifecycle callbacks are emitted from Vespera.NET
    // immediately before the user callback is invoked, preserving entity, frame
    // and assembly-generation ordering across reloads and scene teardown.
    [[nodiscard]] std::vector<ManagedRuntimeEvent> runtime_events_since(std::uint64_t after_sequence = 0) const;
    [[nodiscard]] std::uint64_t latest_runtime_event_sequence() const;
    [[nodiscard]] std::uint32_t assembly_generation() const;
    void clear_runtime_events();

    void shutdown();

    [[nodiscard]] const ManagedScriptHostStatus& status() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vespera
