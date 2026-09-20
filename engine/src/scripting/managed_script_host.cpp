#include <vespera/scripting/managed_script_host.hpp>
#include <vespera/assets/material_asset.hpp>

#include <vespera/audio/audio.hpp>
#include <vespera/assets/asset_catalog.hpp>
#include <vespera/core/log.hpp>
#include <vespera/core/game.hpp>
#include <vespera/input/input.hpp>
#include <vespera/scene/scene.hpp>
#include <vespera/ui/ui.hpp>
#include <vespera/ui/ui_surface.hpp>
#include <vespera/scene/component_access.hpp>
#include <vespera/scene/prefab.hpp>
#include <vespera/scene/scene_raycast.hpp>
#include <vespera/scene/scene_collision.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <unordered_set>
#include <vector>

#if defined(_WIN32) || defined(__linux__)
#define VESPERA_MANAGED_HOSTFXR 1
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__linux__)
#include <dlfcn.h>
#endif

namespace vespera {
namespace {

constexpr std::uint32_t kManagedAbiVersion = 12;

struct GameAssemblySignature {
    std::filesystem::file_time_type write_time{};
    std::uintmax_t size = 0;
    bool valid = false;
};

bool read_game_assembly_signature(const std::filesystem::path& path, GameAssemblySignature& out) {
    std::error_code ec;
    const auto write_time = std::filesystem::last_write_time(path, ec);
    if (ec) return false;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) return false;
    out = {write_time, size, true};
    return true;
}

bool same_signature(const GameAssemblySignature& a, const GameAssemblySignature& b) {
    return a.valid && b.valid && a.write_time == b.write_time && a.size == b.size;
}

struct NativeTransform {
    float px{}, py{}, pz{};
    float rx{}, ry{}, rz{};
    float sx{1.0f}, sy{1.0f}, sz{1.0f};
};

struct NativeTransformUpdate {
    std::uint64_t entity_id = 0;
    NativeTransform transform{};
};

struct NativePerformanceSnapshot {
    std::uint64_t frame_index = 0;
    double frame_ms = 0.0;
    double smoothed_frame_ms = 0.0;
    double max_frame_ms = 0.0;
    double update_ms = 0.0;
    double render_ms = 0.0;
    std::uint64_t scene_passes = 0;
    std::uint64_t world_draw_calls = 0;
    std::uint64_t mesh_considered = 0;
    std::uint64_t mesh_visible = 0;
    std::uint64_t mesh_culled = 0;
    std::uint64_t mesh_draw_calls = 0;
    std::uint64_t sprite_considered = 0;
    std::uint64_t sprite_visible = 0;
    std::uint64_t sprite_culled = 0;
    std::uint64_t sprite_draw_calls = 0;
    std::uint64_t lights_considered = 0;
    std::uint64_t lights_visible = 0;
    std::uint64_t lights_uploaded = 0;
    std::uint64_t ui_draw_calls = 0;
    std::uint64_t total_draw_calls = 0;
};

struct NativeVec2 {
    float x{}, y{};
};

struct NativeVec3 {
    float x{}, y{}, z{};
};

struct NativeCamera {
    NativeVec3 position{};
    float yaw = 0.0f;
    float pitch = 0.0f;
    float vertical_fov_degrees = 75.0f;
    float near_plane = 0.05f;
    float far_plane = 500.0f;
};

struct NativeColor {
    float r{}, g{}, b{}, a{};
};

struct NativeScriptSlot {
    std::uint32_t slot = 0;
};

struct NativeRaycastHit {
    int type = 0;
    float distance = 0.0f;
    NativeVec3 position{};
    NativeVec3 normal{};
    std::uint64_t entity_id = 0;
    std::uint64_t sector_index = 0;
    std::uint64_t side_index = 0;
};

struct ManagedBridgeContext {
    Scene* scene = nullptr;
    InputSystem* input = nullptr;
    AudioSystem* audio = nullptr;
    AssetCatalog* assets = nullptr;
    RuntimePerformanceCounters* performance = nullptr;
    UiSurface* ui_surface = nullptr;
    UiHandleTable ui_handles;
    std::string current_scene_path;
    std::string requested_scene_path;
    bool requested_scene_force_reload = false;
    // Entity.Destroy() is end-of-managed-frame transactional. This prevents a
    // script from invalidating Scene::entities while managed Update() is still
    // iterating and lets OnDisable/OnDestroy observe the Entity before native
    // storage is removed.
    std::vector<std::uint64_t> pending_destroy_ids;
    // Borrowed UTF-8 callback results use one bridge-owned scratch buffer. Managed
    // callers copy the returned text immediately before the next native call.
    std::string scratch_asset_text;
    std::vector<ManagedRuntimeEvent> runtime_events;
    std::uint64_t runtime_event_sequence = 0;
    std::uint64_t runtime_frame = 0;
    std::uint32_t assembly_generation = 0;
    bool camera_written_last_update = false;
};

using LogCallback = void(*)(void*, int, const char*);
using EntityExistsCallback = int(*)(void*, std::uint64_t);
using GetTransformCallback = int(*)(void*, std::uint64_t, NativeTransform*);
using SetTransformCallback = int(*)(void*, std::uint64_t, const NativeTransform*);
using SetTransformsCallback = std::size_t(*)(void*, const NativeTransformUpdate*, std::size_t);
using GetPerformanceCallback = int(*)(void*, NativePerformanceSnapshot*);
using GetCameraCallback = int(*)(void*, NativeCamera*);
using SetCameraCallback = int(*)(void*, const NativeCamera*);
using GetEnabledCallback = int(*)(void*, std::uint64_t, int*);
using SetEnabledCallback = int(*)(void*, std::uint64_t, int);
using ActionValueCallback = float(*)(void*, const char*);
using ActionStateCallback = int(*)(void*, const char*);
using FindEntityCallback = std::uint64_t(*)(void*, const char*);
using QueryEntitiesCallback = std::size_t(*)(void*, int, const char*, std::uint64_t*, std::size_t);
using CreateEntityCallback = std::uint64_t(*)(void*, const char*);
using DestroyEntityCallback = int(*)(void*, std::uint64_t);
using CloneEntityCallback = std::uint64_t(*)(void*, std::uint64_t, const char*);
using HasComponentCallback = int(*)(void*, std::uint64_t, const char*);
using ModifyComponentCallback = int(*)(void*, std::uint64_t, const char*);
using GetEntityTextCallback = const char*(*)(void*, std::uint64_t);
using SetEntityTextCallback = int(*)(void*, std::uint64_t, const char*);
using InstantiatePrefabCallback = std::uint64_t(*)(void*, const char*, const char*, const NativeVec3*);
using Raycast2DCallback = int(*)(void*, const NativeVec3*, const NativeVec3*, float, int, std::uint64_t, NativeRaycastHit*);
using OverlapCircle2DCallback = std::size_t(*)(void*, const NativeVec3*, float, int, std::uint64_t, std::uint64_t*, std::size_t);
using ResolveCircleMotion2DCallback = int(*)(void*, const NativeVec3*, const NativeVec3*, float, std::uint64_t, NativeVec3*);
using AudioPlayOneShotCallback = int(*)(void*, const char*, float);
using AudioPlayVoiceCallback = std::uint64_t(*)(void*, const char*, float, int);
using AudioVoiceActionCallback = int(*)(void*, std::uint64_t);
using AudioVoiceFloatCallback = int(*)(void*, std::uint64_t, float);
using AudioVoiceBoolCallback = int(*)(void*, std::uint64_t, int);
using AudioVoicePositionCallback = int(*)(void*, std::uint64_t, const NativeVec3*, int, float, float);
using AudioVoiceStateCallback = int(*)(void*, std::uint64_t);
using AudioSetListenerPositionCallback = int(*)(void*, const NativeVec3*);
using AudioGetListenerPositionCallback = int(*)(void*, NativeVec3*);
using AudioUseCameraListenerCallback = int(*)(void*);
using AudioSetMasterVolumeCallback = int(*)(void*, float);
using AudioGetMasterVolumeCallback = float(*)(void*);
using AudioStopAllCallback = int(*)(void*);
using AudioActiveVoiceCountCallback = std::size_t(*)(void*);
using RequestSceneLoadCallback = int(*)(void*, const char*);
using GetCurrentScenePathCallback = const char*(*)(void*);
using ResolveAssetPathCallback = const char*(*)(void*, const char*, const char*);
using FindAssetIdCallback = const char*(*)(void*, const char*);
using HasManagedScriptCallback = int(*)(void*, std::uint64_t, const char*);
using ModifyManagedScriptCallback = int(*)(void*, std::uint64_t, const char*);
using GetPropertyFloatCallback = int(*)(void*, std::uint64_t, const char*, const char*, float*);
using SetPropertyFloatCallback = int(*)(void*, std::uint64_t, const char*, const char*, float);
using GetPropertyBoolCallback = int(*)(void*, std::uint64_t, const char*, const char*, int*);
using SetPropertyBoolCallback = int(*)(void*, std::uint64_t, const char*, const char*, int);
using GetPropertyTextCallback = const char*(*)(void*, std::uint64_t, const char*, const char*);
using SetPropertyTextCallback = int(*)(void*, std::uint64_t, const char*, const char*, const char*);
using GetPropertyVec2Callback = int(*)(void*, std::uint64_t, const char*, const char*, NativeVec2*);
using SetPropertyVec2Callback = int(*)(void*, std::uint64_t, const char*, const char*, const NativeVec2*);
using GetPropertyVec3Callback = int(*)(void*, std::uint64_t, const char*, const char*, NativeVec3*);
using SetPropertyVec3Callback = int(*)(void*, std::uint64_t, const char*, const char*, const NativeVec3*);
using GetPropertyColorCallback = int(*)(void*, std::uint64_t, const char*, const char*, NativeColor*);
using SetPropertyColorCallback = int(*)(void*, std::uint64_t, const char*, const char*, const NativeColor*);
using UiFindNodeCallback = std::uint64_t(*)(void*, const char*);
using UiNodeExistsCallback = int(*)(void*, std::uint64_t);
using UiGetNodeTextCallback = const char*(*)(void*, std::uint64_t);
using UiSetNodeTextCallback = int(*)(void*, std::uint64_t, const char*);
using UiGetNodeEnabledCallback = int(*)(void*, std::uint64_t, int*);
using UiSetNodeEnabledCallback = int(*)(void*, std::uint64_t, int);
using UiGetNodeBoolCallback = int(*)(void*, std::uint64_t, int, int*);
using UiSetNodeBoolCallback = int(*)(void*, std::uint64_t, int, int);
using UiGetNodeFloatCallback = int(*)(void*, std::uint64_t, int, float*);
using UiSetNodeFloatCallback = int(*)(void*, std::uint64_t, int, float);
using UiSetNodeColorCallback = int(*)(void*, std::uint64_t, int, const NativeColor*);
using UiSetNodeAssetCallback = int(*)(void*, std::uint64_t, int, const char*, const char*);
using UiConsumeClickCallback = int(*)(void*, std::uint64_t);
using UiGetValueCallback = const char*(*)(void*, std::uint64_t);
using UiSetValueCallback = int(*)(void*, std::uint64_t, const char*);
using UiSetPropertyCallback = int(*)(void*, std::uint64_t, const char*, const char*);
using UiSetClassCallback = int(*)(void*, std::uint64_t, const char*, int);
using RuntimeEventCallback = void(*)(void*, std::uint64_t, const char*, const char*);

struct NativeApi {
    std::uint32_t abi_version = kManagedAbiVersion;
    std::uint32_t struct_size = 0;
    void* user_data = nullptr;
    LogCallback log = nullptr;
    EntityExistsCallback entity_exists = nullptr;
    GetTransformCallback get_transform = nullptr;
    SetTransformCallback set_transform = nullptr;
    GetEnabledCallback get_enabled = nullptr;
    SetEnabledCallback set_enabled = nullptr;
    ActionValueCallback action_value = nullptr;
    ActionStateCallback action_down = nullptr;
    ActionStateCallback action_pressed = nullptr;
    ActionStateCallback action_released = nullptr;
    FindEntityCallback find_entity_by_name = nullptr;
    FindEntityCallback find_entity_with_tag = nullptr;
    QueryEntitiesCallback query_entities = nullptr;
    CreateEntityCallback create_entity = nullptr;
    DestroyEntityCallback destroy_entity = nullptr;
    CloneEntityCallback clone_entity = nullptr;
    HasComponentCallback has_component = nullptr;
    ModifyComponentCallback add_component = nullptr;
    ModifyComponentCallback remove_component = nullptr;
    GetEntityTextCallback get_entity_name = nullptr;
    SetEntityTextCallback set_entity_name = nullptr;
    GetEntityTextCallback get_entity_tag = nullptr;
    SetEntityTextCallback set_entity_tag = nullptr;
    GetEntityTextCallback get_entity_layer = nullptr;
    SetEntityTextCallback set_entity_layer = nullptr;
    InstantiatePrefabCallback instantiate_prefab = nullptr;
    Raycast2DCallback raycast_2d = nullptr;
    OverlapCircle2DCallback overlap_circle_2d = nullptr;
    ResolveCircleMotion2DCallback resolve_circle_motion_2d = nullptr;
    AudioPlayOneShotCallback audio_play_one_shot = nullptr;
    AudioPlayVoiceCallback audio_play_voice = nullptr;
    AudioVoiceActionCallback audio_stop_voice = nullptr;
    AudioVoiceFloatCallback audio_set_voice_volume = nullptr;
    AudioVoiceBoolCallback audio_set_voice_loop = nullptr;
    AudioVoiceBoolCallback audio_set_voice_paused = nullptr;
    AudioVoicePositionCallback audio_set_voice_position = nullptr;
    AudioVoiceStateCallback audio_voice_playing = nullptr;
    AudioSetListenerPositionCallback audio_set_listener_position = nullptr;
    AudioGetListenerPositionCallback audio_get_listener_position = nullptr;
    AudioUseCameraListenerCallback audio_use_camera_listener = nullptr;
    AudioSetMasterVolumeCallback audio_set_master_volume = nullptr;
    AudioGetMasterVolumeCallback audio_get_master_volume = nullptr;
    AudioStopAllCallback audio_stop_all = nullptr;
    AudioActiveVoiceCountCallback audio_active_voice_count = nullptr;
    RequestSceneLoadCallback request_scene_load = nullptr;
    GetCurrentScenePathCallback get_current_scene_path = nullptr;
    ResolveAssetPathCallback resolve_asset_path = nullptr;
    FindAssetIdCallback find_asset_id = nullptr;
    HasManagedScriptCallback has_managed_script = nullptr;
    ModifyManagedScriptCallback add_managed_script = nullptr;
    ModifyManagedScriptCallback remove_managed_script = nullptr;
    GetPropertyFloatCallback get_property_float = nullptr;
    SetPropertyFloatCallback set_property_float = nullptr;
    GetPropertyBoolCallback get_property_bool = nullptr;
    SetPropertyBoolCallback set_property_bool = nullptr;
    GetPropertyTextCallback get_property_text = nullptr;
    SetPropertyTextCallback set_property_text = nullptr;
    GetPropertyVec2Callback get_property_vec2 = nullptr;
    SetPropertyVec2Callback set_property_vec2 = nullptr;
    GetPropertyVec3Callback get_property_vec3 = nullptr;
    SetPropertyVec3Callback set_property_vec3 = nullptr;
    GetPropertyColorCallback get_property_color = nullptr;
    SetPropertyColorCallback set_property_color = nullptr;
    UiFindNodeCallback ui_find_node = nullptr;
    UiNodeExistsCallback ui_node_exists = nullptr;
    UiGetNodeTextCallback ui_get_node_text = nullptr;
    UiSetNodeTextCallback ui_set_node_text = nullptr;
    UiGetNodeEnabledCallback ui_get_node_enabled = nullptr;
    UiSetNodeEnabledCallback ui_set_node_enabled = nullptr;
    UiGetNodeBoolCallback ui_get_node_bool = nullptr;
    UiSetNodeBoolCallback ui_set_node_bool = nullptr;
    UiGetNodeFloatCallback ui_get_node_float = nullptr;
    UiSetNodeFloatCallback ui_set_node_float = nullptr;
    UiSetNodeColorCallback ui_set_node_color = nullptr;
    UiSetNodeAssetCallback ui_set_node_asset = nullptr;
    UiConsumeClickCallback ui_consume_click = nullptr;
    UiGetValueCallback ui_get_value = nullptr;
    UiSetValueCallback ui_set_value = nullptr;
    UiSetPropertyCallback ui_set_property = nullptr;
    UiSetClassCallback ui_set_class = nullptr;
    RuntimeEventCallback trace_runtime_event = nullptr;
    GetCameraCallback get_camera = nullptr;
    SetCameraCallback set_camera = nullptr;
    SetTransformsCallback set_transforms = nullptr;
    GetPerformanceCallback get_performance = nullptr;
};

void managed_runtime_event(void* user, std::uint64_t entity_id, const char* component, const char* callback) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    if (!bridge || !component || !callback) return;
    ManagedRuntimeEvent event;
    event.sequence = ++bridge->runtime_event_sequence;
    event.frame = bridge->runtime_frame;
    event.assembly_generation = bridge->assembly_generation;
    event.entity_id = entity_id;
    event.component = component;
    event.callback = callback;
    bridge->runtime_events.push_back(std::move(event));
    constexpr std::size_t kMaxRuntimeEvents = 4096;
    if (bridge->runtime_events.size() > kMaxRuntimeEvents) {
        bridge->runtime_events.erase(bridge->runtime_events.begin(),
            bridge->runtime_events.begin() + static_cast<std::ptrdiff_t>(bridge->runtime_events.size() - kMaxRuntimeEvents));
    }
}

std::string_view managed_ui_key(ManagedBridgeContext* bridge, std::uint64_t handle) {
    if (!bridge || !bridge->ui_surface || !bridge->ui_handles.exists(handle)) return {};
    return bridge->ui_handles.key(handle);
}

std::uint64_t managed_ui_find_node(void* user, const char* name) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    if (!bridge || !bridge->ui_surface || !name || *name == '\0') return 0;
    return bridge->ui_handles.find(name);
}

int managed_ui_node_exists(void* user, std::uint64_t handle) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    return bridge && bridge->ui_handles.exists(handle) ? 1 : 0;
}

const char* managed_ui_get_node_text(void* user, std::uint64_t handle) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    const std::string_view key = managed_ui_key(bridge, handle);
    if (key.empty()) return "";
    bridge->scratch_asset_text = bridge->ui_surface->text(key);
    return bridge->scratch_asset_text.c_str();
}

int managed_ui_set_node_text(void* user, std::uint64_t handle, const char* value) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    const std::string_view key = managed_ui_key(bridge, handle);
    return !key.empty() && bridge->ui_surface->set_text(key, value ? value : "") ? 1 : 0;
}

int managed_ui_get_node_enabled(void* user, std::uint64_t handle, int* value) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    if (!value) return 0;
    const std::string_view key = managed_ui_key(bridge, handle);
    bool visible = false;
    if (key.empty() || !bridge->ui_surface->visible(key, visible)) return 0;
    *value = visible ? 1 : 0;
    return 1;
}

int managed_ui_set_node_enabled(void* user, std::uint64_t handle, int value) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    const std::string_view key = managed_ui_key(bridge, handle);
    return !key.empty() && bridge->ui_surface->set_visible(key, value != 0) ? 1 : 0;
}

enum class ManagedUiBoolProperty : int {
    Interactable = 1,
    Focused = 2,
    ReadOnly = 3,
};
enum class ManagedUiFloatProperty : int {
    Value = 1,
};

int managed_ui_get_node_bool(void* user, std::uint64_t handle, int property, int* value) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    if (!value) return 0;
    const std::string_view key = managed_ui_key(bridge, handle);
    if (key.empty()) return 0;
    bool result = false;
    switch (static_cast<ManagedUiBoolProperty>(property)) {
        case ManagedUiBoolProperty::Interactable:
            if (!bridge->ui_surface->interactable(key, result)) return 0;
            break;
        case ManagedUiBoolProperty::Focused:
            result = bridge->ui_surface->focused_id() == key;
            break;
        case ManagedUiBoolProperty::ReadOnly:
            if (!bridge->ui_surface->read_only(key, result)) return 0;
            break;
        default: return 0;
    }
    *value = result ? 1 : 0;
    return 1;
}

int managed_ui_set_node_bool(void* user, std::uint64_t handle, int property, int value) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    const std::string_view key = managed_ui_key(bridge, handle);
    if (key.empty()) return 0;
    switch (static_cast<ManagedUiBoolProperty>(property)) {
        case ManagedUiBoolProperty::Interactable:
            return bridge->ui_surface->set_interactable(key, value != 0) ? 1 : 0;
        case ManagedUiBoolProperty::Focused:
            return (value != 0 ? bridge->ui_surface->focus(key, true) : bridge->ui_surface->blur(key)) ? 1 : 0;
        case ManagedUiBoolProperty::ReadOnly:
            return bridge->ui_surface->set_read_only(key, value != 0) ? 1 : 0;
    }
    return 0;
}

int managed_ui_get_node_float(void* user, std::uint64_t handle, int property, float* value) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    if (!value || static_cast<ManagedUiFloatProperty>(property) != ManagedUiFloatProperty::Value) return 0;
    const std::string_view key = managed_ui_key(bridge, handle);
    return !key.empty() && bridge->ui_surface->numeric_value(key, *value) ? 1 : 0;
}

int managed_ui_set_node_float(void* user, std::uint64_t handle, int property, float value) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    if (static_cast<ManagedUiFloatProperty>(property) != ManagedUiFloatProperty::Value) return 0;
    const std::string_view key = managed_ui_key(bridge, handle);
    return !key.empty() && bridge->ui_surface->set_numeric_value(key, value) ? 1 : 0;
}

int managed_ui_set_node_color(void* user, std::uint64_t handle, int property, const NativeColor* value) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    if (!value || property < static_cast<int>(UiColorSlot::Visual)
        || property > static_cast<int>(UiColorSlot::ProgressBackground)) return 0;
    const std::string_view key = managed_ui_key(bridge, handle);
    const std::array<float, 4> color{value->r, value->g, value->b, value->a};
    return !key.empty() && bridge->ui_surface->set_color(key, static_cast<UiColorSlot>(property), color) ? 1 : 0;
}

int managed_ui_set_node_asset(void* user, std::uint64_t handle, int property, const char* asset_id, const char* fallback_path) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    if (!bridge || property < static_cast<int>(UiAssetSlot::Image) || property > static_cast<int>(UiAssetSlot::Font)) return 0;
    const std::string_view key = managed_ui_key(bridge, handle);
    if (key.empty()) return 0;

    AssetReference reference;
    if (asset_id) reference.asset_id = asset_id;
    if (fallback_path) reference.path = fallback_path;
    std::string resolved_path;
    const auto slot = static_cast<UiAssetSlot>(property);
    if (!reference.empty() && bridge->assets) {
        const auto resolved = bridge->assets->resolve_reference(reference);
        if (!resolved) return 0;
        if (slot == UiAssetSlot::Image && resolved.record->kind != AssetKind::Texture) return 0;
        if (slot == UiAssetSlot::Font && resolved.record->kind != AssetKind::Font) return 0;
        reference = {resolved.record->asset_id, resolved.record->relative_path};
        resolved_path = resolved.record->absolute_path.string();
    }
    return bridge->ui_surface->set_asset(key, slot, reference, resolved_path) ? 1 : 0;
}

int managed_ui_consume_click(void* user, std::uint64_t handle) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    const std::string_view key = managed_ui_key(bridge, handle);
    return !key.empty() && bridge->ui_surface->consume_clicks(key) > 0 ? 1 : 0;
}

const char* managed_ui_get_value(void* user, std::uint64_t handle) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    const std::string_view key = managed_ui_key(bridge, handle);
    if (key.empty()) return "";
    bridge->scratch_asset_text = bridge->ui_surface->value(key);
    return bridge->scratch_asset_text.c_str();
}

int managed_ui_set_value(void* user, std::uint64_t handle, const char* value) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    const std::string_view key = managed_ui_key(bridge, handle);
    return !key.empty() && bridge->ui_surface->set_value(key, value ? value : "") ? 1 : 0;
}

int managed_ui_set_property(void* user, std::uint64_t handle, const char* property, const char* value) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    if (!property || !*property || !value) return 0;
    const std::string_view key = managed_ui_key(bridge, handle);
    return !key.empty() && bridge->ui_surface->set_property(key, property, value) ? 1 : 0;
}

int managed_ui_set_class(void* user, std::uint64_t handle, const char* class_name, int enabled) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    if (!class_name || !*class_name) return 0;
    const std::string_view key = managed_ui_key(bridge, handle);
    return !key.empty() && bridge->ui_surface->set_class(key, class_name, enabled != 0) ? 1 : 0;
}

void managed_log(void*, int level, const char* message) {
    const std::string text = message ? std::string(message) : std::string{};
    if (level >= 2) log::error("[C#] " + text);
    else if (level == 1) log::warn("[C#] " + text);
    else log::info("[C#] " + text);
}

int managed_entity_exists(void* user, std::uint64_t id) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    return bridge && bridge->scene && bridge->scene->find_entity(id) ? 1 : 0;
}

int managed_get_transform(void* user, std::uint64_t id, NativeTransform* out) {
    if (!out) return 0;
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    Entity* entity = bridge && bridge->scene ? bridge->scene->find_entity(id) : nullptr;
    if (!entity) return 0;
    const auto& t = entity->transform;
    *out = {t.position.x, t.position.y, t.position.z,
            t.rotation.x, t.rotation.y, t.rotation.z,
            t.scale.x, t.scale.y, t.scale.z};
    return 1;
}

int managed_set_transform(void* user, std::uint64_t id, const NativeTransform* value) {
    if (!value) return 0;
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    Entity* entity = bridge && bridge->scene ? bridge->scene->find_entity(id) : nullptr;
    if (!entity) return 0;
    entity->transform.position = {value->px, value->py, value->pz};
    entity->transform.rotation = {value->rx, value->ry, value->rz};
    entity->transform.scale = {value->sx, value->sy, value->sz};
    return 1;
}

std::size_t managed_set_transforms(void* user, const NativeTransformUpdate* updates, std::size_t count) {
    if (!updates && count != 0) return 0;
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    if (!bridge || !bridge->scene) return 0;
    std::size_t applied = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const NativeTransformUpdate& update = updates[index];
        Entity* entity = bridge->scene->find_entity(update.entity_id);
        if (!entity) continue;
        const NativeTransform& value = update.transform;
        entity->transform.position = {value.px, value.py, value.pz};
        entity->transform.rotation = {value.rx, value.ry, value.rz};
        entity->transform.scale = {value.sx, value.sy, value.sz};
        ++applied;
    }
    return applied;
}

int managed_get_performance(void* user, NativePerformanceSnapshot* out) {
    if (!out) return 0;
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    if (!bridge || !bridge->performance) return 0;
    const RuntimePerformanceCounters& performance = *bridge->performance;
    const RenderFrameStats& render = performance.render;
    *out = {
        performance.frame_index,
        performance.last_frame_ms,
        performance.smoothed_frame_ms,
        performance.max_frame_ms,
        performance.update_ms,
        performance.render_ms,
        render.scene_passes,
        render.world_draw_calls,
        render.mesh_entities_considered,
        render.mesh_instances_visible,
        render.mesh_instances_culled,
        render.mesh_draw_calls,
        render.sprite_entities_considered,
        render.sprite_entities_visible,
        render.sprite_entities_culled,
        render.sprite_draw_calls,
        render.point_lights_considered,
        render.point_lights_frustum_visible,
        render.point_lights_uploaded,
        render.ui_draw_calls,
        render.total_draw_calls()
    };
    return 1;
}

int managed_get_camera(void* user, NativeCamera* out) {
    if (!out) return 0;
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    if (!bridge || !bridge->scene) return 0;
    const Camera& camera = bridge->scene->camera;
    out->position = {camera.position.x, camera.position.y, camera.position.z};
    out->yaw = camera.yaw;
    out->pitch = camera.pitch;
    out->vertical_fov_degrees = camera.vertical_fov_degrees;
    out->near_plane = camera.near_plane;
    out->far_plane = camera.far_plane;
    return 1;
}

int managed_set_camera(void* user, const NativeCamera* value) {
    if (!value) return 0;
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    if (!bridge || !bridge->scene) return 0;
    Camera& camera = bridge->scene->camera;
    camera.position = {value->position.x, value->position.y, value->position.z};
    camera.yaw = value->yaw;
    camera.pitch = std::clamp(value->pitch, -1.55f, 1.55f);
    camera.vertical_fov_degrees = std::clamp(value->vertical_fov_degrees, 1.0f, 179.0f);
    camera.near_plane = (std::max)(value->near_plane, 0.001f);
    camera.far_plane = (std::max)(value->far_plane, camera.near_plane + 0.001f);
    bridge->camera_written_last_update = true;
    return 1;
}

int managed_get_enabled(void* user, std::uint64_t id, int* out) {
    if (!out) return 0;
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    Entity* entity = bridge && bridge->scene ? bridge->scene->find_entity(id) : nullptr;
    if (!entity) return 0;
    *out = entity->enabled ? 1 : 0;
    return 1;
}

int managed_set_enabled(void* user, std::uint64_t id, int enabled) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    Entity* entity = bridge && bridge->scene ? bridge->scene->find_entity(id) : nullptr;
    if (!entity) return 0;
    entity->enabled = enabled != 0;
    return 1;
}


float managed_action_value(void* user, const char* action) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    return bridge && bridge->input && action ? bridge->input->action_value(action) : 0.0f;
}

int managed_action_down(void* user, const char* action) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    return bridge && bridge->input && action && bridge->input->action_down(action) ? 1 : 0;
}

int managed_action_pressed(void* user, const char* action) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    return bridge && bridge->input && action && bridge->input->action_pressed(action) ? 1 : 0;
}

int managed_action_released(void* user, const char* action) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    return bridge && bridge->input && action && bridge->input->action_released(action) ? 1 : 0;
}

std::uint64_t managed_find_entity_by_name(void* user, const char* name) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    if (!bridge || !bridge->scene || !name) return 0;
    const Entity* entity = bridge->scene->find_entity_by_name(name);
    return entity ? entity->id : 0;
}

std::uint64_t managed_find_entity_with_tag(void* user, const char* tag) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    if (!bridge || !bridge->scene || !tag) return 0;
    const Entity* entity = bridge->scene->find_entity_with_tag(tag);
    return entity ? entity->id : 0;
}

std::size_t managed_query_entities(void* user, int query_type, const char* value,
    std::uint64_t* out_ids, std::size_t capacity) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    if (!bridge || !bridge->scene) return 0;

    std::vector<std::uint64_t> ids;
    ids.reserve(bridge->scene->entities.size());
    const std::string_view filter = value ? std::string_view(value) : std::string_view{};
    for (const auto& entity : bridge->scene->entities) {
        bool match = false;
        switch (query_type) {
            case 0: match = true; break; // all
            case 1: match = entity.tag == filter; break;
            case 2: match = entity.layer == filter; break;
            case 3: match = !filter.empty() && entity.has_component(filter); break;
            default: break;
        }
        if (match) ids.push_back(entity.id);
    }
    if (out_ids && capacity > 0) {
        const std::size_t copy_count = (std::min)(capacity, ids.size());
        std::copy_n(ids.begin(), copy_count, out_ids);
    }
    return ids.size();
}

std::uint64_t managed_create_entity(void* user, const char* name) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    if (!bridge || !bridge->scene) return 0;
    return bridge->scene->create_entity(name && *name ? name : "Entity").id;
}

int managed_destroy_entity(void* user, std::uint64_t id) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    if (!bridge || !bridge->scene || !bridge->scene->find_entity(id)) return 0;
    if (std::find(bridge->pending_destroy_ids.begin(), bridge->pending_destroy_ids.end(), id)
        == bridge->pending_destroy_ids.end()) {
        bridge->pending_destroy_ids.push_back(id);
    }
    return 1;
}

std::uint64_t managed_clone_entity(void* user, std::uint64_t id, const char* name) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    if (!bridge || !bridge->scene) return 0;
    Entity* entity = bridge->scene->clone_entity(id, name ? std::string(name) : std::string{});
    return entity ? entity->id : 0;
}

int managed_has_component(void* user, std::uint64_t id, const char* key) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    const Entity* entity = bridge && bridge->scene ? bridge->scene->find_entity(id) : nullptr;
    return entity && key && entity->has_component(key) ? 1 : 0;
}

int managed_add_component(void* user, std::uint64_t id, const char* key) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    Entity* entity = bridge && bridge->scene ? bridge->scene->find_entity(id) : nullptr;
    return entity && key && entity->add_component(key) ? 1 : 0;
}

int managed_remove_component(void* user, std::uint64_t id, const char* key) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    Entity* entity = bridge && bridge->scene ? bridge->scene->find_entity(id) : nullptr;
    return entity && key && entity->remove_component(key) ? 1 : 0;
}


const char* managed_get_entity_name(void* user, std::uint64_t id) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    const Entity* entity = bridge && bridge->scene ? bridge->scene->find_entity(id) : nullptr;
    return entity ? entity->name.c_str() : nullptr;
}
int managed_set_entity_name(void* user, std::uint64_t id, const char* value) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    Entity* entity = bridge && bridge->scene ? bridge->scene->find_entity(id) : nullptr;
    if (!entity || !value) return 0;
    entity->name = value;
    return 1;
}
const char* managed_get_entity_tag(void* user, std::uint64_t id) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    const Entity* entity = bridge && bridge->scene ? bridge->scene->find_entity(id) : nullptr;
    return entity ? entity->tag.c_str() : nullptr;
}
int managed_set_entity_tag(void* user, std::uint64_t id, const char* value) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    Entity* entity = bridge && bridge->scene ? bridge->scene->find_entity(id) : nullptr;
    if (!entity || !value) return 0;
    entity->tag = value;
    return 1;
}
const char* managed_get_entity_layer(void* user, std::uint64_t id) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    const Entity* entity = bridge && bridge->scene ? bridge->scene->find_entity(id) : nullptr;
    return entity ? entity->layer.c_str() : nullptr;
}
int managed_set_entity_layer(void* user, std::uint64_t id, const char* value) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    Entity* entity = bridge && bridge->scene ? bridge->scene->find_entity(id) : nullptr;
    if (!entity || !value) return 0;
    entity->layer = value;
    return 1;
}

std::uint64_t managed_instantiate_prefab(void* user, const char* path, const char* name, const NativeVec3* position) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    if (!bridge || !bridge->scene || !path || !*path) return 0;
    EntityPrefab prefab;
    const auto loaded = load_entity_prefab(*bridge->scene, prefab, std::filesystem::path(path));
    if (!loaded) {
        log::warn(std::string("[C#] Prefab instantiate failed: ") + loaded.message);
        return 0;
    }
    AssetReference source_reference;
    source_reference.path = std::filesystem::path(path);
    if (bridge->assets) {
        if (const auto* record = bridge->assets->find(path)) {
            source_reference.asset_id = record->asset_id;
            source_reference.path = record->relative_path;
        } else {
            std::error_code ec;
            const auto relative = std::filesystem::relative(std::filesystem::path(path), bridge->assets->root(), ec);
            if (!ec) {
                if (const auto* relative_record = bridge->assets->find(relative.generic_string())) {
                    source_reference.asset_id = relative_record->asset_id;
                    source_reference.path = relative_record->relative_path;
                }
            }
        }
    }
    Entity* entity = instantiate_entity_prefab(*bridge->scene, prefab,
        name ? std::string(name) : std::string{}, std::move(source_reference));
    if (!entity) return 0;
    if (position) entity->transform.position = {position->x, position->y, position->z};
    if (bridge->assets) (void)hydrate_scene_materials(*bridge->scene, *bridge->assets);
    return entity->id;
}

int managed_raycast_2d(void* user, const NativeVec3* origin, const NativeVec3* direction,
    float max_distance, int include_triggers, std::uint64_t ignore_entity, NativeRaycastHit* out) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    if (!bridge || !bridge->scene || !origin || !direction || !out) return 0;
    SceneRaycastOptions options;
    options.max_distance = (std::max)(0.0f, max_distance);
    options.include_trigger_colliders = include_triggers != 0;
    options.ignore_entity = ignore_entity;
    const auto hit = raycast_scene_2d(*bridge->scene,
        {origin->x, origin->y, origin->z}, {direction->x, direction->y, direction->z}, options);
    if (!hit) return 0;
    out->type = hit->type == SceneRaycastHitType::EntityCollider ? 1 : 0;
    out->distance = hit->distance;
    out->position = {hit->position.x, hit->position.y, hit->position.z};
    out->normal = {hit->normal.x, hit->normal.y, hit->normal.z};
    out->entity_id = hit->entity_id;
    out->sector_index = hit->sector_index == static_cast<std::size_t>(-1) ? UINT64_MAX : static_cast<std::uint64_t>(hit->sector_index);
    out->side_index = hit->side_index == static_cast<std::size_t>(-1) ? UINT64_MAX : static_cast<std::uint64_t>(hit->side_index);
    return 1;
}


std::size_t managed_overlap_circle_2d(void* user, const NativeVec3* position, float radius,
    int include_triggers, std::uint64_t ignore_entity, std::uint64_t* out_ids, std::size_t capacity) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    if (!bridge || !bridge->scene || !position) return 0;
    const auto ids = scene_circle_overlapping_colliders(*bridge->scene,
        {position->x, position->y, position->z}, (std::max)(0.0f, radius),
        include_triggers != 0, ignore_entity);
    if (out_ids && capacity > 0) {
        const std::size_t copy_count = (std::min)(capacity, ids.size());
        std::copy_n(ids.begin(), copy_count, out_ids);
    }
    return ids.size();
}

int managed_resolve_circle_motion_2d(void* user, const NativeVec3* from, const NativeVec3* candidate,
    float radius, std::uint64_t ignore_entity, NativeVec3* out) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    if (!bridge || !bridge->scene || !from || !candidate || !out) return 0;
    const Vec3 resolved = resolve_circle_motion_against_scene_colliders(*bridge->scene,
        {from->x, from->y, from->z}, {candidate->x, candidate->y, candidate->z},
        (std::max)(0.0f, radius), ignore_entity);
    *out = {resolved.x, resolved.y, resolved.z};
    return 1;
}

int managed_audio_play_one_shot(void* user, const char* path, float volume) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    return bridge && bridge->audio && path && *path
        && bridge->audio->play_one_shot(std::filesystem::path(path), volume) ? 1 : 0;
}



std::uint64_t managed_audio_play_voice(void* user, const char* path, float volume, int loop) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    return bridge && bridge->audio && path && *path
        ? bridge->audio->play(std::filesystem::path(path), volume, loop != 0)
        : kInvalidAudioVoice;
}

int managed_audio_stop_voice(void* user, std::uint64_t handle) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    return bridge && bridge->audio && bridge->audio->stop(handle) ? 1 : 0;
}

int managed_audio_set_voice_volume(void* user, std::uint64_t handle, float volume) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    return bridge && bridge->audio && bridge->audio->set_voice_volume(handle, volume) ? 1 : 0;
}

int managed_audio_set_voice_loop(void* user, std::uint64_t handle, int loop) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    return bridge && bridge->audio && bridge->audio->set_voice_loop(handle, loop != 0) ? 1 : 0;
}

int managed_audio_set_voice_paused(void* user, std::uint64_t handle, int paused) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    return bridge && bridge->audio && bridge->audio->set_voice_paused(handle, paused != 0) ? 1 : 0;
}

int managed_audio_set_voice_position(void* user, std::uint64_t handle, const NativeVec3* position,
    int spatial, float min_distance, float max_distance) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    return bridge && bridge->audio && position
        && bridge->audio->set_voice_position(handle, {position->x, position->y, position->z},
            spatial != 0, min_distance, max_distance) ? 1 : 0;
}

int managed_audio_voice_playing(void* user, std::uint64_t handle) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    return bridge && bridge->audio && bridge->audio->voice_playing(handle) ? 1 : 0;
}

int managed_audio_set_listener_position(void* user, const NativeVec3* position) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    if (!bridge || !bridge->audio || !position) return 0;
    bridge->audio->set_listener_position({position->x, position->y, position->z});
    return 1;
}

int managed_audio_get_listener_position(void* user, NativeVec3* out) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    if (!bridge || !bridge->audio || !out) return 0;
    const Vec3 position = bridge->audio->listener_position();
    *out = {position.x, position.y, position.z};
    return 1;
}

int managed_audio_use_camera_listener(void* user) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    if (!bridge || !bridge->audio) return 0;
    bridge->audio->use_camera_listener();
    return 1;
}

int managed_audio_set_master_volume(void* user, float volume) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    if (!bridge || !bridge->audio) return 0;
    bridge->audio->set_master_volume(volume);
    return 1;
}

float managed_audio_get_master_volume(void* user) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    return bridge && bridge->audio ? bridge->audio->master_volume() : 0.0f;
}

int managed_audio_stop_all(void* user) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    if (!bridge || !bridge->audio) return 0;
    bridge->audio->stop_all();
    return 1;
}

std::size_t managed_audio_active_voice_count(void* user) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    return bridge && bridge->audio ? bridge->audio->active_voice_count() : 0;
}

int managed_request_scene_load(void* user, const char* path) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    if (!bridge || !path || !*path) return 0;
    constexpr std::string_view kReloadCurrent = "@vespera/reload-current";
    if (std::string_view(path) == kReloadCurrent) {
        if (bridge->current_scene_path.empty()) return 0;
        bridge->requested_scene_path = bridge->current_scene_path;
        bridge->requested_scene_force_reload = true;
        return 1;
    }
    bridge->requested_scene_path = path;
    bridge->requested_scene_force_reload = false;
    return 1;
}

const char* managed_get_current_scene_path(void* user) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    return bridge ? bridge->current_scene_path.c_str() : nullptr;
}

std::filesystem::path managed_catalog_relative_path(const AssetCatalog& catalog, const char* value) {
    if (!value || !*value) return {};
    std::filesystem::path path(value);
    std::error_code ec;
    if (path.is_absolute()) {
        const auto relative = std::filesystem::relative(path, catalog.root(), ec);
        if (!ec) return relative.lexically_normal();
        return path.lexically_normal();
    }
    path = path.lexically_normal();
    if (!path.empty()) {
        auto it = path.begin();
        if (it != path.end() && *it == catalog.root().filename()) {
            std::filesystem::path stripped;
            for (++it; it != path.end(); ++it) stripped /= *it;
            if (!stripped.empty()) return stripped.lexically_normal();
        }
    }
    return path;
}

const char* managed_resolve_asset_path(void* user, const char* asset_id, const char* fallback_path) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    if (!bridge || !bridge->assets) return nullptr;
    AssetReference reference;
    if (asset_id) reference.asset_id = asset_id;
    reference.path = managed_catalog_relative_path(*bridge->assets, fallback_path);
    const auto resolved = bridge->assets->resolve_reference(reference);
    if (!resolved) return nullptr;
    bridge->scratch_asset_text = resolved.record->absolute_path.lexically_normal().generic_string();
    return bridge->scratch_asset_text.c_str();
}

const char* managed_find_asset_id(void* user, const char* path) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    if (!bridge || !bridge->assets || !path || !*path) return nullptr;
    const auto relative = managed_catalog_relative_path(*bridge->assets, path);
    const auto* record = bridge->assets->find(relative.generic_string());
    if (!record) return nullptr;
    bridge->scratch_asset_text = record->asset_id;
    return bridge->scratch_asset_text.c_str();
}

int managed_has_managed_script(void* user, std::uint64_t id, const char* class_name) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    const Entity* entity = bridge && bridge->scene ? bridge->scene->find_entity(id) : nullptr;
    if (!entity || !class_name) return 0;
    return std::any_of(entity->managed_scripts.begin(), entity->managed_scripts.end(), [&](const auto& script) {
        return script.class_name == class_name;
    }) ? 1 : 0;
}

int managed_add_managed_script(void* user, std::uint64_t id, const char* class_name) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    Entity* entity = bridge && bridge->scene ? bridge->scene->find_entity(id) : nullptr;
    if (!entity || !class_name || !*class_name) return 0;
    if (managed_has_managed_script(user, id, class_name)) return 0;
    ManagedScriptComponent script;
    script.class_name = class_name;
    entity->managed_scripts.push_back(std::move(script));
    return 1;
}

int managed_remove_managed_script(void* user, std::uint64_t id, const char* class_name) {
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    Entity* entity = bridge && bridge->scene ? bridge->scene->find_entity(id) : nullptr;
    if (!entity || !class_name) return 0;
    const auto before = entity->managed_scripts.size();
    std::erase_if(entity->managed_scripts, [&](const auto& script) { return script.class_name == class_name; });
    return entity->managed_scripts.size() != before ? 1 : 0;
}

int managed_get_property_float(void* user, std::uint64_t id, const char* component, const char* property, float* out) {
    if (!out || !component || !property) return 0;
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    const Entity* entity = bridge && bridge->scene ? bridge->scene->find_entity(id) : nullptr;
    if (!entity) return 0;
    const auto value = get_builtin_component_property(*entity, component, property);
    if (!value) return 0;
    const float* number = std::get_if<float>(&*value);
    if (!number) return 0;
    *out = *number;
    return 1;
}

int managed_set_property_float(void* user, std::uint64_t id, const char* component, const char* property, float value) {
    if (!component || !property) return 0;
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    Entity* entity = bridge && bridge->scene ? bridge->scene->find_entity(id) : nullptr;
    return entity && set_builtin_component_property(*entity, component, property, BuiltinPropertyValue{value}) ? 1 : 0;
}

int managed_get_property_bool(void* user, std::uint64_t id, const char* component, const char* property, int* out) {
    if (!out || !component || !property) return 0;
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    const Entity* entity = bridge && bridge->scene ? bridge->scene->find_entity(id) : nullptr;
    if (!entity) return 0;
    const auto value = get_builtin_component_property(*entity, component, property);
    if (!value) return 0;
    const bool* boolean = std::get_if<bool>(&*value);
    if (!boolean) return 0;
    *out = *boolean ? 1 : 0;
    return 1;
}

int managed_set_property_bool(void* user, std::uint64_t id, const char* component, const char* property, int value) {
    if (!component || !property) return 0;
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    Entity* entity = bridge && bridge->scene ? bridge->scene->find_entity(id) : nullptr;
    return entity && set_builtin_component_property(*entity, component, property, BuiltinPropertyValue{value != 0}) ? 1 : 0;
}

const char* managed_get_property_text(void* user, std::uint64_t id, const char* component, const char* property) {
    static thread_local std::string borrowed;
    if (!component || !property) return nullptr;
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    const Entity* entity = bridge && bridge->scene ? bridge->scene->find_entity(id) : nullptr;
    if (!entity) return nullptr;
    const auto value = get_builtin_component_property(*entity, component, property);
    if (!value) return nullptr;
    const auto* text = std::get_if<std::string>(&*value);
    if (!text) return nullptr;
    borrowed = *text;
    return borrowed.c_str();
}

int managed_set_property_text(void* user, std::uint64_t id, const char* component, const char* property, const char* value) {
    if (!component || !property || !value) return 0;
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    Entity* entity = bridge && bridge->scene ? bridge->scene->find_entity(id) : nullptr;
    if (!entity || !set_builtin_component_property(*entity, component, property, BuiltinPropertyValue{std::string(value)})) return 0;
    if (bridge->assets && std::string_view(component) == "sectorline.mesh_renderer"
        && (std::string_view(property) == "material_asset_id" || std::string_view(property) == "material_path")) {
        (void)hydrate_scene_materials(*bridge->scene, *bridge->assets);
    }
    return 1;
}

int managed_get_property_vec2(void* user, std::uint64_t id, const char* component, const char* property, NativeVec2* out) {
    if (!out || !component || !property) return 0;
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    const Entity* entity = bridge && bridge->scene ? bridge->scene->find_entity(id) : nullptr;
    if (!entity) return 0;
    const auto value = get_builtin_component_property(*entity, component, property);
    if (!value) return 0;
    const auto* vector = std::get_if<Vec2>(&*value);
    if (!vector) return 0;
    *out = {vector->x, vector->z};
    return 1;
}

int managed_set_property_vec2(void* user, std::uint64_t id, const char* component, const char* property, const NativeVec2* value) {
    if (!value || !component || !property) return 0;
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    Entity* entity = bridge && bridge->scene ? bridge->scene->find_entity(id) : nullptr;
    return entity && set_builtin_component_property(*entity, component, property, BuiltinPropertyValue{Vec2{value->x, value->y}}) ? 1 : 0;
}

int managed_get_property_vec3(void* user, std::uint64_t id, const char* component, const char* property, NativeVec3* out) {
    if (!out || !component || !property) return 0;
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    const Entity* entity = bridge && bridge->scene ? bridge->scene->find_entity(id) : nullptr;
    if (!entity) return 0;
    const auto value = get_builtin_component_property(*entity, component, property);
    if (!value) return 0;
    const auto* vector = std::get_if<Vec3>(&*value);
    if (!vector) return 0;
    *out = {vector->x, vector->y, vector->z};
    return 1;
}

int managed_set_property_vec3(void* user, std::uint64_t id, const char* component, const char* property, const NativeVec3* value) {
    if (!value || !component || !property) return 0;
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    Entity* entity = bridge && bridge->scene ? bridge->scene->find_entity(id) : nullptr;
    return entity && set_builtin_component_property(*entity, component, property, BuiltinPropertyValue{Vec3{value->x, value->y, value->z}}) ? 1 : 0;
}

int managed_get_property_color(void* user, std::uint64_t id, const char* component, const char* property, NativeColor* out) {
    if (!out || !component || !property) return 0;
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    const Entity* entity = bridge && bridge->scene ? bridge->scene->find_entity(id) : nullptr;
    if (!entity) return 0;
    const auto value = get_builtin_component_property(*entity, component, property);
    if (!value) return 0;
    const auto* color = std::get_if<std::array<float, 4>>(&*value);
    if (!color) return 0;
    *out = {(*color)[0], (*color)[1], (*color)[2], (*color)[3]};
    return 1;
}

int managed_set_property_color(void* user, std::uint64_t id, const char* component, const char* property, const NativeColor* value) {
    if (!value || !component || !property) return 0;
    auto* bridge = static_cast<ManagedBridgeContext*>(user);
    Entity* entity = bridge && bridge->scene ? bridge->scene->find_entity(id) : nullptr;
    return entity && set_builtin_component_property(*entity, component, property,
        BuiltinPropertyValue{std::array<float, 4>{value->r, value->g, value->b, value->a}}) ? 1 : 0;
}

#ifdef VESPERA_MANAGED_HOSTFXR

#ifdef _WIN32
using char_t = wchar_t;
using host_library_t = HMODULE;
#define VESPERA_HOSTFXR_CALLTYPE __cdecl
#define VESPERA_LOAD_ASSEMBLY_CALLTYPE __stdcall
#else
using char_t = char;
using host_library_t = void*;
#define VESPERA_HOSTFXR_CALLTYPE
#define VESPERA_LOAD_ASSEMBLY_CALLTYPE
#endif

using hostfxr_handle = void*;

enum hostfxr_delegate_type {
    hdt_com_activation = 0,
    hdt_load_in_memory_assembly = 1,
    hdt_winrt_activation = 2,
    hdt_com_register = 3,
    hdt_com_unregister = 4,
    hdt_load_assembly_and_get_function_pointer = 5,
    hdt_get_function_pointer = 6,
};

using hostfxr_initialize_for_runtime_config_fn = int(VESPERA_HOSTFXR_CALLTYPE*)(const char_t*, const void*, hostfxr_handle*);
using hostfxr_get_runtime_delegate_fn = int(VESPERA_HOSTFXR_CALLTYPE*)(hostfxr_handle, hostfxr_delegate_type, void**);
using hostfxr_close_fn = int(VESPERA_HOSTFXR_CALLTYPE*)(hostfxr_handle);
using load_assembly_and_get_function_pointer_fn = int(VESPERA_LOAD_ASSEMBLY_CALLTYPE*)(
    const char_t*, const char_t*, const char_t*, const char_t*, void*, void**);

enum class ManagedDispatchCommand : int {
    Initialize = 1,
    LoadGameAssembly = 2,
    CreateScript = 3,
    StartAll = 4,
    UpdateAll = 5,
    Shutdown = 6,
    ApplyField = 7,
    ValidateCandidateScript = 8,
    CommitCandidateAssembly = 9,
    DiscardCandidateAssembly = 10,
    TriggerEnter = 11,
    TriggerExit = 12,
    StartScript = 13,
    DestroyScript = 14,
    TriggerStay = 15,
};

using managed_dispatch_fn = int(*)(int, const void*, const char*, std::uint64_t, float);

std::basic_string<char_t> host_absolute(const std::filesystem::path& path) {
#ifdef _WIN32
    return std::filesystem::absolute(path).lexically_normal().wstring();
#else
    return std::filesystem::absolute(path).lexically_normal().string();
#endif
}

std::string utf8_absolute(const std::filesystem::path& path) {
    const auto u8 = std::filesystem::absolute(path).lexically_normal().u8string();
    return {reinterpret_cast<const char*>(u8.data()), u8.size()};
}

std::tuple<int, int, int> parse_version_triplet(const std::filesystem::path& path) {
    int a = 0, b = 0, c = 0;
    const std::string text = path.filename().string();
    std::sscanf(text.c_str(), "%d.%d.%d", &a, &b, &c);
    return {a, b, c};
}

void append_dotnet_root(std::vector<std::filesystem::path>& roots, const char* env_name) {
    if (const char* value = std::getenv(env_name); value && *value) roots.emplace_back(value);
}

std::filesystem::path current_executable_directory() {
#ifdef _WIN32
    std::array<wchar_t, 32768> module_path{};
    const DWORD module_length = GetModuleFileNameW(nullptr, module_path.data(), static_cast<DWORD>(module_path.size()));
    if (module_length > 0 && module_length < module_path.size()) {
        return std::filesystem::path(module_path.data()).parent_path();
    }
#else
    std::error_code ec;
    const auto executable = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec && !executable.empty()) return executable.parent_path();
#endif
    return {};
}

std::filesystem::path find_hostfxr() {
    std::vector<std::filesystem::path> roots;
    const auto executable_directory = current_executable_directory();
    if (!executable_directory.empty()) roots.emplace_back(executable_directory / "dotnet");
    append_dotnet_root(roots, "DOTNET_ROOT");
    append_dotnet_root(roots, "DOTNET_ROOT_X64");
#ifdef _WIN32
    append_dotnet_root(roots, "ProgramW6432");
    append_dotnet_root(roots, "ProgramFiles");
#else
    roots.emplace_back("/usr/share/dotnet");
    roots.emplace_back("/usr/local/share/dotnet");
#endif

    std::vector<std::filesystem::path> candidates;
    for (auto root : roots) {
#ifdef _WIN32
        if (root.filename() != L"dotnet") root /= L"dotnet";
        constexpr const wchar_t* kHostFxrLibrary = L"hostfxr.dll";
#else
        if (root.filename() != "dotnet" && std::filesystem::is_directory(root / "dotnet")) root /= "dotnet";
        constexpr const char* kHostFxrLibrary = "libhostfxr.so";
#endif
        const auto fxr_root = root / "host" / "fxr";
        std::error_code ec;
        if (!std::filesystem::is_directory(fxr_root, ec)) continue;
        for (const auto& entry : std::filesystem::directory_iterator(fxr_root, ec)) {
            if (!entry.is_directory()) continue;
            const auto candidate = entry.path() / kHostFxrLibrary;
            if (std::filesystem::exists(candidate, ec)) candidates.push_back(candidate);
        }
    }
    if (candidates.empty()) return {};
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        return parse_version_triplet(a.parent_path()) < parse_version_triplet(b.parent_path());
    });
    return candidates.back();
}

host_library_t load_host_library(const std::filesystem::path& path) {
#ifdef _WIN32
    return LoadLibraryW(path.c_str());
#else
    return dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
#endif
}

void* host_symbol(host_library_t library, const char* name) {
    if (!library) return nullptr;
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(library, name));
#else
    return dlsym(library, name);
#endif
}

#ifdef _WIN32
constexpr const wchar_t* kManagedEntryPointType = L"Vespera.Managed.EntryPoint, Vespera.NET";
constexpr const wchar_t* kManagedDispatchMethod = L"Dispatch";
#else
constexpr const char* kManagedEntryPointType = "Vespera.Managed.EntryPoint, Vespera.NET";
constexpr const char* kManagedDispatchMethod = "Dispatch";
#endif

#endif

} // namespace

struct ManagedScriptHost::Impl {
    struct LiveScriptAttachment {
        std::uint64_t entity_id = 0;
        std::uint32_t slot = 0;
        std::string class_name;

        bool operator==(const LiveScriptAttachment& other) const {
            return entity_id == other.entity_id && slot == other.slot && class_name == other.class_name;
        }
    };

    ManagedScriptHostStatus status;
    Scene* scene = nullptr;
    InputSystem* input = nullptr;
    AudioSystem* audio = nullptr;
    AssetCatalog* assets = nullptr;
    UiSurface* ui_surface = nullptr;
    ManagedBridgeContext bridge{};
    ManagedScriptHostConfig config;
    std::vector<LiveScriptAttachment> live_scripts;

#ifdef VESPERA_MANAGED_HOSTFXR
    host_library_t hostfxr_library = nullptr;
    managed_dispatch_fn managed_dispatch = nullptr;
    GameAssemblySignature observed_game_assembly{};
    GameAssemblySignature pending_game_assembly{};
    double auto_reload_poll_accumulator = 0.0;
    double auto_reload_stable_seconds = 0.0;
    bool auto_reload_pending = false;

    void commit_pending_entity_destroys(bool dispatch_managed_cleanup) {
        if (!scene || bridge.pending_destroy_ids.empty()) return;
        auto pending = std::move(bridge.pending_destroy_ids);
        bridge.pending_destroy_ids.clear();
        for (const std::uint64_t id : pending) {
            if (!scene->find_entity(id)) continue;
            if (dispatch_managed_cleanup && managed_dispatch) {
                for (const auto& live : live_scripts) {
                    if (live.entity_id != id) continue;
                    NativeScriptSlot native_slot{live.slot};
                    managed_dispatch(static_cast<int>(ManagedDispatchCommand::DestroyScript), &native_slot,
                        live.class_name.c_str(), live.entity_id, 0.0f);
                }
            }
            std::erase_if(live_scripts, [id](const LiveScriptAttachment& live) {
                return live.entity_id == id;
            });
            if (scene->destroy_entity(id)) {
                log::info(std::format("[C#] Deferred Entity.Destroy committed for entity {}", id));
            }
        }
        status.script_count = live_scripts.size();
    }
#endif
};

ManagedScriptHost::ManagedScriptHost() : impl_(std::make_unique<Impl>()) {}
ManagedScriptHost::~ManagedScriptHost() { shutdown(); }

bool ManagedScriptHost::initialize(Scene& scene, InputSystem& input, AudioSystem& audio, AssetCatalog& assets,
                                   const ManagedScriptHostConfig& config, UiSurface* ui_surface,
                                   RuntimePerformanceCounters* performance) {
    shutdown();
    impl_->scene = &scene;
    impl_->input = &input;
    impl_->audio = &audio;
    impl_->assets = &assets;
    impl_->ui_surface = ui_surface;
    impl_->bridge.scene = &scene;
    impl_->bridge.input = &input;
    impl_->bridge.audio = &audio;
    impl_->bridge.assets = &assets;
    impl_->bridge.performance = performance;
    impl_->bridge.ui_surface = ui_surface;
    impl_->bridge.ui_handles.bind(ui_surface);
    impl_->bridge.requested_scene_path.clear();
    impl_->bridge.requested_scene_force_reload = false;
    impl_->bridge.pending_destroy_ids.clear();
    impl_->bridge.scratch_asset_text.clear();
    impl_->config = config;

#ifndef VESPERA_MANAGED_HOSTFXR
    (void)config;
    impl_->status.message = "managed scripting host is unavailable on this platform";
    return false;
#else
    if (!std::filesystem::exists(config.runtime_config)
        || !std::filesystem::exists(config.bridge_assembly)
        || !std::filesystem::exists(config.game_assembly)) {
        impl_->status.message = "managed assemblies/runtime config are not built";
        return false;
    }

    const auto hostfxr_path = find_hostfxr();
    if (hostfxr_path.empty()) {
        impl_->status.message = ".NET hostfxr was not found; use a portable export or install a .NET 8+ x64 runtime/SDK";
        return false;
    }
    impl_->status.available = true;
    if (!impl_->hostfxr_library) {
        impl_->hostfxr_library = load_host_library(hostfxr_path);
    }
    if (!impl_->hostfxr_library) {
        impl_->status.message = "failed to load hostfxr";
        return false;
    }

    const auto init_fxr = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(
        host_symbol(impl_->hostfxr_library, "hostfxr_initialize_for_runtime_config"));
    const auto get_delegate = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(
        host_symbol(impl_->hostfxr_library, "hostfxr_get_runtime_delegate"));
    const auto close_fxr = reinterpret_cast<hostfxr_close_fn>(
        host_symbol(impl_->hostfxr_library, "hostfxr_close"));
    if (!init_fxr || !get_delegate || !close_fxr) {
        impl_->status.message = "hostfxr exports are incomplete";
        shutdown();
        return false;
    }

    hostfxr_handle context = nullptr;
    const auto runtime_config = host_absolute(config.runtime_config);
    int rc = init_fxr(runtime_config.c_str(), nullptr, &context);
    if (rc < 0 || !context) {
        impl_->status.message = std::format("hostfxr runtime initialization failed (0x{:08X})", static_cast<unsigned>(rc));
        shutdown();
        return false;
    }

    void* load_assembly_ptr = nullptr;
    rc = get_delegate(context, hdt_load_assembly_and_get_function_pointer, &load_assembly_ptr);
    close_fxr(context);
    const auto load_assembly = reinterpret_cast<load_assembly_and_get_function_pointer_fn>(load_assembly_ptr);
    if (rc < 0 || !load_assembly) {
        impl_->status.message = std::format("hostfxr could not provide managed assembly loader (0x{:08X})", static_cast<unsigned>(rc));
        shutdown();
        return false;
    }

    const auto bridge = host_absolute(config.bridge_assembly);
    const auto unmanaged_callers_only = reinterpret_cast<const char_t*>(static_cast<std::intptr_t>(-1));

    void* dispatch_ptr = nullptr;
    rc = load_assembly(bridge.c_str(), kManagedEntryPointType, kManagedDispatchMethod,
        unmanaged_callers_only, nullptr, &dispatch_ptr);
    impl_->managed_dispatch = reinterpret_cast<managed_dispatch_fn>(dispatch_ptr);
    if (rc < 0 || !impl_->managed_dispatch) {
        impl_->status.message = std::format("could not bind Vespera.NET dispatcher (HRESULT 0x{:08X})", static_cast<unsigned>(rc));
        shutdown();
        return false;
    }

    NativeApi api;
    api.struct_size = static_cast<std::uint32_t>(sizeof(NativeApi));
    api.user_data = &impl_->bridge;
    api.log = managed_log;
    api.entity_exists = managed_entity_exists;
    api.get_transform = managed_get_transform;
    api.set_transform = managed_set_transform;
    api.get_enabled = managed_get_enabled;
    api.set_enabled = managed_set_enabled;
    api.action_value = managed_action_value;
    api.action_down = managed_action_down;
    api.action_pressed = managed_action_pressed;
    api.action_released = managed_action_released;
    api.find_entity_by_name = managed_find_entity_by_name;
    api.find_entity_with_tag = managed_find_entity_with_tag;
    api.query_entities = managed_query_entities;
    api.create_entity = managed_create_entity;
    api.destroy_entity = managed_destroy_entity;
    api.clone_entity = managed_clone_entity;
    api.has_component = managed_has_component;
    api.add_component = managed_add_component;
    api.remove_component = managed_remove_component;
    api.get_entity_name = managed_get_entity_name;
    api.set_entity_name = managed_set_entity_name;
    api.get_entity_tag = managed_get_entity_tag;
    api.set_entity_tag = managed_set_entity_tag;
    api.get_entity_layer = managed_get_entity_layer;
    api.set_entity_layer = managed_set_entity_layer;
    api.instantiate_prefab = managed_instantiate_prefab;
    api.raycast_2d = managed_raycast_2d;
    api.overlap_circle_2d = managed_overlap_circle_2d;
    api.resolve_circle_motion_2d = managed_resolve_circle_motion_2d;
    api.audio_play_one_shot = managed_audio_play_one_shot;
    api.audio_play_voice = managed_audio_play_voice;
    api.audio_stop_voice = managed_audio_stop_voice;
    api.audio_set_voice_volume = managed_audio_set_voice_volume;
    api.audio_set_voice_loop = managed_audio_set_voice_loop;
    api.audio_set_voice_paused = managed_audio_set_voice_paused;
    api.audio_set_voice_position = managed_audio_set_voice_position;
    api.audio_voice_playing = managed_audio_voice_playing;
    api.audio_set_listener_position = managed_audio_set_listener_position;
    api.audio_get_listener_position = managed_audio_get_listener_position;
    api.audio_use_camera_listener = managed_audio_use_camera_listener;
    api.audio_set_master_volume = managed_audio_set_master_volume;
    api.audio_get_master_volume = managed_audio_get_master_volume;
    api.audio_stop_all = managed_audio_stop_all;
    api.audio_active_voice_count = managed_audio_active_voice_count;
    api.request_scene_load = managed_request_scene_load;
    api.get_current_scene_path = managed_get_current_scene_path;
    api.resolve_asset_path = managed_resolve_asset_path;
    api.find_asset_id = managed_find_asset_id;
    api.has_managed_script = managed_has_managed_script;
    api.add_managed_script = managed_add_managed_script;
    api.remove_managed_script = managed_remove_managed_script;
    api.get_property_float = managed_get_property_float;
    api.set_property_float = managed_set_property_float;
    api.get_property_bool = managed_get_property_bool;
    api.set_property_bool = managed_set_property_bool;
    api.get_property_text = managed_get_property_text;
    api.set_property_text = managed_set_property_text;
    api.get_property_vec2 = managed_get_property_vec2;
    api.set_property_vec2 = managed_set_property_vec2;
    api.get_property_vec3 = managed_get_property_vec3;
    api.set_property_vec3 = managed_set_property_vec3;
    api.get_property_color = managed_get_property_color;
    api.set_property_color = managed_set_property_color;
    api.ui_find_node = managed_ui_find_node;
    api.ui_node_exists = managed_ui_node_exists;
    api.ui_get_node_text = managed_ui_get_node_text;
    api.ui_set_node_text = managed_ui_set_node_text;
    api.ui_get_node_enabled = managed_ui_get_node_enabled;
    api.ui_set_node_enabled = managed_ui_set_node_enabled;
    api.ui_get_node_bool = managed_ui_get_node_bool;
    api.ui_set_node_bool = managed_ui_set_node_bool;
    api.ui_get_node_float = managed_ui_get_node_float;
    api.ui_set_node_float = managed_ui_set_node_float;
    api.ui_set_node_color = managed_ui_set_node_color;
    api.ui_set_node_asset = managed_ui_set_node_asset;
    api.ui_consume_click = managed_ui_consume_click;
    api.ui_get_value = managed_ui_get_value;
    api.ui_set_value = managed_ui_set_value;
    api.ui_set_property = managed_ui_set_property;
    api.ui_set_class = managed_ui_set_class;
    api.trace_runtime_event = managed_runtime_event;
    api.get_camera = managed_get_camera;
    api.set_camera = managed_set_camera;
    api.set_transforms = managed_set_transforms;
    api.get_performance = managed_get_performance;
    if (impl_->managed_dispatch(static_cast<int>(ManagedDispatchCommand::Initialize), &api, nullptr, 0, 0.0f) == 0) {
        impl_->status.message = "Vespera.NET rejected the native ABI";
        shutdown();
        return false;
    }

    const std::string game_assembly = utf8_absolute(config.game_assembly);
    if (impl_->managed_dispatch(static_cast<int>(ManagedDispatchCommand::LoadGameAssembly), nullptr,
            game_assembly.c_str(), 0, 0.0f) == 0) {
        impl_->status.message = "Vespera.NET could not prepare the game script assembly";
        shutdown();
        return false;
    }

    bool candidate_valid = true;
    for (const auto& entity : scene.entities) {
        for (const auto& script : entity.managed_scripts) {
            if (!script.enabled || script.class_name.empty()) continue;
            if (impl_->managed_dispatch(static_cast<int>(ManagedDispatchCommand::ValidateCandidateScript), nullptr,
                    script.class_name.c_str(), entity.id, 0.0f) == 0) {
                candidate_valid = false;
            }
        }
    }
    if (!candidate_valid) {
        impl_->managed_dispatch(static_cast<int>(ManagedDispatchCommand::DiscardCandidateAssembly), nullptr, nullptr, 0, 0.0f);
        impl_->status.message = "game script assembly is incompatible with one or more enabled scene attachments";
        shutdown();
        return false;
    }
    if (impl_->managed_dispatch(static_cast<int>(ManagedDispatchCommand::CommitCandidateAssembly), nullptr, nullptr, 0, 0.0f) == 0) {
        impl_->managed_dispatch(static_cast<int>(ManagedDispatchCommand::DiscardCandidateAssembly), nullptr, nullptr, 0, 0.0f);
        impl_->status.message = "Vespera.NET could not commit the prepared game script assembly";
        shutdown();
        return false;
    }
    ++impl_->bridge.assembly_generation;

    std::size_t created = 0;
    impl_->live_scripts.clear();
    for (const auto& entity : scene.entities) {
        for (std::size_t slot = 0; slot < entity.managed_scripts.size(); ++slot) {
            const auto& script = entity.managed_scripts[slot];
            if (!script.enabled || script.class_name.empty()) continue;
            NativeScriptSlot native_slot{static_cast<std::uint32_t>(slot)};

            const int script_handle = impl_->managed_dispatch(
                static_cast<int>(ManagedDispatchCommand::CreateScript), &native_slot,
                script.class_name.c_str(), entity.id, 0.0f);
            impl_->live_scripts.push_back({entity.id, native_slot.slot, script.class_name});
            if (script_handle <= 0) continue;

            ++created;
            for (const auto& field : script.fields) {
                const int applied = impl_->managed_dispatch(
                    static_cast<int>(ManagedDispatchCommand::ApplyField),
                    field.field_name.c_str(), field.serialized_value.c_str(),
                    static_cast<std::uint64_t>(script_handle), 0.0f);
                if (applied == 0) {
                    log::warn(std::format(
                        "[C#] Could not apply exposed field '{}' on {} (entity {})",
                        field.field_name, script.class_name, entity.id));
                }
            }
        }
    }

    impl_->status.initialized = true;
    impl_->status.script_count = created;
    read_game_assembly_signature(config.game_assembly, impl_->observed_game_assembly);
    impl_->pending_game_assembly = {};
    impl_->auto_reload_poll_accumulator = 0.0;
    impl_->auto_reload_stable_seconds = 0.0;
    impl_->auto_reload_pending = false;
    impl_->status.message = std::format(".NET runtime online; {} C# component{} created{}",
        created, created == 1 ? "" : "s", config.auto_reload ? "; automatic reload watching enabled" : "");
    return true;
#endif
}

void ManagedScriptHost::start() {
#ifdef VESPERA_MANAGED_HOSTFXR
    if (impl_->status.initialized && impl_->managed_dispatch) {
        impl_->managed_dispatch(static_cast<int>(ManagedDispatchCommand::StartAll), nullptr, nullptr, 0, 0.0f);
    }
#endif
}

void ManagedScriptHost::update(double delta_seconds) {
#ifdef VESPERA_MANAGED_HOSTFXR
    if (impl_->status.initialized && impl_->managed_dispatch) {
        if (impl_->config.auto_reload && !impl_->config.game_assembly.empty()) {
            const double poll_seconds = (std::max)(0.05, impl_->config.auto_reload_poll_seconds);
            const double debounce_seconds = (std::max)(0.0, impl_->config.auto_reload_debounce_seconds);
            impl_->auto_reload_poll_accumulator += (std::max)(0.0, delta_seconds);
            if (impl_->auto_reload_poll_accumulator >= poll_seconds) {
                const double elapsed = impl_->auto_reload_poll_accumulator;
                impl_->auto_reload_poll_accumulator = 0.0;
                GameAssemblySignature current;
                if (read_game_assembly_signature(impl_->config.game_assembly, current)) {
                    if (!impl_->observed_game_assembly.valid) {
                        impl_->observed_game_assembly = current;
                    } else if (!same_signature(current, impl_->observed_game_assembly)) {
                        if (!impl_->auto_reload_pending || !same_signature(current, impl_->pending_game_assembly)) {
                            impl_->pending_game_assembly = current;
                            impl_->auto_reload_stable_seconds = 0.0;
                            impl_->auto_reload_pending = true;
                        } else {
                            impl_->auto_reload_stable_seconds += elapsed;
                        }

                        if (impl_->auto_reload_pending && impl_->auto_reload_stable_seconds >= debounce_seconds) {
                            log::info("[C#] Detected a stable managed game build; reloading scripts automatically...");
                            const auto attempted_signature = impl_->pending_game_assembly;
                            if (reload()) {
                                log::info("[C#] Automatic reload complete: " + impl_->status.message);
                            } else {
                                log::warn("[C#] Automatic reload rejected/failed; the active script runtime was preserved when possible: " + impl_->status.message);
                                // Do not retry the exact same failed file every few hundred ms.
                                // A manual Space reload or the next successful build can retry.
                                impl_->observed_game_assembly = attempted_signature;
                            }
                            impl_->pending_game_assembly = {};
                            impl_->auto_reload_stable_seconds = 0.0;
                            impl_->auto_reload_pending = false;
                        }
                    } else {
                        impl_->pending_game_assembly = {};
                        impl_->auto_reload_stable_seconds = 0.0;
                        impl_->auto_reload_pending = false;
                    }
                }
            }
        }

        // Keep live CLR script instances synchronized with runtime mutations to
        // Entity::managed_scripts. The common stable case compares authored
        // attachments directly with the live list in O(N) without allocating.
        // Only an actual mismatch builds hashed sets for reconciliation.
        bool attachments_match = true;
        std::size_t attachment_index = 0;
        for (const auto& entity : impl_->scene->entities) {
            for (std::size_t slot = 0; slot < entity.managed_scripts.size(); ++slot) {
                const auto& script = entity.managed_scripts[slot];
                if (!script.enabled || script.class_name.empty()) continue;
                if (attachment_index >= impl_->live_scripts.size()) {
                    attachments_match = false;
                    break;
                }
                const Impl::LiveScriptAttachment candidate{
                    entity.id, static_cast<std::uint32_t>(slot), script.class_name};
                if (!(impl_->live_scripts[attachment_index] == candidate)) {
                    attachments_match = false;
                    break;
                }
                ++attachment_index;
            }
            if (!attachments_match) break;
        }
        if (attachments_match && attachment_index != impl_->live_scripts.size()) attachments_match = false;

        if (!attachments_match) {
            std::vector<Impl::LiveScriptAttachment> current_scripts;
            current_scripts.reserve(impl_->live_scripts.size() + 8);
            for (const auto& entity : impl_->scene->entities) {
                for (std::size_t slot = 0; slot < entity.managed_scripts.size(); ++slot) {
                    const auto& script = entity.managed_scripts[slot];
                    if (!script.enabled || script.class_name.empty()) continue;
                    current_scripts.push_back({entity.id, static_cast<std::uint32_t>(slot), script.class_name});
                }
            }

            struct AttachmentHash {
                std::size_t operator()(const Impl::LiveScriptAttachment& value) const noexcept {
                    std::size_t hash = std::hash<std::uint64_t>{}(value.entity_id);
                    hash ^= std::hash<std::uint32_t>{}(value.slot) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
                    hash ^= std::hash<std::string>{}(value.class_name) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
                    return hash;
                }
            };
            const std::unordered_set<Impl::LiveScriptAttachment, AttachmentHash> current_set(
                current_scripts.begin(), current_scripts.end());
            const std::unordered_set<Impl::LiveScriptAttachment, AttachmentHash> live_set(
                impl_->live_scripts.begin(), impl_->live_scripts.end());

            for (const auto& live : impl_->live_scripts) {
                if (current_set.contains(live)) continue;
                NativeScriptSlot native_slot{live.slot};
                impl_->managed_dispatch(static_cast<int>(ManagedDispatchCommand::DestroyScript), &native_slot,
                    live.class_name.c_str(), live.entity_id, 0.0f);
            }

            for (const auto& current : current_scripts) {
                if (live_set.contains(current)) continue;
                const Entity* entity = impl_->scene->find_entity(current.entity_id);
                if (!entity || current.slot >= entity->managed_scripts.size()) continue;
                const auto& script = entity->managed_scripts[current.slot];
                NativeScriptSlot native_slot{current.slot};
                const int script_handle = impl_->managed_dispatch(
                    static_cast<int>(ManagedDispatchCommand::CreateScript), &native_slot,
                    script.class_name.c_str(), current.entity_id, 0.0f);
                if (script_handle <= 0) continue;
                for (const auto& field : script.fields) {
                    impl_->managed_dispatch(static_cast<int>(ManagedDispatchCommand::ApplyField),
                        field.field_name.c_str(), field.serialized_value.c_str(),
                        static_cast<std::uint64_t>(script_handle), 0.0f);
                }
                impl_->managed_dispatch(static_cast<int>(ManagedDispatchCommand::StartScript), nullptr,
                    nullptr, static_cast<std::uint64_t>(script_handle), 0.0f);
                log::info(std::format("[C#] Runtime-attached {} on entity {} (slot {})",
                    script.class_name, current.entity_id, current.slot));
            }
            impl_->live_scripts = std::move(current_scripts);
            impl_->status.script_count = impl_->live_scripts.size();
        }

        ++impl_->bridge.runtime_frame;
        impl_->bridge.camera_written_last_update = false;
        impl_->managed_dispatch(static_cast<int>(ManagedDispatchCommand::UpdateAll), nullptr, nullptr, 0,
            static_cast<float>((std::min)(delta_seconds, 0.1)));

        // Commit Entity.Destroy() requests only after all managed Update calls have
        // returned. Destroy the CLR attachments first while the native Entity still
        // exists, so OnDisable/OnDestroy can inspect its final state safely.
        impl_->commit_pending_entity_destroys(true);
    }
#else
    (void)delta_seconds;
#endif
}

bool ManagedScriptHost::camera_was_written_last_update() const {
    return impl_ && impl_->bridge.camera_written_last_update;
}

bool ManagedScriptHost::reload() {
#ifdef VESPERA_MANAGED_HOSTFXR
    if (!impl_ || !impl_->status.initialized || !impl_->scene || !impl_->managed_dispatch
        || impl_->config.game_assembly.empty()) {
        if (impl_) impl_->status.message = "managed reload requested before the scripting host was initialized";
        return false;
    }

    // Keep CoreCLR/Vespera.NET alive and replace only the collectible game-code
    // context. 0.5.5 performs this as a two-phase transaction: prepare the new
    // assembly, verify every enabled scene attachment exists in it, and only then
    // allow the managed host to call OnDestroy() and commit the replacement.
    const std::string game_assembly = utf8_absolute(impl_->config.game_assembly);
    if (impl_->managed_dispatch(static_cast<int>(ManagedDispatchCommand::LoadGameAssembly), nullptr,
            game_assembly.c_str(), 0, 0.0f) == 0) {
        impl_->status.message = "C# reload failed while preparing the replacement game assembly; active scripts remain loaded";
        return false;
    }

    bool candidate_valid = true;
    for (const auto& entity : impl_->scene->entities) {
        for (const auto& script : entity.managed_scripts) {
            if (!script.enabled || script.class_name.empty()) continue;
            if (impl_->managed_dispatch(static_cast<int>(ManagedDispatchCommand::ValidateCandidateScript), nullptr,
                    script.class_name.c_str(), entity.id, 0.0f) == 0) {
                candidate_valid = false;
            }
        }
    }
    if (!candidate_valid) {
        impl_->managed_dispatch(static_cast<int>(ManagedDispatchCommand::DiscardCandidateAssembly), nullptr, nullptr, 0, 0.0f);
        impl_->status.message = "C# reload rejected because the replacement is missing/invalid for one or more enabled scene attachments; active scripts were preserved";
        return false;
    }
    if (impl_->managed_dispatch(static_cast<int>(ManagedDispatchCommand::CommitCandidateAssembly), nullptr, nullptr, 0, 0.0f) == 0) {
        impl_->managed_dispatch(static_cast<int>(ManagedDispatchCommand::DiscardCandidateAssembly), nullptr, nullptr, 0, 0.0f);
        impl_->status.message = "C# reload failed while committing the prepared replacement; active scripts were preserved when possible";
        return false;
    }
    ++impl_->bridge.assembly_generation;

    // Old script OnDestroy callbacks may have requested Entity.Destroy(). Commit
    // those requests before creating replacement script instances so a just-reloaded
    // script never binds to an entity already scheduled for removal.
    impl_->commit_pending_entity_destroys(false);

    std::size_t created = 0;
    impl_->live_scripts.clear();
    for (const auto& entity : impl_->scene->entities) {
        for (std::size_t slot = 0; slot < entity.managed_scripts.size(); ++slot) {
            const auto& script = entity.managed_scripts[slot];
            if (!script.enabled || script.class_name.empty()) continue;
            NativeScriptSlot native_slot{static_cast<std::uint32_t>(slot)};

            const int script_handle = impl_->managed_dispatch(
                static_cast<int>(ManagedDispatchCommand::CreateScript), &native_slot,
                script.class_name.c_str(), entity.id, 0.0f);
            impl_->live_scripts.push_back({entity.id, native_slot.slot, script.class_name});
            if (script_handle <= 0) continue;

            ++created;
            for (const auto& field : script.fields) {
                const int applied = impl_->managed_dispatch(
                    static_cast<int>(ManagedDispatchCommand::ApplyField),
                    field.field_name.c_str(), field.serialized_value.c_str(),
                    static_cast<std::uint64_t>(script_handle), 0.0f);
                if (applied == 0) {
                    log::warn(std::format(
                        "[C#] Could not reapply exposed field '{}' on {} (entity {})",
                        field.field_name, script.class_name, entity.id));
                }
            }
        }
    }

    impl_->status.script_count = created;
    impl_->managed_dispatch(static_cast<int>(ManagedDispatchCommand::StartAll), nullptr, nullptr, 0, 0.0f);
    read_game_assembly_signature(impl_->config.game_assembly, impl_->observed_game_assembly);
    impl_->pending_game_assembly = {};
    impl_->auto_reload_stable_seconds = 0.0;
    impl_->auto_reload_pending = false;
    impl_->status.message = std::format(".NET scripts reloaded in-place; {} C# component{} created",
        created, created == 1 ? "" : "s");
    return true;
#else
    return false;
#endif
}

void ManagedScriptHost::trigger_enter(std::uint64_t trigger_entity, std::uint64_t other_entity) {
#ifdef VESPERA_MANAGED_HOSTFXR
    if (impl_ && impl_->status.initialized && impl_->managed_dispatch) {
        impl_->managed_dispatch(static_cast<int>(ManagedDispatchCommand::TriggerEnter),
            reinterpret_cast<const void*>(static_cast<std::uintptr_t>(other_entity)), nullptr,
            trigger_entity, 0.0f);
        // Trigger callbacks execute outside UpdateAll in the reference runtime.
        // Once dispatch returns to native code it is safe to honor any Destroy()
        // requests they queued without allowing one extra managed Update frame.
        impl_->commit_pending_entity_destroys(true);
    }
#else
    (void)trigger_entity;
    (void)other_entity;
#endif
}

void ManagedScriptHost::trigger_exit(std::uint64_t trigger_entity, std::uint64_t other_entity) {
#ifdef VESPERA_MANAGED_HOSTFXR
    if (impl_ && impl_->status.initialized && impl_->managed_dispatch) {
        impl_->managed_dispatch(static_cast<int>(ManagedDispatchCommand::TriggerExit),
            reinterpret_cast<const void*>(static_cast<std::uintptr_t>(other_entity)), nullptr,
            trigger_entity, 0.0f);
        // Trigger callbacks execute outside UpdateAll in the reference runtime.
        // Once dispatch returns to native code it is safe to honor any Destroy()
        // requests they queued without allowing one extra managed Update frame.
        impl_->commit_pending_entity_destroys(true);
    }
#else
    (void)trigger_entity;
    (void)other_entity;
#endif
}

void ManagedScriptHost::trigger_stay(std::uint64_t trigger_entity, std::uint64_t other_entity) {
#ifdef VESPERA_MANAGED_HOSTFXR
    if (impl_ && impl_->status.initialized && impl_->managed_dispatch) {
        impl_->managed_dispatch(static_cast<int>(ManagedDispatchCommand::TriggerStay),
            reinterpret_cast<const void*>(static_cast<std::uintptr_t>(other_entity)), nullptr,
            trigger_entity, 0.0f);
        // Trigger callbacks execute outside UpdateAll in the reference runtime.
        // Once dispatch returns to native code it is safe to honor any Destroy()
        // requests they queued without allowing one extra managed Update frame.
        impl_->commit_pending_entity_destroys(true);
    }
#else
    (void)trigger_entity;
    (void)other_entity;
#endif
}

std::optional<ManagedSceneLoadRequest> ManagedScriptHost::take_scene_load_request() {
    if (!impl_ || impl_->bridge.requested_scene_path.empty()) return std::nullopt;
    ManagedSceneLoadRequest request;
    request.path = impl_->bridge.requested_scene_path;
    request.force_reload = impl_->bridge.requested_scene_force_reload;
    impl_->bridge.requested_scene_path.clear();
    impl_->bridge.requested_scene_force_reload = false;
    return request;
}

void ManagedScriptHost::set_current_scene_path(const std::filesystem::path& path) {
    if (!impl_) return;
    impl_->bridge.current_scene_path = path.generic_string();
}

std::vector<ManagedRuntimeEvent> ManagedScriptHost::runtime_events_since(std::uint64_t after_sequence) const {
    std::vector<ManagedRuntimeEvent> out;
    if (!impl_) return out;
    for (const auto& event : impl_->bridge.runtime_events) {
        if (event.sequence > after_sequence) out.push_back(event);
    }
    return out;
}

std::uint64_t ManagedScriptHost::latest_runtime_event_sequence() const {
    return impl_ ? impl_->bridge.runtime_event_sequence : 0;
}

std::uint32_t ManagedScriptHost::assembly_generation() const {
    return impl_ ? impl_->bridge.assembly_generation : 0;
}

void ManagedScriptHost::clear_runtime_events() {
    if (!impl_) return;
    impl_->bridge.runtime_events.clear();
}

void ManagedScriptHost::shutdown() {
#ifdef VESPERA_MANAGED_HOSTFXR
    if (impl_ && impl_->managed_dispatch) {
        // Safe even during partial initialization; this clears any bridge state
        // that may have been established before a later host step failed.
        impl_->managed_dispatch(static_cast<int>(ManagedDispatchCommand::Shutdown), nullptr, nullptr, 0, 0.0f);
        impl_->commit_pending_entity_destroys(false);
    }
    // hostfxr/CoreCLR native libraries are process-lifetime once loaded. The
    // official hosting samples explicitly warn not to FreeLibrary them.
    if (impl_) {
        impl_->managed_dispatch = nullptr;
    }
#endif
    if (impl_) {
        const bool was_available = impl_->status.available;
        const std::string last_message = impl_->status.message;
        impl_->status = {};
        impl_->status.available = was_available;
        impl_->status.message = last_message;
        impl_->scene = nullptr;
        impl_->input = nullptr;
        impl_->audio = nullptr;
        impl_->assets = nullptr;
        impl_->ui_surface = nullptr;
        impl_->bridge.ui_surface = nullptr;
        impl_->bridge.ui_handles.bind(nullptr);
        // Runtime QA telemetry intentionally survives managed shutdown/reinitialize
        // boundaries so scene switches, hot reload and final teardown can be
        // inspected as one ordered lifecycle stream. All bridge-owned pointers and
        // transient requests are still cleared.
        auto runtime_events = std::move(impl_->bridge.runtime_events);
        const auto runtime_event_sequence = impl_->bridge.runtime_event_sequence;
        const auto runtime_frame = impl_->bridge.runtime_frame;
        const auto assembly_generation = impl_->bridge.assembly_generation;
        impl_->bridge = {};
        impl_->bridge.runtime_events = std::move(runtime_events);
        impl_->bridge.runtime_event_sequence = runtime_event_sequence;
        impl_->bridge.runtime_frame = runtime_frame;
        impl_->bridge.assembly_generation = assembly_generation;
        impl_->live_scripts.clear();
#ifdef VESPERA_MANAGED_HOSTFXR
        impl_->observed_game_assembly = {};
        impl_->pending_game_assembly = {};
        impl_->auto_reload_poll_accumulator = 0.0;
        impl_->auto_reload_stable_seconds = 0.0;
        impl_->auto_reload_pending = false;
#endif
    }
}

const ManagedScriptHostStatus& ManagedScriptHost::status() const {
    return impl_->status;
}

} // namespace vespera
