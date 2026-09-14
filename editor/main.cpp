#include <vespera/core/version.hpp>
#include <vespera/core/game.hpp>
#include <vespera/assets/asset_catalog.hpp>
#include <vespera/assets/material_asset.hpp>
#include <vespera/assets/asset_authoring.hpp>
#include <vespera/assets/audio_clip_asset.hpp>
#include <vespera/assets/build_manifest.hpp>
#include <vespera/assets/project_package.hpp>
#include <vespera/assets/font_asset.hpp>
#include <vespera/assets/texture_importer.hpp>
#include <vespera/assets/sprite_clip_asset.hpp>
#include <vespera/assets/sprite_sheet_asset.hpp>
#include <vespera/render/texture_data.hpp>
#include <vespera/project/project.hpp>
#include <vespera/runtime/player_project.hpp>
#include <vespera/render/render_backend.hpp>
#if defined(_WIN32)
#include <vespera/render/d3d12/d3d12_native.hpp>
#endif
#include <vespera/scene/scene.hpp>
#include <vespera/scene/scene_io.hpp>
#include <vespera/scene/scene_hierarchy.hpp>
#include <vespera/scene/component_access.hpp>
#include <vespera/scene/prefab.hpp>
#include <vespera/scene/scene_validation.hpp>
#include <vespera/scene/scene_stats.hpp>
#include <vespera/world/sector_world.hpp>
#include <vespera/ui/ui_io.hpp>
#include <vespera/ui/ui_render.hpp>
#include <vespera/ui/rmlui_surface.hpp>
#include "editor_command.hpp"
#include "extension_api.hpp"
#include "automation_server.hpp"
#include "automation_tools.hpp"
#include "play_runtime.hpp"
#include "rml_source_editor.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl3.h>
#if defined(_WIN32)
#include <imgui_impl_dx12.h>
#include <wrl/client.h>
#else
#include <imgui_impl_sdlrenderer3.h>
#endif
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cfloat>
#include <cstdlib>
#include <chrono>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <future>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <thread>
#include <utility>
#include <unordered_map>
#include <vector>

namespace {

constexpr float kRadiansToDegrees = 57.29577951308232f;
constexpr float kDegreesToRadians = 0.017453292519943295f;

#if defined(_WIN32)
void editor_startup_trace(std::string_view message, bool reset = false) {
    std::ofstream stream(
        "vespera_editor_startup.log",
        reset ? std::ios::trunc : std::ios::app
    );
    if (stream) {
        stream << message << '\n';
        stream.flush();
    }
}
#endif

#if defined(_WIN32)
struct EditorD3D12DescriptorAllocator {
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
    UINT descriptor_size = 0;
    std::vector<bool> used;

    bool reserve(D3D12_CPU_DESCRIPTOR_HANDLE& out_cpu, D3D12_GPU_DESCRIPTOR_HANDLE& out_gpu) {
        out_cpu = {};
        out_gpu = {};
        if (!heap || descriptor_size == 0) return false;
        for (std::size_t i = 0; i < used.size(); ++i) {
            if (used[i]) continue;
            used[i] = true;
            out_cpu = heap->GetCPUDescriptorHandleForHeapStart();
            out_gpu = heap->GetGPUDescriptorHandleForHeapStart();
            out_cpu.ptr += static_cast<SIZE_T>(i) * descriptor_size;
            out_gpu.ptr += static_cast<UINT64>(i) * descriptor_size;
            return true;
        }
        return false;
    }

    static void allocate(
        ImGui_ImplDX12_InitInfo* info,
        D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu,
        D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu
    ) {
        auto* allocator = static_cast<EditorD3D12DescriptorAllocator*>(info->UserData);
        if (!allocator || !allocator->heap || allocator->descriptor_size == 0) {
            *out_cpu = {};
            *out_gpu = {};
            return;
        }

        if (allocator->reserve(*out_cpu, *out_gpu)) return;

        // Dear ImGui treats a zero handle as an allocation failure. The editor
        // reserves enough descriptors for its own UI plus future thumbnails;
        // reaching this path means the tooling heap should be grown.
        *out_cpu = {};
        *out_gpu = {};
    }

    static void free(
        ImGui_ImplDX12_InitInfo* info,
        D3D12_CPU_DESCRIPTOR_HANDLE cpu,
        D3D12_GPU_DESCRIPTOR_HANDLE
    ) {
        auto* allocator = static_cast<EditorD3D12DescriptorAllocator*>(info->UserData);
        if (!allocator || !allocator->heap || allocator->descriptor_size == 0 || cpu.ptr == 0) return;
        const SIZE_T base = allocator->heap->GetCPUDescriptorHandleForHeapStart().ptr;
        if (cpu.ptr < base) return;
        const SIZE_T offset = cpu.ptr - base;
        if ((offset % allocator->descriptor_size) != 0) return;
        const std::size_t index = static_cast<std::size_t>(offset / allocator->descriptor_size);
        if (index < allocator->used.size()) allocator->used[index] = false;
    }
};

struct EditorScenePreviewTarget {
    Microsoft::WRL::ComPtr<ID3D12Resource> texture;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_descriptor{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_descriptor{};
    UINT width = 0;
    UINT height = 0;
    bool descriptor_reserved = false;

    bool reserve_descriptor(EditorD3D12DescriptorAllocator& allocator) {
        if (descriptor_reserved) return true;
        descriptor_reserved = allocator.reserve(cpu_descriptor, gpu_descriptor);
        return descriptor_reserved;
    }

    bool ensure(
        vespera::D3D12NativeAccess& native,
        EditorD3D12DescriptorAllocator& allocator,
        UINT requested_width,
        UINT requested_height
    ) {
        if (requested_width == 0 || requested_height == 0) return false;
        if (!reserve_descriptor(allocator)) return false;
        if (texture && width == requested_width && height == requested_height) return true;

        native.d3d12_wait_for_gpu();
        texture.Reset();
        width = 0;
        height = 0;

        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heap.CreationNodeMask = 1;
        heap.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = requested_width;
        desc.Height = requested_height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = native.d3d12_backbuffer_format();
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        const HRESULT hr = native.d3d12_device()->CreateCommittedResource(
            &heap,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            nullptr,
            IID_PPV_ARGS(&texture)
        );
        if (FAILED(hr)) return false;

        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Format = desc.Format;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MostDetailedMip = 0;
        srv.Texture2D.MipLevels = 1;
        srv.Texture2D.PlaneSlice = 0;
        srv.Texture2D.ResourceMinLODClamp = 0.0f;
        native.d3d12_device()->CreateShaderResourceView(texture.Get(), &srv, cpu_descriptor);

        width = requested_width;
        height = requested_height;
        return true;
    }

    [[nodiscard]] ImTextureID texture_id() const {
        return texture ? static_cast<ImTextureID>(gpu_descriptor.ptr) : ImTextureID_Invalid;
    }
};
#endif

enum class SelectionKind {
    None,
    Camera,
    Sector,
    Entity,
    Material,
    SpriteClip,
    Asset,
};

constexpr std::size_t kNoSubSelection = std::numeric_limits<std::size_t>::max();

struct Selection {
    SelectionKind kind = SelectionKind::None;
    std::size_t index = 0;
    // Sector vertex selection. Kept separate from side_index so the editor can
    // highlight/edit a wall/portal edge without confusing vertex drag state.
    std::size_t sub_index = kNoSubSelection;
    vespera::SceneObjectId object_id = vespera::kInvalidSceneObjectId;
    std::size_t side_index = kNoSubSelection;
    std::string asset_id;
};

struct ConsoleEntry {
    enum class Level { Info, Warning, Error } level = Level::Info;
    std::string text;
};

struct ManagedFieldMetadata {
    std::string name, type, display_name, clr_type, tooltip;
    bool has_range = false;
    float range_min = 0.0f;
    float range_max = 0.0f;
    std::vector<std::string> enum_values;
    std::vector<std::string> aliases;
};
struct ManagedScriptMetadata { std::string class_name; std::vector<ManagedFieldMetadata> fields; };
struct ManagedMetadataCatalog {
    std::vector<ManagedScriptMetadata> scripts;
    std::string assembly;
    std::filesystem::path path;
    bool loaded = false;
    const ManagedScriptMetadata* find(std::string_view name) const {
        for (const auto& script : scripts) if (script.class_name == name) return &script;
        return nullptr;
    }
};

enum class SceneDragKind {
    None,
    SectorVertex,
    Entity,
    Camera,
};

enum class SceneTool {
    View,
    Move,
    Rotate,
    Scale,
};

enum class GizmoAxis {
    None,
    X,
    Y,
    Z,
    Uniform,
};

struct SceneView3DState {
    vespera::Camera camera;
    ImVec2 content_min{};
    ImVec2 content_max{};
    bool visible = false;
    bool hovered = false;
    bool focused = false;
    bool initialized = false;
    bool frame_selection_pending = true;
    float move_speed = 4.0f;
    SceneTool tool = SceneTool::Move;
    bool local_space = false;
    bool snap_enabled = true;
    float translation_snap = 0.25f;
    float rotation_snap_degrees = 15.0f;
    float scale_snap = 0.10f;
    GizmoAxis active_axis = GizmoAxis::None;
    vespera::SceneObjectId drag_entity_id = vespera::kInvalidSceneObjectId;
    vespera::TransformComponent drag_start_transform{};
    std::vector<std::pair<vespera::SceneObjectId, vespera::TransformComponent>> drag_start_transforms;
    vespera::Vec3 drag_pivot{};
    bool center_pivot = true;
    ImVec2 drag_start_mouse{};
    ImVec2 drag_axis_screen_unit{};
    float drag_world_length = 1.0f;
    float drag_start_angle = 0.0f;
    bool gizmo_dragging = false;
    bool gizmo_drag_changed = false;
    ImTextureID preview_texture = ImTextureID_Invalid;
    ImVec2 preview_uv0{0.0f, 0.0f};
    ImVec2 preview_uv1{1.0f, 1.0f};
};

enum class EditorPlayState {
    Editing,
    Playing,
    Paused,
};

struct GameViewState {
    ImVec2 content_min{};
    ImVec2 content_max{};
    bool visible = false;
    bool hovered = false;
    bool focused = false;
    bool input_captured = false;
    bool focus_pending = false;
    ImTextureID preview_texture = ImTextureID_Invalid;
    ImVec2 preview_uv0{0.0f, 0.0f};
    ImVec2 preview_uv1{1.0f, 1.0f};
};

struct SceneViewState {
    float zoom = 64.0f;
    ImVec2 pan{0.0f, 0.0f};
    bool frame_all_pending = true;
    bool frame_selection_pending = false;
    bool snap_enabled = true;
    float snap_step = 0.25f;
    SceneDragKind drag_kind = SceneDragKind::None;
    std::size_t drag_index = 0;
    std::size_t drag_sub_index = kNoSubSelection;
    bool drag_changed = false;
};

struct HistorySnapshot {
    vespera::Scene scene;
    Selection selection;
    std::vector<vespera::SceneObjectId> selected_entity_ids;
    std::uint64_t state_id = 0;
    std::string label;
};

struct PlayEditBackup {
    vespera::Scene scene;
    Selection selection;
    std::vector<vespera::SceneObjectId> selected_entity_ids;
    vespera::SceneObjectId hierarchy_anchor_id = vespera::kInvalidSceneObjectId;
    std::vector<HistorySnapshot> undo_stack;
    std::vector<HistorySnapshot> redo_stack;
    std::uint64_t current_state_id = 1;
    std::uint64_t saved_state_id = 1;
    std::uint64_t next_state_id = 2;
    bool dirty = false;
};

struct TextureImportEditState {
    std::string asset_id;
    vespera::TextureImportSettings settings{};
};

struct MaterialAssetEditState {
    std::string asset_id;
    std::string source_hash;
    vespera::MaterialAsset material{};
    bool loaded = false;
    std::string message;
};

struct AssetInspectionCache {
    std::string asset_id;
    std::string source_hash;
    bool texture_attempted = false;
    bool texture_decoded = false;
    std::uint32_t texture_width = 0;
    std::uint32_t texture_height = 0;
    std::size_t decoded_bytes = 0;
    std::string message;
};

struct AssetThumbnail {
    std::string source_hash;
    bool attempted = false;
    bool valid = false;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<ImU32> pixels;
};

struct UiAuthoringState {
    bool open = false;
    std::string asset_id;
    std::filesystem::path path;
    vespera::UiDocument document;
    vespera::UiNodeId selected_node = vespera::kInvalidUiNodeId;
    int preview_width = 1280;
    int preview_height = 720;
    bool dirty = false;
    int preview_drag_mode = 0; // 0 none, 1 move, 2 resize
    std::string message;
};

enum class PendingAction {
    None,
    OpenScene,
    OpenProject,
    Exit,
};

struct EditorBuildJobState {
    bool window_open = false;
    int configuration_index = 1; // 0 Debug, 1 Development, 2 Release
    bool running = false;
    bool last_succeeded = false;
    bool last_launch_requested = false;
    std::future<int> future;
    std::filesystem::path log_path;
    std::uintmax_t log_bytes_consumed = 0;
    std::string log_partial;
    std::vector<std::string> log_tail;
    std::filesystem::path last_output_directory;
    std::string status = "Ready";
};

struct EditorState {
    vespera::Scene scene;
    Selection selection;
    std::vector<vespera::SceneObjectId> selected_entity_ids;
    vespera::SceneObjectId hierarchy_anchor_id = vespera::kInvalidSceneObjectId;
    SceneViewState scene_view;
    SceneView3DState scene_view_3d;
    GameViewState game_view;
    std::string game_view_text_input;
    bool game_view_backspace_pending = false;
    EditorPlayState play_state = EditorPlayState::Editing;
    vespera::Scene play_scene;
    std::unique_ptr<vespera::editor::PlayRuntime> play_runtime;
    std::vector<vespera::ManagedRuntimeEvent> last_runtime_events;
    std::uint64_t last_runtime_event_sequence = 0;
    std::uint32_t last_runtime_assembly_generation = 0;
    // Runtime UI backends during Play Mode. New projects prefer project startup RML;
    // legacy .slui remains a compatibility/QA fallback until migration is complete.
    vespera::UiDocument play_ui_document;
    vespera::UiRenderCache play_ui_cache;
    bool play_ui_loaded = false;
    std::unique_ptr<vespera::RmlUiSurface> play_rml_ui;
    bool play_rml_ui_loaded = false;
    int play_rml_view_width = 1280;
    int play_rml_view_height = 720;
    UiAuthoringState ui_authoring;
    vespera::editor::RmlSourceEditorState rml_source_editor;
    std::optional<PlayEditBackup> play_edit_backup;
    double play_time_seconds = 0.0;
    double play_last_wall_seconds = -1.0;
    std::filesystem::path scene_path;
    std::filesystem::path assets_root;
    std::filesystem::path project_path;
    vespera::VesperaProject project;
    bool project_loaded = false;
    vespera::AssetCatalog asset_catalog;
    vespera::AssetCatalogRefreshReport last_asset_report;
    AssetInspectionCache asset_inspection_cache;
    std::unordered_map<std::string, AssetThumbnail> asset_thumbnails;
    TextureImportEditState texture_import_edit;
    MaterialAssetEditState material_asset_edit;
    ImGuiTextFilter asset_filter;
    int asset_kind_filter = 0;
    bool asset_grid_view = true;
    float asset_tile_size = 92.0f;
    std::filesystem::path asset_browser_folder;
    std::string asset_move_id;
    std::string asset_move_path;
    bool request_asset_move_popup = false;
    bool asset_auto_refresh = true;
    double next_asset_refresh_time = 0.0;
    std::string open_path_text;
    std::string project_path_text;
    std::string save_path_text;
    std::vector<ConsoleEntry> console;
    bool console_show_info = true;
    bool console_show_warnings = true;
    bool console_show_errors = true;
    std::vector<vespera::editor::EditorCommandRecord> command_log;
    vespera::editor::EditorExtensionRegistry extension_registry;
    std::uint64_t next_command_sequence = 1;
    std::unique_ptr<vespera::editor::AutomationServer> automation_server;
    std::uint16_t automation_port = 46787;
    bool request_reset_layout = false;
    ManagedMetadataCatalog managed_metadata;
    EditorBuildJobState build_job;
    vespera::RuntimePerformanceCounters performance;
    double play_update_ms = 0.0;
    ImGuiTextFilter entity_filter;
    std::vector<HistorySnapshot> undo_stack;
    std::vector<HistorySnapshot> redo_stack;
    std::optional<HistorySnapshot> active_edit_before;
    ImGuiID active_edit_item = 0;
    bool active_edit_changed = false;
    std::uint64_t current_state_id = 1;
    std::uint64_t saved_state_id = 1;
    std::uint64_t next_state_id = 2;
    bool dirty = false;
    bool request_open_popup = false;
    bool request_open_project_popup = false;
    bool request_save_as_popup = false;
    bool request_unsaved_popup = false;
    PendingAction pending_action = PendingAction::None;
    std::filesystem::path pending_open_path;
};

bool start_editor_build_job(EditorState& state, std::string_view configuration, bool launch_after);
void update_editor_build_job(EditorState& state);
void draw_build_game_window(EditorState& state);

void register_core_extension_capabilities(EditorState& state) {
    if (!state.extension_registry.extensions().empty()) return;
    std::string error;
    (void)state.extension_registry.register_extension({
        "vespera.core", "Vespera Core Editor", std::string(vespera::kEngineVersion),
        vespera::editor::kEditorExtensionApiVersion}, &error);
    for (const auto& tool : vespera::editor::kAutomationTools) {
        vespera::editor::EditorExtensionCommandDescriptor command;
        command.extension_id = "vespera.core";
        command.name = std::string(tool.name);
        command.description = std::string(tool.description);
        command.mutating = tool.mutating;
        command.undoable = tool.undoable;
        command.allowed_during_play = tool.allowed_during_play;
        (void)state.extension_registry.register_command(std::move(command), &error);
    }
}

std::optional<std::size_t> entity_index_from_id(const EditorState& state, vespera::SceneObjectId id);

vespera::TransformComponent editor_world_transform(const EditorState& state, const vespera::Entity& entity) {
    return vespera::entity_world_transform(state.scene, entity);
}

void set_editor_world_transform(EditorState& state, vespera::SceneObjectId id, const vespera::TransformComponent& world) {
    auto* entity = state.scene.find_entity(id);
    if (!entity) return;
    if (entity->parent_id == vespera::kInvalidSceneObjectId) entity->transform = world;
    else entity->transform = vespera::inverse_compose_transform(
        vespera::entity_world_transform(state.scene, entity->parent_id), world);
}

std::size_t entity_hierarchy_depth(const EditorState& state, const vespera::Entity& entity) {
    std::size_t depth = 0;
    vespera::SceneObjectId parent = entity.parent_id;
    while (parent != vespera::kInvalidSceneObjectId && depth < state.scene.entities.size()) {
        const auto* node = state.scene.find_entity(parent);
        if (!node) break;
        ++depth;
        parent = node->parent_id;
    }
    return depth;
}

bool entity_is_multi_selected(const EditorState& state, vespera::SceneObjectId id) {
    return std::find(state.selected_entity_ids.begin(), state.selected_entity_ids.end(), id) != state.selected_entity_ids.end();
}

void select_entity(EditorState& state, std::size_t index) {
    if (index >= state.scene.entities.size()) {
        state.selection = {};
        state.selected_entity_ids.clear();
        state.hierarchy_anchor_id = vespera::kInvalidSceneObjectId;
        return;
    }
    const auto id = state.scene.entities[index].id;
    state.selection = {SelectionKind::Entity, index, kNoSubSelection, id};
    state.selected_entity_ids.assign(1, id);
    state.hierarchy_anchor_id = id;
}

void toggle_entity_selection(EditorState& state, std::size_t index) {
    if (index >= state.scene.entities.size()) return;
    const auto id = state.scene.entities[index].id;
    if (state.selection.kind != SelectionKind::Entity) state.selected_entity_ids.clear();
    const auto it = std::find(state.selected_entity_ids.begin(), state.selected_entity_ids.end(), id);
    if (it == state.selected_entity_ids.end()) {
        state.selected_entity_ids.push_back(id);
        state.selection = {SelectionKind::Entity, index, kNoSubSelection, id};
    } else {
        state.selected_entity_ids.erase(it);
        if (state.selected_entity_ids.empty()) {
            state.selection = {};
        } else if (state.selection.object_id == id) {
            const auto next_id = state.selected_entity_ids.back();
            if (const auto next_index = entity_index_from_id(state, next_id)) {
                state.selection = {SelectionKind::Entity, *next_index, kNoSubSelection, next_id};
            }
        }
    }
    state.hierarchy_anchor_id = id;
}

void select_entity_range(EditorState& state, std::size_t index) {
    if (index >= state.scene.entities.size()) return;
    std::size_t anchor = index;
    if (state.selection.kind == SelectionKind::Entity) {
        if (const auto resolved = entity_index_from_id(state, state.hierarchy_anchor_id)) anchor = *resolved;
    } else {
        state.selected_entity_ids.clear();
        state.hierarchy_anchor_id = state.scene.entities[index].id;
    }
    const auto first = std::min(anchor, index);
    const auto last = std::max(anchor, index);
    state.selected_entity_ids.clear();
    for (std::size_t i = first; i <= last; ++i) state.selected_entity_ids.push_back(state.scene.entities[i].id);
    const auto id = state.scene.entities[index].id;
    state.selection = {SelectionKind::Entity, index, kNoSubSelection, id};
    if (state.hierarchy_anchor_id == vespera::kInvalidSceneObjectId) state.hierarchy_anchor_id = id;
}

std::optional<std::size_t> selected_entity_index(const EditorState& state) {
    if (state.selection.kind != SelectionKind::Entity) {
        return std::nullopt;
    }
    if (state.selection.object_id != vespera::kInvalidSceneObjectId) {
        for (std::size_t i = 0; i < state.scene.entities.size(); ++i) {
            if (state.scene.entities[i].id == state.selection.object_id) {
                return i;
            }
        }
    }
    if (state.selection.index < state.scene.entities.size()) {
        return state.selection.index;
    }
    return std::nullopt;
}

std::optional<std::size_t> entity_index_from_id(const EditorState& state, vespera::SceneObjectId id) {
    if (id == vespera::kInvalidSceneObjectId) return std::nullopt;
    for (std::size_t i = 0; i < state.scene.entities.size(); ++i) {
        if (state.scene.entities[i].id == id) return i;
    }
    return std::nullopt;
}

void commit_active_edit(EditorState& state);
void push_console(EditorState& state, ConsoleEntry::Level level, std::string text);

void append_command_audit(
    EditorState& state,
    vespera::editor::EditorCommandKind kind,
    std::string label,
    bool succeeded = true,
    std::uint64_t before_state_id = 0,
    std::uint64_t after_state_id = 0,
    vespera::SceneObjectId target_entity_id = vespera::kInvalidSceneObjectId,
    std::string target_asset_id = {}) {
    vespera::editor::EditorCommandRecord record;
    record.sequence = state.next_command_sequence++;
    record.kind = kind;
    record.label = std::move(label);
    record.succeeded = succeeded;
    record.before_state_id = before_state_id == 0 ? state.current_state_id : before_state_id;
    record.after_state_id = after_state_id == 0 ? state.current_state_id : after_state_id;
    record.target_entity_id = target_entity_id;
    record.target_asset_id = std::move(target_asset_id);
    state.command_log.push_back(std::move(record));
    if (state.command_log.size() > 512) {
        state.command_log.erase(state.command_log.begin(), state.command_log.begin() + 128);
    }
}

bool editor_is_playing(const EditorState& state) {
    return state.play_state != EditorPlayState::Editing;
}

void set_game_input_capture(EditorState& state, bool captured) {
    if (!editor_is_playing(state)) captured = false;
    if (state.game_view.input_captured == captured) return;
    state.game_view.input_captured = captured;
    if (SDL_Window* window = SDL_GetKeyboardFocus()) {
        if (!SDL_SetWindowRelativeMouseMode(window, captured)) {
            push_console(state, ConsoleEntry::Level::Warning,
                std::format("Could not {} Game input capture: {}",
                    captured ? "enable" : "release", SDL_GetError()));
            state.game_view.input_captured = false;
        }
    }
}

void feed_play_runtime_input(EditorState& state) {
    if (!state.play_runtime || !editor_is_playing(state)) return;
    auto& runtime = *state.play_runtime;
    runtime.begin_input_frame();
    const bool captured = state.game_view.input_captured;
    const bool rml_ui_input = state.play_rml_ui_loaded
        && state.game_view.visible
        && state.game_view.focused
        && !captured;

    int key_count = 0;
    const bool* keys = SDL_GetKeyboardState(&key_count);
    const auto raw_key_down = [&](SDL_Scancode scancode) {
        const int index = static_cast<int>(scancode);
        return keys && index >= 0 && index < key_count && keys[index];
    };
    const auto gameplay_key_down = [&](SDL_Scancode scancode) {
        return captured && raw_key_down(scancode);
    };
    const auto ui_key_down = [&](SDL_Scancode scancode) {
        return (captured || rml_ui_input) && raw_key_down(scancode);
    };

    runtime.set_key(vespera::Key::W, gameplay_key_down(SDL_SCANCODE_W));
    runtime.set_key(vespera::Key::A, gameplay_key_down(SDL_SCANCODE_A));
    runtime.set_key(vespera::Key::S, gameplay_key_down(SDL_SCANCODE_S));
    runtime.set_key(vespera::Key::D, gameplay_key_down(SDL_SCANCODE_D));
    runtime.set_key(vespera::Key::LeftShift,
        gameplay_key_down(SDL_SCANCODE_LSHIFT) || gameplay_key_down(SDL_SCANCODE_RSHIFT));
    runtime.set_key(vespera::Key::Space, gameplay_key_down(SDL_SCANCODE_SPACE));
    runtime.set_key(vespera::Key::L, gameplay_key_down(SDL_SCANCODE_L));

    // Navigation/editing keys remain available while the Game view is focused
    // with a project RML surface active, even before first-person capture.
    runtime.set_key(vespera::Key::Enter, ui_key_down(SDL_SCANCODE_RETURN));
    runtime.set_key(vespera::Key::Tab, ui_key_down(SDL_SCANCODE_TAB));
    runtime.set_key(vespera::Key::Backspace, ui_key_down(SDL_SCANCODE_BACKSPACE));
    runtime.set_key(vespera::Key::Up, ui_key_down(SDL_SCANCODE_UP));
    runtime.set_key(vespera::Key::Down, ui_key_down(SDL_SCANCODE_DOWN));
    runtime.set_key(vespera::Key::Left, ui_key_down(SDL_SCANCODE_LEFT));
    runtime.set_key(vespera::Key::Right, ui_key_down(SDL_SCANCODE_RIGHT));

    if (captured) {
        float mouse_x = 0.0f;
        float mouse_y = 0.0f;
        SDL_GetRelativeMouseState(&mouse_x, &mouse_y);
        runtime.add_mouse_delta(mouse_x, mouse_y);
    } else if (state.play_rml_ui_loaded && state.game_view.visible) {
        const ImGuiIO& io = ImGui::GetIO();
        const float view_w = std::max(1.0f, state.game_view.content_max.x - state.game_view.content_min.x);
        const float view_h = std::max(1.0f, state.game_view.content_max.y - state.game_view.content_min.y);
        const float local_x = (io.MousePos.x - state.game_view.content_min.x) / view_w;
        const float local_y = (io.MousePos.y - state.game_view.content_min.y) / view_h;
        if (state.game_view.hovered) {
            runtime.set_mouse_position(
                std::clamp(local_x, 0.0f, 1.0f) * static_cast<float>(std::max(1, state.play_rml_view_width)),
                std::clamp(local_y, 0.0f, 1.0f) * static_cast<float>(std::max(1, state.play_rml_view_height)));
        } else {
            runtime.set_mouse_position(-10000.0f, -10000.0f);
        }
        runtime.set_mouse_button(vespera::MouseButton::Left, state.game_view.hovered && io.MouseDown[ImGuiMouseButton_Left]);
        runtime.set_mouse_button(vespera::MouseButton::Right, state.game_view.hovered && io.MouseDown[ImGuiMouseButton_Right]);
        runtime.set_mouse_button(vespera::MouseButton::Middle, state.game_view.hovered && io.MouseDown[ImGuiMouseButton_Middle]);
        if (rml_ui_input && !state.game_view_text_input.empty()) {
            runtime.add_text_input(state.game_view_text_input);
            state.game_view_text_input.clear();
        }
        // Backspace is delivered through InputSystem for RmlUi; the legacy flag
        // is only consumed by the .slui compatibility renderer.
        state.game_view_backspace_pending = false;
    }
}


void start_play_mode(EditorState& state) {
    if (editor_is_playing(state)) return;
    if (!state.project_loaded && state.scene_path.empty()) {
        push_console(state, ConsoleEntry::Level::Warning,
            "Play Mode requires either an open Vespera project or an explicitly opened scene. The empty editor scene will not be run.");
        return;
    }
    state.last_runtime_events.clear();
    state.last_runtime_event_sequence = 0;
    state.last_runtime_assembly_generation = 0;
    commit_active_edit(state);
    PlayEditBackup backup;
    backup.scene = state.scene;
    backup.selection = state.selection;
    backup.selected_entity_ids = state.selected_entity_ids;
    backup.hierarchy_anchor_id = state.hierarchy_anchor_id;
    backup.undo_stack = state.undo_stack;
    backup.redo_stack = state.redo_stack;
    backup.current_state_id = state.current_state_id;
    backup.saved_state_id = state.saved_state_id;
    backup.next_state_id = state.next_state_id;
    backup.dirty = state.dirty;
    state.play_edit_backup = std::move(backup);
    state.play_scene = state.scene;
    state.play_time_seconds = 0.0;
    state.play_last_wall_seconds = -1.0;
    state.play_state = EditorPlayState::Playing;
    state.game_view.focus_pending = true;
    state.play_ui_loaded = false;
    state.play_ui_cache.clear();
    state.play_ui_document.clear();
    if (state.play_rml_ui) {
        state.play_rml_ui->shutdown();
        state.play_rml_ui.reset();
    }
    state.play_rml_ui_loaded = false;
    state.play_rml_view_width = std::max(1, state.project_loaded ? state.project.window_width : 1280);
    state.play_rml_view_height = std::max(1, state.project_loaded ? state.project.window_height : 720);

    if (state.project_loaded) {
        // Match standalone vespera_player: project startup RML is the preferred
        // runtime UI. This closes the old Editor-Play-only .slui path for new projects.
        const auto rml_selection = vespera::select_runtime_rml_document(state.project, state.asset_catalog);
        if (rml_selection.document) {
            std::vector<std::filesystem::path> font_paths;
            for (const auto* font_asset : state.asset_catalog.records_of_kind(vespera::AssetKind::Font)) {
                if (font_asset) font_paths.push_back(font_asset->absolute_path);
            }
            auto rml = std::make_unique<vespera::RmlUiSurface>();
            const auto loaded = rml->initialize(
                rml_selection.document->absolute_path,
                state.play_rml_view_width,
                state.play_rml_view_height,
                font_paths);
            if (loaded) {
                state.play_rml_ui_loaded = true;
                state.play_rml_ui = std::move(rml);
                push_console(state, ConsoleEntry::Level::Info,
                    "Play Mode UI: RmlUi - " + rml_selection.message);
            } else {
                push_console(state, ConsoleEntry::Level::Warning,
                    "Play Mode RML UI failed to initialize: " + loaded.message);
            }
        }

        // Compatibility path for the reference QA project and older projects that
        // have no startup RML. Do not grow this path with new authoring features.
        if (!state.play_rml_ui_loaded) {
            const vespera::AssetRecord* ui_asset = state.asset_catalog.find("ui/reference_hud.slui");
            if (!ui_asset) {
                const auto ui_records = state.asset_catalog.records_of_kind(vespera::AssetKind::UiDocument);
                if (!ui_records.empty()) ui_asset = ui_records.front();
            }
            if (ui_asset) {
                const auto loaded = vespera::load_ui_document(state.play_ui_document, ui_asset->absolute_path);
                if (loaded) {
                    state.play_ui_cache.prepare_images(state.play_ui_document,
                        [&](const vespera::AssetReference& reference) -> std::optional<vespera::TextureData> {
                            const auto resolved = state.asset_catalog.resolve_reference(reference);
                            if (!resolved || resolved.record->kind != vespera::AssetKind::Texture) return std::nullopt;
                            const auto imported = vespera::import_texture(resolved.record->absolute_path, resolved.record->display_name);
                            if (!imported) return std::nullopt;
                            return imported.texture;
                        },
                        [&](const vespera::AssetReference& reference) -> std::optional<std::filesystem::path> {
                            const auto resolved = state.asset_catalog.resolve_reference(reference);
                            if (!resolved || resolved.record->kind != vespera::AssetKind::Font) return std::nullopt;
                            return resolved.record->absolute_path;
                        });
                    state.play_ui_loaded = true;
                    push_console(state, ConsoleEntry::Level::Info,
                        std::format("Play Mode UI: legacy .slui '{}' ({} nodes, {} image warning(s))",
                            ui_asset->relative_path.generic_string(), state.play_ui_document.nodes().size(),
                            state.play_ui_cache.image_warnings().size()));
                } else {
                    push_console(state, ConsoleEntry::Level::Warning, "Play Mode legacy UI: " + loaded.message);
                }
            }
        }
    }
    state.play_runtime = std::make_unique<vespera::editor::PlayRuntime>();
    if (state.project_loaded) {
        state.play_runtime->start(
            state.play_scene, state.project, state.asset_catalog, state.scene_path,
            state.play_ui_loaded ? &state.play_ui_document : nullptr,
            state.play_rml_ui_loaded ? state.play_rml_ui.get() : nullptr,
            &state.performance);
        const auto& runtime_status = state.play_runtime->status();
        push_console(state,
            runtime_status.managed_ready ? ConsoleEntry::Level::Info : ConsoleEntry::Level::Warning,
            "Play Mode: " + runtime_status.message);
    } else {
        push_console(state, ConsoleEntry::Level::Warning,
            "Play Mode started without a loaded project; embedded C#/audio services are unavailable.");
    }
    append_command_audit(state, vespera::editor::EditorCommandKind::EnterPlayMode, "Enter Play Mode");
    push_console(state, ConsoleEntry::Level::Info,
        "Play Mode started from an isolated scene copy. Open Game and click the viewport to capture WASD + mouse; Escape releases input. Stop discards runtime changes.");
}

void stop_play_mode(EditorState& state) {
    if (!editor_is_playing(state)) return;
    set_game_input_capture(state, false);
    if (state.play_runtime) {
        state.play_runtime->stop();
        state.last_runtime_events = state.play_runtime->runtime_events_since(0);
        state.last_runtime_event_sequence = state.play_runtime->latest_runtime_event_sequence();
        state.last_runtime_assembly_generation = state.play_runtime->assembly_generation();
        state.play_runtime.reset();
    }
    if (state.play_edit_backup) {
        auto backup = std::move(*state.play_edit_backup);
        state.scene = std::move(backup.scene);
        state.selection = std::move(backup.selection);
        state.selected_entity_ids = std::move(backup.selected_entity_ids);
        state.hierarchy_anchor_id = backup.hierarchy_anchor_id;
        state.undo_stack = std::move(backup.undo_stack);
        state.redo_stack = std::move(backup.redo_stack);
        state.current_state_id = backup.current_state_id;
        state.saved_state_id = backup.saved_state_id;
        state.next_state_id = backup.next_state_id;
        state.dirty = backup.dirty;
    }
    state.play_edit_backup.reset();
    state.play_ui_loaded = false;
    state.play_ui_document.clear();
    state.play_ui_cache.clear();
    state.play_rml_ui_loaded = false;
    if (state.play_rml_ui) {
        state.play_rml_ui->shutdown();
        state.play_rml_ui.reset();
    }
    state.play_scene = {};
    state.play_time_seconds = 0.0;
    state.play_last_wall_seconds = -1.0;
    state.play_state = EditorPlayState::Editing;
    append_command_audit(state, vespera::editor::EditorCommandKind::ExitPlayMode, "Exit Play Mode");
    push_console(state, ConsoleEntry::Level::Info, "Play Mode stopped; runtime services torn down and edit scene restored unchanged.");
}

void toggle_play_pause(EditorState& state) {
    if (!editor_is_playing(state)) return;
    if (state.play_state == EditorPlayState::Paused) {
        state.play_state = EditorPlayState::Playing;
        state.play_last_wall_seconds = -1.0;
        if (state.play_runtime) state.play_runtime->set_paused(false);
        append_command_audit(state, vespera::editor::EditorCommandKind::ResumePlayMode, "Resume Play Mode");
    } else {
        state.play_state = EditorPlayState::Paused;
        if (state.play_runtime) state.play_runtime->set_paused(true);
        append_command_audit(state, vespera::editor::EditorCommandKind::PausePlayMode, "Pause Play Mode");
    }
}

void step_play_mode(EditorState& state) {
    if (!editor_is_playing(state)) return;
    state.play_state = EditorPlayState::Paused;
    if (state.play_runtime) state.play_runtime->set_paused(true);
    feed_play_runtime_input(state);
    if (state.play_runtime) state.play_runtime->update(state.play_scene, 1.0 / 60.0);
    state.play_time_seconds += 1.0 / 60.0;
    append_command_audit(state, vespera::editor::EditorCommandKind::StepPlayMode, "Step Play Mode");
}

void update_play_clock(EditorState& state, double wall_seconds) {
    if (!editor_is_playing(state)) return;
    if (state.play_state == EditorPlayState::Playing) {
        double dt = 0.0;
        if (state.play_last_wall_seconds >= 0.0) {
            dt = std::clamp(wall_seconds - state.play_last_wall_seconds, 0.0, 0.1);
            state.play_time_seconds += dt;
        }
        state.play_last_wall_seconds = wall_seconds;
        feed_play_runtime_input(state);
        if (state.play_runtime && dt > 0.0) state.play_runtime->update(state.play_scene, dt);
    } else {
        state.play_last_wall_seconds = wall_seconds;
        // Keep previous/current input snapshots coherent while paused so Resume
        // does not synthesize a burst of Pressed actions.
        feed_play_runtime_input(state);
    }
}

void repair_selection(EditorState& state) {
    if (state.selection.kind == SelectionKind::Entity) {
        state.selected_entity_ids.erase(
            std::remove_if(state.selected_entity_ids.begin(), state.selected_entity_ids.end(), [&](vespera::SceneObjectId id) {
                return !entity_index_from_id(state, id).has_value();
            }),
            state.selected_entity_ids.end());
        const auto index = selected_entity_index(state);
        if (!index) {
            state.selection = {};
            state.selected_entity_ids.clear();
            return;
        }
        state.selection.index = *index;
        state.selection.object_id = state.scene.entities[*index].id;
        if (!entity_is_multi_selected(state, state.selection.object_id)) state.selected_entity_ids.push_back(state.selection.object_id);
    } else if (state.selection.kind == SelectionKind::Sector) {
        const auto& sectors = state.scene.world.sectors();
        if (state.selection.index >= sectors.size()) {
            state.selection = {};
            return;
        }
        const auto& sector = sectors[state.selection.index];
        if (state.selection.sub_index >= sector.vertices.size()) {
            state.selection.sub_index = kNoSubSelection;
        }
        if (state.selection.side_index >= sector.sides.size()) {
            state.selection.side_index = kNoSubSelection;
        }
    } else if (state.selection.kind == SelectionKind::Material
        && state.selection.index >= state.scene.world.materials().size()) {
        state.selection = {};
    } else if (state.selection.kind == SelectionKind::SpriteClip
        && state.selection.index >= state.scene.sprite_clips.size()) {
        state.selection = {};
    } else if (state.selection.kind == SelectionKind::Asset
        && (state.selection.asset_id.empty() || !state.asset_catalog.find_by_id(state.selection.asset_id))) {
        state.selection = {};
    }
}

float snap_coordinate(float value, const SceneViewState& view, bool temporarily_disable);
vespera::Vec2 snap_world_point(vespera::Vec2 point, const SceneViewState& view, bool temporarily_disable);

std::size_t register_catalog_texture_assets(
    vespera::Scene& scene,
    const vespera::AssetCatalog& catalog,
    std::vector<std::string>* warnings = nullptr
) {
    std::size_t registered = 0;
    for (const auto* record : catalog.records_of_kind(vespera::AssetKind::Texture)) {
        if (!record) continue;
        const auto imported = vespera::import_texture(record->absolute_path, record->display_name);
        if (imported && imported.texture.valid()) {
            scene.world.add_texture(imported.texture);
        } else {
            scene.world.add_texture(vespera::make_missing_texture_placeholder(record->display_name));
            if (warnings) {
                warnings->push_back("Texture asset '" + record->relative_path.generic_string()
                    + "' could not be decoded; using the Vespera missing-texture checkerboard. "
                    + imported.message);
            }
        }
        ++registered;
    }
    return registered;
}

vespera::AssetCatalog build_read_only_catalog(const std::filesystem::path& assets_root) {
    vespera::AssetCatalog catalog;
    vespera::AssetCatalogRefreshOptions options;
    options.write_metadata = false;
    vespera::AssetCatalogRefreshReport report;
    std::string ignored_error;
    (void)catalog.refresh(assets_root, options, &report, &ignored_error);
    return catalog;
}

void push_console(EditorState& state, ConsoleEntry::Level level, std::string text) {
    state.console.push_back({level, std::move(text)});
    constexpr std::size_t kMaxEntries = 500;
    if (state.console.size() > kMaxEntries) {
        state.console.erase(state.console.begin(), state.console.begin() + (state.console.size() - kMaxEntries));
    }
}


bool load_managed_metadata(EditorState& state, bool report = true) {
    ManagedMetadataCatalog catalog;
    catalog.path = std::filesystem::path("managed") / "Vespera.ScriptMetadata.txt";
    std::ifstream input(catalog.path);
    if (!input) {
        state.managed_metadata = std::move(catalog);
        if (report) push_console(state, ConsoleEntry::Level::Warning, "C# metadata not found; run build.ps1 to regenerate managed metadata.");
        return false;
    }
    ManagedScriptMetadata* current = nullptr;
    std::string line; int version = 0;
    while (std::getline(input, line)) {
        const auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos || line[first] == '#') continue;
        std::istringstream stream(line.substr(first)); std::string cmd; stream >> cmd;
        if (cmd == "vespera_script_metadata") { stream >> version; if (version < 1 || version > 4) break; }
        else if (cmd == "assembly") stream >> std::quoted(catalog.assembly);
        else if (cmd == "script") { ManagedScriptMetadata meta; stream >> std::quoted(meta.class_name); catalog.scripts.push_back(std::move(meta)); current = &catalog.scripts.back(); }
        else if (cmd == "field" && current) {
            ManagedFieldMetadata f;
            stream >> std::quoted(f.name) >> std::quoted(f.type) >> std::quoted(f.display_name) >> std::quoted(f.clr_type);
            if (version >= 2) {
                int has_range = 0;
                stream >> std::quoted(f.tooltip) >> has_range >> f.range_min >> f.range_max;
                f.has_range = has_range != 0;
            }
            current->fields.push_back(std::move(f));
        }
        else if (cmd == "enum_value" && current && !current->fields.empty()) {
            std::string value; stream >> std::quoted(value);
            current->fields.back().enum_values.push_back(std::move(value));
        }
        else if (cmd == "alias" && current && !current->fields.empty()) {
            std::string value; stream >> std::quoted(value);
            if (!value.empty()) current->fields.back().aliases.push_back(std::move(value));
        }
        else if (cmd == "endscript") current = nullptr;
        else if (cmd == "end_metadata") { catalog.loaded = version >= 1 && version <= 4; break; }
    }
    state.managed_metadata = std::move(catalog);
    if (report) push_console(state, state.managed_metadata.loaded ? ConsoleEntry::Level::Info : ConsoleEntry::Level::Warning,
        state.managed_metadata.loaded ? std::format("Loaded C# metadata: {} component type(s).", state.managed_metadata.scripts.size()) : "C# metadata file is invalid.");
    return state.managed_metadata.loaded;
}

std::string default_managed_value(const ManagedFieldMetadata& field) {
    if (field.type == "bool") return "false";
    if (field.type == "int") {
        if (field.has_range && (0.0f < field.range_min || 0.0f > field.range_max))
            return std::to_string(static_cast<int>(field.range_min));
        return "0";
    }
    if (field.type == "float") {
        if (field.has_range && (0.0f < field.range_min || 0.0f > field.range_max))
            return std::format("{}", field.range_min);
        return "0";
    }
    if (field.type == "enum") return field.enum_values.empty() ? std::string{} : field.enum_values.front();
    if (field.type == "vec2") return "0 0";
    if (field.type == "vec3") return "0 0 0";
    if (field.type == "color") return "1 1 1 1";
    return {};
}

std::optional<std::filesystem::path> managed_build_helper_path() {
#if defined(_WIN32)
    char* raw = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&raw, &size, "VESPERA_MANAGED_BUILD_HELPER") != 0 || !raw || !*raw) {
        if (raw) std::free(raw);
        return std::nullopt;
    }
    std::filesystem::path path(raw);
    std::free(raw);
    return path;
#else
    return std::nullopt;
#endif
}

void load_managed_build_diagnostics(EditorState& state, const std::filesystem::path& path, int process_result) {
    std::ifstream input(path);
    if (!input) {
        push_console(state, ConsoleEntry::Level::Error,
            std::format("C# build process returned {}, but no diagnostics file was produced.", process_result));
        return;
    }

    std::string status;
    std::string tfm;
    std::vector<std::string> output_tail;
    bool saw_diagnostic = false;
    std::string line;
    while (std::getline(input, line)) {
        const auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) continue;
        std::istringstream stream(line.substr(first));
        std::string command;
        stream >> command;
        if (command == "status") {
            stream >> std::quoted(status);
        } else if (command == "tfm") {
            stream >> std::quoted(tfm);
        } else if (command == "diagnostic") {
            std::string severity, code, file, message;
            int source_line = 0, column = 0;
            stream >> std::quoted(severity) >> std::quoted(code) >> std::quoted(file)
                >> source_line >> column >> std::quoted(message);
            const auto level = severity == "error" ? ConsoleEntry::Level::Error : ConsoleEntry::Level::Warning;
            const auto location = file.empty() ? std::string{} : std::format("{}({},{})", file, source_line, column);
            push_console(state, level, std::format("{}{}{}: {}",
                code, location.empty() ? "" : " ", location, message));
            saw_diagnostic = true;
        } else if (command == "output") {
            std::string text;
            stream >> std::quoted(text);
            if (!text.empty()) output_tail.push_back(std::move(text));
        }
    }

    if (status == "success" && process_result == 0) {
        push_console(state, ConsoleEntry::Level::Info,
            tfm.empty() ? "C# build succeeded. Last-good managed output updated; a running reference game will auto-reload the committed game assembly."
                        : std::format("C# build succeeded ({}). Last-good managed output updated; a running reference game will auto-reload the committed game assembly.", tfm));
        load_managed_metadata(state, true);
    } else {
        push_console(state, ConsoleEntry::Level::Error,
            "C# build failed. The previous good managed assembly and metadata were preserved.");
        if (!saw_diagnostic) {
            for (const auto& text : output_tail) push_console(state, ConsoleEntry::Level::Error, text);
        }
    }
}

void build_managed_scripts(EditorState& state) {
#if defined(_WIN32)
    const auto helper = managed_build_helper_path();
    if (!helper || !std::filesystem::exists(*helper)) {
        push_console(state, ConsoleEntry::Level::Warning,
            "Build C# is unavailable in this launch. Start the editor with run.ps1 so it can locate the managed build helper.");
        return;
    }

    const auto managed_dir = std::filesystem::absolute(std::filesystem::path("managed")).lexically_normal();
    // A Build C# request is synchronous on the editor main thread, but MCP may
    // retry after a transport timeout. Never allow a later invocation to read
    // diagnostics left by an earlier compiler failure. Each build gets a fresh
    // file and that file is removed after it is consumed.
    static std::uint64_t managed_build_sequence = 0;
    const auto diagnostics = managed_dir / std::format(
        "Vespera.ManagedBuildDiagnostics.{}.txt", ++managed_build_sequence);
    std::error_code diagnostics_ec;
    std::filesystem::remove(diagnostics, diagnostics_ec);
    const auto quote = [](const std::filesystem::path& value) {
        return std::string("\"") + value.string() + "\"";
    };
    const auto quote_text = [](const std::string& value) {
        return std::string("\"") + value + "\"";
    };
    std::string command = std::format(
        "powershell.exe -NoProfile -ExecutionPolicy Bypass -File {} -EditorManagedDir {} -DiagnosticsFile {}",
        quote(*helper), quote(managed_dir), quote(diagnostics));
    if (state.project_loaded && !state.project.managed_project.empty()) {
        command += " -Project " + quote(state.project.managed_project_path());
        if (!state.project.managed_assembly.empty()) {
            command += " -GameAssemblyName " + quote_text(state.project.managed_assembly);
        }
    }
    push_console(state, ConsoleEntry::Level::Info, "Building C# scripts...");
    const int result = std::system(command.c_str());
    load_managed_build_diagnostics(state, diagnostics, result);
    diagnostics_ec.clear();
    std::filesystem::remove(diagnostics, diagnostics_ec);
    if (diagnostics_ec) {
        push_console(state, ConsoleEntry::Level::Warning,
            "Could not remove per-build C# diagnostics file: " + diagnostics_ec.message());
    }
#else
    push_console(state, ConsoleEntry::Level::Warning, "Editor-side C# compilation is Windows-first in 0.5.5.");
#endif
}

void refresh_asset_catalog(EditorState& state, bool report_success = false, bool force_rehash = false) {
    if (state.assets_root.empty()) return;
    std::string error;
    vespera::AssetCatalogRefreshReport report;
    vespera::AssetCatalogRefreshOptions options;
    options.force_rehash = force_rehash;
    if (!state.asset_catalog.refresh(state.assets_root, options, &report, &error)) {
        push_console(state, ConsoleEntry::Level::Warning, "Asset catalog refresh failed: " + error);
        return;
    }
    const auto previous_report = state.last_asset_report;
    state.last_asset_report = report;
    repair_selection(state);
    const bool meaningful_change = report.metadata_created != 0
        || report.metadata_updated != 0
        || report.metadata_repaired != 0
        || report.metadata_missing != previous_report.metadata_missing
        || report.orphaned_metadata != previous_report.orphaned_metadata
        || report.dependency_edges != previous_report.dependency_edges
        || report.stable_reference_edges != previous_report.stable_reference_edges
        || report.broken_dependencies != previous_report.broken_dependencies
        || report.stale_fallback_paths != previous_report.stale_fallback_paths
        || report.assets_changed != 0 || report.assets_moved != 0 || report.assets_removed != 0;
    if (report_success || meaningful_change) {
        push_console(state,
            (report.metadata_missing != 0 || report.orphaned_metadata != 0)
                ? ConsoleEntry::Level::Warning : ConsoleEntry::Level::Info,
            std::format(
                "Asset refresh: {} assets | {} imported | {} reimported | {} repaired | {} missing | {} orphan metadata | {} hashes | {} fast | {} deps | {} stable refs | {} broken | {} stale paths | +{} ~{} move{} -{}",
                report.scanned_assets,
                report.metadata_created,
                report.metadata_updated,
                report.metadata_repaired,
                report.metadata_missing,
                report.orphaned_metadata,
                report.hashes_computed,
                report.fast_path_hits,
                report.dependency_edges,
                report.stable_reference_edges,
                report.broken_dependencies,
                report.stale_fallback_paths,
                report.assets_added, report.assets_changed, report.assets_moved, report.assets_removed));
        for (const auto& change : report.changes) {
            if (change.kind == vespera::AssetCatalogChangeKind::Moved) {
                push_console(state, ConsoleEntry::Level::Info,
                    "Asset moved (stable ID preserved): " + change.old_path.generic_string() + " -> " + change.new_path.generic_string());
            } else if (change.kind == vespera::AssetCatalogChangeKind::Removed) {
                push_console(state, ConsoleEntry::Level::Warning, "Asset removed: " + change.old_path.generic_string());
            }
        }
    }
    const auto material_report = vespera::hydrate_scene_materials(state.scene, state.asset_catalog);
    if (!material_report.message.empty() && (report_success || meaningful_change)) {
        push_console(state, ConsoleEntry::Level::Warning, "Material hydration: " + material_report.message);
    }
    if (editor_is_playing(state)) {
        (void)vespera::hydrate_scene_materials(state.play_scene, state.asset_catalog);
    }
}

void repair_asset_fallback_paths(EditorState& state, bool persist_project = true) {
    if (!state.project_loaded) {
        push_console(state, ConsoleEntry::Level::Warning, "Stable fallback repair requires an open Vespera project.");
        return;
    }

    auto report = vespera::repair_stable_asset_fallbacks(state.project, state.asset_catalog);
    bool project_saved = true;
    if (persist_project && report.project_references_repaired != 0) {
        const auto saved = vespera::save_vespera_project(state.project, state.project_path);
        project_saved = static_cast<bool>(saved);
        push_console(state, project_saved ? ConsoleEntry::Level::Info : ConsoleEntry::Level::Error, saved.message);
    }
    for (const auto& warning : report.warnings) {
        push_console(state, ConsoleEntry::Level::Warning, "Reference repair: " + warning);
    }
    if (report.descriptor_files_rewritten != 0) refresh_asset_catalog(state, false, true);

    const auto level = (!project_saved || !report.warnings.empty())
        ? ConsoleEntry::Level::Warning : ConsoleEntry::Level::Info;
    push_console(state, level, std::format(
        "Stable fallback repair: {} project reference(s) | {} descriptor reference(s) across {} file(s)",
        report.project_references_repaired,
        report.descriptor_references_repaired,
        report.descriptor_files_rewritten));
}

void request_asset_move(EditorState& state, const vespera::AssetRecord& record) {
    state.asset_move_id = record.asset_id;
    state.asset_move_path = record.relative_path.generic_string();
    state.request_asset_move_popup = true;
}

void draw_asset_move_popup(EditorState& state) {
    if (state.request_asset_move_popup) {
        ImGui::OpenPopup("Move / Rename Asset");
        state.request_asset_move_popup = false;
    }
    bool open = true;
    if (!ImGui::BeginPopupModal("Move / Rename Asset", &open, ImGuiWindowFlags_AlwaysAutoResize)) return;

    const auto* record = state.asset_catalog.find_by_id(state.asset_move_id);
    if (!record) {
        ImGui::TextDisabled("The selected asset is no longer present in the catalog.");
        if (ImGui::Button("Close", ImVec2(100.0f, 0.0f))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    ImGui::TextUnformatted(record->display_name.c_str());
    ImGui::TextDisabled("Stable ID: %s", record->asset_id.c_str());
    ImGui::Spacing();
    ImGui::SetNextItemWidth(520.0f);
    ImGui::InputText("Path within Assets", &state.asset_move_path);
    ImGui::TextDisabled("Moves the source file and its .vmeta together. Local RML/RCSS paths are repaired automatically.");

    if (ImGui::Button("Move / Rename", ImVec2(140.0f, 0.0f))) {
        const auto old_absolute = record->absolute_path;
        const auto moved = vespera::move_project_asset(state.asset_catalog, record->asset_id, state.asset_move_path);
        if (!moved) {
            push_console(state, ConsoleEntry::Level::Error, "Asset move failed: " + moved.message);
        } else {
            const bool moved_open_scene = !state.scene_path.empty()
                && old_absolute.lexically_normal() == state.scene_path.lexically_normal();
            if (moved_open_scene) state.scene_path = (state.assets_root / moved.new_path).lexically_normal();
            state.asset_browser_folder = moved.new_path.parent_path();
            push_console(state, ConsoleEntry::Level::Info, moved.message);
            refresh_asset_catalog(state, true, false);
            // Safe editor moves also repair readable fallback paths. Manual/external
            // filesystem moves remain supported and can be repaired from Project Settings.
            repair_asset_fallback_paths(state, true);
            state.selection.kind = SelectionKind::Asset;
            state.selection.asset_id = moved.asset_id;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

std::filesystem::path derive_assets_root(const std::filesystem::path& scene_path) {
    auto parent = scene_path.parent_path();
    if (parent.filename() == "scenes") {
        return parent.parent_path();
    }
    return parent;
}

HistorySnapshot capture_snapshot(const EditorState& state, std::string label = {}) {
    return {state.scene, state.selection, state.selected_entity_ids, state.current_state_id, std::move(label)};
}

void refresh_dirty(EditorState& state) {
    state.dirty = state.current_state_id != state.saved_state_id;
}

void clear_active_edit(EditorState& state) {
    state.active_edit_before.reset();
    state.active_edit_item = 0;
    state.active_edit_changed = false;
}

void push_history_before(EditorState& state, HistorySnapshot before, std::string label) {
    before.label = std::move(label);
    state.undo_stack.push_back(std::move(before));
    constexpr std::size_t kHistoryLimit = 128;
    if (state.undo_stack.size() > kHistoryLimit) {
        state.undo_stack.erase(state.undo_stack.begin());
    }
    state.redo_stack.clear();
    state.current_state_id = state.next_state_id++;
    refresh_dirty(state);
}

void begin_edit(EditorState& state, HistorySnapshot before, std::string label, ImGuiID item_id = 0) {
    if (state.active_edit_before) {
        return;
    }
    before.label = std::move(label);
    state.active_edit_before = std::move(before);
    state.active_edit_item = item_id;
    state.active_edit_changed = false;
}

void commit_active_edit(EditorState& state) {
    if (!state.active_edit_before) {
        return;
    }
    if (state.active_edit_changed) {
        HistorySnapshot before = std::move(*state.active_edit_before);
        const std::string label = before.label;
        push_history_before(state, std::move(before), label);
    }
    clear_active_edit(state);
}

void track_item_edit(EditorState& state, HistorySnapshot before, std::string label, ImGuiID item_id, bool changed) {
    if (changed) {
        state.dirty = true; // Reflect in-progress edits before the transaction commits.
    }
    if (changed && !state.active_edit_before) {
        begin_edit(state, std::move(before), std::move(label), item_id);
    }
    if (state.active_edit_before && state.active_edit_item == item_id) {
        state.active_edit_changed = state.active_edit_changed || changed;
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            commit_active_edit(state);
        }
    }
}

void record_immediate_edit(EditorState& state, HistorySnapshot before, std::string label) {
    commit_active_edit(state);
    push_history_before(state, std::move(before), std::move(label));
}

template <typename Mutator>
bool execute_editor_command(
    EditorState& state,
    vespera::editor::EditorCommandKind kind,
    std::string label,
    Mutator&& mutator
) {
    commit_active_edit(state);
    const std::uint64_t before_state_id = state.current_state_id;
    HistorySnapshot before = capture_snapshot(state);
    const bool succeeded = mutator();
    if (!succeeded) {
        append_command_audit(state, kind, label, false, before_state_id, state.current_state_id);
        return false;
    }
    const std::string console_label = label;
    record_immediate_edit(state, std::move(before), std::move(label));
    append_command_audit(state, kind, console_label, true, before_state_id, state.current_state_id);
    push_console(state, ConsoleEntry::Level::Info, console_label);
    return true;
}

bool entity_name_exists(const vespera::Scene& scene, std::string_view name) {
    return std::any_of(scene.entities.begin(), scene.entities.end(), [&](const vespera::Entity& entity) {
        return entity.name == name;
    });
}

std::string unique_entity_name(const vespera::Scene& scene, std::string base) {
    if (!entity_name_exists(scene, base)) {
        return base;
    }
    for (int suffix = 2; suffix < 10000; ++suffix) {
        const std::string candidate = std::format("{} {}", base, suffix);
        if (!entity_name_exists(scene, candidate)) {
            return candidate;
        }
    }
    return base + " Unique";
}

bool clip_name_exists(const vespera::Scene& scene, std::string_view name, std::size_t ignore = kNoSubSelection) {
    for (std::size_t i = 0; i < scene.sprite_clips.size(); ++i) {
        if (i != ignore && scene.sprite_clips[i].name == name) {
            return true;
        }
    }
    return false;
}

std::string unique_clip_name(const vespera::Scene& scene, std::string base) {
    if (!clip_name_exists(scene, base)) {
        return base;
    }
    for (int suffix = 2; suffix < 10000; ++suffix) {
        const std::string candidate = std::format("{} {}", base, suffix);
        if (!clip_name_exists(scene, candidate)) {
            return candidate;
        }
    }
    return base + " Unique";
}

bool sector_name_exists(const vespera::Scene& scene, std::string_view name, std::size_t ignore = kNoSubSelection) {
    const auto& sectors = scene.world.sectors();
    for (std::size_t i = 0; i < sectors.size(); ++i) {
        if (i != ignore && sectors[i].name == name) {
            return true;
        }
    }
    return false;
}

std::string unique_sector_name(const vespera::Scene& scene, std::string base) {
    if (!sector_name_exists(scene, base)) {
        return base;
    }
    for (int suffix = 2; suffix < 10000; ++suffix) {
        const std::string candidate = std::format("{} {}", base, suffix);
        if (!sector_name_exists(scene, candidate)) {
            return candidate;
        }
    }
    return base + " Unique";
}

bool material_name_exists(const vespera::Scene& scene, std::string_view name, std::size_t ignore = kNoSubSelection) {
    const auto& materials = scene.world.materials();
    for (std::size_t i = 0; i < materials.size(); ++i) {
        if (i != ignore && materials[i].name == name) {
            return true;
        }
    }
    return false;
}

std::string unique_material_name(const vespera::Scene& scene, std::string base) {
    if (!material_name_exists(scene, base)) {
        return base;
    }
    for (int suffix = 2; suffix < 10000; ++suffix) {
        const std::string candidate = std::format("{} {}", base, suffix);
        if (!material_name_exists(scene, candidate)) {
            return candidate;
        }
    }
    return base + " Unique";
}

vespera::TextureId default_sprite_texture(const vespera::Scene& scene) {
    const auto& textures = scene.world.textures();
    for (std::size_t i = 0; i < textures.size(); ++i) {
        if (textures[i].name == "Test Sprite") {
            return static_cast<vespera::TextureId>(i);
        }
    }
    return textures.empty() ? vespera::kInvalidTexture : 0u;
}

void resize_clip_frames(
    vespera::SpriteAnimationClip& clip,
    std::uint32_t new_directions,
    std::uint32_t new_frames,
    vespera::TextureId fallback
) {
    new_directions = (new_directions == 4u || new_directions == 8u) ? new_directions : 1u;
    new_frames = std::clamp(new_frames, 1u, 16u);
    std::vector<vespera::TextureId> resized(
        static_cast<std::size_t>(new_directions) * new_frames,
        fallback
    );
    const std::uint32_t copy_directions = std::min(clip.direction_count, new_directions);
    const std::uint32_t copy_frames = std::min(clip.frame_count, new_frames);
    for (std::uint32_t direction = 0; direction < copy_directions; ++direction) {
        for (std::uint32_t frame = 0; frame < copy_frames; ++frame) {
            const auto old_index = static_cast<std::size_t>(direction) * clip.frame_count + frame;
            const auto new_index = static_cast<std::size_t>(direction) * new_frames + frame;
            if (old_index < clip.textures.size()) {
                resized[new_index] = clip.textures[old_index];
            }
        }
    }
    clip.direction_count = new_directions;
    clip.frame_count = new_frames;
    clip.textures = std::move(resized);
}

vespera::Vec2 sector_centroid(const vespera::Sector& sector) {
    vespera::Vec2 center{};
    if (sector.vertices.empty()) {
        return center;
    }
    for (const auto& vertex : sector.vertices) {
        center.x += vertex.x;
        center.z += vertex.z;
    }
    const float inv_count = 1.0f / static_cast<float>(sector.vertices.size());
    center.x *= inv_count;
    center.z *= inv_count;
    return center;
}

float floor_height_at(const vespera::Scene& scene, vespera::Vec2 point, float fallback = 0.0f) {
    if (const auto sector_index = scene.world.find_sector_index(point)) {
        return scene.world.sectors()[*sector_index].floor_height;
    }
    return fallback;
}

vespera::Vec2 default_creation_point(const EditorState& state) {
    if (state.selection.kind == SelectionKind::Sector && state.selection.index < state.scene.world.sectors().size()) {
        return sector_centroid(state.scene.world.sectors()[state.selection.index]);
    }
    if (const auto index = selected_entity_index(state)) {
        const auto& entity = state.scene.entities[*index];
        const auto world = editor_world_transform(state, entity);
        return {world.position.x, world.position.z};
    }
    return {state.scene.camera.position.x, state.scene.camera.position.z};
}

bool command_create_sector(EditorState& state) {
    return execute_editor_command(state, vespera::editor::EditorCommandKind::CreateSector, "Create sector", [&]() {
        vespera::Sector sector;
        sector.name = unique_sector_name(state.scene, "Sector");
        sector.floor_height = 0.0f;
        sector.ceiling_height = 3.0f;

        vespera::Vec2 center = snap_world_point(default_creation_point(state), state.scene_view, false);
        if (state.selection.kind == SelectionKind::Sector
            && state.selection.index < state.scene.world.sectors().size()) {
            const auto& source = state.scene.world.sectors()[state.selection.index];
            sector.floor_height = source.floor_height;
            sector.ceiling_height = source.ceiling_height;
            sector.floor_material = source.floor_material;
            sector.ceiling_material = source.ceiling_material;
            sector.wall_material = source.wall_material;

            float max_x = source.vertices.empty() ? center.x : source.vertices.front().x;
            float min_z = source.vertices.empty() ? center.z : source.vertices.front().z;
            float max_z = min_z;
            for (const auto& vertex : source.vertices) {
                max_x = std::max(max_x, vertex.x);
                min_z = std::min(min_z, vertex.z);
                max_z = std::max(max_z, vertex.z);
            }
            const float gap = state.scene_view.snap_enabled ? std::max(state.scene_view.snap_step, 0.05f) : 0.25f;
            center = snap_world_point({max_x + 2.0f + gap, (min_z + max_z) * 0.5f}, state.scene_view, false);
        } else if (!state.scene.world.materials().empty()) {
            sector.floor_material = 0u;
            sector.ceiling_material = 0u;
            sector.wall_material = 0u;
        }

        constexpr float half_size = 2.0f;
        sector.vertices = {
            {center.x - half_size, center.z - half_size},
            {center.x + half_size, center.z - half_size},
            {center.x + half_size, center.z + half_size},
            {center.x - half_size, center.z + half_size},
        };
        sector.sides.resize(sector.vertices.size());
        for (auto& side : sector.sides) {
            side.material = sector.wall_material;
            side.adjacent_sector = -1;
        }

        const std::size_t index = state.scene.world.add_sector(std::move(sector));
        state.selection = {SelectionKind::Sector, index};
        state.scene_view.frame_selection_pending = true;
        return true;
    });
}

bool command_duplicate_selected_sector(EditorState& state) {
    if (state.selection.kind != SelectionKind::Sector
        || state.selection.index >= state.scene.world.sectors().size()) {
        return false;
    }
    const std::size_t source_index = state.selection.index;
    return execute_editor_command(state, vespera::editor::EditorCommandKind::DuplicateSector, "Duplicate sector", [&]() {
        vespera::Sector copy = state.scene.world.sectors()[source_index];
        copy.name = unique_sector_name(state.scene, copy.name + " Copy");

        float min_x = copy.vertices.empty() ? 0.0f : copy.vertices.front().x;
        float max_x = min_x;
        for (const auto& vertex : copy.vertices) {
            min_x = std::min(min_x, vertex.x);
            max_x = std::max(max_x, vertex.x);
        }
        const float gap = state.scene_view.snap_enabled ? std::max(state.scene_view.snap_step, 0.05f) : 0.25f;
        const float offset = std::max(max_x - min_x, 1.0f) + gap;
        for (auto& vertex : copy.vertices) {
            vertex.x = snap_coordinate(vertex.x + offset, state.scene_view, false);
        }
        for (auto& side : copy.sides) {
            // A duplicate must never silently inherit portal links into the old
            // topology. The copied wall materials remain intact.
            side.adjacent_sector = -1;
        }

        const std::size_t index = state.scene.world.add_sector(std::move(copy));
        state.selection = {SelectionKind::Sector, index};
        state.scene_view.frame_selection_pending = true;
        return true;
    });
}

bool command_delete_selected_sector(EditorState& state) {
    if (state.selection.kind != SelectionKind::Sector
        || state.selection.index >= state.scene.world.sectors().size()) {
        return false;
    }
    const std::size_t delete_index = state.selection.index;
    return execute_editor_command(state, vespera::editor::EditorCommandKind::DeleteSector, "Delete sector", [&]() {
        if (!state.scene.world.erase_sector(delete_index)) {
            return false;
        }
        const auto& sectors = state.scene.world.sectors();
        if (sectors.empty()) {
            state.selection = {};
        } else {
            state.selection = {SelectionKind::Sector, std::min(delete_index, sectors.size() - 1)};
        }
        return true;
    });
}

bool command_create_material(EditorState& state) {
    return execute_editor_command(state, vespera::editor::EditorCommandKind::CreateMaterial, "Create material", [&]() {
        vespera::WorldMaterial material;
        material.name = unique_material_name(state.scene, "Material");
        material.color = {1.0f, 1.0f, 1.0f, 1.0f};
        material.texture = state.scene.world.textures().empty() ? vespera::kInvalidTexture : 0u;
        material.uv_scale = {0.5f, 0.5f};
        const auto id = state.scene.world.add_material(std::move(material));
        state.selection = {SelectionKind::Material, static_cast<std::size_t>(id)};
        return true;
    });
}

bool command_duplicate_selected_material(EditorState& state) {
    if (state.selection.kind != SelectionKind::Material
        || state.selection.index >= state.scene.world.materials().size()) {
        return false;
    }
    const std::size_t source_index = state.selection.index;
    return execute_editor_command(state, vespera::editor::EditorCommandKind::DuplicateMaterial, "Duplicate material", [&]() {
        auto copy = state.scene.world.materials()[source_index];
        copy.name = unique_material_name(state.scene, copy.name + " Copy");
        const auto id = state.scene.world.add_material(std::move(copy));
        state.selection = {SelectionKind::Material, static_cast<std::size_t>(id)};
        return true;
    });
}

bool command_delete_selected_material(EditorState& state) {
    if (state.selection.kind != SelectionKind::Material
        || state.selection.index >= state.scene.world.materials().size()) {
        return false;
    }
    const std::size_t delete_index = state.selection.index;
    return execute_editor_command(state, vespera::editor::EditorCommandKind::DeleteMaterial, "Delete material", [&]() {
        if (!state.scene.world.erase_material(delete_index)) {
            return false;
        }
        const auto& materials = state.scene.world.materials();
        if (materials.empty()) {
            state.selection = {};
        } else {
            state.selection = {SelectionKind::Material, std::min(delete_index, materials.size() - 1)};
        }
        return true;
    });
}

bool command_set_sector_portal_target(
    EditorState& state,
    std::size_t sector_index,
    std::size_t side_index,
    int target_sector
) {
    const auto& sectors = state.scene.world.sectors();
    if (sector_index >= sectors.size()
        || side_index >= sectors[sector_index].sides.size()
        || target_sector < -1
        || target_sector >= static_cast<int>(sectors.size())
        || target_sector == static_cast<int>(sector_index)) {
        return false;
    }

    return execute_editor_command(state, vespera::editor::EditorCommandKind::SetPortalTarget, target_sector >= 0 ? "Set portal target" : "Make sector side solid", [&]() {
        // Work on copies so all reciprocal repairs become one undoable edit.
        std::vector<vespera::Sector> edited = state.scene.world.sectors();
        auto& source = edited[sector_index];
        const int previous_target = source.sides[side_index].adjacent_sector;

        if (previous_target >= 0 && previous_target < static_cast<int>(edited.size())) {
            if (const auto old_match = vespera::find_matching_sector_side(
                    state.scene.world, sector_index, side_index, static_cast<std::size_t>(previous_target))) {
                auto& old_target = edited[static_cast<std::size_t>(previous_target)];
                if (*old_match < old_target.sides.size()
                    && old_target.sides[*old_match].adjacent_sector == static_cast<int>(sector_index)) {
                    old_target.sides[*old_match].adjacent_sector = -1;
                }
            }
        }

        source.sides[side_index].adjacent_sector = target_sector;
        int displaced_target = -1;
        if (target_sector >= 0) {
            if (const auto match = vespera::find_matching_sector_side(
                    state.scene.world, sector_index, side_index, static_cast<std::size_t>(target_sector))) {
                auto& target = edited[static_cast<std::size_t>(target_sector)];
                if (*match < target.sides.size()) {
                    displaced_target = target.sides[*match].adjacent_sector;
                    if (displaced_target >= 0
                        && displaced_target < static_cast<int>(edited.size())
                        && displaced_target != static_cast<int>(sector_index)) {
                        if (const auto displaced_match = vespera::find_matching_sector_side(
                                state.scene.world,
                                static_cast<std::size_t>(target_sector),
                                *match,
                                static_cast<std::size_t>(displaced_target))) {
                            auto& displaced = edited[static_cast<std::size_t>(displaced_target)];
                            if (*displaced_match < displaced.sides.size()
                                && displaced.sides[*displaced_match].adjacent_sector == target_sector) {
                                displaced.sides[*displaced_match].adjacent_sector = -1;
                            }
                        }
                    }
                    target.sides[*match].adjacent_sector = static_cast<int>(sector_index);
                }
            }
        }

        bool changed = false;
        for (std::size_t i = 0; i < edited.size(); ++i) {
            if (i == sector_index
                || static_cast<int>(i) == previous_target
                || static_cast<int>(i) == target_sector
                || static_cast<int>(i) == displaced_target) {
                changed |= state.scene.world.set_sector(i, std::move(edited[i]));
            }
        }
        state.selection = {SelectionKind::Sector, sector_index};
        state.selection.side_index = side_index;
        return changed;
    });
}

bool command_create_entity(EditorState& state, std::string_view requested_name = {}) {
    return execute_editor_command(state, vespera::editor::EditorCommandKind::CreateEntity, "Create entity", [&]() {
        const vespera::Vec2 point = snap_world_point(default_creation_point(state), state.scene_view, false);
        const std::string base_name = requested_name.empty() ? "Entity" : std::string(requested_name);
        auto& entity = state.scene.create_entity(unique_entity_name(state.scene, base_name));
        entity.transform.position = {point.x, floor_height_at(state.scene, point), point.z};
        select_entity(state, state.scene.entities.size() - 1);
        state.scene_view.frame_selection_pending = true;
        return true;
    });
}

bool command_create_sprite_entity(EditorState& state, std::string_view requested_name = {}) {
    return execute_editor_command(state, vespera::editor::EditorCommandKind::CreateSpriteEntity, "Create sprite entity", [&]() {
        const vespera::Vec2 point = snap_world_point(default_creation_point(state), state.scene_view, false);
        const std::string base_name = requested_name.empty() ? "Sprite Entity" : std::string(requested_name);
        auto& entity = state.scene.create_entity(unique_entity_name(state.scene, base_name));
        entity.transform.position = {point.x, floor_height_at(state.scene, point), point.z};
        auto& sprite = entity.add_sprite_renderer();
        sprite.size = {1.0f, 1.5f};
        sprite.texture = default_sprite_texture(state.scene);
        sprite.color = {1.0f, 1.0f, 1.0f, 1.0f};
        select_entity(state, state.scene.entities.size() - 1);
        state.scene_view.frame_selection_pending = true;
        return true;
    });
}

bool command_create_primitive_entity(EditorState& state, vespera::PrimitiveMeshType primitive) {
    using vespera::editor::EditorCommandKind;
    EditorCommandKind kind = EditorCommandKind::CreateCubeEntity;
    std::string base_name = "Cube";
    switch (primitive) {
        case vespera::PrimitiveMeshType::Cube: kind=EditorCommandKind::CreateCubeEntity; base_name="Cube"; break;
        case vespera::PrimitiveMeshType::Plane: kind=EditorCommandKind::CreatePlaneEntity; base_name="Plane"; break;
        case vespera::PrimitiveMeshType::Cylinder: kind=EditorCommandKind::CreateCylinderEntity; base_name="Cylinder"; break;
        case vespera::PrimitiveMeshType::Sphere: kind=EditorCommandKind::CreateSphereEntity; base_name="Sphere"; break;
    }
    return execute_editor_command(state, kind, "Create " + base_name, [&]() {
        const vespera::Vec2 point = snap_world_point(default_creation_point(state), state.scene_view, false);
        auto& entity = state.scene.create_entity(unique_entity_name(state.scene, base_name));
        const float floor = floor_height_at(state.scene, point);
        entity.transform.position = {point.x, floor + (primitive == vespera::PrimitiveMeshType::Plane ? 0.01f : 0.5f), point.z};
        auto& mesh = entity.add_mesh_renderer(); mesh.primitive = primitive; mesh.texture = vespera::kInvalidTexture;
        select_entity(state, state.scene.entities.size() - 1); state.scene_view.frame_selection_pending = true;
        return true;
    });
}

bool command_create_trigger_entity(EditorState& state, std::string_view requested_name = {}) {
    return execute_editor_command(state, vespera::editor::EditorCommandKind::CreateTriggerEntity, "Create trigger entity", [&]() {
        const vespera::Vec2 point = snap_world_point(default_creation_point(state), state.scene_view, false);
        const std::string base_name = requested_name.empty() ? "Trigger Volume" : std::string(requested_name);
        auto& entity = state.scene.create_entity(unique_entity_name(state.scene, base_name));
        entity.tag = "trigger";
        entity.layer = "Triggers";
        entity.transform.position = {point.x, floor_height_at(state.scene, point), point.z};
        auto& collider = entity.add_cylinder_collider();
        collider.radius = 1.0f;
        collider.height = 2.0f;
        collider.center = {0.0f, 1.0f, 0.0f};
        collider.is_trigger = true;
        select_entity(state, state.scene.entities.size() - 1);
        state.scene_view.frame_selection_pending = true;
        return true;
    });
}

bool command_create_point_light_entity(EditorState& state, std::string_view requested_name = {}) {
    return execute_editor_command(state, vespera::editor::EditorCommandKind::CreatePointLightEntity, "Create point light entity", [&]() {
        const vespera::Vec2 point = snap_world_point(default_creation_point(state), state.scene_view, false);
        const std::string base_name = requested_name.empty() ? "Point Light" : std::string(requested_name);
        auto& entity = state.scene.create_entity(unique_entity_name(state.scene, base_name));
        entity.tag = "light";
        entity.layer = "Lighting";
        entity.transform.position = {point.x, floor_height_at(state.scene, point) + 1.8f, point.z};
        auto& light = entity.add_point_light();
        light.color = {1.0f, 0.72f, 0.42f, 1.0f};
        light.intensity = 1.35f;
        light.radius = 5.0f;
        select_entity(state, state.scene.entities.size() - 1);
        state.scene_view.frame_selection_pending = true;
        return true;
    });
}

bool command_reorder_entity(EditorState& state, vespera::SceneObjectId dragged_id, vespera::SceneObjectId target_id) {
    if (dragged_id == target_id) return false;
    const auto source_index = entity_index_from_id(state, dragged_id);
    const auto target_index = entity_index_from_id(state, target_id);
    if (!source_index || !target_index) return false;
    return execute_editor_command(state, vespera::editor::EditorCommandKind::ReorderEntity, "Reorder entity", [&]() {
        auto source = entity_index_from_id(state, dragged_id);
        auto target = entity_index_from_id(state, target_id);
        if (!source || !target || *source == *target) return false;
        auto moved = std::move(state.scene.entities[*source]);
        state.scene.entities.erase(state.scene.entities.begin() + static_cast<std::ptrdiff_t>(*source));
        std::size_t insertion = *target;
        if (*source < *target && insertion > 0) --insertion;
        insertion = std::min(insertion, state.scene.entities.size());
        state.scene.entities.insert(
            state.scene.entities.begin() + static_cast<std::ptrdiff_t>(insertion),
            std::move(moved)
        );
        repair_selection(state);
        return true;
    });
}

bool command_reparent_entity(EditorState& state, vespera::SceneObjectId child_id, vespera::SceneObjectId parent_id) {
    const bool unparent = parent_id == vespera::kInvalidSceneObjectId;
    return execute_editor_command(
        state,
        unparent ? vespera::editor::EditorCommandKind::UnparentEntity : vespera::editor::EditorCommandKind::ReparentEntity,
        unparent ? "Unparent entity" : "Reparent entity",
        [&]() {
            std::string error;
            if (!vespera::reparent_scene_entity(state.scene, child_id, parent_id, true, &error)) {
                if (!error.empty()) push_console(state, ConsoleEntry::Level::Warning, "Hierarchy: " + error);
                return false;
            }
            // Keep a newly parented subtree adjacent to its parent in authoring
            // order so the flat ImGui Hierarchy still reads like a real tree.
            if (parent_id != vespera::kInvalidSceneObjectId) {
                std::vector<vespera::Entity> subtree;
                std::vector<vespera::Entity> remaining;
                subtree.reserve(state.scene.entities.size());
                remaining.reserve(state.scene.entities.size());
                for (auto& candidate : state.scene.entities) {
                    if (candidate.id == child_id || vespera::scene_entity_is_descendant_of(state.scene, candidate.id, child_id)) {
                        subtree.push_back(std::move(candidate));
                    } else {
                        remaining.push_back(std::move(candidate));
                    }
                }
                const auto parent_it = std::find_if(remaining.begin(), remaining.end(), [parent_id](const vespera::Entity& candidate) {
                    return candidate.id == parent_id;
                });
                if (parent_it != remaining.end()) {
                    const auto insertion = static_cast<std::size_t>(std::distance(remaining.begin(), parent_it)) + 1u;
                    remaining.insert(
                        remaining.begin() + static_cast<std::ptrdiff_t>(insertion),
                        std::make_move_iterator(subtree.begin()),
                        std::make_move_iterator(subtree.end()));
                    state.scene.entities = std::move(remaining);
                }
            }
            repair_selection(state);
            return true;
        });
}

bool command_duplicate_selected_entity(EditorState& state) {
    const auto selected = selected_entity_index(state);
    if (!selected) return false;
    const std::size_t source_index = *selected;
    return execute_editor_command(state, vespera::editor::EditorCommandKind::DuplicateEntity, "Duplicate entity", [&]() {
        const auto source_id = state.scene.entities[source_index].id;
        const std::string copy_name = unique_entity_name(state.scene, state.scene.entities[source_index].name + " Copy");
        vespera::Entity* copy = state.scene.clone_entity(source_id, copy_name);
        if (!copy) return false;
        const float offset = state.scene_view.snap_enabled ? std::max(state.scene_view.snap_step, 0.05f) : 0.25f;
        copy->transform.position.x += offset;
        copy->transform.position.z += offset;
        copy->transform.position.y = floor_height_at(
            state.scene,
            {copy->transform.position.x, copy->transform.position.z},
            copy->transform.position.y);
        select_entity(state, state.scene.entities.size() - 1);
        state.scene_view.frame_selection_pending = true;
        return true;
    });
}

bool command_delete_selected_entity(EditorState& state) {
    const auto selected = selected_entity_index(state);
    if (!selected) return false;
    const std::size_t delete_index = *selected;
    return execute_editor_command(state, vespera::editor::EditorCommandKind::DeleteEntity, "Delete entity", [&]() {
        const auto delete_id = state.scene.entities[delete_index].id;
        if (!state.scene.destroy_entity(delete_id)) return false;
        if (state.scene.entities.empty()) state.selection = {};
        else select_entity(state, std::min(delete_index, state.scene.entities.size() - 1));
        return true;
    });
}

std::string sanitize_asset_stem(std::string value) {
    for (char& ch : value) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (!std::isalnum(uch) && ch != '-' && ch != '_') ch = '_';
    }
    while (!value.empty() && value.front() == '_') value.erase(value.begin());
    while (!value.empty() && value.back() == '_') value.pop_back();
    if (value.empty()) value = "Prefab";
    return value;
}

std::optional<std::string> project_relative_asset_key(
    const EditorState& state,
    const std::filesystem::path& absolute_path
) {
    if (state.assets_root.empty()) return std::nullopt;
    std::error_code ec;
    const auto relative = std::filesystem::relative(absolute_path, state.assets_root, ec).lexically_normal();
    if (ec || relative.empty() || relative.is_absolute()) return std::nullopt;
    for (const auto& part : relative) {
        if (part == "..") return std::nullopt;
    }
    return relative.generic_string();
}

std::optional<std::filesystem::path> resolve_prefab_source_path(
    const EditorState& state,
    const vespera::AssetReference& source
) {
    if (state.assets_root.empty() || source.empty()) return std::nullopt;
    const auto resolved = state.asset_catalog.resolve_reference(source);
    if (resolved) return resolved.record->absolute_path.lexically_normal();
    const std::filesystem::path relative(source.path);
    if (relative.empty() || relative.is_absolute()) return std::nullopt;
    for (const auto& part : relative) {
        if (part == "..") return std::nullopt;
    }
    return (state.assets_root / relative).lexically_normal();
}

std::optional<vespera::AssetReference> project_asset_reference(
    const EditorState& state,
    const std::filesystem::path& absolute_path
) {
    const auto relative = project_relative_asset_key(state, absolute_path);
    if (!relative) return std::nullopt;
    vespera::AssetReference reference;
    reference.path = *relative;
    if (const auto* record = state.asset_catalog.find(*relative)) reference.asset_id = record->asset_id;
    return reference;
}

std::size_t hydrate_scene_asset_references(EditorState& state) {
    std::size_t changed = 0;
    for (auto& entity : state.scene.entities) {
        if (!entity.prefab_source.empty()) {
            const auto resolved = state.asset_catalog.resolve_reference(entity.prefab_source);
            if (resolved) {
                const auto current_path = resolved.record->relative_path.lexically_normal();
                if (entity.prefab_source.asset_id != resolved.record->asset_id
                    || entity.prefab_source.path.lexically_normal() != current_path) {
                    entity.prefab_source.asset_id = resolved.record->asset_id;
                    entity.prefab_source.path = current_path;
                    ++changed;
                }
            }
        }
        if (entity.mesh_renderer && !entity.mesh_renderer->material.empty()) {
            auto& reference = entity.mesh_renderer->material;
            const auto resolved = state.asset_catalog.resolve_reference(reference);
            if (resolved && resolved.record->kind == vespera::AssetKind::Material) {
                const auto current_path = resolved.record->relative_path.lexically_normal();
                if (reference.asset_id != resolved.record->asset_id || reference.path.lexically_normal() != current_path) {
                    reference.asset_id = resolved.record->asset_id;
                    reference.path = current_path;
                    ++changed;
                }
            }
        }
    }
    return changed;
}

std::filesystem::path unique_prefab_asset_path(const EditorState& state, std::string_view entity_name) {
    const auto directory = state.assets_root / "prefabs";
    const std::string stem = sanitize_asset_stem(std::string(entity_name));
    std::filesystem::path candidate = directory / (stem + ".slprefab");
    std::error_code ec;
    if (!std::filesystem::exists(candidate, ec)) return candidate;
    for (int suffix = 2; suffix < 10000; ++suffix) {
        candidate = directory / std::format("{}_{}.slprefab", stem, suffix);
        ec.clear();
        if (!std::filesystem::exists(candidate, ec)) return candidate;
    }
    return directory / (stem + "_Unique.slprefab");
}

bool save_prefab_verified(EditorState& state, const vespera::Entity& entity, const std::filesystem::path& path) {
    // A single-Entity prefab has one root. Editor-authored prefab assets store
    // that root at local origin; scene placement remains an instance concern.
    vespera::Entity prefab_entity = entity;
    prefab_entity.transform.position = {};

    std::filesystem::path temporary = path;
    temporary += ".sectorline-tmp";
    std::filesystem::path backup = path;
    backup += ".bak";

    std::error_code ec;
    std::filesystem::remove(temporary, ec);

    const auto result = vespera::save_entity_prefab(state.scene, prefab_entity, temporary);
    if (!result) {
        push_console(state, ConsoleEntry::Level::Error, "Prefab save failed: " + result.message);
        return false;
    }

    vespera::EntityPrefab verification;
    const auto verify = vespera::load_entity_prefab(state.scene, verification, temporary);
    if (!verify) {
        std::filesystem::remove(temporary, ec);
        push_console(state, ConsoleEntry::Level::Error, "Prefab verification failed: " + verify.message);
        return false;
    }

    ec.clear();
    const bool had_original = std::filesystem::exists(path, ec) && !ec;
    if (had_original) {
        ec.clear();
        std::filesystem::remove(backup, ec);
        ec.clear();
        std::filesystem::rename(path, backup, ec);
        if (ec) {
            const std::string message = ec.message();
            std::filesystem::remove(temporary, ec);
            push_console(state, ConsoleEntry::Level::Error, "Prefab save failed while creating backup: " + message);
            return false;
        }
    }

    ec.clear();
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        const std::string message = ec.message();
        if (had_original) {
            std::error_code rollback_error;
            std::filesystem::rename(backup, path, rollback_error);
        }
        std::filesystem::remove(temporary, ec);
        push_console(state, ConsoleEntry::Level::Error, "Prefab save failed while installing verified asset: " + message);
        return false;
    }

    push_console(state, ConsoleEntry::Level::Info, "Saved + verified prefab: " + path.string());
    return true;
}

bool command_create_prefab_from_selected(EditorState& state) {
    const auto selected = selected_entity_index(state);
    if (!selected || state.assets_root.empty()) return false;
    const std::size_t entity_index = *selected;
    const auto prefab_path = unique_prefab_asset_path(state, state.scene.entities[entity_index].name);
    if (!save_prefab_verified(state, state.scene.entities[entity_index], prefab_path)) return false;
    refresh_asset_catalog(state, true);
    const auto source = project_asset_reference(state, prefab_path);
    if (!source) {
        push_console(state, ConsoleEntry::Level::Warning, "Prefab was saved, but its project asset reference could not be resolved.");
        return false;
    }

    return execute_editor_command(state, vespera::editor::EditorCommandKind::CreatePrefab, "Link entity to new prefab", [&]() {
        if (entity_index >= state.scene.entities.size()) return false;
        state.scene.entities[entity_index].prefab_source = *source;
        select_entity(state, entity_index);
        return true;
    });
}

bool command_apply_selected_to_prefab(EditorState& state) {
    const auto selected = selected_entity_index(state);
    if (!selected) return false;
    const auto& entity = state.scene.entities[*selected];
    const auto path = resolve_prefab_source_path(state, entity.prefab_source);
    if (!path) {
        push_console(state, ConsoleEntry::Level::Error, "Apply Prefab failed: entity has no valid project-relative prefab source.");
        return false;
    }
    const bool saved = save_prefab_verified(state, entity, *path);
    append_command_audit(state, vespera::editor::EditorCommandKind::ApplyPrefab,
        "Apply prefab", saved, state.current_state_id, state.current_state_id,
        entity.id, entity.prefab_source.asset_id);
    if (saved) {
        refresh_asset_catalog(state);
        push_console(state, ConsoleEntry::Level::Info, "Applied entity to prefab source: " + path->generic_string());
    }
    return saved;
}

bool command_revert_selected_from_prefab(EditorState& state) {
    const auto selected = selected_entity_index(state);
    if (!selected) return false;
    const std::size_t entity_index = *selected;
    const auto source = state.scene.entities[entity_index].prefab_source;
    const auto path = resolve_prefab_source_path(state, source);
    if (!path) {
        push_console(state, ConsoleEntry::Level::Error, "Revert Prefab failed: entity has no valid project-relative prefab source.");
        return false;
    }

    vespera::EntityPrefab prefab;
    const auto load = vespera::load_entity_prefab(state.scene, prefab, *path);
    if (!load) {
        push_console(state, ConsoleEntry::Level::Error, "Revert Prefab failed: " + load.message);
        return false;
    }

    return execute_editor_command(state, vespera::editor::EditorCommandKind::RevertPrefab, "Revert prefab instance", [&]() {
        if (entity_index >= state.scene.entities.size()) return false;
        const auto id = state.scene.entities[entity_index].id;
        const std::string instance_name = state.scene.entities[entity_index].name;
        const auto instance_parent = state.scene.entities[entity_index].parent_id;
        const vespera::Vec3 instance_position = state.scene.entities[entity_index].transform.position;
        vespera::Entity replacement = prefab.prototype;
        replacement.id = id;
        replacement.name = instance_name;
        replacement.parent_id = instance_parent;
        replacement.prefab_source = source;
        replacement.transform.position = instance_position;
        state.scene.entities[entity_index] = std::move(replacement);
        (void)vespera::hydrate_scene_materials(state.scene, state.asset_catalog);
        select_entity(state, entity_index);
        state.scene_view.frame_selection_pending = true;
        return true;
    });
}

bool command_unpack_selected_prefab(EditorState& state) {
    const auto selected = selected_entity_index(state);
    if (!selected || state.scene.entities[*selected].prefab_source.empty()) return false;
    const std::size_t entity_index = *selected;
    return execute_editor_command(state, vespera::editor::EditorCommandKind::UnpackPrefab, "Unpack prefab instance", [&]() {
        if (entity_index >= state.scene.entities.size()) return false;
        state.scene.entities[entity_index].prefab_source = {};
        select_entity(state, entity_index);
        return true;
    });
}

bool command_instantiate_prefab(
    EditorState& state,
    const std::filesystem::path& prefab_path,
    std::optional<vespera::Vec3> world_position = std::nullopt,
    vespera::editor::EditorCommandKind command_kind = vespera::editor::EditorCommandKind::InstantiatePrefab
) {
    vespera::EntityPrefab prefab;
    const auto load = vespera::load_entity_prefab(state.scene, prefab, prefab_path);
    if (!load) {
        push_console(state, ConsoleEntry::Level::Error, "Instantiate Prefab failed: " + load.message);
        return false;
    }
    const auto source = project_asset_reference(state, prefab_path);
    if (!source) {
        push_console(state, ConsoleEntry::Level::Error, "Instantiate Prefab failed: asset is outside the current assets root.");
        return false;
    }

    const std::string label = command_kind == vespera::editor::EditorCommandKind::DropPrefabIntoScene
        ? "Drop prefab into Scene" : "Instantiate prefab";
    return execute_editor_command(state, command_kind, label, [&]() {
        const std::string name = unique_entity_name(state.scene, prefab.prototype.name);
        vespera::Entity* entity = vespera::instantiate_entity_prefab(state.scene, prefab, name, *source);
        if (!entity) return false;
        if (world_position) {
            entity->transform.position = *world_position;
        } else {
            const vespera::Vec2 point = snap_world_point(default_creation_point(state), state.scene_view, false);
            entity->transform.position = {point.x, floor_height_at(state.scene, point), point.z};
        }
        (void)vespera::hydrate_scene_materials(state.scene, state.asset_catalog);
        select_entity(state, state.scene.entities.size() - 1);
        state.scene_view.frame_selection_pending = true;
        state.scene_view_3d.frame_selection_pending = true;
        return true;
    });
}

std::string canonical_editor_asset_name(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (unsigned char c : value) {
        if (c >= 'A' && c <= 'Z') c = static_cast<unsigned char>(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) result.push_back(static_cast<char>(c));
    }
    return result;
}

vespera::TextureId scene_texture_for_asset(
    const EditorState& state,
    const vespera::AssetRecord& record
) {
    if (record.kind != vespera::AssetKind::Texture) return vespera::kInvalidTexture;
    const std::string wanted = canonical_editor_asset_name(record.display_name);
    const auto& textures = state.scene.world.textures();
    for (std::size_t i = 0; i < textures.size(); ++i) {
        if (canonical_editor_asset_name(textures[i].name) == wanted) {
            return static_cast<vespera::TextureId>(i);
        }
    }
    return vespera::kInvalidTexture;
}

const vespera::AssetRecord* asset_record_from_payload(EditorState& state, const ImGuiPayload* payload) {
    if (!payload || !payload->Data || payload->DataSize <= 1) return nullptr;
    const char* value = static_cast<const char*>(payload->Data);
    if (value[payload->DataSize - 1] != '\0') return nullptr;
    return state.asset_catalog.find_by_id(value);
}

void begin_project_asset_drag(const vespera::AssetRecord& record) {
    if (!ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) return;
    ImGui::SetDragDropPayload("VESPERA_PROJECT_ASSET", record.asset_id.c_str(), record.asset_id.size() + 1);
    ImGui::TextUnformatted(record.display_name.c_str());
    ImGui::TextDisabled("%s", vespera::asset_kind_name(record.kind).data());
    if (record.kind == vespera::AssetKind::EntityPrefab) ImGui::TextDisabled("Drop into Scene to instantiate");
    else if (record.kind == vespera::AssetKind::Texture) ImGui::TextDisabled("Drop onto a Texture field in Inspector");
    else if (record.kind == vespera::AssetKind::Material) ImGui::TextDisabled("Drop onto a Mesh Renderer Material field");
    else if (record.kind == vespera::AssetKind::Font) ImGui::TextDisabled("Drop onto a runtime UI Font field");
    ImGui::EndDragDropSource();
}

bool accept_texture_asset_drop(EditorState& state, vespera::TextureId& texture, const char* field_name) {
    if (!ImGui::BeginDragDropTarget()) return false;
    bool changed = false;
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VESPERA_PROJECT_ASSET")) {
        if (const auto* record = asset_record_from_payload(state, payload); record && record->kind == vespera::AssetKind::Texture) {
            const auto resolved = scene_texture_for_asset(state, *record);
            if (resolved != vespera::kInvalidTexture) {
                texture = resolved;
                changed = true;
                append_command_audit(state, vespera::editor::EditorCommandKind::AssignTextureAsset,
                    std::string("Assign texture asset to ") + field_name, true,
                    state.current_state_id, state.current_state_id,
                    state.selection.object_id, record->asset_id);
            } else {
                push_console(state, ConsoleEntry::Level::Warning,
                    "Texture drop ignored: asset is not registered as a texture resource in the open scene: "
                    + record->relative_path.generic_string());
            }
        }
    }
    ImGui::EndDragDropTarget();
    return changed;
}

std::filesystem::path unique_material_asset_path(const EditorState& state) {
    std::filesystem::path folder = state.asset_browser_folder;
    if (folder.empty()) folder = "materials";
    const auto directory = state.assets_root / folder;
    std::filesystem::path candidate = directory / "new_material.slmat";
    std::error_code ec;
    if (!std::filesystem::exists(candidate, ec)) return candidate;
    for (int suffix = 2; suffix < 10000; ++suffix) {
        candidate = directory / std::format("new_material_{}.slmat", suffix);
        ec.clear();
        if (!std::filesystem::exists(candidate, ec)) return candidate;
    }
    return directory / "new_material_unique.slmat";
}

bool command_create_material_asset(EditorState& state) {
    if (state.assets_root.empty()) {
        push_console(state, ConsoleEntry::Level::Warning, "Create Material Asset requires an open project/assets root.");
        return false;
    }
    const auto path = unique_material_asset_path(state);
    vespera::MaterialAsset material;
    material.name = path.stem().string();
    material.properties.shader = vespera::BuiltinMaterialShader::Lit;
    const auto saved = vespera::save_material_asset(path, material);
    if (!saved) {
        push_console(state, ConsoleEntry::Level::Error, "Material asset creation failed: " + saved.message);
        append_command_audit(state, vespera::editor::EditorCommandKind::CreateMaterialAsset,
            "Create Material Asset", false, state.current_state_id, state.current_state_id);
        return false;
    }
    refresh_asset_catalog(state, true, true);
    const auto relative = std::filesystem::relative(path, state.assets_root).lexically_normal();
    if (const auto* record = state.asset_catalog.find(relative.generic_string())) {
        state.selection = {SelectionKind::Asset, 0, kNoSubSelection, vespera::kInvalidSceneObjectId, kNoSubSelection, record->asset_id};
        state.material_asset_edit = {};
        append_command_audit(state, vespera::editor::EditorCommandKind::CreateMaterialAsset,
            "Create Material Asset", true, state.current_state_id, state.current_state_id,
            vespera::kInvalidSceneObjectId, record->asset_id);
    }
    state.asset_browser_folder = relative.parent_path();
    push_console(state, ConsoleEntry::Level::Info, "Created Material asset: " + relative.generic_string());
    return true;
}

bool accept_material_asset_drop(EditorState& state, vespera::MeshRendererComponent& mesh) {
    if (!ImGui::BeginDragDropTarget()) return false;
    bool changed = false;
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VESPERA_PROJECT_ASSET")) {
        if (const auto* record = asset_record_from_payload(state, payload); record && record->kind == vespera::AssetKind::Material) {
            mesh.material = {record->asset_id, record->relative_path};
            mesh.material_resolved = false;
            (void)vespera::hydrate_scene_materials(state.scene, state.asset_catalog);
            append_command_audit(state, vespera::editor::EditorCommandKind::AssignMaterialAsset,
                "Assign Material asset", true, state.current_state_id, state.current_state_id,
                state.selection.object_id, record->asset_id);
            changed = true;
        }
    }
    ImGui::EndDragDropTarget();
    return changed;
}

bool command_create_clip(EditorState& state) {
    return execute_editor_command(state, vespera::editor::EditorCommandKind::CreateSpriteClip, "Create sprite clip", [&]() {
        vespera::SpriteAnimationClip clip;
        clip.name = unique_clip_name(state.scene, "Sprite Clip");
        clip.direction_count = 1;
        clip.frame_count = 1;
        clip.frames_per_second = 4.0f;
        clip.loop = true;
        clip.textures = {default_sprite_texture(state.scene)};
        state.scene.sprite_clips.push_back(std::move(clip));
        state.selection = {SelectionKind::SpriteClip, state.scene.sprite_clips.size() - 1};
        return true;
    });
}

bool command_duplicate_selected_clip(EditorState& state) {
    if (state.selection.kind != SelectionKind::SpriteClip || state.selection.index >= state.scene.sprite_clips.size()) {
        return false;
    }
    const std::size_t source = state.selection.index;
    return execute_editor_command(state, vespera::editor::EditorCommandKind::DuplicateSpriteClip, "Duplicate sprite clip", [&]() {
        auto copy = state.scene.sprite_clips[source];
        copy.name = unique_clip_name(state.scene, copy.name + " Copy");
        state.scene.sprite_clips.push_back(std::move(copy));
        state.selection = {SelectionKind::SpriteClip, state.scene.sprite_clips.size() - 1};
        return true;
    });
}

bool command_delete_selected_clip(EditorState& state) {
    if (state.selection.kind != SelectionKind::SpriteClip || state.selection.index >= state.scene.sprite_clips.size()) {
        return false;
    }
    const std::size_t delete_index = state.selection.index;
    const std::string deleted_name = state.scene.sprite_clips[delete_index].name;
    return execute_editor_command(state, vespera::editor::EditorCommandKind::DeleteSpriteClip, "Delete sprite clip", [&]() {
        for (auto& entity : state.scene.entities) {
            if (entity.sprite_renderer && entity.sprite_renderer->animation_clip == deleted_name) {
                entity.sprite_renderer->animation_clip.clear();
            }
        }
        state.scene.sprite_clips.erase(state.scene.sprite_clips.begin() + static_cast<std::ptrdiff_t>(delete_index));
        if (state.scene.sprite_clips.empty()) {
            state.selection = {};
        } else {
            state.selection = {SelectionKind::SpriteClip, std::min(delete_index, state.scene.sprite_clips.size() - 1)};
        }
        return true;
    });
}

bool command_duplicate_selected_entities(EditorState& state) {
    if (state.selection.kind != SelectionKind::Entity || state.selected_entity_ids.size() < 2) return false;
    const auto source_ids = state.selected_entity_ids;
    return execute_editor_command(state, vespera::editor::EditorCommandKind::DuplicateEntity, "Duplicate entities", [&]() {
        std::vector<vespera::SceneObjectId> copies;
        std::unordered_map<vespera::SceneObjectId, vespera::SceneObjectId> duplicate_ids;
        const float offset = state.scene_view.snap_enabled ? std::max(state.scene_view.snap_step, 0.05f) : 0.25f;
        for (const auto source_id : source_ids) {
            const auto source_index = entity_index_from_id(state, source_id);
            if (!source_index) continue;
            const auto source_name = state.scene.entities[*source_index].name;
            vespera::Entity* copy = state.scene.clone_entity(source_id, unique_entity_name(state.scene, source_name + " Copy"));
            if (!copy) continue;
            duplicate_ids[source_id] = copy->id;
            copies.push_back(copy->id);
        }
        for (const auto source_id : source_ids) {
            const auto copy_found = duplicate_ids.find(source_id);
            if (copy_found == duplicate_ids.end()) continue;
            auto* copy = state.scene.find_entity(copy_found->second);
            const auto* source = state.scene.find_entity(source_id);
            if (!copy || !source) continue;
            if (const auto parent_copy = duplicate_ids.find(source->parent_id); parent_copy != duplicate_ids.end()) {
                copy->parent_id = parent_copy->second;
            } else {
                auto world = editor_world_transform(state, *copy);
                world.position.x += offset;
                world.position.z += offset;
                set_editor_world_transform(state, copy->id, world);
            }
        }
        if (copies.empty()) return false;
        state.selected_entity_ids = copies;
        const auto primary = entity_index_from_id(state, copies.back());
        if (primary) state.selection = {SelectionKind::Entity, *primary, kNoSubSelection, copies.back()};
        state.hierarchy_anchor_id = copies.front();
        state.scene_view.frame_selection_pending = true;
        return true;
    });
}

bool command_delete_selected_entities(EditorState& state) {
    if (state.selection.kind != SelectionKind::Entity || state.selected_entity_ids.size() < 2) return false;
    const auto delete_ids = state.selected_entity_ids;
    return execute_editor_command(state, vespera::editor::EditorCommandKind::DeleteEntity, "Delete entities", [&]() {
        bool removed = false;
        for (const auto id : delete_ids) removed = state.scene.destroy_entity(id) || removed;
        state.selected_entity_ids.clear();
        state.hierarchy_anchor_id = vespera::kInvalidSceneObjectId;
        if (state.scene.entities.empty()) state.selection = {};
        else select_entity(state, std::min<std::size_t>(state.selection.index, state.scene.entities.size() - 1));
        return removed;
    });
}

bool command_duplicate_selection(EditorState& state) {
    if (state.selection.kind == SelectionKind::Sector) return command_duplicate_selected_sector(state);
    if (state.selection.kind == SelectionKind::Entity) {
        if (state.selected_entity_ids.size() > 1) return command_duplicate_selected_entities(state);
        return command_duplicate_selected_entity(state);
    }
    if (state.selection.kind == SelectionKind::Material) return command_duplicate_selected_material(state);
    if (state.selection.kind == SelectionKind::SpriteClip) return command_duplicate_selected_clip(state);
    return false;
}

bool command_delete_selection(EditorState& state) {
    if (state.selection.kind == SelectionKind::Sector) return command_delete_selected_sector(state);
    if (state.selection.kind == SelectionKind::Entity) {
        if (state.selected_entity_ids.size() > 1) return command_delete_selected_entities(state);
        return command_delete_selected_entity(state);
    }
    if (state.selection.kind == SelectionKind::Material) return command_delete_selected_material(state);
    if (state.selection.kind == SelectionKind::SpriteClip) return command_delete_selected_clip(state);
    return false;
}

void validate_scene_to_console(EditorState& state) {
    const auto issues = vespera::validate_scene(state.scene);
    if (issues.empty()) {
        push_console(state, ConsoleEntry::Level::Info, "Scene validation passed with no issues.");
        return;
    }
    int errors = 0;
    int warnings = 0;
    for (const auto& issue : issues) {
        if (issue.severity == vespera::SceneValidationSeverity::Error) {
            ++errors;
            push_console(state, ConsoleEntry::Level::Error, issue.message);
        } else {
            ++warnings;
            push_console(state, ConsoleEntry::Level::Warning, issue.message);
        }
    }
    push_console(state, errors > 0 ? ConsoleEntry::Level::Error : ConsoleEntry::Level::Warning,
        std::format("Scene validation: {} error(s), {} warning(s).", errors, warnings));
}

bool undo(EditorState& state) {
    commit_active_edit(state);
    if (state.undo_stack.empty()) {
        return false;
    }
    HistorySnapshot previous = std::move(state.undo_stack.back());
    state.undo_stack.pop_back();
    HistorySnapshot current = capture_snapshot(state, previous.label);
    state.redo_stack.push_back(std::move(current));
    state.scene = std::move(previous.scene);
    state.selection = previous.selection;
    state.selected_entity_ids = std::move(previous.selected_entity_ids);
    repair_selection(state);
    state.current_state_id = previous.state_id;
    refresh_dirty(state);
    push_console(state, ConsoleEntry::Level::Info, "Undo: " + previous.label);
    return true;
}

bool redo(EditorState& state) {
    commit_active_edit(state);
    if (state.redo_stack.empty()) {
        return false;
    }
    HistorySnapshot next = std::move(state.redo_stack.back());
    state.redo_stack.pop_back();
    HistorySnapshot current = capture_snapshot(state, next.label);
    state.undo_stack.push_back(std::move(current));
    state.scene = std::move(next.scene);
    state.selection = next.selection;
    state.selected_entity_ids = std::move(next.selected_entity_ids);
    repair_selection(state);
    state.current_state_id = next.state_id;
    refresh_dirty(state);
    push_console(state, ConsoleEntry::Level::Info, "Redo: " + next.label);
    return true;
}

void reset_history_after_open(EditorState& state) {
    clear_active_edit(state);
    state.undo_stack.clear();
    state.redo_stack.clear();
    state.current_state_id = state.next_state_id++;
    state.saved_state_id = state.current_state_id;
    refresh_dirty(state);
}

bool open_scene(EditorState& state, const std::filesystem::path& path);

bool open_project(EditorState& state, const std::filesystem::path& path) {
    vespera::VesperaProject candidate;
    const auto result = vespera::load_vespera_project(candidate, path);
    if (!result) {
        push_console(state, ConsoleEntry::Level::Error, "Project open failed: " + result.message);
        return false;
    }

    const auto previous_project = state.project;
    const auto previous_project_path = state.project_path;
    const auto previous_assets_root = state.assets_root;
    const auto previous_asset_browser_folder = state.asset_browser_folder;
    const bool previous_project_loaded = state.project_loaded;

    state.project = std::move(candidate);
    state.project_path = state.project.project_file;
    state.project_path_text = state.project_path.string();
    state.project_loaded = true;
    state.assets_root = state.project.assets_root();
    state.asset_browser_folder.clear();
    refresh_asset_catalog(state, true);
    const auto startup_resolution = state.asset_catalog.resolve_reference(state.project.startup_scene_reference());
    const auto startup_path = startup_resolution ? startup_resolution.record->absolute_path : state.project.startup_scene_path();
    if (startup_resolution.stale_fallback_path) {
        push_console(state, ConsoleEntry::Level::Warning,
            "Startup scene stable ID resolved after its fallback path changed: "
            + state.project.startup_scene.generic_string() + " -> " + startup_resolution.record->relative_path.generic_string());
    }
    if (!open_scene(state, startup_path)) {
        state.project = previous_project;
        state.project_path = previous_project_path;
        state.project_loaded = previous_project_loaded;
        state.assets_root = previous_assets_root;
        state.asset_browser_folder = previous_asset_browser_folder;
        if (!state.assets_root.empty()) refresh_asset_catalog(state);
        return false;
    }

    push_console(state, ConsoleEntry::Level::Info, result.message);
    push_console(state, ConsoleEntry::Level::Info, "Project file: " + state.project_path.string());
    return true;
}

bool open_scene(EditorState& state, const std::filesystem::path& path) {
    if (editor_is_playing(state)) stop_play_mode(state);
    if (path.empty()) {
        push_console(state, ConsoleEntry::Level::Error, "Open failed: scene path is empty.");
        return false;
    }

    const auto absolute_path = std::filesystem::absolute(path).lexically_normal();
    const auto target_assets_root = state.project_loaded
        ? state.project.assets_root()
        : derive_assets_root(absolute_path);

    vespera::Scene candidate;
    std::vector<std::string> texture_warnings;
    if (state.project_loaded) {
        (void)register_catalog_texture_assets(candidate, state.asset_catalog, &texture_warnings);
    } else {
        const auto local_catalog = build_read_only_catalog(target_assets_root);
        (void)register_catalog_texture_assets(candidate, local_catalog, &texture_warnings);
    }
    const auto result = vespera::load_scene_text_resilient(candidate, absolute_path, &texture_warnings);
    if (!result) {
        push_console(state, ConsoleEntry::Level::Error, "Open failed: " + result.message);
        return false;
    }
    for (const auto& warning : texture_warnings) {
        push_console(state, ConsoleEntry::Level::Warning, warning);
    }

    state.scene = std::move(candidate);
    state.scene_path = absolute_path;
    state.assets_root = target_assets_root;
    refresh_asset_catalog(state);
    const std::size_t hydrated_asset_references = hydrate_scene_asset_references(state);
    if (hydrated_asset_references != 0) {
        push_console(state, ConsoleEntry::Level::Info, "Scene asset references upgraded/refreshed in memory: "
            + std::to_string(hydrated_asset_references));
    }
    state.selection = {};
    state.scene_view.drag_kind = SceneDragKind::None;
    state.scene_view.frame_all_pending = true;
    state.scene_view.frame_selection_pending = false;
    state.scene_view_3d.initialized = false;
    state.scene_view_3d.frame_selection_pending = true;
    state.open_path_text = state.scene_path.string();
    state.save_path_text = state.scene_path.string();
    reset_history_after_open(state);
    push_console(state, ConsoleEntry::Level::Info, "Opened: " + state.scene_path.string());
    push_console(state, ConsoleEntry::Level::Info, result.message);
    return true;
}

bool save_scene(EditorState& state, const std::filesystem::path& path) {
    if (editor_is_playing(state)) {
        push_console(state, ConsoleEntry::Level::Warning, "Save is disabled during Play Mode; Stop first to return to the edit scene.");
        return false;
    }
    commit_active_edit(state);
    if (path.empty()) {
        push_console(state, ConsoleEntry::Level::Error, "Save failed: scene path is empty.");
        return false;
    }

    const auto absolute_path = std::filesystem::absolute(path).lexically_normal();

    // Stable IDs are authoritative. Asset moves repair descriptors on disk, but
    // the currently open edit Scene can still hold an older human-readable
    // fallback path in memory. Canonicalize those references against the live
    // AssetCatalog immediately before serialization so Save can never write an
    // already-stale prefab/material fallback back into a repaired descriptor.
    const std::size_t refreshed_asset_references = hydrate_scene_asset_references(state);
    if (refreshed_asset_references != 0) {
        push_console(state, ConsoleEntry::Level::Info, std::format(
            "Refreshed {} stable asset fallback reference(s) before scene save.",
            refreshed_asset_references));
    }

    std::filesystem::path temporary = absolute_path;
    temporary += ".sectorline-tmp";
    std::filesystem::path backup = absolute_path;
    backup += ".bak";

    const auto result = vespera::save_scene_text(state.scene, temporary);
    if (!result) {
        push_console(state, ConsoleEntry::Level::Error, "Save failed: " + result.message);
        return false;
    }

    // Verify the exact bytes we are about to install can be loaded back through
    // the public scene loader before touching the user's current scene file.
    vespera::Scene verification;
    std::vector<std::string> verification_texture_warnings;
    const auto verify_assets_root = state.project_loaded ? state.project.assets_root() : derive_assets_root(absolute_path);
    if (state.project_loaded) {
        (void)register_catalog_texture_assets(verification, state.asset_catalog, &verification_texture_warnings);
    } else {
        const auto local_catalog = build_read_only_catalog(verify_assets_root);
        (void)register_catalog_texture_assets(verification, local_catalog, &verification_texture_warnings);
    }
    const auto verify_result = vespera::load_scene_text_resilient(verification, temporary, &verification_texture_warnings);
    if (!verify_result) {
        std::error_code cleanup_error;
        std::filesystem::remove(temporary, cleanup_error);
        push_console(state, ConsoleEntry::Level::Error, "Save validation failed: " + verify_result.message);
        return false;
    }
    for (const auto& warning : verification_texture_warnings) {
        push_console(state, ConsoleEntry::Level::Warning, "Save validation: " + warning);
    }

    std::error_code ec;
    const bool had_original = std::filesystem::exists(absolute_path, ec) && !ec;
    if (had_original) {
        ec.clear();
        std::filesystem::remove(backup, ec); // Keep at most one previous-save backup.
        ec.clear();
        std::filesystem::rename(absolute_path, backup, ec);
        if (ec) {
            const std::string message = ec.message();
            std::error_code cleanup_error;
            std::filesystem::remove(temporary, cleanup_error);
            push_console(state, ConsoleEntry::Level::Error, "Save failed while creating backup: " + message);
            return false;
        }
    }

    ec.clear();
    std::filesystem::rename(temporary, absolute_path, ec);
    if (ec) {
        const std::string message = ec.message();
        if (had_original) {
            std::error_code rollback_error;
            std::filesystem::rename(backup, absolute_path, rollback_error);
            if (rollback_error) {
                push_console(state, ConsoleEntry::Level::Error,
                    "Save failed and backup rollback also failed: " + rollback_error.message());
                return false;
            }
        }
        std::error_code cleanup_error;
        std::filesystem::remove(temporary, cleanup_error);
        push_console(state, ConsoleEntry::Level::Error, "Save failed while installing verified scene: " + message);
        return false;
    }

    state.scene_path = absolute_path;
    state.assets_root = state.project_loaded ? state.project.assets_root() : derive_assets_root(state.scene_path);
    refresh_asset_catalog(state);
    state.open_path_text = state.scene_path.string();
    state.save_path_text = state.scene_path.string();
    state.saved_state_id = state.current_state_id;
    refresh_dirty(state);
    push_console(state, ConsoleEntry::Level::Info, "Saved + verified: " + state.scene_path.string());
    if (had_original) {
        push_console(state, ConsoleEntry::Level::Info, "Previous scene backup: " + backup.string());
    }
    return true;
}

std::string window_title(const EditorState& state) {
    if (state.project_loaded) {
        std::string project_name = state.project.name.empty()
            ? (state.project_path.empty() ? std::string("Untitled Project") : state.project_path.stem().string())
            : state.project.name;
        if (state.dirty) project_name += " *";
        return std::format("{} - Vespera Editor {}", project_name, vespera::kEngineVersion);
    }
    std::string name = state.scene_path.empty() ? "Untitled" : state.scene_path.filename().string();
    if (state.dirty) name += " *";
    return std::format("{} - Vespera Editor {}", name, vespera::kEngineVersion);
}

ImVec2 world_to_screen(const SceneViewState& view, ImVec2 canvas_min, ImVec2 canvas_size, vespera::Vec2 point) {
    const ImVec2 center{canvas_min.x + canvas_size.x * 0.5f, canvas_min.y + canvas_size.y * 0.5f};
    return {
        center.x + view.pan.x + point.x * view.zoom,
        center.y + view.pan.y + point.z * view.zoom,
    };
}

vespera::Vec2 screen_to_world(const SceneViewState& view, ImVec2 canvas_min, ImVec2 canvas_size, ImVec2 point) {
    const ImVec2 center{canvas_min.x + canvas_size.x * 0.5f, canvas_min.y + canvas_size.y * 0.5f};
    return {
        (point.x - center.x - view.pan.x) / view.zoom,
        (point.y - center.y - view.pan.y) / view.zoom,
    };
}

void frame_all(EditorState& state, ImVec2 canvas_size) {
    float min_x = std::numeric_limits<float>::max();
    float min_z = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float max_z = std::numeric_limits<float>::lowest();
    bool any = false;

    for (const auto& sector : state.scene.world.sectors()) {
        for (const auto& vertex : sector.vertices) {
            min_x = std::min(min_x, vertex.x);
            min_z = std::min(min_z, vertex.z);
            max_x = std::max(max_x, vertex.x);
            max_z = std::max(max_z, vertex.z);
            any = true;
        }
    }
    for (const auto& entity : state.scene.entities) {
        const auto world = editor_world_transform(state, entity);
        min_x = std::min(min_x, world.position.x);
        min_z = std::min(min_z, world.position.z);
        max_x = std::max(max_x, world.position.x);
        max_z = std::max(max_z, world.position.z);
        any = true;
    }

    min_x = std::min(min_x, state.scene.camera.position.x);
    min_z = std::min(min_z, state.scene.camera.position.z);
    max_x = std::max(max_x, state.scene.camera.position.x);
    max_z = std::max(max_z, state.scene.camera.position.z);
    any = true;

    if (!any || canvas_size.x <= 40.0f || canvas_size.y <= 40.0f) {
        return;
    }

    const float width = std::max(max_x - min_x, 1.0f);
    const float height = std::max(max_z - min_z, 1.0f);
    const float usable_x = std::max(canvas_size.x - 80.0f, 40.0f);
    const float usable_y = std::max(canvas_size.y - 80.0f, 40.0f);
    state.scene_view.zoom = std::clamp(std::min(usable_x / width, usable_y / height), 12.0f, 220.0f);

    const float center_x = (min_x + max_x) * 0.5f;
    const float center_z = (min_z + max_z) * 0.5f;
    state.scene_view.pan = {-center_x * state.scene_view.zoom, -center_z * state.scene_view.zoom};
    state.scene_view.frame_all_pending = false;
}


void frame_selection(EditorState& state, ImVec2 canvas_size) {
    float min_x = 0.0f;
    float min_z = 0.0f;
    float max_x = 0.0f;
    float max_z = 0.0f;
    bool have_bounds = false;

    if (state.selection.kind == SelectionKind::Sector && state.selection.index < state.scene.world.sectors().size()) {
        const auto& sector = state.scene.world.sectors()[state.selection.index];
        if (!sector.vertices.empty()) {
            min_x = max_x = sector.vertices.front().x;
            min_z = max_z = sector.vertices.front().z;
            for (const auto& vertex : sector.vertices) {
                min_x = std::min(min_x, vertex.x);
                min_z = std::min(min_z, vertex.z);
                max_x = std::max(max_x, vertex.x);
                max_z = std::max(max_z, vertex.z);
            }
            have_bounds = true;
        }
    } else if (state.selection.kind == SelectionKind::Entity && state.selection.index < state.scene.entities.size()) {
        const auto& entity = state.scene.entities[state.selection.index];
        const auto world = editor_world_transform(state, entity);
        min_x = max_x = world.position.x;
        min_z = max_z = world.position.z;
        have_bounds = true;
    } else if (state.selection.kind == SelectionKind::Camera) {
        min_x = max_x = state.scene.camera.position.x;
        min_z = max_z = state.scene.camera.position.z;
        have_bounds = true;
    }

    if (!have_bounds || canvas_size.x <= 40.0f || canvas_size.y <= 40.0f) {
        state.scene_view.frame_selection_pending = false;
        return;
    }

    const float width = max_x - min_x;
    const float height = max_z - min_z;
    if (width > 0.05f || height > 0.05f) {
        const float usable_x = std::max(canvas_size.x - 120.0f, 40.0f);
        const float usable_y = std::max(canvas_size.y - 120.0f, 40.0f);
        state.scene_view.zoom = std::clamp(
            std::min(usable_x / std::max(width, 1.0f), usable_y / std::max(height, 1.0f)),
            20.0f,
            180.0f
        );
    } else {
        state.scene_view.zoom = std::clamp(std::max(state.scene_view.zoom, 84.0f), 20.0f, 180.0f);
    }

    const float center_x = (min_x + max_x) * 0.5f;
    const float center_z = (min_z + max_z) * 0.5f;
    state.scene_view.pan = {-center_x * state.scene_view.zoom, -center_z * state.scene_view.zoom};
    state.scene_view.frame_selection_pending = false;
}

ImU32 material_color(const vespera::SectorWorld& world, vespera::MaterialId id, float alpha = 0.34f) {
    if (id == vespera::kInvalidMaterial || id >= world.materials().size()) {
        return ImGui::GetColorU32(ImVec4(0.30f, 0.33f, 0.38f, alpha));
    }
    const auto& color = world.materials()[id].color;
    return ImGui::GetColorU32(ImVec4(color[0], color[1], color[2], alpha));
}

float snap_coordinate(float value, const SceneViewState& view, bool temporarily_disable) {
    if (!view.snap_enabled || temporarily_disable || view.snap_step <= 0.0001f) {
        return value;
    }
    return std::round(value / view.snap_step) * view.snap_step;
}

vespera::Vec2 snap_world_point(vespera::Vec2 point, const SceneViewState& view, bool temporarily_disable) {
    return {
        snap_coordinate(point.x, view, temporarily_disable),
        snap_coordinate(point.z, view, temporarily_disable),
    };
}

bool close_enough(vespera::Vec2 a, vespera::Vec2 b, float epsilon = 0.0001f) {
    return std::abs(a.x - b.x) <= epsilon && std::abs(a.z - b.z) <= epsilon;
}

float point_segment_distance_sq(ImVec2 point, ImVec2 a, ImVec2 b) {
    const float ab_x = b.x - a.x;
    const float ab_y = b.y - a.y;
    const float length_sq = ab_x * ab_x + ab_y * ab_y;
    if (length_sq <= 0.000001f) {
        const float dx = point.x - a.x;
        const float dy = point.y - a.y;
        return dx * dx + dy * dy;
    }
    const float t = std::clamp(((point.x - a.x) * ab_x + (point.y - a.y) * ab_y) / length_sq, 0.0f, 1.0f);
    const float closest_x = a.x + ab_x * t;
    const float closest_y = a.y + ab_y * t;
    const float dx = point.x - closest_x;
    const float dy = point.y - closest_y;
    return dx * dx + dy * dy;
}

bool valid_edit_polygon(const vespera::Sector& sector) {
    if (sector.vertices.size() < 3) {
        return false;
    }

    float area_twice = 0.0f;
    for (std::size_t i = 0; i < sector.vertices.size(); ++i) {
        const auto& a = sector.vertices[i];
        const auto& b = sector.vertices[(i + 1) % sector.vertices.size()];
        const float dx = b.x - a.x;
        const float dz = b.z - a.z;
        if (dx * dx + dz * dz < 0.000001f) {
            return false;
        }
        area_twice += a.x * b.z - b.x * a.z;
    }
    if (area_twice <= 0.0001f) {
        return false; // Vespera sectors are authored counter-clockwise.
    }

    // Collinear split points are allowed because portal boundaries may divide a
    // straight wall. A negative turn would make the polygon concave/invalid for
    // the current convex-sector mesher.
    for (std::size_t i = 0; i < sector.vertices.size(); ++i) {
        const auto& a = sector.vertices[i];
        const auto& b = sector.vertices[(i + 1) % sector.vertices.size()];
        const auto& c = sector.vertices[(i + 2) % sector.vertices.size()];
        const float abx = b.x - a.x;
        const float abz = b.z - a.z;
        const float bcx = c.x - b.x;
        const float bcz = c.z - b.z;
        const float cross = abx * bcz - abz * bcx;
        if (cross < -0.0001f) {
            return false;
        }
    }
    return true;
}

bool move_welded_vertex(EditorState& state, std::size_t sector_index, std::size_t vertex_index, vespera::Vec2 destination) {
    const auto& source_sectors = state.scene.world.sectors();
    if (sector_index >= source_sectors.size() || vertex_index >= source_sectors[sector_index].vertices.size()) {
        return false;
    }
    const vespera::Vec2 original = source_sectors[sector_index].vertices[vertex_index];
    if (close_enough(original, destination)) {
        return false;
    }

    std::vector<vespera::Sector> edited = source_sectors;
    std::vector<bool> changed(edited.size(), false);
    for (std::size_t sector_i = 0; sector_i < edited.size(); ++sector_i) {
        for (auto& vertex : edited[sector_i].vertices) {
            if (close_enough(vertex, original)) {
                vertex = destination;
                changed[sector_i] = true;
            }
        }
        if (changed[sector_i] && !valid_edit_polygon(edited[sector_i])) {
            return false;
        }
    }

    bool any = false;
    for (std::size_t sector_i = 0; sector_i < edited.size(); ++sector_i) {
        if (changed[sector_i]) {
            any |= state.scene.world.set_sector(sector_i, std::move(edited[sector_i]));
        }
    }
    return any;
}

void draw_grid(ImDrawList* draw, const SceneViewState& view, ImVec2 canvas_min, ImVec2 canvas_max, ImVec2 canvas_size) {
    float grid_world = 1.0f;
    while (grid_world * view.zoom < 26.0f) {
        grid_world *= 2.0f;
    }
    while (grid_world * view.zoom > 120.0f) {
        grid_world *= 0.5f;
    }

    const auto world_min = screen_to_world(view, canvas_min, canvas_size, canvas_min);
    const auto world_max = screen_to_world(view, canvas_min, canvas_size, canvas_max);
    const float start_x = std::floor(std::min(world_min.x, world_max.x) / grid_world) * grid_world;
    const float end_x = std::ceil(std::max(world_min.x, world_max.x) / grid_world) * grid_world;
    const float start_z = std::floor(std::min(world_min.z, world_max.z) / grid_world) * grid_world;
    const float end_z = std::ceil(std::max(world_min.z, world_max.z) / grid_world) * grid_world;

    const ImU32 minor = ImGui::GetColorU32(ImVec4(0.28f, 0.30f, 0.34f, 0.42f));
    const ImU32 axis = ImGui::GetColorU32(ImVec4(0.48f, 0.50f, 0.55f, 0.62f));

    int line_budget = 400;
    for (float x = start_x; x <= end_x && line_budget-- > 0; x += grid_world) {
        const ImVec2 a = world_to_screen(view, canvas_min, canvas_size, {x, start_z});
        const ImVec2 b = world_to_screen(view, canvas_min, canvas_size, {x, end_z});
        draw->AddLine(a, b, std::abs(x) < 0.0001f ? axis : minor, 1.0f);
    }
    for (float z = start_z; z <= end_z && line_budget-- > 0; z += grid_world) {
        const ImVec2 a = world_to_screen(view, canvas_min, canvas_size, {start_x, z});
        const ImVec2 b = world_to_screen(view, canvas_min, canvas_size, {end_x, z});
        draw->AddLine(a, b, std::abs(z) < 0.0001f ? axis : minor, 1.0f);
    }
}

void select_at_world(EditorState& state, vespera::Vec2 world_point) {
    // Entity markers get priority over sectors.
    const float entity_pick_radius = 12.0f / std::max(state.scene_view.zoom, 1.0f);
    for (std::size_t i = state.scene.entities.size(); i-- > 0;) {
        const auto& entity = state.scene.entities[i];
        const auto transform = editor_world_transform(state, entity);
        const float dx = transform.position.x - world_point.x;
        const float dz = transform.position.z - world_point.z;
        if ((dx * dx + dz * dz) <= entity_pick_radius * entity_pick_radius) {
            select_entity(state, i);
            return;
        }
    }

    const float camera_dx = state.scene.camera.position.x - world_point.x;
    const float camera_dz = state.scene.camera.position.z - world_point.z;
    const float camera_pick_radius = 13.0f / std::max(state.scene_view.zoom, 1.0f);
    if ((camera_dx * camera_dx + camera_dz * camera_dz) <= camera_pick_radius * camera_pick_radius) {
        state.selection = {SelectionKind::Camera, 0};
        return;
    }

    const auto& sectors = state.scene.world.sectors();
    for (std::size_t i = sectors.size(); i-- > 0;) {
        if (vespera::point_inside_sector(sectors[i], world_point)) {
            state.selection = {SelectionKind::Sector, i};
            return;
        }
    }

    state.selection = {};
}

void draw_scene_view(EditorState& state) {
    ImGui::Begin("Sector");

    if (ImGui::Button("Frame All")) {
        state.scene_view.frame_all_pending = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Frame Selected")) {
        state.scene_view.frame_selection_pending = true;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Snap", &state.scene_view.snap_enabled);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(74.0f);
    ImGui::DragFloat("##snap_step", &state.scene_view.snap_step, 0.05f, 0.05f, 10.0f, "%.2f");
    state.scene_view.snap_step = std::max(state.scene_view.snap_step, 0.05f);
    ImGui::SameLine();
    ImGui::TextDisabled("L-drag selected vertices/entities   Alt bypass snap   M-drag pan   Wheel zoom");

    ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    canvas_size.x = std::max(canvas_size.x, 100.0f);
    canvas_size.y = std::max(canvas_size.y, 100.0f);
    const ImVec2 canvas_min = ImGui::GetCursorScreenPos();
    const ImVec2 canvas_max{canvas_min.x + canvas_size.x, canvas_min.y + canvas_size.y};

    ImGui::InvisibleButton("##scene_canvas", canvas_size);
    const bool hovered = ImGui::IsItemHovered();
    const ImGuiIO& io = ImGui::GetIO();

    if (state.scene_view.frame_all_pending) {
        frame_all(state, canvas_size);
    }
    if (state.scene_view.frame_selection_pending) {
        frame_selection(state, canvas_size);
    }

    if (hovered && std::abs(io.MouseWheel) > 0.001f && state.scene_view.drag_kind == SceneDragKind::None) {
        const float old_zoom = state.scene_view.zoom;
        const float factor = std::pow(1.12f, io.MouseWheel);
        state.scene_view.zoom = std::clamp(old_zoom * factor, 8.0f, 320.0f);
        const ImVec2 mouse = io.MousePos;
        const auto before = screen_to_world({old_zoom, state.scene_view.pan, false}, canvas_min, canvas_size, mouse);
        const ImVec2 after_screen = world_to_screen(state.scene_view, canvas_min, canvas_size, before);
        state.scene_view.pan.x += mouse.x - after_screen.x;
        state.scene_view.pan.y += mouse.y - after_screen.y;
    }

    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) && state.scene_view.drag_kind == SceneDragKind::None) {
        state.scene_view.pan.x += io.MouseDelta.x;
        state.scene_view.pan.y += io.MouseDelta.y;
    }

    // Begin direct manipulation. Selected sector vertices get first priority,
    // then sprite/camera markers, then ordinary sector selection.
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        commit_active_edit(state);
        bool began_drag = false;
        if (state.selection.kind == SelectionKind::Sector
            && state.selection.index < state.scene.world.sectors().size()) {
            const auto& sector = state.scene.world.sectors()[state.selection.index];
            float best_distance_sq = 11.0f * 11.0f;
            std::size_t best_vertex = kNoSubSelection;
            for (std::size_t vertex_i = 0; vertex_i < sector.vertices.size(); ++vertex_i) {
                const ImVec2 p = world_to_screen(state.scene_view, canvas_min, canvas_size, sector.vertices[vertex_i]);
                const float dx = p.x - io.MousePos.x;
                const float dy = p.y - io.MousePos.y;
                const float distance_sq = dx * dx + dy * dy;
                if (distance_sq <= best_distance_sq) {
                    best_distance_sq = distance_sq;
                    best_vertex = vertex_i;
                }
            }
            if (best_vertex != kNoSubSelection) {
                state.selection.sub_index = best_vertex;
                state.selection.side_index = kNoSubSelection;
                state.scene_view.drag_kind = SceneDragKind::SectorVertex;
                state.scene_view.drag_index = state.selection.index;
                state.scene_view.drag_sub_index = best_vertex;
                state.scene_view.drag_changed = false;
                begin_edit(state, capture_snapshot(state), "Move sector vertex");
                began_drag = true;
            }
        }

        if (!began_drag
            && state.selection.kind == SelectionKind::Sector
            && state.selection.index < state.scene.world.sectors().size()) {
            const auto& sector = state.scene.world.sectors()[state.selection.index];
            float best_distance_sq = 7.0f * 7.0f;
            std::size_t best_side = kNoSubSelection;
            for (std::size_t side = 0; side < sector.vertices.size(); ++side) {
                const ImVec2 a = world_to_screen(state.scene_view, canvas_min, canvas_size, sector.vertices[side]);
                const ImVec2 b = world_to_screen(state.scene_view, canvas_min, canvas_size, sector.vertices[(side + 1u) % sector.vertices.size()]);
                const float distance_sq = point_segment_distance_sq(io.MousePos, a, b);
                if (distance_sq <= best_distance_sq) {
                    best_distance_sq = distance_sq;
                    best_side = side;
                }
            }
            if (best_side != kNoSubSelection) {
                state.selection.sub_index = kNoSubSelection;
                state.selection.side_index = best_side;
                began_drag = true; // The click was consumed as an edge selection.
            }
        }

        if (!began_drag) {
            const auto clicked_world = screen_to_world(state.scene_view, canvas_min, canvas_size, io.MousePos);
            const float entity_pick_radius = 12.0f / std::max(state.scene_view.zoom, 1.0f);
            for (std::size_t i = state.scene.entities.size(); i-- > 0;) {
                const auto& entity = state.scene.entities[i];
                const auto world = editor_world_transform(state, entity);
                const float dx = world.position.x - clicked_world.x;
                const float dz = world.position.z - clicked_world.z;
                if (dx * dx + dz * dz <= entity_pick_radius * entity_pick_radius) {
                    select_entity(state, i);
                    state.scene_view.drag_kind = SceneDragKind::Entity;
                    state.scene_view.drag_index = i;
                    state.scene_view.drag_changed = false;
                    begin_edit(state, capture_snapshot(state), "Move entity");
                    began_drag = true;
                    break;
                }
            }
        }

        if (!began_drag) {
            const auto clicked_world = screen_to_world(state.scene_view, canvas_min, canvas_size, io.MousePos);
            const float dx = state.scene.camera.position.x - clicked_world.x;
            const float dz = state.scene.camera.position.z - clicked_world.z;
            const float pick_radius = 13.0f / std::max(state.scene_view.zoom, 1.0f);
            if (dx * dx + dz * dz <= pick_radius * pick_radius) {
                state.selection = {SelectionKind::Camera, 0};
                state.scene_view.drag_kind = SceneDragKind::Camera;
                state.scene_view.drag_changed = false;
                begin_edit(state, capture_snapshot(state), "Move camera");
                began_drag = true;
            }
        }

        if (!began_drag) {
            state.selection.sub_index = kNoSubSelection;
            state.selection.side_index = kNoSubSelection;
            select_at_world(state, screen_to_world(state.scene_view, canvas_min, canvas_size, io.MousePos));
        }
    }

    if (state.scene_view.drag_kind != SceneDragKind::None && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        vespera::Vec2 destination = screen_to_world(state.scene_view, canvas_min, canvas_size, io.MousePos);
        destination = snap_world_point(destination, state.scene_view, io.KeyAlt);
        bool changed = false;
        if (state.scene_view.drag_kind == SceneDragKind::SectorVertex) {
            changed = move_welded_vertex(
                state,
                state.scene_view.drag_index,
                state.scene_view.drag_sub_index,
                destination
            );
        } else if (state.scene_view.drag_kind == SceneDragKind::Entity
            && state.scene_view.drag_index < state.scene.entities.size()) {
            auto& entity = state.scene.entities[state.scene_view.drag_index];
            auto world = editor_world_transform(state, entity);
            if (std::abs(world.position.x - destination.x) > 0.0001f
                || std::abs(world.position.z - destination.z) > 0.0001f) {
                world.position.x = destination.x;
                world.position.z = destination.z;
                set_editor_world_transform(state, entity.id, world);
                changed = true;
            }
        } else if (state.scene_view.drag_kind == SceneDragKind::Camera) {
            auto& camera = state.scene.camera;
            if (std::abs(camera.position.x - destination.x) > 0.0001f
                || std::abs(camera.position.z - destination.z) > 0.0001f) {
                camera.position.x = destination.x;
                camera.position.z = destination.z;
                changed = true;
            }
        }
        state.scene_view.drag_changed = state.scene_view.drag_changed || changed;
        state.active_edit_changed = state.active_edit_changed || changed;
        if (changed) {
            state.dirty = true;
        }
    }

    if (state.scene_view.drag_kind != SceneDragKind::None && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        commit_active_edit(state);
        state.scene_view.drag_kind = SceneDragKind::None;
        state.scene_view.drag_sub_index = kNoSubSelection;
        state.scene_view.drag_changed = false;
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(canvas_min, canvas_max, true);
    draw->AddRectFilled(canvas_min, canvas_max, ImGui::GetColorU32(ImVec4(0.075f, 0.082f, 0.095f, 1.0f)));
    draw_grid(draw, state.scene_view, canvas_min, canvas_max, canvas_size);

    const auto& sectors = state.scene.world.sectors();
    for (std::size_t i = 0; i < sectors.size(); ++i) {
        const auto& sector = sectors[i];
        if (sector.vertices.size() < 3) {
            continue;
        }

        std::vector<ImVec2> points;
        points.reserve(sector.vertices.size());
        for (const auto& vertex : sector.vertices) {
            points.push_back(world_to_screen(state.scene_view, canvas_min, canvas_size, vertex));
        }

        const bool selected = state.selection.kind == SelectionKind::Sector && state.selection.index == i;
        const ImU32 fill = material_color(state.scene.world, sector.floor_material, selected ? 0.52f : 0.28f);
        const ImU32 outline = selected
            ? ImGui::GetColorU32(ImVec4(0.95f, 0.73f, 0.26f, 1.0f))
            : ImGui::GetColorU32(ImVec4(0.66f, 0.70f, 0.78f, 0.86f));
        draw->AddConvexPolyFilled(points.data(), static_cast<int>(points.size()), fill);
        draw->AddPolyline(points.data(), static_cast<int>(points.size()), outline, ImDrawFlags_Closed, selected ? 2.5f : 1.5f);

        for (std::size_t side = 0; side < sector.sides.size() && side < points.size(); ++side) {
            if (sector.sides[side].adjacent_sector >= 0) {
                const ImVec2 a = points[side];
                const ImVec2 b = points[(side + 1) % points.size()];
                draw->AddLine(a, b, ImGui::GetColorU32(ImVec4(0.27f, 0.72f, 0.95f, 1.0f)), 3.0f);
            }
        }
        if (selected && state.selection.side_index < points.size()) {
            const std::size_t side = state.selection.side_index;
            draw->AddLine(
                points[side],
                points[(side + 1u) % points.size()],
                ImGui::GetColorU32(ImVec4(1.0f, 0.62f, 0.18f, 1.0f)),
                5.0f
            );
        }

        if (selected) {
            for (std::size_t vertex_i = 0; vertex_i < points.size(); ++vertex_i) {
                const bool vertex_selected = state.selection.sub_index == vertex_i;
                const float radius = vertex_selected ? 6.0f : 4.5f;
                draw->AddCircleFilled(points[vertex_i], radius,
                    vertex_selected
                        ? ImGui::GetColorU32(ImVec4(1.0f, 0.78f, 0.28f, 1.0f))
                        : ImGui::GetColorU32(ImVec4(0.90f, 0.92f, 0.96f, 1.0f)));
                draw->AddCircle(points[vertex_i], radius + 1.5f,
                    ImGui::GetColorU32(ImVec4(0.08f, 0.09f, 0.11f, 1.0f)), 0, 1.5f);
            }
        }
    }

    for (std::size_t i = 0; i < state.scene.entities.size(); ++i) {
        const auto& entity = state.scene.entities[i];
        const auto world_transform = editor_world_transform(state, entity);
        const ImVec2 p = world_to_screen(
            state.scene_view, canvas_min, canvas_size,
            {world_transform.position.x, world_transform.position.z});
        const bool selected = state.selection.kind == SelectionKind::Entity && state.selection.index == i;
        const float radius = selected ? 8.0f : 6.0f;
        const float marker_alpha = entity.enabled ? 1.0f : 0.32f;
        if (entity.point_light) {
            const auto& light = *entity.point_light;
            const float scale_radius = (std::max)({
                std::abs(world_transform.scale.x),
                std::abs(world_transform.scale.y),
                std::abs(world_transform.scale.z)
            });
            const float screen_light_radius = std::max(light.radius * scale_radius * state.scene_view.zoom, 3.0f);
            const ImVec4 light_color{
                std::clamp(light.color[0], 0.0f, 1.0f),
                std::clamp(light.color[1], 0.0f, 1.0f),
                std::clamp(light.color[2], 0.0f, 1.0f),
                selected ? 0.72f : 0.36f
            };
            draw->AddCircle(p, screen_light_radius, ImGui::GetColorU32(light_color), 0, selected ? 2.5f : 1.5f);
            draw->AddLine({p.x - 7.0f, p.y}, {p.x + 7.0f, p.y}, ImGui::GetColorU32(light_color), 2.0f);
            draw->AddLine({p.x, p.y - 7.0f}, {p.x, p.y + 7.0f}, ImGui::GetColorU32(light_color), 2.0f);
        }
        if (entity.cylinder_collider) {
            const auto& collider = *entity.cylinder_collider;
            const float world_radius = collider.radius * std::max(std::abs(world_transform.scale.x), std::abs(world_transform.scale.z));
            const float screen_radius = world_radius * state.scene_view.zoom;
            const float local_center_x = collider.center.x * world_transform.scale.x;
            const float local_center_z = collider.center.z * world_transform.scale.z;
            const float yaw_cos = std::cos(world_transform.rotation.y);
            const float yaw_sin = std::sin(world_transform.rotation.y);
            const ImVec2 collider_p = world_to_screen(
                state.scene_view, canvas_min, canvas_size,
                {
                    world_transform.position.x + local_center_x * yaw_cos + local_center_z * yaw_sin,
                    world_transform.position.z - local_center_x * yaw_sin + local_center_z * yaw_cos
                });
            const ImVec4 collider_color = collider.is_trigger
                ? ImVec4(0.72f, 0.48f, 0.95f, selected ? 0.95f : 0.58f)
                : ImVec4(0.30f, 0.86f, 0.52f, selected ? 0.95f : 0.58f);
            draw->AddCircle(collider_p, std::max(screen_radius, 2.0f), ImGui::GetColorU32(collider_color), 0, selected ? 2.5f : 1.5f);
        }
        ImVec4 marker_color{0.46f, 0.68f, 0.98f, marker_alpha};
        if (entity.sprite_renderer) {
            const auto& color = entity.sprite_renderer->color;
            marker_color = {color[0], color[1], color[2], marker_alpha};
        }
        if (entity.point_light) {
            const auto& color = entity.point_light->color;
            marker_color = {color[0], color[1], color[2], marker_alpha};
        }
        draw->AddCircleFilled(p, radius, ImGui::GetColorU32(marker_color));
        draw->AddCircle(p, radius + 2.0f, selected
            ? ImGui::GetColorU32(ImVec4(1.0f, 0.78f, 0.28f, 1.0f))
            : ImGui::GetColorU32(ImVec4(0.10f, 0.12f, 0.15f, 1.0f)), 0, selected ? 2.5f : 1.5f);

        if (selected || (entity.sprite_renderer && !entity.sprite_renderer->animation_clip.empty())) {
            const float face_x = std::sin(world_transform.rotation.y);
            const float face_z = std::cos(world_transform.rotation.y);
            const float arrow_length = selected ? 24.0f : 15.0f;
            const ImVec2 face_tip{p.x + face_x * arrow_length, p.y + face_z * arrow_length};
            draw->AddLine(p, face_tip,
                selected
                    ? ImGui::GetColorU32(ImVec4(1.0f, 0.78f, 0.28f, 1.0f))
                    : ImGui::GetColorU32(ImVec4(0.68f, 0.78f, 0.90f, 0.85f)),
                selected ? 2.5f : 1.5f);
        }
        if (selected) {
            std::string base_label = entity.name;
            if (entity.sprite_renderer && !entity.sprite_renderer->animation_clip.empty()) {
                base_label = std::format("{}  [{}]", entity.name, entity.sprite_renderer->animation_clip);
            } else if (!entity.sprite_renderer) {
                base_label = std::format("{}  [Transform]", entity.name);
            }
            if (entity.cylinder_collider) {
                base_label += entity.cylinder_collider->is_trigger ? "  [Trigger]" : "  [Collider]";
            }
            if (entity.tag != "Untagged" || entity.layer != "Default") {
                base_label += std::format("  [tag:{} | layer:{}]", entity.tag, entity.layer);
            }
            const std::string label = entity.enabled ? base_label : std::format("{}  (disabled)", base_label);
            draw->AddText({p.x + 10.0f, p.y - 18.0f}, ImGui::GetColorU32(ImVec4(0.96f, 0.96f, 0.98f, 1.0f)), label.c_str());
        }
    }

    const auto& camera = state.scene.camera;
    const ImVec2 camera_pos = world_to_screen(state.scene_view, canvas_min, canvas_size, {camera.position.x, camera.position.z});
    const float dir_x = std::sin(camera.yaw);
    const float dir_z = std::cos(camera.yaw);
    const ImVec2 tip{camera_pos.x + dir_x * 22.0f, camera_pos.y + dir_z * 22.0f};
    const bool camera_selected = state.selection.kind == SelectionKind::Camera;
    draw->AddCircleFilled(camera_pos, camera_selected ? 7.0f : 5.5f,
        ImGui::GetColorU32(ImVec4(0.96f, 0.43f, 0.32f, 1.0f)));
    draw->AddLine(camera_pos, tip,
        camera_selected ? ImGui::GetColorU32(ImVec4(1.0f, 0.82f, 0.38f, 1.0f)) : ImGui::GetColorU32(ImVec4(0.96f, 0.43f, 0.32f, 1.0f)), 2.5f);

    if (hovered) {
        const auto mouse_world = screen_to_world(state.scene_view, canvas_min, canvas_size, io.MousePos);
        const std::string snap_suffix = state.scene_view.snap_enabled
            ? std::format("   snap {:.2f}", state.scene_view.snap_step)
            : std::string{};
        const std::string coords = std::format(
            "X {:.2f}   Z {:.2f}   {:.0f}%{}",
            mouse_world.x,
            mouse_world.z,
            state.scene_view.zoom / 64.0f * 100.0f,
            snap_suffix
        );
        const ImVec2 text_size = ImGui::CalcTextSize(coords.c_str());
        const ImVec2 box_min{canvas_min.x + 8.0f, canvas_max.y - text_size.y - 14.0f};
        const ImVec2 box_max{box_min.x + text_size.x + 12.0f, canvas_max.y - 6.0f};
        draw->AddRectFilled(box_min, box_max, ImGui::GetColorU32(ImVec4(0.03f, 0.035f, 0.045f, 0.86f)), 4.0f);
        draw->AddText({box_min.x + 6.0f, box_min.y + 4.0f}, ImGui::GetColorU32(ImGuiCol_TextDisabled), coords.c_str());
    }

    draw->PopClipRect();
    ImGui::End();
}

void draw_hierarchy(EditorState& state) {
    ImGui::Begin("Hierarchy");

    ImGui::TextColored(ImVec4(0.58f, 0.63f, 1.0f, 1.0f), "HIERARCHY");
    ImGui::SameLine();
    ImGui::TextDisabled("%zu entities  /  %zu sectors", state.scene.entities.size(), state.scene.world.sectors().size());
    if (state.selection.kind == SelectionKind::Entity && state.selected_entity_ids.size() > 1) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.96f, 0.78f, 0.32f, 1.0f), "| %zu selected", state.selected_entity_ids.size());
    }
    if (ImGui::Button("+ Create", ImVec2(92.0f, 0.0f))) ImGui::OpenPopup("HierarchyCreateMenu");
    if (ImGui::BeginPopup("HierarchyCreateMenu")) {
        if (ImGui::MenuItem("Empty Entity")) command_create_entity(state);
        if (ImGui::BeginMenu("3D Object")) {
            if (ImGui::MenuItem("Cube")) command_create_primitive_entity(state, vespera::PrimitiveMeshType::Cube);
            if (ImGui::MenuItem("Plane")) command_create_primitive_entity(state, vespera::PrimitiveMeshType::Plane);
            if (ImGui::MenuItem("Cylinder")) command_create_primitive_entity(state, vespera::PrimitiveMeshType::Cylinder);
            if (ImGui::MenuItem("Sphere")) command_create_primitive_entity(state, vespera::PrimitiveMeshType::Sphere);
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Sprite Entity")) command_create_sprite_entity(state);
        if (ImGui::MenuItem("Trigger Volume")) command_create_trigger_entity(state);
        if (ImGui::MenuItem("Point Light")) command_create_point_light_entity(state);
        ImGui::Separator();
        if (ImGui::MenuItem("Sector")) command_create_sector(state);
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Ctrl+D  Duplicate   Del  Delete");
    ImGui::Separator();

    if (ImGui::Selectable("Camera", state.selection.kind == SelectionKind::Camera)) {
        commit_active_edit(state);
        state.selection = {SelectionKind::Camera, 0};
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            state.scene_view.frame_selection_pending = true;
        }
    }

    std::optional<std::size_t> duplicate_sector;
    std::optional<std::size_t> delete_sector;
    if (ImGui::TreeNodeEx("Sectors", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginPopupContextItem("sector_group_context")) {
            if (ImGui::MenuItem("Create Sector")) {
                command_create_sector(state);
            }
            ImGui::EndPopup();
        }
        const auto& sectors = state.scene.world.sectors();
        for (std::size_t i = 0; i < sectors.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            const bool selected = state.selection.kind == SelectionKind::Sector && state.selection.index == i;
            if (ImGui::Selectable(sectors[i].name.c_str(), selected)) {
                commit_active_edit(state);
                state.selection = {SelectionKind::Sector, i};
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    state.scene_view.frame_selection_pending = true;
                }
            }
            if (ImGui::BeginPopupContextItem("sector_context")) {
                if (ImGui::MenuItem("Duplicate", "Ctrl+D")) duplicate_sector = i;
                if (ImGui::MenuItem("Delete", "Del")) delete_sector = i;
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    if (duplicate_sector && *duplicate_sector < state.scene.world.sectors().size()) {
        state.selection = {SelectionKind::Sector, *duplicate_sector};
        command_duplicate_selection(state);
    } else if (delete_sector && *delete_sector < state.scene.world.sectors().size()) {
        state.selection = {SelectionKind::Sector, *delete_sector};
        command_delete_selection(state);
    }

    std::optional<std::size_t> duplicate_entity;
    std::optional<std::size_t> delete_entity;
    std::optional<std::pair<vespera::SceneObjectId, vespera::SceneObjectId>> reorder_entity;
    std::optional<std::pair<vespera::SceneObjectId, vespera::SceneObjectId>> reparent_entity;
    std::optional<vespera::SceneObjectId> unparent_entity;
    std::optional<vespera::SceneObjectId> apply_prefab_entity;
    std::optional<vespera::SceneObjectId> revert_prefab_entity;
    std::optional<vespera::SceneObjectId> unpack_prefab_entity;
    state.entity_filter.Draw("Filter entities", -1.0f);
    if (ImGui::TreeNodeEx("Entities", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginPopupContextItem("entity_group_context")) {
            if (ImGui::MenuItem("Create Empty Entity")) command_create_entity(state);
            if (ImGui::BeginMenu("3D Object")) {
                if (ImGui::MenuItem("Cube")) command_create_primitive_entity(state, vespera::PrimitiveMeshType::Cube);
                if (ImGui::MenuItem("Plane")) command_create_primitive_entity(state, vespera::PrimitiveMeshType::Plane);
                if (ImGui::MenuItem("Cylinder")) command_create_primitive_entity(state, vespera::PrimitiveMeshType::Cylinder);
                if (ImGui::MenuItem("Sphere")) command_create_primitive_entity(state, vespera::PrimitiveMeshType::Sphere);
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Create Sprite Entity")) command_create_sprite_entity(state);
            if (ImGui::MenuItem("Create Trigger Volume")) command_create_trigger_entity(state);
            if (ImGui::MenuItem("Create Point Light")) command_create_point_light_entity(state);
            ImGui::EndPopup();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VESPERA_ENTITY_HIERARCHY")) {
                if (payload->DataSize == sizeof(vespera::SceneObjectId)) {
                    unparent_entity = *static_cast<const vespera::SceneObjectId*>(payload->Data);
                }
            }
            ImGui::EndDragDropTarget();
        }

        for (std::size_t i = 0; i < state.scene.entities.size(); ++i) {
            ImGui::PushID(static_cast<int>(i) + 100000);
            const auto& entity = state.scene.entities[i];
            const bool selected = state.selection.kind == SelectionKind::Entity && entity_is_multi_selected(state, entity.id);
            std::string hierarchy_name = entity.name;
            if (entity.tag != "Untagged") hierarchy_name += std::format("  [{}]", entity.tag);
            if (!entity.prefab_source.empty()) hierarchy_name += "  [Prefab]";
            if (entity.sprite_renderer) hierarchy_name += "  [Sprite]";
            if (entity.mesh_renderer) hierarchy_name += "  [Mesh]";
            if (entity.cylinder_collider) hierarchy_name += entity.cylinder_collider->is_trigger ? "  [Trigger]" : "  [Collider]";
            if (entity.point_light) hierarchy_name += "  [Light]";
            if (!entity.managed_scripts.empty()) hierarchy_name += std::format("  [C#:{}]", entity.managed_scripts.size());
            if (!entity.enabled) hierarchy_name += "  (disabled)";
            const std::string filter_text = hierarchy_name + " " + entity.layer;
            if (!state.entity_filter.PassFilter(filter_text.c_str())) {
                ImGui::PopID();
                continue;
            }
            const float hierarchy_indent = static_cast<float>(entity_hierarchy_depth(state, entity)) * 16.0f;
            if (hierarchy_indent > 0.0f) ImGui::Indent(hierarchy_indent);
            if (entity.parent_id != vespera::kInvalidSceneObjectId) hierarchy_name = "  > " + hierarchy_name;
            if (ImGui::Selectable(hierarchy_name.c_str(), selected)) {
                commit_active_edit(state);
                const auto& io = ImGui::GetIO();
                if (io.KeyShift) select_entity_range(state, i);
                else if (io.KeyCtrl) toggle_entity_selection(state, i);
                else select_entity(state, i);
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) state.scene_view.frame_selection_pending = true;
            }
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                const auto dragged_id = entity.id;
                ImGui::SetDragDropPayload("VESPERA_ENTITY_HIERARCHY", &dragged_id, sizeof(dragged_id));
                ImGui::TextUnformatted(entity.name.c_str());
                ImGui::TextDisabled("Drop on entity = parent  |  Ctrl-drop = reorder  |  drop on Entities = unparent");
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VESPERA_ENTITY_HIERARCHY")) {
                    if (payload->DataSize == sizeof(vespera::SceneObjectId)) {
                        const auto dragged_id = *static_cast<const vespera::SceneObjectId*>(payload->Data);
                        if (dragged_id != entity.id) {
                            if (ImGui::GetIO().KeyCtrl) reorder_entity = std::pair{dragged_id, entity.id};
                            else reparent_entity = std::pair{dragged_id, entity.id};
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
            if (ImGui::BeginPopupContextItem("entity_context")) {
                if (ImGui::MenuItem("Duplicate", "Ctrl+D")) duplicate_entity = i;
                if (entity.parent_id != vespera::kInvalidSceneObjectId && ImGui::MenuItem("Unparent")) unparent_entity = entity.id;
                if (!entity.prefab_source.empty()) {
                    ImGui::SeparatorText("Prefab");
                    if (ImGui::MenuItem("Apply to Prefab")) apply_prefab_entity = entity.id;
                    if (ImGui::MenuItem("Revert from Prefab")) revert_prefab_entity = entity.id;
                    if (ImGui::MenuItem("Unpack Prefab")) unpack_prefab_entity = entity.id;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Delete", "Del")) delete_entity = i;
                ImGui::EndPopup();
            }
            if (hierarchy_indent > 0.0f) ImGui::Unindent(hierarchy_indent);
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    if (duplicate_entity && *duplicate_entity < state.scene.entities.size()) {
        select_entity(state, *duplicate_entity);
        command_duplicate_selection(state);
    } else if (delete_entity && *delete_entity < state.scene.entities.size()) {
        select_entity(state, *delete_entity);
        command_delete_selection(state);
    } else if (reparent_entity) {
        command_reparent_entity(state, reparent_entity->first, reparent_entity->second);
    } else if (unparent_entity) {
        command_reparent_entity(state, *unparent_entity, vespera::kInvalidSceneObjectId);
    } else if (reorder_entity) {
        command_reorder_entity(state, reorder_entity->first, reorder_entity->second);
    } else if (apply_prefab_entity) {
        if (const auto index = entity_index_from_id(state, *apply_prefab_entity)) {
            select_entity(state, *index);
            command_apply_selected_to_prefab(state);
        }
    } else if (revert_prefab_entity) {
        if (const auto index = entity_index_from_id(state, *revert_prefab_entity)) {
            select_entity(state, *index);
            command_revert_selected_from_prefab(state);
        }
    } else if (unpack_prefab_entity) {
        if (const auto index = entity_index_from_id(state, *unpack_prefab_entity)) {
            select_entity(state, *index);
            command_unpack_selected_prefab(state);
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled("%zu sectors", state.scene.world.sectors().size());
    ImGui::TextDisabled("%zu entities", state.scene.entities.size());
    ImGui::TextDisabled("Ctrl-click toggles | Shift-click range | drag = parent | Ctrl-drag = reorder");
    ImGui::TextDisabled("%zu sprite clips", state.scene.sprite_clips.size());
    ImGui::End();
}

bool material_combo(const char* label, vespera::MaterialId& id, const vespera::SectorWorld& world) {
    const auto& materials = world.materials();
    const char* preview = "<none>";
    if (id != vespera::kInvalidMaterial && id < materials.size()) {
        preview = materials[id].name.c_str();
    }

    bool changed = false;
    if (ImGui::BeginCombo(label, preview)) {
        const bool none_selected = id == vespera::kInvalidMaterial;
        if (ImGui::Selectable("<none>", none_selected)) {
            id = vespera::kInvalidMaterial;
            changed = true;
        }
        for (std::size_t i = 0; i < materials.size(); ++i) {
            const bool selected = id == static_cast<vespera::MaterialId>(i);
            if (ImGui::Selectable(materials[i].name.c_str(), selected)) {
                id = static_cast<vespera::MaterialId>(i);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool texture_combo(const char* label, vespera::TextureId& id, const vespera::SectorWorld& world) {
    const auto& textures = world.textures();
    const char* preview = "<none>";
    if (id != vespera::kInvalidTexture && id < textures.size()) {
        preview = textures[id].name.c_str();
    }

    bool changed = false;
    if (ImGui::BeginCombo(label, preview)) {
        const bool none_selected = id == vespera::kInvalidTexture;
        if (ImGui::Selectable("<none>", none_selected)) {
            id = vespera::kInvalidTexture;
            changed = true;
        }
        for (std::size_t i = 0; i < textures.size(); ++i) {
            const bool selected = id == static_cast<vespera::TextureId>(i);
            if (ImGui::Selectable(textures[i].name.c_str(), selected)) {
                id = static_cast<vespera::TextureId>(i);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool sprite_clip_combo(const char* label, std::string& clip_name, const vespera::Scene& scene) {
    const char* preview = clip_name.empty() ? "<static>" : clip_name.c_str();
    bool changed = false;
    if (ImGui::BeginCombo(label, preview)) {
        if (ImGui::Selectable("<static>", clip_name.empty())) {
            clip_name.clear();
            changed = true;
        }
        for (const auto& clip : scene.sprite_clips) {
            const bool selected = clip.name == clip_name;
            const std::string label_text = std::format(
                "{}  ({} dirs x {} frames @ {:.1f} fps)",
                clip.name,
                clip.direction_count,
                clip.frame_count,
                clip.frames_per_second
            );
            if (ImGui::Selectable(label_text.c_str(), selected)) {
                clip_name = clip.name;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}


std::optional<vespera::Vec3> selected_focus_point_3d(const EditorState& state) {
    switch (state.selection.kind) {
        case SelectionKind::Entity: {
            const auto index = selected_entity_index(state);
            if (!index) return std::nullopt;
            if (state.selected_entity_ids.size() > 1) {
                vespera::Vec3 point{};
                std::size_t count = 0;
                for (const auto id : state.selected_entity_ids) {
                    const auto entity_index = entity_index_from_id(state, id);
                    if (!entity_index) continue;
                    const auto world = editor_world_transform(state, state.scene.entities[*entity_index]);
                    point.x += world.position.x;
                    point.y += world.position.y;
                    point.z += world.position.z;
                    ++count;
                }
                if (count > 0) {
                    const float inv = 1.0f / static_cast<float>(count);
                    point.x *= inv; point.y *= inv; point.z *= inv;
                    return point;
                }
            }
            const auto& entity = state.scene.entities[*index];
            const auto world = editor_world_transform(state, entity);
            vespera::Vec3 point = world.position;
            if (entity.sprite_renderer) point.y += entity.sprite_renderer->size.z * world.scale.y * 0.5f;
            return point;
        }
        case SelectionKind::Camera:
            return state.scene.camera.position;
        case SelectionKind::Sector: {
            const auto& sectors = state.scene.world.sectors();
            if (state.selection.index >= sectors.size() || sectors[state.selection.index].vertices.empty()) return std::nullopt;
            const auto& sector = sectors[state.selection.index];
            vespera::Vec3 point{};
            for (const auto& v : sector.vertices) {
                point.x += v.x;
                point.z += v.z;
            }
            const float inv = 1.0f / static_cast<float>(sector.vertices.size());
            point.x *= inv;
            point.z *= inv;
            point.y = (sector.floor_height + sector.ceiling_height) * 0.5f;
            return point;
        }
        default:
            return std::nullopt;
    }
}

void point_editor_camera_at(vespera::Camera& camera, const vespera::Vec3& target) {
    const float dx = target.x - camera.position.x;
    const float dy = target.y - camera.position.y;
    const float dz = target.z - camera.position.z;
    const float horizontal = std::sqrt(dx * dx + dz * dz);
    if (horizontal < 0.0001f && std::abs(dy) < 0.0001f) return;
    camera.yaw = std::atan2(dx, dz);
    camera.pitch = std::atan2(dy, std::max(horizontal, 0.0001f));
}


vespera::Vec3 vec3_add(vespera::Vec3 a, vespera::Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

vespera::Vec3 vec3_sub(vespera::Vec3 a, vespera::Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

vespera::Vec3 vec3_mul(vespera::Vec3 a, float scalar) {
    return {a.x * scalar, a.y * scalar, a.z * scalar};
}

float vec3_dot(vespera::Vec3 a, vespera::Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

vespera::Vec3 vec3_cross(vespera::Vec3 a, vespera::Vec3 b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

vespera::Vec3 vec3_normalize(vespera::Vec3 value) {
    const float len_sq = vec3_dot(value, value);
    if (len_sq <= 0.000001f) return {0.0f, 0.0f, 0.0f};
    return vec3_mul(value, 1.0f / std::sqrt(len_sq));
}

struct EditorCameraBasis {
    vespera::Vec3 forward{};
    vespera::Vec3 right{};
    vespera::Vec3 up{};
};

EditorCameraBasis editor_camera_basis(const vespera::Camera& camera) {
    const float cos_pitch = std::cos(camera.pitch);
    EditorCameraBasis basis;
    basis.forward = vec3_normalize({
        std::sin(camera.yaw) * cos_pitch,
        std::sin(camera.pitch),
        std::cos(camera.yaw) * cos_pitch,
    });
    basis.right = vec3_normalize({std::cos(camera.yaw), 0.0f, -std::sin(camera.yaw)});
    basis.up = vec3_normalize(vec3_cross(basis.forward, basis.right));
    return basis;
}

struct ProjectedPoint {
    ImVec2 screen{};
    float depth = 0.0f;
    bool visible = false;
};

ProjectedPoint project_scene_point(
    const vespera::Camera& camera,
    ImVec2 canvas_min,
    ImVec2 canvas_size,
    vespera::Vec3 world
) {
    const auto basis = editor_camera_basis(camera);
    const auto relative = vec3_sub(world, camera.position);
    const float depth = vec3_dot(relative, basis.forward);
    if (depth <= std::max(camera.near_plane, 0.01f)) return {{}, depth, false};

    const float aspect = canvas_size.x / std::max(canvas_size.y, 1.0f);
    const float fov = std::clamp(camera.vertical_fov_degrees, 30.0f, 130.0f) * kDegreesToRadians;
    const float tan_half = std::tan(fov * 0.5f);
    if (tan_half <= 0.00001f) return {{}, depth, false};
    const float view_x = vec3_dot(relative, basis.right);
    const float view_y = vec3_dot(relative, basis.up);
    const float ndc_x = view_x / (depth * tan_half * aspect);
    const float ndc_y = view_y / (depth * tan_half);
    const ImVec2 screen{
        canvas_min.x + (ndc_x * 0.5f + 0.5f) * canvas_size.x,
        canvas_min.y + (0.5f - ndc_y * 0.5f) * canvas_size.y,
    };
    const bool visible = ndc_x >= -1.15f && ndc_x <= 1.15f && ndc_y >= -1.15f && ndc_y <= 1.15f;
    return {screen, depth, visible};
}

vespera::Vec3 rotate_axis_euler(vespera::Vec3 axis, const vespera::Vec3& rotation) {
    // XYZ Euler basis matching the Transform semantic API. This is editor-only
    // orientation math; no renderer handles or matrices leak into Scene data.
    const float cx = std::cos(rotation.x), sx = std::sin(rotation.x);
    const float cy = std::cos(rotation.y), sy = std::sin(rotation.y);
    const float cz = std::cos(rotation.z), sz = std::sin(rotation.z);

    vespera::Vec3 v{
        axis.x,
        axis.y * cx - axis.z * sx,
        axis.y * sx + axis.z * cx,
    };
    v = {
        v.x * cy + v.z * sy,
        v.y,
        -v.x * sy + v.z * cy,
    };
    v = {
        v.x * cz - v.y * sz,
        v.x * sz + v.y * cz,
        v.z,
    };
    return vec3_normalize(v);
}

vespera::Vec3 gizmo_world_axis(const vespera::TransformComponent& transform, GizmoAxis axis, bool local_space) {
    vespera::Vec3 value{};
    switch (axis) {
        case GizmoAxis::X: value = {1.0f, 0.0f, 0.0f}; break;
        case GizmoAxis::Y: value = {0.0f, 1.0f, 0.0f}; break;
        case GizmoAxis::Z: value = {0.0f, 0.0f, 1.0f}; break;
        default: return {};
    }
    return local_space ? rotate_axis_euler(value, transform.rotation) : value;
}

float snap_scalar(float value, float step, bool bypass) {
    if (bypass || step <= 0.0001f) return value;
    return std::round(value / step) * step;
}

float screen_distance_sq(ImVec2 a, ImVec2 b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

ImU32 gizmo_axis_color(GizmoAxis axis, bool active) {
    ImVec4 color;
    switch (axis) {
        case GizmoAxis::X: color = {0.92f, 0.25f, 0.24f, 1.0f}; break;
        case GizmoAxis::Y: color = {0.35f, 0.88f, 0.34f, 1.0f}; break;
        case GizmoAxis::Z: color = {0.27f, 0.52f, 0.98f, 1.0f}; break;
        case GizmoAxis::Uniform: color = {0.88f, 0.88f, 0.90f, 1.0f}; break;
        default: color = {0.7f, 0.7f, 0.72f, 1.0f}; break;
    }
    if (active) {
        color.x = std::min(color.x + 0.16f, 1.0f);
        color.y = std::min(color.y + 0.16f, 1.0f);
        color.z = std::min(color.z + 0.16f, 1.0f);
    }
    return ImGui::GetColorU32(color);
}

bool angle_in_arc(float angle, float begin, float end) {
    constexpr float two_pi = 6.2831853071795864769f;
    auto norm = [two_pi](float v) {
        while (v < 0.0f) v += two_pi;
        while (v >= two_pi) v -= two_pi;
        return v;
    };
    angle = norm(angle); begin = norm(begin); end = norm(end);
    if (begin <= end) return angle >= begin && angle <= end;
    return angle >= begin || angle <= end;
}

void draw_arc(ImDrawList* draw, ImVec2 center, float radius, float begin, float end, ImU32 color, float thickness) {
    constexpr int segments = 28;
    constexpr float two_pi = 6.2831853071795864769f;
    while (end < begin) end += two_pi;
    ImVec2 previous{center.x + std::cos(begin) * radius, center.y + std::sin(begin) * radius};
    for (int i = 1; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        const float angle = begin + (end - begin) * t;
        ImVec2 next{center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius};
        draw->AddLine(previous, next, color, thickness);
        previous = next;
    }
}

std::optional<std::size_t> pick_entity_in_3d(
    const EditorState& state,
    ImVec2 canvas_min,
    ImVec2 canvas_size,
    ImVec2 mouse
) {
    std::optional<std::size_t> best;
    float best_depth = std::numeric_limits<float>::max();
    float best_distance_sq = 20.0f * 20.0f;
    for (std::size_t i = 0; i < state.scene.entities.size(); ++i) {
        const auto& entity = state.scene.entities[i];
        if (!entity.enabled) continue;
        const auto world = editor_world_transform(state, entity);
        vespera::Vec3 pick_point = world.position;
        if (entity.sprite_renderer) {
            pick_point.y += entity.sprite_renderer->size.z * world.scale.y * 0.5f;
        }
        const auto projected = project_scene_point(state.scene_view_3d.camera, canvas_min, canvas_size, pick_point);
        if (!projected.visible) continue;
        float pick_radius = entity.sprite_renderer ? 26.0f : 15.0f;
        if (entity.point_light) pick_radius = std::max(pick_radius, 18.0f);
        const float distance_sq = screen_distance_sq(projected.screen, mouse);
        if (distance_sq <= pick_radius * pick_radius
            && (distance_sq < best_distance_sq - 0.01f || projected.depth < best_depth)) {
            best = i;
            best_distance_sq = distance_sq;
            best_depth = projected.depth;
        }
    }
    return best;
}

vespera::Vec3 selected_entity_gizmo_pivot(const EditorState& state, bool center_pivot) {
    const auto selected = selected_entity_index(state);
    if (!selected) return {};
    if (!center_pivot || state.selected_entity_ids.size() <= 1) {
        return editor_world_transform(state, state.scene.entities[*selected]).position;
    }
    vespera::Vec3 center{};
    std::size_t count = 0;
    for (const auto id : state.selected_entity_ids) {
        const auto index = entity_index_from_id(state, id);
        if (!index) continue;
        center = vec3_add(center, editor_world_transform(state, state.scene.entities[*index]).position);
        ++count;
    }
    return count > 0 ? vec3_mul(center, 1.0f / static_cast<float>(count))
                     : editor_world_transform(state, state.scene.entities[*selected]).position;
}

std::vector<std::pair<vespera::SceneObjectId, vespera::TransformComponent>> capture_selected_entity_transforms(
    const EditorState& state
) {
    std::vector<std::pair<vespera::SceneObjectId, vespera::TransformComponent>> result;
    if (state.selection.kind != SelectionKind::Entity) return result;
    result.reserve(std::max<std::size_t>(state.selected_entity_ids.size(), 1));
    if (!state.selected_entity_ids.empty()) {
        for (const auto id : state.selected_entity_ids) {
            const auto index = entity_index_from_id(state, id);
            if (index) result.emplace_back(id, editor_world_transform(state, state.scene.entities[*index]));
        }
    } else if (const auto selected = selected_entity_index(state)) {
        const auto& entity = state.scene.entities[*selected];
        result.emplace_back(entity.id, editor_world_transform(state, entity));
    }
    std::stable_sort(result.begin(), result.end(), [&](const auto& a, const auto& b) {
        const auto* ea = state.scene.find_entity(a.first);
        const auto* eb = state.scene.find_entity(b.first);
        const std::size_t da = ea ? entity_hierarchy_depth(state, *ea) : 0;
        const std::size_t db = eb ? entity_hierarchy_depth(state, *eb) : 0;
        return da < db;
    });
    return result;
}

vespera::Vec3 rotate_vector_around_axis(vespera::Vec3 value, vespera::Vec3 axis, float radians) {
    const float axis_length = std::sqrt(vec3_dot(axis, axis));
    if (axis_length <= 0.000001f) return value;
    axis = vec3_mul(axis, 1.0f / axis_length);
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return vec3_add(
        vec3_add(vec3_mul(value, c), vec3_mul(vec3_cross(axis, value), s)),
        vec3_mul(axis, vec3_dot(axis, value) * (1.0f - c))
    );
}

vespera::editor::EditorCommandKind transform_command_kind(SceneTool tool) {
    switch (tool) {
        case SceneTool::Move: return vespera::editor::EditorCommandKind::MoveEntityTransform;
        case SceneTool::Rotate: return vespera::editor::EditorCommandKind::RotateEntityTransform;
        case SceneTool::Scale: return vespera::editor::EditorCommandKind::ScaleEntityTransform;
        case SceneTool::View: break;
    }
    return vespera::editor::EditorCommandKind::Unknown;
}

const char* scene_tool_name(SceneTool tool) {
    switch (tool) {
        case SceneTool::View: return "View";
        case SceneTool::Move: return "Move";
        case SceneTool::Rotate: return "Rotate";
        case SceneTool::Scale: return "Scale";
    }
    return "Tool";
}

void cancel_3d_gizmo_drag(EditorState& state) {
    auto& view = state.scene_view_3d;
    if (!view.gizmo_dragging) return;
    for (const auto& [id, transform] : view.drag_start_transforms) {
        set_editor_world_transform(state, id, transform);
    }
    clear_active_edit(state);
    refresh_dirty(state);
    view.gizmo_dragging = false;
    view.gizmo_drag_changed = false;
    view.active_axis = GizmoAxis::None;
    view.drag_entity_id = vespera::kInvalidSceneObjectId;
    view.drag_start_transforms.clear();
}

void commit_3d_gizmo_drag(EditorState& state) {
    auto& view = state.scene_view_3d;
    if (!view.gizmo_dragging) return;
    const bool changed = view.gizmo_drag_changed;
    const SceneTool tool = view.tool;
    const std::size_t transform_count = view.drag_start_transforms.size();
    commit_active_edit(state);
    if (changed) {
        append_command_audit(
            state,
            transform_command_kind(tool),
            transform_count > 1
                ? std::format("{} {} entity transforms", scene_tool_name(tool), transform_count)
                : std::string(scene_tool_name(tool)) + " entity transform"
        );
    }
    view.gizmo_dragging = false;
    view.gizmo_drag_changed = false;
    view.active_axis = GizmoAxis::None;
    view.drag_entity_id = vespera::kInvalidSceneObjectId;
    view.drag_start_transforms.clear();
}

void frame_3d_selection(EditorState& state) {
    auto focus = selected_focus_point_3d(state);
    if (!focus) {
        const auto& sectors = state.scene.world.sectors();
        if (!sectors.empty()) {
            vespera::Vec3 center{};
            std::size_t count = 0;
            float min_floor = std::numeric_limits<float>::max();
            float max_ceil = std::numeric_limits<float>::lowest();
            for (const auto& sector : sectors) {
                for (const auto& vertex : sector.vertices) {
                    center.x += vertex.x;
                    center.z += vertex.z;
                    ++count;
                }
                min_floor = std::min(min_floor, sector.floor_height);
                max_ceil = std::max(max_ceil, sector.ceiling_height);
            }
            if (count > 0) {
                center.x /= static_cast<float>(count);
                center.z /= static_cast<float>(count);
                center.y = (min_floor + max_ceil) * 0.5f;
                focus = center;
            }
        }
    }
    if (!focus) return;

    state.scene_view_3d.camera.position = {
        focus->x - 5.5f,
        focus->y + 3.5f,
        focus->z - 5.5f,
    };
    point_editor_camera_at(state.scene_view_3d.camera, *focus);
    state.scene_view_3d.frame_selection_pending = false;
}

void draw_scene_view_3d(EditorState& state) {
    auto& view = state.scene_view_3d;
    view.visible = false;
    view.hovered = false;
    view.focused = false;

    if (!ImGui::Begin("Scene")) {
        ImGui::End();
        return;
    }

    ImGuiWindow* current_window = ImGui::GetCurrentWindow();
    const bool tab_visible = !current_window->DockIsActive || current_window->DockTabIsVisible;

    if (!view.initialized) {
        view.camera = state.scene.camera;
        view.camera.near_plane = std::max(view.camera.near_plane, 0.02f);
        view.initialized = true;
    }

    auto tool_button = [&](const char* label, SceneTool tool, const char* tooltip) {
        const bool active = view.tool == tool;
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(label, ImVec2(30.0f, 0.0f))) {
            if (view.gizmo_dragging) commit_3d_gizmo_drag(state);
            view.tool = tool;
        }
        if (active) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
    };
    tool_button("Q", SceneTool::View, "View tool (Q)");
    ImGui::SameLine(); tool_button("W", SceneTool::Move, "Move tool (W)");
    ImGui::SameLine(); tool_button("E", SceneTool::Rotate, "Rotate tool (E)");
    ImGui::SameLine(); tool_button("R", SceneTool::Scale, "Scale tool (R)");
    ImGui::SameLine();
    if (ImGui::Button("Frame Selected")) view.frame_selection_pending = true;
    ImGui::SameLine();
    if (ImGui::Button("Scene Camera")) {
        view.camera = state.scene.camera;
        view.initialized = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(view.local_space ? "Local" : "Global")) view.local_space = !view.local_space;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Transform orientation: %s", view.local_space ? "Local" : "Global");
    ImGui::SameLine();
    if (ImGui::Button(view.center_pivot ? "Center" : "Pivot")) view.center_pivot = !view.center_pivot;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Multi-selection gizmo position: %s", view.center_pivot ? "selection center" : "active entity pivot");
    ImGui::SameLine();
    ImGui::Checkbox("Snap", &view.snap_enabled);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(68.0f);
    if (view.tool == SceneTool::Move) {
        ImGui::DragFloat("##tool_snap", &view.translation_snap, 0.05f, 0.01f, 10.0f, "%.2f");
        view.translation_snap = std::max(view.translation_snap, 0.01f);
    } else if (view.tool == SceneTool::Rotate) {
        ImGui::DragFloat("##tool_snap", &view.rotation_snap_degrees, 1.0f, 1.0f, 90.0f, "%.0f deg");
        view.rotation_snap_degrees = std::clamp(view.rotation_snap_degrees, 1.0f, 90.0f);
    } else if (view.tool == SceneTool::Scale) {
        ImGui::DragFloat("##tool_snap", &view.scale_snap, 0.05f, 0.01f, 2.0f, "%.2f");
        view.scale_snap = std::max(view.scale_snap, 0.01f);
    } else {
        ImGui::TextDisabled("--");
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(72.0f);
    ImGui::DragFloat("Speed", &view.move_speed, 0.1f, 0.25f, 30.0f, "%.1f");
    view.move_speed = std::clamp(view.move_speed, 0.25f, 30.0f);
    ImGui::SameLine();
    ImGui::TextDisabled("LMB select/edit   RMB + WASD/QE fly   F frame   Alt bypass snap");

    if (view.frame_selection_pending) frame_3d_selection(state);

    ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    canvas_size.x = std::max(canvas_size.x, 64.0f);
    canvas_size.y = std::max(canvas_size.y, 64.0f);
    const ImVec2 canvas_min = ImGui::GetCursorScreenPos();

    if (view.preview_texture != ImTextureID_Invalid) {
        ImGui::Image(
            ImTextureRef(view.preview_texture),
            canvas_size,
            view.preview_uv0,
            view.preview_uv1
        );
    } else {
        ImGui::InvisibleButton(
            "##scene_view_3d_canvas",
            canvas_size,
            ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight
        );
    }
    const ImVec2 canvas_max = ImGui::GetItemRectMax();
    view.content_min = ImGui::GetItemRectMin();
    view.content_max = canvas_max;
    view.hovered = tab_visible && ImGui::IsItemHovered();
    view.focused = tab_visible && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    view.visible = tab_visible && canvas_size.x > 1.0f && canvas_size.y > 1.0f;

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VESPERA_PROJECT_ASSET")) {
            if (const auto* record = asset_record_from_payload(state, payload); record) {
                if (record->kind == vespera::AssetKind::EntityPrefab) {
                    const float cos_pitch = std::cos(view.camera.pitch);
                    const vespera::Vec3 forward{
                        std::sin(view.camera.yaw) * cos_pitch,
                        std::sin(view.camera.pitch),
                        std::cos(view.camera.yaw) * cos_pitch,
                    };
                    const vespera::Vec3 drop_position{
                        view.camera.position.x + forward.x * 3.0f,
                        view.camera.position.y + forward.y * 3.0f,
                        view.camera.position.z + forward.z * 3.0f,
                    };
                    command_instantiate_prefab(state, record->absolute_path, drop_position,
                        vespera::editor::EditorCommandKind::DropPrefabIntoScene);
                } else if (record->kind == vespera::AssetKind::Material) {
                    const auto picked = pick_entity_in_3d(state, canvas_min, canvas_size, ImGui::GetIO().MousePos);
                    if (picked && *picked < state.scene.entities.size() && state.scene.entities[*picked].mesh_renderer) {
                        commit_active_edit(state);
                        const std::uint64_t before_state_id = state.current_state_id;
                        HistorySnapshot before = capture_snapshot(state);
                        auto& entity = state.scene.entities[*picked];
                        entity.mesh_renderer->material = {record->asset_id, record->relative_path};
                        entity.mesh_renderer->material_resolved = false;
                        (void)vespera::hydrate_scene_materials(state.scene, state.asset_catalog);
                        const auto entity_id = entity.id;
                        select_entity(state, *picked);
                        record_immediate_edit(state, std::move(before), "Assign Material from Project");
                        append_command_audit(state, vespera::editor::EditorCommandKind::AssignMaterialAsset,
                            "Assign Material from Project", true, before_state_id, state.current_state_id,
                            entity_id, record->asset_id);
                        push_console(state, ConsoleEntry::Level::Info,
                            "Assigned Material '" + record->display_name + "' to " + entity.name + ".");
                    } else {
                        push_console(state, ConsoleEntry::Level::Warning,
                            "Material drop needs a Mesh Renderer under the cursor.");
                    }
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImGuiIO& io = ImGui::GetIO();
    if (view.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        ImGui::SetWindowFocus();
    }

    const bool navigating = view.hovered && ImGui::IsMouseDown(ImGuiMouseButton_Right);
    if (navigating) {
        constexpr float kLookSensitivity = 0.0045f;
        view.camera.yaw += io.MouseDelta.x * kLookSensitivity;
        view.camera.pitch = std::clamp(
            view.camera.pitch - io.MouseDelta.y * kLookSensitivity,
            -1.50f,
            1.50f
        );

        const float dt = std::clamp(io.DeltaTime, 0.0f, 0.1f);
        const float speed = view.move_speed * (ImGui::IsKeyDown(ImGuiKey_LeftShift) ? 3.0f : 1.0f);
        const float cos_pitch = std::cos(view.camera.pitch);
        const vespera::Vec3 forward{
            std::sin(view.camera.yaw) * cos_pitch,
            std::sin(view.camera.pitch),
            std::cos(view.camera.yaw) * cos_pitch,
        };
        const vespera::Vec3 right{
            std::cos(view.camera.yaw),
            0.0f,
            -std::sin(view.camera.yaw),
        };

        float move_forward = 0.0f;
        float move_right = 0.0f;
        float move_up = 0.0f;
        if (ImGui::IsKeyDown(ImGuiKey_W)) move_forward += 1.0f;
        if (ImGui::IsKeyDown(ImGuiKey_S)) move_forward -= 1.0f;
        if (ImGui::IsKeyDown(ImGuiKey_D)) move_right += 1.0f;
        if (ImGui::IsKeyDown(ImGuiKey_A)) move_right -= 1.0f;
        if (ImGui::IsKeyDown(ImGuiKey_E)) move_up += 1.0f;
        if (ImGui::IsKeyDown(ImGuiKey_Q)) move_up -= 1.0f;

        const float step = speed * dt;
        view.camera.position.x += (forward.x * move_forward + right.x * move_right) * step;
        view.camera.position.y += (forward.y * move_forward + move_up) * step;
        view.camera.position.z += (forward.z * move_forward + right.z * move_right) * step;
    }

    if (view.hovered && std::abs(io.MouseWheel) > 0.001f && !navigating) {
        view.move_speed = std::clamp(view.move_speed * std::pow(1.15f, io.MouseWheel), 0.25f, 30.0f);
    }

    if (view.focused && !navigating && !io.WantTextInput && !view.gizmo_dragging) {
        if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) view.tool = SceneTool::View;
        if (ImGui::IsKeyPressed(ImGuiKey_W, false)) view.tool = SceneTool::Move;
        if (ImGui::IsKeyPressed(ImGuiKey_E, false)) view.tool = SceneTool::Rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) view.tool = SceneTool::Scale;
        if (ImGui::IsKeyPressed(ImGuiKey_F, false)) view.frame_selection_pending = true;
    }

    const auto selected_index = selected_entity_index(state);
    const ImVec2 live_canvas_size{canvas_max.x - canvas_min.x, canvas_max.y - canvas_min.y};
    bool gizmo_consumed_click = false;

    if (selected_index && *selected_index < state.scene.entities.size() && view.tool != SceneTool::View) {
        auto& entity = state.scene.entities[*selected_index];
        const vespera::Vec3 gizmo_origin = selected_entity_gizmo_pivot(state, view.center_pivot);
        const auto origin_projected = project_scene_point(view.camera, canvas_min, live_canvas_size, gizmo_origin);
        if (origin_projected.visible) {
            const float world_per_pixel = (2.0f * origin_projected.depth
                * std::tan(std::clamp(view.camera.vertical_fov_degrees, 30.0f, 130.0f) * kDegreesToRadians * 0.5f))
                / std::max(live_canvas_size.y, 1.0f);
            const float gizmo_world_length = std::clamp(world_per_pixel * 76.0f, 0.15f, 25.0f);

            std::array<GizmoAxis, 3> axes{GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z};
            struct AxisProjection { GizmoAxis axis; ImVec2 end; ImVec2 unit; float screen_length; };
            std::vector<AxisProjection> projected_axes;
            projected_axes.reserve(3);
            for (const auto axis : axes) {
                const auto world_axis = gizmo_world_axis(editor_world_transform(state, entity), axis, view.local_space);
                const auto endpoint = project_scene_point(
                    view.camera, canvas_min, live_canvas_size,
                    vec3_add(gizmo_origin, vec3_mul(world_axis, gizmo_world_length))
                );
                if (!endpoint.visible) continue;
                const float dx = endpoint.screen.x - origin_projected.screen.x;
                const float dy = endpoint.screen.y - origin_projected.screen.y;
                const float len = std::sqrt(dx * dx + dy * dy);
                if (len < 12.0f) continue;
                projected_axes.push_back({axis, endpoint.screen, {dx / len, dy / len}, len});
            }

            if (view.tool == SceneTool::Move || view.tool == SceneTool::Scale) {
                if (view.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    float best_distance_sq = 8.0f * 8.0f;
                    GizmoAxis picked = GizmoAxis::None;
                    ImVec2 picked_unit{};
                    for (const auto& axis : projected_axes) {
                        const float d = point_segment_distance_sq(io.MousePos, origin_projected.screen, axis.end);
                        if (d <= best_distance_sq) {
                            best_distance_sq = d;
                            picked = axis.axis;
                            picked_unit = axis.unit;
                        }
                    }
                    if (view.tool == SceneTool::Scale
                        && screen_distance_sq(io.MousePos, origin_projected.screen) <= 9.0f * 9.0f) {
                        picked = GizmoAxis::Uniform;
                        picked_unit = {0.7071067f, -0.7071067f};
                    }
                    if (picked != GizmoAxis::None) {
                        commit_active_edit(state);
                        view.active_axis = picked;
                        view.drag_entity_id = entity.id;
                        view.drag_start_transform = editor_world_transform(state, entity);
                        view.drag_start_transforms = capture_selected_entity_transforms(state);
                        view.drag_pivot = gizmo_origin;
                        view.drag_start_mouse = io.MousePos;
                        view.drag_axis_screen_unit = picked_unit;
                        view.drag_world_length = gizmo_world_length;
                        view.gizmo_dragging = true;
                        view.gizmo_drag_changed = false;
                        begin_edit(state, capture_snapshot(state),
                            view.tool == SceneTool::Move ? "Move entity gizmo" : "Scale entity gizmo");
                        gizmo_consumed_click = true;
                    }
                }
            } else if (view.tool == SceneTool::Rotate) {
                constexpr float ring_radius = 54.0f;
                if (view.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    const float dx = io.MousePos.x - origin_projected.screen.x;
                    const float dy = io.MousePos.y - origin_projected.screen.y;
                    const float radius = std::sqrt(dx * dx + dy * dy);
                    if (std::abs(radius - ring_radius) <= 9.0f) {
                        float angle = std::atan2(dy, dx);
                        if (angle < 0.0f) angle += 6.2831853071795864769f;
                        GizmoAxis picked = GizmoAxis::None;
                        if (angle_in_arc(angle, -0.15f, 1.82f)) picked = GizmoAxis::X;
                        else if (angle_in_arc(angle, 1.95f, 3.92f)) picked = GizmoAxis::Y;
                        else if (angle_in_arc(angle, 4.05f, 6.02f)) picked = GizmoAxis::Z;
                        if (picked != GizmoAxis::None) {
                            commit_active_edit(state);
                            view.active_axis = picked;
                            view.drag_entity_id = entity.id;
                            view.drag_start_transform = editor_world_transform(state, entity);
                            view.drag_start_transforms = capture_selected_entity_transforms(state);
                            view.drag_pivot = gizmo_origin;
                            view.drag_start_mouse = io.MousePos;
                            view.drag_start_angle = std::atan2(dy, dx);
                            view.gizmo_dragging = true;
                            view.gizmo_drag_changed = false;
                            begin_edit(state, capture_snapshot(state), "Rotate entity gizmo");
                            gizmo_consumed_click = true;
                        }
                    }
                }
            }
        }
    }

    if (view.gizmo_dragging) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            cancel_3d_gizmo_drag(state);
        } else if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (const auto drag_index = entity_index_from_id(state, view.drag_entity_id)) {
                (void)drag_index;
                const bool bypass_snap = io.KeyAlt || !view.snap_enabled;
                const bool multi = view.drag_start_transforms.size() > 1;
                const auto world_axis = gizmo_world_axis(view.drag_start_transform, view.active_axis, view.local_space);
                const float mouse_dx = io.MousePos.x - view.drag_start_mouse.x;
                const float mouse_dy = io.MousePos.y - view.drag_start_mouse.y;
                const float pixels = mouse_dx * view.drag_axis_screen_unit.x + mouse_dy * view.drag_axis_screen_unit.y;
                float move_amount = snap_scalar(pixels / 76.0f * view.drag_world_length, view.translation_snap, bypass_snap);
                float scale_amount = snap_scalar(pixels / 90.0f, view.scale_snap, bypass_snap);

                float rotation_radians = 0.0f;
                if (view.tool == SceneTool::Rotate) {
                    const auto projected = project_scene_point(view.camera, canvas_min, live_canvas_size, view.drag_pivot);
                    const float angle = std::atan2(io.MousePos.y - projected.screen.y, io.MousePos.x - projected.screen.x);
                    float delta = angle - view.drag_start_angle;
                    while (delta > 3.14159265f) delta -= 6.28318531f;
                    while (delta < -3.14159265f) delta += 6.28318531f;
                    const float degrees = snap_scalar(delta * kRadiansToDegrees, view.rotation_snap_degrees, bypass_snap);
                    rotation_radians = degrees * kDegreesToRadians;
                }

                bool any_changed = false;
                for (const auto& [id, start_transform] : view.drag_start_transforms) {
                    const auto index = entity_index_from_id(state, id);
                    if (!index) continue;
                    vespera::TransformComponent next = start_transform;

                    if (view.tool == SceneTool::Move) {
                        next.position = vec3_add(start_transform.position, vec3_mul(world_axis, move_amount));
                    } else if (view.tool == SceneTool::Scale) {
                        if (!multi) {
                            if (view.active_axis == GizmoAxis::Uniform) {
                                next.scale.x = std::max(0.01f, start_transform.scale.x + scale_amount);
                                next.scale.y = std::max(0.01f, start_transform.scale.y + scale_amount);
                                next.scale.z = std::max(0.01f, start_transform.scale.z + scale_amount);
                            } else if (view.active_axis == GizmoAxis::X) next.scale.x = std::max(0.01f, start_transform.scale.x + scale_amount);
                            else if (view.active_axis == GizmoAxis::Y) next.scale.y = std::max(0.01f, start_transform.scale.y + scale_amount);
                            else if (view.active_axis == GizmoAxis::Z) next.scale.z = std::max(0.01f, start_transform.scale.z + scale_amount);
                        } else {
                            const float factor = std::max(0.01f, 1.0f + scale_amount);
                            vespera::Vec3 relative{
                                start_transform.position.x - view.drag_pivot.x,
                                start_transform.position.y - view.drag_pivot.y,
                                start_transform.position.z - view.drag_pivot.z
                            };
                            if (view.active_axis == GizmoAxis::Uniform) {
                                relative = vec3_mul(relative, factor);
                                next.scale = vec3_mul(start_transform.scale, factor);
                            } else {
                                const float along = vec3_dot(relative, world_axis);
                                relative = vec3_add(relative, vec3_mul(world_axis, along * (factor - 1.0f)));
                                if (view.active_axis == GizmoAxis::X) next.scale.x = std::max(0.01f, start_transform.scale.x * factor);
                                else if (view.active_axis == GizmoAxis::Y) next.scale.y = std::max(0.01f, start_transform.scale.y * factor);
                                else if (view.active_axis == GizmoAxis::Z) next.scale.z = std::max(0.01f, start_transform.scale.z * factor);
                            }
                            next.position = vec3_add(view.drag_pivot, relative);
                        }
                    } else if (view.tool == SceneTool::Rotate) {
                        if (multi) {
                            vespera::Vec3 relative{
                                start_transform.position.x - view.drag_pivot.x,
                                start_transform.position.y - view.drag_pivot.y,
                                start_transform.position.z - view.drag_pivot.z
                            };
                            next.position = vec3_add(
                                view.drag_pivot,
                                rotate_vector_around_axis(relative, world_axis, rotation_radians)
                            );
                        }
                        if (view.active_axis == GizmoAxis::X) next.rotation.x = start_transform.rotation.x + rotation_radians;
                        else if (view.active_axis == GizmoAxis::Y) next.rotation.y = start_transform.rotation.y + rotation_radians;
                        else if (view.active_axis == GizmoAxis::Z) next.rotation.z = start_transform.rotation.z + rotation_radians;
                    }

                    const auto changed_vec3 = [](vespera::Vec3 a, vespera::Vec3 b) {
                        return std::abs(a.x-b.x) > 0.00001f || std::abs(a.y-b.y) > 0.00001f || std::abs(a.z-b.z) > 0.00001f;
                    };
                    const auto live_transform = editor_world_transform(state, state.scene.entities[*index]);
                    if (changed_vec3(live_transform.position, next.position)
                        || changed_vec3(live_transform.rotation, next.rotation)
                        || changed_vec3(live_transform.scale, next.scale)) {
                        set_editor_world_transform(state, id, next);
                        any_changed = true;
                    }
                }

                if (any_changed) {
                    view.gizmo_drag_changed = true;
                    state.active_edit_changed = true;
                    state.dirty = true;
                }
            }
        } else {
            commit_3d_gizmo_drag(state);
        }
    }

    // Scene click selection. Gizmo handles consume their click first; otherwise
    // entity markers are picked in projected screen space. Sector topology stays
    // intentionally authored in the dedicated Sector tab for now.
    if (view.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
        && !navigating && !gizmo_consumed_click && !view.gizmo_dragging) {
        commit_active_edit(state);
        if (const auto picked = pick_entity_in_3d(state, canvas_min, live_canvas_size, io.MousePos)) {
            if (io.KeyCtrl) toggle_entity_selection(state, *picked);
            else select_entity(state, *picked);
        } else if (!io.KeyCtrl) {
            state.selection = {};
            state.selected_entity_ids.clear();
        }
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (view.preview_texture == ImTextureID_Invalid) {
        const char* waiting = "Preparing live D3D12 preview...";
        const ImVec2 waiting_size = ImGui::CalcTextSize(waiting);
        draw->AddText(
            {(canvas_min.x + canvas_max.x - waiting_size.x) * 0.5f,
             (canvas_min.y + canvas_max.y - waiting_size.y) * 0.5f},
            ImGui::GetColorU32(ImVec4(0.72f, 0.74f, 0.80f, 1.0f)),
            waiting
        );
    }
    const ImU32 border = ImGui::GetColorU32(ImVec4(0.36f, 0.39f, 0.45f, 0.9f));
    draw->AddRect(canvas_min, canvas_max, border, 0.0f, 0, 1.0f);
    const ImVec2 center{(canvas_min.x + canvas_max.x) * 0.5f, (canvas_min.y + canvas_max.y) * 0.5f};
    const ImU32 crosshair = ImGui::GetColorU32(ImVec4(0.92f, 0.92f, 0.92f, 0.65f));
    draw->AddLine({center.x - 5.0f, center.y}, {center.x + 5.0f, center.y}, crosshair, 1.0f);
    draw->AddLine({center.x, center.y - 5.0f}, {center.x, center.y + 5.0f}, crosshair, 1.0f);

    if (state.selection.kind == SelectionKind::Entity && state.selected_entity_ids.size() > 1) {
        for (const auto id : state.selected_entity_ids) {
            const auto index = entity_index_from_id(state, id);
            if (!index) continue;
            const auto world = editor_world_transform(state, state.scene.entities[*index]);
            const auto point = project_scene_point(view.camera, canvas_min, live_canvas_size, world.position);
            if (point.visible) draw->AddCircle(point.screen, 8.0f, ImGui::GetColorU32(ImVec4(0.96f, 0.70f, 0.20f, 0.78f)), 20, 1.5f);
        }
    }

    if (const auto draw_selected = selected_entity_index(state)) {
        const auto& entity = state.scene.entities[*draw_selected];
        const vespera::Vec3 gizmo_origin = selected_entity_gizmo_pivot(state, view.center_pivot);
        const auto origin = project_scene_point(view.camera, canvas_min, live_canvas_size, gizmo_origin);
        if (origin.visible) {
            // Selection marker remains visible even with View tool active.
            draw->AddCircle(origin.screen, 11.0f, ImGui::GetColorU32(ImVec4(1.0f, 0.78f, 0.22f, 0.95f)), 24, 2.0f);
            const std::string label = state.selected_entity_ids.size() > 1
                ? std::format("{} selected", state.selected_entity_ids.size())
                : (entity.name.empty() ? std::format("Entity {}", entity.id) : entity.name);
            draw->AddText({origin.screen.x + 14.0f, origin.screen.y - 18.0f},
                ImGui::GetColorU32(ImVec4(1.0f, 0.91f, 0.68f, 0.96f)), label.c_str());

            if (view.tool != SceneTool::View) {
                const float world_per_pixel = (2.0f * origin.depth
                    * std::tan(std::clamp(view.camera.vertical_fov_degrees, 30.0f, 130.0f) * kDegreesToRadians * 0.5f))
                    / std::max(live_canvas_size.y, 1.0f);
                const float length = std::clamp(world_per_pixel * 76.0f, 0.15f, 25.0f);
                if (view.tool == SceneTool::Move || view.tool == SceneTool::Scale) {
                    for (const auto axis : {GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z}) {
                        const auto world_axis = gizmo_world_axis(editor_world_transform(state, entity), axis, view.local_space);
                        const auto endpoint = project_scene_point(view.camera, canvas_min, live_canvas_size,
                            vec3_add(gizmo_origin, vec3_mul(world_axis, length)));
                        if (!endpoint.visible) continue;
                        const bool active = view.gizmo_dragging && view.active_axis == axis;
                        const ImU32 color = gizmo_axis_color(axis, active);
                        draw->AddLine(origin.screen, endpoint.screen, color, active ? 4.0f : 3.0f);
                        const float dx = endpoint.screen.x - origin.screen.x;
                        const float dy = endpoint.screen.y - origin.screen.y;
                        const float len = std::sqrt(dx*dx + dy*dy);
                        if (len > 4.0f) {
                            const ImVec2 unit{dx/len, dy/len};
                            if (view.tool == SceneTool::Move) {
                                const ImVec2 perp{-unit.y, unit.x};
                                const ImVec2 tip = endpoint.screen;
                                const ImVec2 a{tip.x - unit.x*10.0f + perp.x*5.0f, tip.y - unit.y*10.0f + perp.y*5.0f};
                                const ImVec2 b{tip.x - unit.x*10.0f - perp.x*5.0f, tip.y - unit.y*10.0f - perp.y*5.0f};
                                draw->AddTriangleFilled(tip, a, b, color);
                            } else {
                                draw->AddRectFilled({endpoint.screen.x-4.5f, endpoint.screen.y-4.5f},
                                    {endpoint.screen.x+4.5f, endpoint.screen.y+4.5f}, color, 1.0f);
                            }
                        }
                    }
                    if (view.tool == SceneTool::Scale) {
                        const ImU32 center_color = gizmo_axis_color(GizmoAxis::Uniform,
                            view.gizmo_dragging && view.active_axis == GizmoAxis::Uniform);
                        draw->AddRectFilled({origin.screen.x-5.0f, origin.screen.y-5.0f},
                            {origin.screen.x+5.0f, origin.screen.y+5.0f}, center_color, 1.0f);
                    }
                } else if (view.tool == SceneTool::Rotate) {
                    constexpr float ring_radius = 54.0f;
                    draw_arc(draw, origin.screen, ring_radius, -0.15f, 1.82f,
                        gizmo_axis_color(GizmoAxis::X, view.gizmo_dragging && view.active_axis == GizmoAxis::X), 3.0f);
                    draw_arc(draw, origin.screen, ring_radius, 1.95f, 3.92f,
                        gizmo_axis_color(GizmoAxis::Y, view.gizmo_dragging && view.active_axis == GizmoAxis::Y), 3.0f);
                    draw_arc(draw, origin.screen, ring_radius, 4.05f, 6.02f,
                        gizmo_axis_color(GizmoAxis::Z, view.gizmo_dragging && view.active_axis == GizmoAxis::Z), 3.0f);
                    draw->AddText({origin.screen.x + 41.0f, origin.screen.y + 20.0f}, gizmo_axis_color(GizmoAxis::X, false), "X");
                    draw->AddText({origin.screen.x - 45.0f, origin.screen.y + 12.0f}, gizmo_axis_color(GizmoAxis::Y, false), "Y");
                    draw->AddText({origin.screen.x + 12.0f, origin.screen.y - 58.0f}, gizmo_axis_color(GizmoAxis::Z, false), "Z");
                }
            }
        }
    }

    const std::string camera_text = std::format(
        "Editor Camera  X {:.2f}  Y {:.2f}  Z {:.2f}",
        view.camera.position.x,
        view.camera.position.y,
        view.camera.position.z
    );
    const ImVec2 text_pos{canvas_min.x + 10.0f, canvas_min.y + 9.0f};
    const ImVec2 text_size = ImGui::CalcTextSize(camera_text.c_str());
    draw->AddRectFilled(
        {text_pos.x - 5.0f, text_pos.y - 4.0f},
        {text_pos.x + text_size.x + 5.0f, text_pos.y + text_size.y + 4.0f},
        ImGui::GetColorU32(ImVec4(0.03f, 0.035f, 0.045f, 0.72f)),
        3.0f
    );
    draw->AddText(text_pos, ImGui::GetColorU32(ImVec4(0.88f, 0.90f, 0.94f, 1.0f)), camera_text.c_str());

    ImGui::End();
}

void draw_game_view(EditorState& state) {
    auto& view = state.game_view;
    view.visible = false;
    view.hovered = false;
    view.focused = false;
    if (view.focus_pending && editor_is_playing(state)) {
        ImGui::SetNextWindowFocus();
        view.focus_pending = false;
    }
    if (!ImGui::Begin("Game")) {
        ImGui::End();
        return;
    }

    ImGuiWindow* current_window = ImGui::GetCurrentWindow();
    const bool tab_visible = !current_window->DockIsActive || current_window->DockTabIsVisible;
    const bool playing = editor_is_playing(state);
    if (playing) {
        const char* status = state.play_state == EditorPlayState::Paused ? "PAUSED" : "PLAYING";
        ImGui::TextColored(ImVec4(0.69f, 0.58f, 1.0f, 1.0f), "%s", status);
        ImGui::SameLine();
        ImGui::TextDisabled("%.2f s", state.play_time_seconds);
        if (state.play_runtime) {
            const auto& runtime_status = state.play_runtime->status();
            ImGui::SameLine();
            if (runtime_status.managed_ready) {
                ImGui::TextColored(ImVec4(0.48f, 0.86f, 0.62f, 1.0f),
                    "C# %zu", runtime_status.managed_script_count);
            } else {
                ImGui::TextDisabled("native play");
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("| %s", view.input_captured ? "INPUT CAPTURED - Esc releases" : "UI pointer | click empty view for WASD + mouse");
    } else {
        ImGui::TextDisabled("Game camera preview. Press Play, then click the Game viewport to run with WASD + mouse.");
    }

    ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    canvas_size.x = std::max(canvas_size.x, 64.0f);
    canvas_size.y = std::max(canvas_size.y, 64.0f);
    const ImVec2 canvas_min = ImGui::GetCursorScreenPos();
    if (view.preview_texture != ImTextureID_Invalid) {
        ImGui::Image(ImTextureRef(view.preview_texture), canvas_size, view.preview_uv0, view.preview_uv1);
    } else {
        ImGui::InvisibleButton("##game_view_canvas", canvas_size);
    }
    const ImVec2 canvas_max = ImGui::GetItemRectMax();
    view.content_min = ImGui::GetItemRectMin();
    view.content_max = canvas_max;
    view.hovered = tab_visible && ImGui::IsItemHovered();
    view.focused = tab_visible && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    view.visible = tab_visible && canvas_size.x > 1.0f && canvas_size.y > 1.0f;

    if (!view.visible && view.input_captured) {
        set_game_input_capture(state, false);
    }

    // UI gets first refusal on a Game-view click while the pointer is free.
    // This keeps first-person mouse capture from stealing the same click that
    // should press a runtime Button/TextInput. Clicking empty game space still
    // captures relative mouse for normal WASD + mouselook play.
    bool ui_interactable_under_pointer = false;
    if (playing && view.hovered && !view.input_captured) {
        if (state.play_rml_ui_loaded && state.play_rml_ui) {
            // RmlUi hover is resolved from the previous runtime frame. This is
            // sufficient to give controls first refusal without exposing RmlUi DOM types.
            ui_interactable_under_pointer = !state.play_rml_ui->hovered_id().empty();
        } else if (state.play_ui_loaded) {
            const ImGuiIO& io = ImGui::GetIO();
            const float local_x = std::clamp(io.MousePos.x - view.content_min.x, 0.0f, canvas_size.x);
            const float local_y = std::clamp(io.MousePos.y - view.content_min.y, 0.0f, canvas_size.y);
            const auto layout = vespera::resolve_ui_layout(
                state.play_ui_document, canvas_size.x, canvas_size.y);
            ui_interactable_under_pointer = vespera::ui_hit_test(
                state.play_ui_document, layout, {local_x, local_y}, true).has_value();
        }
    }
    if (playing && view.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ImGui::SetWindowFocus();
        if (!ui_interactable_under_pointer) {
            set_game_input_capture(state, true);
        }
    }
    if (view.input_captured && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        set_game_input_capture(state, false);
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 border = view.input_captured
        ? ImGui::GetColorU32(ImVec4(0.49f, 0.55f, 1.0f, 1.0f))
        : ImGui::GetColorU32(ImVec4(0.36f, 0.39f, 0.45f, 0.9f));
    draw->AddRect(canvas_min, canvas_max, border, 0.0f, 0, view.input_captured ? 2.0f : 1.0f);
    if (view.preview_texture == ImTextureID_Invalid) {
        const char* waiting = "Preparing Game preview...";
        const ImVec2 size = ImGui::CalcTextSize(waiting);
        draw->AddText({(canvas_min.x + canvas_max.x - size.x) * 0.5f,
                       (canvas_min.y + canvas_max.y - size.y) * 0.5f},
            ImGui::GetColorU32(ImGuiCol_TextDisabled), waiting);
    }

    if (playing) {
        const char* note = view.input_captured
            ? "WASD move | mouse look | Shift sprint | Esc release | Stop restores edit scene"
            : "Runtime UI is clickable | click empty Game space to capture FPS input";
        const ImVec2 size = ImGui::CalcTextSize(note);
        const ImVec2 pos{canvas_min.x + 10.0f, canvas_max.y - size.y - 12.0f};
        draw->AddRectFilled({pos.x - 5.0f, pos.y - 4.0f},
            {pos.x + size.x + 5.0f, pos.y + size.y + 4.0f},
            ImGui::GetColorU32(ImVec4(0.03f, 0.035f, 0.045f, 0.80f)), 3.0f);
        draw->AddText(pos, ImGui::GetColorU32(view.input_captured ? ImGuiCol_Text : ImGuiCol_TextDisabled), note);
    }

    ImGui::End();
}


const vespera::AssetRecord* ui_authoring_record(const EditorState& state) {
    if (state.ui_authoring.asset_id.empty()) return nullptr;
    return state.asset_catalog.find_by_id(state.ui_authoring.asset_id);
}

bool save_ui_authoring(EditorState& state) {
    auto& ui = state.ui_authoring;
    if (!ui.open || ui.path.empty()) return false;
    const auto saved = vespera::save_ui_document(ui.document, ui.path);
    ui.message = saved.message;
    if (!saved) {
        push_console(state, ConsoleEntry::Level::Error, "UI authoring save failed: " + saved.message);
        return false;
    }
    ui.dirty = false;
    refresh_asset_catalog(state, false, true);
    push_console(state, ConsoleEntry::Level::Info, saved.message);
    return true;
}

bool open_ui_authoring(EditorState& state, const vespera::AssetRecord& record) {
    if (record.kind != vespera::AssetKind::UiDocument) return false;
    auto& ui = state.ui_authoring;
    if (ui.dirty && !ui.path.empty()) {
        if (ui.path == record.absolute_path) {
            ui.open = true;
            return true;
        }
        if (!save_ui_authoring(state)) return false;
    }
    vespera::UiDocument document;
    const auto loaded = vespera::load_ui_document(document, record.absolute_path);
    if (!loaded) {
        ui.message = loaded.message;
        push_console(state, ConsoleEntry::Level::Error, "UI authoring open failed: " + loaded.message);
        return false;
    }
    ui.open = true;
    ui.asset_id = record.asset_id;
    ui.path = record.absolute_path;
    ui.document = std::move(document);
    ui.selected_node = vespera::kInvalidUiNodeId;
    for (const auto& node : ui.document.nodes()) {
        if (node.type == vespera::UiNodeType::Canvas) { ui.selected_node = node.id; break; }
    }
    if (ui.selected_node == vespera::kInvalidUiNodeId && !ui.document.nodes().empty())
        ui.selected_node = ui.document.nodes().front().id;
    ui.preview_width = 1280;
    ui.preview_height = 720;
    if (const auto* canvas = ui.document.find(ui.selected_node); canvas && canvas->type == vespera::UiNodeType::Canvas) {
        ui.preview_width = std::max(320, static_cast<int>(std::lround(canvas->canvas.reference_resolution.x)));
        ui.preview_height = std::max(200, static_cast<int>(std::lround(canvas->canvas.reference_resolution.y)));
    }
    ui.dirty = false;
    ui.preview_drag_mode = 0;
    ui.message = loaded.message;
    return true;
}

void mark_ui_dirty(EditorState& state) {
    state.ui_authoring.dirty = true;
}

bool ui_node_has_children(const vespera::UiDocument& document, vespera::UiNodeId id) {
    return std::any_of(document.nodes().begin(), document.nodes().end(),
        [id](const vespera::UiNode& node) { return node.parent_id == id; });
}

void draw_ui_authoring_tree_node(UiAuthoringState& ui, vespera::UiNodeId id) {
    const auto* node = ui.document.find(id);
    if (!node) return;
    const bool children = ui_node_has_children(ui.document, id);
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!children) flags |= ImGuiTreeNodeFlags_Leaf;
    if (ui.selected_node == id) flags |= ImGuiTreeNodeFlags_Selected;
    ImGui::PushID(static_cast<int>(id));
    const std::string label = std::format("{}  [{}]", node->name, vespera::ui_node_type_name(node->type));
    const bool open = ImGui::TreeNodeEx("##ui_node", flags, "%s", label.c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) ui.selected_node = id;
    if (open) {
        for (const auto& child : ui.document.nodes()) {
            if (child.parent_id == id) draw_ui_authoring_tree_node(ui, child.id);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void add_ui_authoring_node(EditorState& state, vespera::UiNodeType type) {
    auto& ui = state.ui_authoring;
    auto& node = ui.document.create_node(type);
    if (type != vespera::UiNodeType::Canvas) {
        vespera::UiNodeId parent = ui.selected_node;
        if (parent == vespera::kInvalidUiNodeId || !ui.document.find(parent)) {
            for (const auto& candidate : ui.document.nodes()) {
                if (candidate.type == vespera::UiNodeType::Canvas) { parent = candidate.id; break; }
            }
        }
        std::string error;
        if (parent != vespera::kInvalidUiNodeId && !ui.document.reparent(node.id, parent, &error)) {
            ui.message = error;
        }
    }
    ui.selected_node = node.id;
    ui.dirty = true;
}

void draw_ui_node_properties(EditorState& state) {
    auto& ui = state.ui_authoring;
    auto* node = ui.document.find(ui.selected_node);
    if (!node) {
        ImGui::TextDisabled("Select a UI node.");
        return;
    }

    ImGui::SeparatorText("Node");
    if (ImGui::InputText("Name", &node->name)) mark_ui_dirty(state);
    if (ImGui::Checkbox("Enabled", &node->enabled)) mark_ui_dirty(state);
    if (ImGui::DragInt("Z Order", &node->z_order, 1.0f)) mark_ui_dirty(state);

    std::string parent_label = "<root>";
    if (const auto* parent = ui.document.find(node->parent_id)) parent_label = parent->name;
    if (node->type != vespera::UiNodeType::Canvas && ImGui::BeginCombo("Parent", parent_label.c_str())) {
        const bool root_selected = node->parent_id == vespera::kInvalidUiNodeId;
        if (ImGui::Selectable("<root>", root_selected)) {
            std::string error;
            if (ui.document.reparent(node->id, vespera::kInvalidUiNodeId, &error)) mark_ui_dirty(state);
            else ui.message = error;
        }
        for (const auto& candidate : ui.document.nodes()) {
            if (candidate.id == node->id || ui.document.is_descendant(candidate.id, node->id)) continue;
            const bool selected = node->parent_id == candidate.id;
            if (ImGui::Selectable(candidate.name.c_str(), selected)) {
                std::string error;
                if (ui.document.reparent(node->id, candidate.id, &error)) mark_ui_dirty(state);
                else ui.message = error;
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SeparatorText("Rect Transform");
    if (ImGui::Button("Top Left")) {
        node->rect.anchor_min = node->rect.anchor_max = {0.0f, 0.0f}; mark_ui_dirty(state);
    }
    ImGui::SameLine();
    if (ImGui::Button("Center")) {
        node->rect.anchor_min = node->rect.anchor_max = {0.5f, 0.5f}; mark_ui_dirty(state);
    }
    ImGui::SameLine();
    if (ImGui::Button("Stretch")) {
        node->rect.anchor_min = {0.0f, 0.0f}; node->rect.anchor_max = {1.0f, 1.0f}; mark_ui_dirty(state);
    }
    float anchor_min[2]{node->rect.anchor_min.x, node->rect.anchor_min.y};
    float anchor_max[2]{node->rect.anchor_max.x, node->rect.anchor_max.y};
    float offset_min[2]{node->rect.offset_min.x, node->rect.offset_min.y};
    float offset_max[2]{node->rect.offset_max.x, node->rect.offset_max.y};
    if (ImGui::DragFloat2("Anchor Min", anchor_min, 0.01f, 0.0f, 1.0f, "%.2f")) {
        node->rect.anchor_min = {anchor_min[0], anchor_min[1]}; mark_ui_dirty(state);
    }
    if (ImGui::DragFloat2("Anchor Max", anchor_max, 0.01f, 0.0f, 1.0f, "%.2f")) {
        node->rect.anchor_max = {anchor_max[0], anchor_max[1]}; mark_ui_dirty(state);
    }
    if (ImGui::DragFloat2("Offset Min", offset_min, 1.0f)) {
        node->rect.offset_min = {offset_min[0], offset_min[1]}; mark_ui_dirty(state);
    }
    if (ImGui::DragFloat2("Offset Max", offset_max, 1.0f)) {
        node->rect.offset_max = {offset_max[0], offset_max[1]}; mark_ui_dirty(state);
    }
    float margin[4]{node->margin.left,node->margin.top,node->margin.right,node->margin.bottom};
    float padding[4]{node->padding.left,node->padding.top,node->padding.right,node->padding.bottom};
    if (ImGui::DragFloat4("Margin LTRB", margin, 0.5f)) {
        node->margin={margin[0],margin[1],margin[2],margin[3]}; mark_ui_dirty(state);
    }
    if (ImGui::DragFloat4("Padding LTRB", padding, 0.5f)) {
        node->padding={padding[0],padding[1],padding[2],padding[3]}; mark_ui_dirty(state);
    }

    if (node->type == vespera::UiNodeType::Canvas) {
        ImGui::SeparatorText("Canvas");
        float reference[2]{node->canvas.reference_resolution.x,node->canvas.reference_resolution.y};
        if (ImGui::DragFloat2("Reference Resolution", reference, 1.0f, 1.0f, 8192.0f, "%.0f")) {
            node->canvas.reference_resolution={reference[0],reference[1]}; mark_ui_dirty(state);
        }
        if (ImGui::SliderFloat("Match Width / Height", &node->canvas.match_width_or_height, 0.0f, 1.0f)) mark_ui_dirty(state);
    }

    if (node->type != vespera::UiNodeType::Canvas && node->type != vespera::UiNodeType::Text
        && node->type != vespera::UiNodeType::Button && node->type != vespera::UiNodeType::ProgressBar) {
        ImGui::SeparatorText("Visual");
        if (ImGui::ColorEdit4("Color", node->visual.color.data())) mark_ui_dirty(state);
    }
    if (node->type != vespera::UiNodeType::Canvas) {
        ImGui::SeparatorText("Surface Style");
        if (ImGui::SliderFloat("Opacity", &node->surface.opacity, 0.0f, 1.0f)) mark_ui_dirty(state);
        if (ImGui::DragFloat("Corner Radius", &node->surface.corner_radius, 0.5f, 0.0f, 256.0f)) mark_ui_dirty(state);
        if (ImGui::DragFloat("Border Width", &node->surface.border_width, 0.25f, 0.0f, 64.0f)) mark_ui_dirty(state);
        if (ImGui::ColorEdit4("Border Color", node->surface.border_color.data())) mark_ui_dirty(state);
        float shadow_offset[2]{node->surface.shadow_offset.x,node->surface.shadow_offset.y};
        if (ImGui::DragFloat2("Shadow Offset", shadow_offset, 0.5f, -128.0f, 128.0f)) {
            node->surface.shadow_offset={shadow_offset[0],shadow_offset[1]}; mark_ui_dirty(state);
        }
        if (ImGui::DragFloat("Shadow Softness", &node->surface.shadow_softness, 0.5f, 0.0f, 32.0f)) mark_ui_dirty(state);
        if (ImGui::ColorEdit4("Shadow Color", node->surface.shadow_color.data())) mark_ui_dirty(state);
        int image_fit=static_cast<int>(node->surface.image_fit);
        const char* image_fit_names[]={"Stretch","Contain","Cover"};
        if (ImGui::Combo("Image Fit", &image_fit, image_fit_names, 3)) {
            node->surface.image_fit=static_cast<vespera::UiImageFit>(image_fit); mark_ui_dirty(state);
        }
        float slice[4]{node->surface.nine_slice.left,node->surface.nine_slice.top,node->surface.nine_slice.right,node->surface.nine_slice.bottom};
        if (ImGui::DragFloat4("9-Slice LTRB", slice, 0.5f, 0.0f, 2048.0f)) {
            node->surface.nine_slice={slice[0],slice[1],slice[2],slice[3]}; mark_ui_dirty(state);
        }
    }

    if (node->type == vespera::UiNodeType::Text || node->type == vespera::UiNodeType::Button
        || node->type == vespera::UiNodeType::ProgressBar || node->type == vespera::UiNodeType::TextInput
        || node->type == vespera::UiNodeType::Modal || node->type == vespera::UiNodeType::Tooltip
        || node->type == vespera::UiNodeType::Tabs) {
        ImGui::SeparatorText("Text");
        if (ImGui::InputTextMultiline("Text", &node->text.text, ImVec2(-1.0f, 64.0f))) mark_ui_dirty(state);
        if (ImGui::InputText("Font Family", &node->text.font_family)) mark_ui_dirty(state);
        if (ImGui::DragFloat("Font Size", &node->text.font_size, 0.5f, 4.0f, 256.0f)) mark_ui_dirty(state);
        int font_weight=static_cast<int>(node->text.font_weight);
        if (ImGui::SliderInt("Font Weight", &font_weight, 100, 900)) {
            node->text.font_weight=static_cast<std::uint16_t>(std::clamp(font_weight,100,900)); mark_ui_dirty(state);
        }
        if (ImGui::Checkbox("Italic", &node->text.italic)) mark_ui_dirty(state);
        ImGui::SameLine();
        if (ImGui::Checkbox("Word Wrap", &node->text.wrap)) mark_ui_dirty(state);
        if (ImGui::DragFloat("Line Spacing", &node->text.line_spacing, 0.02f, 0.5f, 4.0f)) mark_ui_dirty(state);
        if (ImGui::ColorEdit4("Text Color", node->text.color.data())) mark_ui_dirty(state);
        int h_align=static_cast<int>(node->text.horizontal_alignment);
        const char* h_names[]={"Left","Center","Right"};
        if (ImGui::Combo("Horizontal Align", &h_align, h_names, 3)) {
            node->text.horizontal_alignment=static_cast<vespera::UiHorizontalAlignment>(h_align); mark_ui_dirty(state);
        }
        int v_align=static_cast<int>(node->text.vertical_alignment);
        const char* v_names[]={"Top","Middle","Bottom"};
        if (ImGui::Combo("Vertical Align", &v_align, v_names, 3)) {
            node->text.vertical_alignment=static_cast<vespera::UiVerticalAlignment>(v_align); mark_ui_dirty(state);
        }
        float text_shadow[2]{node->text.shadow_offset.x,node->text.shadow_offset.y};
        if (ImGui::DragFloat2("Text Shadow Offset", text_shadow, 0.5f, -64.0f, 64.0f)) {
            node->text.shadow_offset={text_shadow[0],text_shadow[1]}; mark_ui_dirty(state);
        }
        if (ImGui::ColorEdit4("Text Shadow Color", node->text.shadow_color.data())) mark_ui_dirty(state);
    }

    const bool text_capable = node->type == vespera::UiNodeType::Text || node->type == vespera::UiNodeType::Button
        || node->type == vespera::UiNodeType::ProgressBar || node->type == vespera::UiNodeType::TextInput
        || node->type == vespera::UiNodeType::Modal || node->type == vespera::UiNodeType::Tooltip
        || node->type == vespera::UiNodeType::Tabs;
    const bool image_capable = node->type != vespera::UiNodeType::Canvas && node->type != vespera::UiNodeType::Text
        && node->type != vespera::UiNodeType::ProgressBar;
    if (image_capable || text_capable) {
        ImGui::SeparatorText("Project Assets");
        if (image_capable) {
            const auto resolved = state.asset_catalog.resolve_reference(node->visual.image);
            const std::string label = resolved ? resolved.record->display_name : (node->visual.image.empty() ? "None" : node->visual.image.path.generic_string());
            ImGui::TextUnformatted("Image"); ImGui::SameLine(72.0f);
            ImGui::Button((label + "##UiImageAsset").c_str(), ImVec2(-28.0f, 0.0f));
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VESPERA_PROJECT_ASSET")) {
                    if (const auto* record = asset_record_from_payload(state, payload); record && record->kind == vespera::AssetKind::Texture) {
                        node->visual.image = {record->asset_id, record->relative_path}; mark_ui_dirty(state);
                    }
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("X##ClearUiImage")) { node->visual.image = {}; mark_ui_dirty(state); }
        }
        if (text_capable) {
            const auto resolved = state.asset_catalog.resolve_reference(node->text.font);
            const std::string label = resolved ? resolved.record->display_name : (node->text.font.empty() ? "Fallback" : node->text.font.path.generic_string());
            ImGui::TextUnformatted("Font"); ImGui::SameLine(72.0f);
            ImGui::Button((label + "##UiFontAsset").c_str(), ImVec2(-28.0f, 0.0f));
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VESPERA_PROJECT_ASSET")) {
                    if (const auto* record = asset_record_from_payload(state, payload); record && record->kind == vespera::AssetKind::Font) {
                        node->text.font = {record->asset_id, record->relative_path}; mark_ui_dirty(state);
                    }
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("X##ClearUiFont")) { node->text.font = {}; mark_ui_dirty(state); }
        }
    }

    if (node->type == vespera::UiNodeType::Button) {
        ImGui::SeparatorText("Button");
        if (ImGui::Checkbox("Interactable", &node->button.interactable)) mark_ui_dirty(state);
        if (ImGui::ColorEdit4("Normal", node->button.normal_color.data())) mark_ui_dirty(state);
        if (ImGui::ColorEdit4("Hovered", node->button.hovered_color.data())) mark_ui_dirty(state);
        if (ImGui::ColorEdit4("Pressed", node->button.pressed_color.data())) mark_ui_dirty(state);
        if (ImGui::ColorEdit4("Disabled", node->button.disabled_color.data())) mark_ui_dirty(state);
    }

    if (node->type == vespera::UiNodeType::ProgressBar) {
        ImGui::SeparatorText("Progress Bar");
        if (ImGui::DragFloat("Value", &node->progress.value, 0.01f)) mark_ui_dirty(state);
        if (ImGui::DragFloat("Minimum", &node->progress.minimum, 0.01f)) mark_ui_dirty(state);
        if (ImGui::DragFloat("Maximum", &node->progress.maximum, 0.01f)) mark_ui_dirty(state);
        if (ImGui::ColorEdit4("Background", node->progress.background_color.data())) mark_ui_dirty(state);
        if (ImGui::ColorEdit4("Fill", node->progress.fill_color.data())) mark_ui_dirty(state);
    }

    ImGui::SeparatorText("Layout");
    int layout_mode = static_cast<int>(node->layout.mode);
    const char* layout_names[] = {"None","Horizontal","Vertical","Grid"};
    if (ImGui::Combo("Layout Mode", &layout_mode, layout_names, 4)) {
        node->layout.mode = static_cast<vespera::UiLayoutMode>(layout_mode); mark_ui_dirty(state);
    }
    float spacing[2]{node->layout.spacing.x,node->layout.spacing.y};
    float cell[2]{node->layout.cell_size.x,node->layout.cell_size.y};
    if (ImGui::DragFloat2("Spacing", spacing, 0.5f, 0.0f, 256.0f)) {
        node->layout.spacing={spacing[0],spacing[1]}; mark_ui_dirty(state);
    }
    if (ImGui::DragFloat2("Cell Size (0=auto)", cell, 0.5f, 0.0f, 2048.0f)) {
        node->layout.cell_size={cell[0],cell[1]}; mark_ui_dirty(state);
    }
    int columns = static_cast<int>(node->layout.columns);
    if (ImGui::DragInt("Columns", &columns, 1.0f, 1, 64)) {
        node->layout.columns=static_cast<std::uint32_t>(std::max(1,columns)); mark_ui_dirty(state);
    }
    if (ImGui::Checkbox("Clip Children", &node->layout.clip_children)) mark_ui_dirty(state);

    if (node->type == vespera::UiNodeType::ScrollView) {
        ImGui::SeparatorText("Scroll View");
        float offset[2]{node->scroll.offset.x,node->scroll.offset.y};
        if (ImGui::DragFloat2("Scroll Offset", offset, 1.0f, 0.0f, 100000.0f)) {
            node->scroll.offset={offset[0],offset[1]}; mark_ui_dirty(state);
        }
    }
    if (node->type == vespera::UiNodeType::Tabs) {
        ImGui::SeparatorText("Tabs");
        int active=static_cast<int>(node->tabs.active_index);
        if (ImGui::DragInt("Active Child", &active, 1.0f, 0, 128)) {
            node->tabs.active_index=static_cast<std::uint32_t>(std::max(0,active)); mark_ui_dirty(state);
        }
    }
    if (node->type == vespera::UiNodeType::TextInput) {
        ImGui::SeparatorText("Text Input");
        if (ImGui::InputText("Placeholder", &node->input.placeholder)) mark_ui_dirty(state);
        int max_len=static_cast<int>(node->input.max_length);
        if (ImGui::DragInt("Max Length", &max_len, 1.0f, 1, 65536)) {
            node->input.max_length=static_cast<std::uint32_t>(std::max(1,max_len)); mark_ui_dirty(state);
        }
        if (ImGui::Checkbox("Read Only", &node->input.read_only)) mark_ui_dirty(state);
    }
}

ImU32 ui_preview_color(const vespera::UiNode& node) {
    auto c = node.visual.color;
    if (node.type == vespera::UiNodeType::Button) c = node.button.normal_color;
    else if (node.type == vespera::UiNodeType::ProgressBar) c = node.progress.background_color;
    return ImGui::GetColorU32(ImVec4(c[0],c[1],c[2],std::max(0.08f,c[3] * std::clamp(node.surface.opacity,0.0f,1.0f))));
}

void draw_ui_authoring_preview(EditorState& state) {
    auto& ui = state.ui_authoring;
    const auto layout = vespera::resolve_ui_layout(ui.document,
        static_cast<float>(ui.preview_width), static_cast<float>(ui.preview_height));

    ImVec2 avail = ImGui::GetContentRegionAvail();
    avail.x = std::max(avail.x, 120.0f); avail.y = std::max(avail.y, 120.0f);
    const float fit = std::max(0.05f, std::min(avail.x / static_cast<float>(ui.preview_width),
                                               avail.y / static_cast<float>(ui.preview_height)));
    const ImVec2 size{static_cast<float>(ui.preview_width)*fit, static_cast<float>(ui.preview_height)*fit};
    const ImVec2 origin{ImGui::GetCursorScreenPos().x + std::max(0.0f,(avail.x-size.x)*0.5f),
                        ImGui::GetCursorScreenPos().y + std::max(0.0f,(avail.y-size.y)*0.5f)};
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("##ui_authoring_canvas", size);
    const bool hovered=ImGui::IsItemHovered();
    ImDrawList* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin,{origin.x+size.x,origin.y+size.y},ImGui::GetColorU32(ImVec4(0.025f,0.030f,0.042f,1.0f)));
    draw->AddRect(origin,{origin.x+size.x,origin.y+size.y},ImGui::GetColorU32(ImVec4(0.30f,0.34f,0.44f,1.0f)));

    for (const auto& resolved : layout.nodes) {
        if (!resolved.enabled || resolved.type == vespera::UiNodeType::Canvas) continue;
        const auto* node=ui.document.find(resolved.id); if(!node) continue;
        const ImVec2 a{origin.x+resolved.rect.x*fit,origin.y+resolved.rect.y*fit};
        const ImVec2 b{a.x+resolved.rect.width*fit,a.y+resolved.rect.height*fit};
        const float rounding=std::max(0.0f,node->surface.corner_radius*fit);
        if(node->surface.shadow_color[3]>0.001f){
            auto sc=node->surface.shadow_color;sc[3]*=std::clamp(node->surface.opacity,0.0f,1.0f);
            const ImVec2 sa{a.x+node->surface.shadow_offset.x*fit,a.y+node->surface.shadow_offset.y*fit};
            const ImVec2 sb{b.x+node->surface.shadow_offset.x*fit,b.y+node->surface.shadow_offset.y*fit};
            draw->AddRectFilled(sa,sb,ImGui::GetColorU32(ImVec4(sc[0],sc[1],sc[2],sc[3])),rounding);
        }
        draw->AddRectFilled(a,b,ui_preview_color(*node),rounding);
        if(node->surface.border_width>0.01f&&node->surface.border_color[3]>0.001f){
            auto bc=node->surface.border_color;bc[3]*=std::clamp(node->surface.opacity,0.0f,1.0f);
            draw->AddRect(a,b,ImGui::GetColorU32(ImVec4(bc[0],bc[1],bc[2],bc[3])),rounding,0,std::max(1.0f,node->surface.border_width*fit));
        }
        if (node->type == vespera::UiNodeType::ProgressBar) {
            const float range=std::max(0.0001f,node->progress.maximum-node->progress.minimum);
            const float t=std::clamp((node->progress.value-node->progress.minimum)/range,0.0f,1.0f);
            auto fc=node->progress.fill_color;
            draw->AddRectFilled(a,{a.x+(b.x-a.x)*t,b.y},ImGui::GetColorU32(ImVec4(fc[0],fc[1],fc[2],fc[3])),rounding);
        }
        if (node->type == vespera::UiNodeType::Image) {
            draw->AddText({a.x+4.0f,a.y+3.0f},ImGui::GetColorU32(ImGuiCol_TextDisabled),"IMG");
        } else {
            const std::string text = node->text.text.empty() ? node->name : node->text.text;
            if (!text.empty() && (b.x-a.x)>18.0f && (b.y-a.y)>12.0f) {
                auto tc=node->text.color;tc[3]*=std::clamp(node->surface.opacity,0.0f,1.0f);
                const ImU32 text_color=ImGui::GetColorU32(ImVec4(tc[0],tc[1],tc[2],tc[3]));
                if(node->text.shadow_color[3]>0.001f){auto sc=node->text.shadow_color;draw->AddText({a.x+4.0f+node->text.shadow_offset.x*fit,a.y+3.0f+node->text.shadow_offset.y*fit},ImGui::GetColorU32(ImVec4(sc[0],sc[1],sc[2],sc[3])),text.c_str());}
                draw->AddText({a.x+4.0f,a.y+3.0f},text_color,text.c_str());
            }
        }
        if (ui.selected_node == node->id) {
            draw->AddRect(a,b,ImGui::GetColorU32(ImVec4(1.0f,0.78f,0.30f,1.0f)),2.0f,0,2.0f);
            draw->AddRectFilled({b.x-6.0f,b.y-6.0f},{b.x+2.0f,b.y+2.0f},ImGui::GetColorU32(ImVec4(1.0f,0.78f,0.30f,1.0f)));
        }
    }

    const ImGuiIO& io=ImGui::GetIO();
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const vespera::UiVec2 point{(io.MousePos.x-origin.x)/fit,(io.MousePos.y-origin.y)/fit};
        const auto* selected_resolved=layout.find(ui.selected_node);
        if (selected_resolved) {
            const ImVec2 corner{origin.x+(selected_resolved->rect.x+selected_resolved->rect.width)*fit,
                                origin.y+(selected_resolved->rect.y+selected_resolved->rect.height)*fit};
            const float dx=io.MousePos.x-corner.x,dy=io.MousePos.y-corner.y;
            if (dx*dx+dy*dy <= 12.0f*12.0f) ui.preview_drag_mode=2;
        }
        if (ui.preview_drag_mode==0) {
            ui.selected_node=vespera::kInvalidUiNodeId;
            for (auto it=layout.nodes.rbegin();it!=layout.nodes.rend();++it) {
                if (!it->enabled || it->type==vespera::UiNodeType::Canvas) continue;
                const auto& r=it->rect;
                if(point.x>=r.x&&point.y>=r.y&&point.x<=r.x+r.width&&point.y<=r.y+r.height){ui.selected_node=it->id;break;}
            }
            if (ui.selected_node!=vespera::kInvalidUiNodeId) ui.preview_drag_mode=1;
        }
    }
    if (ui.preview_drag_mode!=0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (auto* node=ui.document.find(ui.selected_node)) {
            bool managed_by_parent=false;
            if (const auto* parent=ui.document.find(node->parent_id)) {
                const auto mode=parent->layout.mode;
                managed_by_parent=mode!=vespera::UiLayoutMode::None || parent->type==vespera::UiNodeType::List || parent->type==vespera::UiNodeType::Grid;
            }
            if (!managed_by_parent) {
                const float dx=io.MouseDelta.x/fit,dy=io.MouseDelta.y/fit;
                if (std::abs(dx)>0.0f||std::abs(dy)>0.0f) {
                    if(ui.preview_drag_mode==1){
                        node->rect.offset_min.x+=dx;node->rect.offset_min.y+=dy;
                        node->rect.offset_max.x+=dx;node->rect.offset_max.y+=dy;
                    } else {
                        node->rect.offset_max.x+=dx;node->rect.offset_max.y+=dy;
                    }
                    mark_ui_dirty(state);
                }
            }
        }
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) ui.preview_drag_mode=0;
}

void draw_rml_source_authoring(EditorState& state) {
    if (!vespera::editor::draw_rml_source_editor(state.rml_source_editor)) return;
    push_console(state, ConsoleEntry::Level::Info,
        "RmlUi source saved: " + state.rml_source_editor.path.generic_string());
    refresh_asset_catalog(state, false, true);

    if (state.play_rml_ui_loaded && state.play_rml_ui && state.play_rml_ui->initialized()) {
        std::error_code ec;
        const auto active = std::filesystem::absolute(state.play_rml_ui->document_path(), ec).lexically_normal();
        ec.clear();
        const auto edited = std::filesystem::absolute(state.rml_source_editor.path, ec).lexically_normal();
        bool reload_active = !ec && active == edited;
        if (!reload_active && !state.rml_source_editor.asset_id.empty()) {
            std::error_code relative_ec;
            const auto active_relative = std::filesystem::relative(active, state.asset_catalog.root(), relative_ec);
            if (!relative_ec) {
                if (const auto* active_record = state.asset_catalog.find(active_relative.generic_string())) {
                    for (const auto* dependency : state.asset_catalog.dependencies_of(active_record->asset_id)) {
                        if (dependency && dependency->target_asset_id == state.rml_source_editor.asset_id) {
                            reload_active = true;
                            break;
                        }
                    }
                }
            }
        }
        if (reload_active) {
            if (state.play_rml_ui->reload())
                push_console(state, ConsoleEntry::Level::Info, "Play Mode RML reloaded after source save.");
            else
                push_console(state, ConsoleEntry::Level::Warning, "Play Mode RML reload failed after source save.");
        }
    }
}

void draw_ui_authoring(EditorState& state) {
    auto& ui=state.ui_authoring;
    if(!ui.open) return;
    bool open=ui.open;
    std::string title=std::format("UI Authoring{}###VesperaUiAuthoring",ui.dirty?" *":"");
    if(!ImGui::Begin(title.c_str(),&open)){ImGui::End();ui.open=open;return;}
    ui.open=open;
    if(ImGui::Button("Save")) save_ui_authoring(state);
    ImGui::SameLine();
    ImGui::BeginDisabled(ui.dirty);
    if(ImGui::Button("Reload")) {
        if(const auto* record=ui_authoring_record(state)) open_ui_authoring(state,*record);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if(ImGui::Button("Add")) ImGui::OpenPopup("##ui_add_node");
    if(ImGui::BeginPopup("##ui_add_node")) {
        const std::array<vespera::UiNodeType,12> types{
            vespera::UiNodeType::Panel,vespera::UiNodeType::Text,vespera::UiNodeType::Image,
            vespera::UiNodeType::Button,vespera::UiNodeType::ProgressBar,vespera::UiNodeType::ScrollView,
            vespera::UiNodeType::List,vespera::UiNodeType::Grid,vespera::UiNodeType::Tabs,
            vespera::UiNodeType::Modal,vespera::UiNodeType::Tooltip,vespera::UiNodeType::TextInput};
        for(const auto type:types) if(ImGui::MenuItem(std::string(vespera::ui_node_type_name(type)).c_str())) add_ui_authoring_node(state,type);
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    bool can_delete=false;
    if(const auto* n=ui.document.find(ui.selected_node)) can_delete=n->type!=vespera::UiNodeType::Canvas;
    if(!can_delete) ImGui::BeginDisabled();
    if(ImGui::Button("Delete")) {ui.document.destroy_node(ui.selected_node);ui.selected_node=vespera::kInvalidUiNodeId;ui.dirty=true;}
    if(!can_delete) ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    const char* preset = (ui.preview_width==1920&&ui.preview_height==1080)?"1920 x 1080":"1280 x 720";
    if(ImGui::BeginCombo("Preview",preset)) {
        if(ImGui::Selectable("1280 x 720",ui.preview_width==1280&&ui.preview_height==720)){ui.preview_width=1280;ui.preview_height=720;}
        if(ImGui::Selectable("1920 x 1080",ui.preview_width==1920&&ui.preview_height==1080)){ui.preview_width=1920;ui.preview_height=1080;}
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s",ui.path.filename().string().c_str());
    if(!ui.message.empty()) ImGui::TextDisabled("%s",ui.message.c_str());

    const float left=220.0f,right=330.0f;
    ImGui::BeginChild("##ui_hierarchy",ImVec2(left,0.0f),true);
    ImGui::SeparatorText("UI Hierarchy");
    for(const auto& node:ui.document.nodes()) if(node.parent_id==vespera::kInvalidUiNodeId) draw_ui_authoring_tree_node(ui,node.id);
    ImGui::EndChild();
    ImGui::SameLine();
    const float center=std::max(160.0f,ImGui::GetContentRegionAvail().x-right-8.0f);
    ImGui::BeginChild("##ui_preview",ImVec2(center,0.0f),true);
    ImGui::TextDisabled("Click to select | drag to move | drag lower-right handle to resize");
    draw_ui_authoring_preview(state);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##ui_properties",ImVec2(0.0f,0.0f),true);
    draw_ui_node_properties(state);
    ImGui::EndChild();
    ImGui::End();
}

void draw_inspector(EditorState& state) {
    ImGui::Begin("Inspector");
    draw_asset_move_popup(state);

    if (state.selection.kind == SelectionKind::Asset) {
        const auto* record = state.asset_catalog.find_by_id(state.selection.asset_id);
        if (!record) {
            state.selection = {};
            ImGui::TextDisabled("Selected asset is no longer present in the catalog.");
            ImGui::End();
            return;
        }

        ImGui::TextColored(ImVec4(0.58f, 0.63f, 1.0f, 1.0f), "ASSET INSPECTOR");
        ImGui::BeginChild("AssetSummaryCard", ImVec2(0.0f, 112.0f), true);
        ImGui::SetWindowFontScale(1.08f);
        ImGui::TextUnformatted(record->display_name.c_str());
        ImGui::SetWindowFontScale(1.0f);
        ImGui::TextColored(ImVec4(0.56f, 0.68f, 0.95f, 1.0f), "%s", vespera::asset_kind_name(record->kind).data());
        ImGui::Spacing();
        ImGui::TextDisabled("Path");
        ImGui::SameLine(72.0f); ImGui::TextWrapped("%s", record->relative_path.generic_string().c_str());
        ImGui::TextDisabled("Importer");
        ImGui::SameLine(72.0f); ImGui::TextUnformatted(record->importer.c_str());
        ImGui::TextDisabled("State");
        ImGui::SameLine(72.0f); ImGui::TextColored(ImVec4(0.48f, 0.82f, 0.62f, 1.0f), "%s", vespera::asset_import_state_name(record->import_state).data());
        ImGui::EndChild();
        ImGui::Spacing();
        ImGui::TextDisabled("Source size");
        ImGui::Text("%llu bytes", static_cast<unsigned long long>(record->source_size));

        if (record->kind == vespera::AssetKind::Texture) {
            auto& cache = state.asset_inspection_cache;
            if (cache.asset_id != record->asset_id || cache.source_hash != record->source_hash) {
                cache = {};
                cache.asset_id = record->asset_id;
                cache.source_hash = record->source_hash;
                cache.texture_attempted = true;
                const auto imported = vespera::import_texture(record->absolute_path, record->display_name);
                cache.texture_decoded = static_cast<bool>(imported);
                cache.message = imported.message;
                if (imported) {
                    cache.texture_width = imported.texture.width;
                    cache.texture_height = imported.texture.height;
                    cache.decoded_bytes = imported.texture.rgba8.size();
                }
            }
            ImGui::SeparatorText("Texture");
            if (cache.texture_decoded) {
                ImGui::Text("Dimensions: %u x %u", cache.texture_width, cache.texture_height);
                ImGui::Text("Decoded: RGBA8 (%zu bytes)", cache.decoded_bytes);
            } else {
                ImGui::TextDisabled("Decoder: not available yet");
                ImGui::TextWrapped("%s", cache.message.c_str());
            }

            auto& edit = state.texture_import_edit;
            if (edit.asset_id != record->asset_id) {
                edit.asset_id = record->asset_id;
                edit.settings = record->texture_settings;
            }
            ImGui::SeparatorText("Import Settings");
            ImGui::TextDisabled("Stored in .vmeta v3; renderer-independent source intent.");
            const char* usage_items[] = {"World", "Sprite", "UI", "Data"};
            const char* filter_items[] = {"Nearest", "Linear"};
            const char* wrap_items[] = {"Repeat", "Clamp"};
            const char* color_items[] = {"sRGB", "Linear"};
            const char* alpha_items[] = {"Auto", "Opaque", "Cutout", "Blend"};
            const char* mip_items[] = {"Auto", "On", "Off"};
            int usage = static_cast<int>(edit.settings.usage);
            int filter = static_cast<int>(edit.settings.filter);
            int wrap_u = static_cast<int>(edit.settings.wrap_u);
            int wrap_v = static_cast<int>(edit.settings.wrap_v);
            int color_space = static_cast<int>(edit.settings.color_space);
            int alpha = static_cast<int>(edit.settings.alpha_mode);
            int mipmaps = static_cast<int>(edit.settings.mipmaps);
            if (ImGui::Combo("Usage", &usage, usage_items, IM_ARRAYSIZE(usage_items))) edit.settings.usage = static_cast<vespera::TextureUsage>(usage);
            if (ImGui::Combo("Filter", &filter, filter_items, IM_ARRAYSIZE(filter_items))) edit.settings.filter = static_cast<vespera::TextureFilter>(filter);
            if (ImGui::Combo("Wrap U", &wrap_u, wrap_items, IM_ARRAYSIZE(wrap_items))) edit.settings.wrap_u = static_cast<vespera::TextureWrap>(wrap_u);
            if (ImGui::Combo("Wrap V", &wrap_v, wrap_items, IM_ARRAYSIZE(wrap_items))) edit.settings.wrap_v = static_cast<vespera::TextureWrap>(wrap_v);
            if (ImGui::Combo("Color Space", &color_space, color_items, IM_ARRAYSIZE(color_items))) edit.settings.color_space = static_cast<vespera::TextureColorSpace>(color_space);
            if (ImGui::Combo("Alpha", &alpha, alpha_items, IM_ARRAYSIZE(alpha_items))) edit.settings.alpha_mode = static_cast<vespera::TextureAlphaMode>(alpha);
            if (ImGui::Combo("Mipmaps", &mipmaps, mip_items, IM_ARRAYSIZE(mip_items))) edit.settings.mipmaps = static_cast<vespera::TextureMipmapMode>(mipmaps);
            ImGui::InputInt("Max Size", &edit.settings.max_size);
            edit.settings.max_size = (std::max)(0, edit.settings.max_size);
            const bool settings_dirty = !(edit.settings == record->texture_settings) || !record->texture_settings_authored;
            ImGui::BeginDisabled(!settings_dirty);
            if (ImGui::Button("Apply Import Settings")) {
                std::string error;
                if (state.asset_catalog.save_texture_import_settings(record->asset_id, edit.settings, &error)) {
                    const auto saved_path = record->relative_path.generic_string();
                    push_console(state, ConsoleEntry::Level::Info, "Texture import settings saved: " + saved_path);
                    refresh_asset_catalog(state, false, true);
                    ImGui::End();
                    return;
                } else {
                    push_console(state, ConsoleEntry::Level::Error, "Texture import settings failed: " + error);
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Revert Settings")) edit.settings = record->texture_settings;
            if (!record->texture_settings_authored) ImGui::TextDisabled("Using compatibility defaults until Apply is pressed.");
        }

        if (record->kind == vespera::AssetKind::Font) {
            ImGui::SeparatorText("Font Source");
            const auto inspected = vespera::inspect_font_asset(record->absolute_path);
            if (inspected) {
                ImGui::Text("Container: %s", vespera::font_container_name(inspected.font.container));
                ImGui::Text("SFNT tables: %u", inspected.font.table_count);
                ImGui::TextDisabled("Font identity/import is ready. Text shaping/atlas/rendering arrives with runtime UI.");
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.42f, 1.0f), "Invalid font source");
                ImGui::TextWrapped("%s", inspected.message.c_str());
            }
        }

        if (record->kind == vespera::AssetKind::RmlDocument
            || record->kind == vespera::AssetKind::RmlStyleSheet) {
            ImGui::SeparatorText("RmlUi Source");
            const auto dependencies = state.asset_catalog.dependencies_of(record->asset_id);
            std::size_t broken = 0;
            for (const auto* dependency : dependencies) if (dependency && !dependency->resolved) ++broken;
            ImGui::Text("Dependencies: %zu | Broken: %zu", dependencies.size(), broken);
            ImGui::TextDisabled("Standard .rml/.rcss source; local paths participate in Vespera controlled-move repair.");
            if (ImGui::Button("Open RML / RCSS Source", ImVec2(-1.0f, 0.0f))) {
                std::string error;
                if (!vespera::editor::open_rml_source_editor(
                        state.rml_source_editor, record->absolute_path, record->asset_id, &error)) {
                    push_console(state, ConsoleEntry::Level::Error, "RmlUi source editor: " + error);
                }
            }
            for (const auto* dependency : dependencies) {
                if (!dependency || dependency->resolved) continue;
                ImGui::BulletText("Broken %s: %s", dependency->reason.c_str(), dependency->reference.c_str());
            }
        }

        if (record->kind == vespera::AssetKind::UiDocument) {
            ImGui::SeparatorText("Runtime UI Document");
            vespera::UiDocument ui_document;
            const auto loaded_ui = vespera::load_ui_document(ui_document, record->absolute_path);
            if (!loaded_ui) {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.42f, 1.0f), "Invalid .slui document");
                ImGui::TextWrapped("%s", loaded_ui.message.c_str());
            } else {
                const auto layout = vespera::resolve_ui_layout(ui_document, 1280.0f, 720.0f);
                std::size_t canvas_count = 0, button_count = 0, image_count = 0, text_count = 0, widget_count = 0;
                for (const auto& node : ui_document.nodes()) {
                    if (node.type == vespera::UiNodeType::Canvas) ++canvas_count;
                    else if (node.type == vespera::UiNodeType::Button) ++button_count;
                    else if (node.type == vespera::UiNodeType::Image) ++image_count;
                    else if (node.type == vespera::UiNodeType::Text) ++text_count;
                    else ++widget_count;
                }
                ImGui::Text("Nodes: %zu | Canvas: %zu | Text: %zu | Image: %zu | Button: %zu | Other: %zu",
                    ui_document.nodes().size(), canvas_count, text_count, image_count, button_count, widget_count);
                ImGui::Text("1280x720 layout: %zu resolved | %zu warning(s)", layout.nodes.size(), layout.warnings.size());
                ImGui::TextDisabled("Legacy .slui compatibility/QA authoring. New project UI should use RML/RCSS.");
                if (ImGui::Button("Open UI Authoring", ImVec2(-1.0f, 0.0f))) open_ui_authoring(state, *record);
                for (const auto& warning : layout.warnings) ImGui::BulletText("%s", warning.c_str());
            }
        }

        if (record->kind == vespera::AssetKind::Material) {
            auto& edit = state.material_asset_edit;
            if (edit.asset_id != record->asset_id || edit.source_hash != record->source_hash) {
                edit = {};
                edit.asset_id = record->asset_id;
                edit.source_hash = record->source_hash;
                const auto loaded = vespera::load_material_asset(record->absolute_path);
                edit.loaded = static_cast<bool>(loaded);
                edit.message = loaded.message;
                if (loaded) edit.material = loaded.material;
            }
            ImGui::SeparatorText("Material");
            if (!edit.loaded) {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.42f, 1.0f), "Invalid Material asset");
                ImGui::TextWrapped("%s", edit.message.c_str());
            } else {
                ImGui::InputText("Name", &edit.material.name);
                const char* shaders[] = {"Vespera/Lit", "Vespera/Unlit"};
                int shader_index = edit.material.properties.shader == vespera::BuiltinMaterialShader::Unlit ? 1 : 0;
                if (ImGui::Combo("Shader", &shader_index, shaders, IM_ARRAYSIZE(shaders))) {
                    edit.material.properties.shader = shader_index == 1
                        ? vespera::BuiltinMaterialShader::Unlit : vespera::BuiltinMaterialShader::Lit;
                }

                const auto base_resolution = state.asset_catalog.resolve_reference(edit.material.base_texture);
                std::string base_label = "None";
                if (base_resolution && base_resolution.record->kind == vespera::AssetKind::Texture)
                    base_label = base_resolution.record->display_name;
                else if (!edit.material.base_texture.path.empty()) base_label = edit.material.base_texture.path.filename().string();
                ImGui::TextUnformatted("Base Texture");
                ImGui::SameLine(108.0f);
                if (ImGui::Button((base_label + "##MaterialBaseTexture").c_str(), ImVec2(-28.0f, 0.0f))) {
                    if (base_resolution && base_resolution.record->kind == vespera::AssetKind::Texture) {
                        state.selection = {SelectionKind::Asset, 0, kNoSubSelection, vespera::kInvalidSceneObjectId, kNoSubSelection, base_resolution.record->asset_id};
                    }
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VESPERA_PROJECT_ASSET")) {
                        if (const auto* texture = asset_record_from_payload(state, payload); texture && texture->kind == vespera::AssetKind::Texture) {
                            edit.material.base_texture = {texture->asset_id, texture->relative_path};
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("X##ClearMaterialTexture")) edit.material.base_texture = {};

                ImGui::ColorEdit4("Base Color", edit.material.properties.base_color.data());
                ImGui::ColorEdit3("Emission Color", edit.material.properties.emission_color.data());
                ImGui::DragFloat("Emission Strength", &edit.material.properties.emission_strength, 0.05f, 0.0f, 32.0f, "%.2f");
                edit.material.properties.emission_strength = std::max(0.0f, edit.material.properties.emission_strength);
                ImGui::SliderFloat("Alpha Cutoff", &edit.material.properties.alpha_cutoff, 0.0f, 1.0f, "%.2f");
                ImGui::TextDisabled("Lit uses Vespera point lighting. Unlit ignores scene lights. Emission is additive.");

                if (ImGui::Button("Apply Material", ImVec2(132.0f, 0.0f))) {
                    const auto saved = vespera::save_material_asset(record->absolute_path, edit.material);
                    if (saved) {
                        append_command_audit(state, vespera::editor::EditorCommandKind::SaveMaterialAsset,
                            "Save Material Asset", true, state.current_state_id, state.current_state_id,
                            vespera::kInvalidSceneObjectId, record->asset_id);
                        push_console(state, ConsoleEntry::Level::Info, "Material saved: " + record->relative_path.generic_string());
                        state.material_asset_edit = {};
                        refresh_asset_catalog(state, false, true);
                        ImGui::End();
                        return;
                    }
                    push_console(state, ConsoleEntry::Level::Error, "Material save failed: " + saved.message);
                }
                ImGui::SameLine();
                if (ImGui::Button("Revert Material")) state.material_asset_edit = {};
            }
        }

        if (record->kind == vespera::AssetKind::SpriteClip) {
            ImGui::SeparatorText("Sprite Clip");
            const auto loaded = vespera::load_sprite_clip_asset(record->absolute_path, state.asset_catalog, state.scene.world);
            if (loaded) {
                ImGui::Text("%u direction(s) x %u frame(s)", loaded.clip.direction_count, loaded.clip.frame_count);
                ImGui::Text("Playback: %.2f fps | %s", loaded.clip.frames_per_second, loaded.clip.loop ? "loop" : "once");
                if (ImGui::Button("Apply / Replace in Scene")) {
                    auto before = capture_snapshot(state);
                    bool replaced = false;
                    for (auto& clip : state.scene.sprite_clips) {
                        if (clip.name == loaded.clip.name) { clip = loaded.clip; replaced = true; break; }
                    }
                    if (!replaced) state.scene.sprite_clips.push_back(loaded.clip);
                    record_immediate_edit(state, std::move(before), replaced ? "Replace Sprite Clip from Asset" : "Add Sprite Clip from Asset");
                }
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.42f, 1.0f), "Invalid sprite clip asset");
                ImGui::TextWrapped("%s", loaded.message.c_str());
            }
        }

        if (record->kind == vespera::AssetKind::SpriteSheet) {
            ImGui::SeparatorText("Sprite Sheet");
            vespera::SectorWorld scratch_world;
            const auto preview = vespera::load_sprite_sheet_asset(record->absolute_path, state.asset_catalog, scratch_world);
            if (preview) {
                ImGui::Text("Sheet: %u x %u", preview.sheet_width, preview.sheet_height);
                ImGui::Text("Frames: %u direction(s) x %u frame(s) | %u x %u each",
                    preview.clip.direction_count, preview.clip.frame_count, preview.frame_width, preview.frame_height);
                ImGui::Text("Playback: %.2f fps | %s", preview.clip.frames_per_second, preview.clip.loop ? "loop" : "once");
                ImGui::TextDisabled("Source: %s | %s", preview.source_path.generic_string().c_str(),
                    preview.source_resolved_by_id ? "stable ID" : "fallback path");
                if (preview.source_fallback_stale) ImGui::TextColored(ImVec4(1.0f, 0.68f, 0.25f, 1.0f), "Fallback path is stale; stable ID still resolves.");
                ImGui::TextDisabled("Runtime-ready external asset. Stable source references survive source-file moves.");
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.42f, 1.0f), "Invalid sprite sheet asset");
                ImGui::TextWrapped("%s", preview.message.c_str());
            }
        }

        if (record->kind == vespera::AssetKind::AudioClip) {
            ImGui::SeparatorText("Audio Clip");
            const auto loaded = vespera::load_audio_clip_asset(record->absolute_path, state.asset_catalog);
            if (loaded) {
                ImGui::Text("Source: %s", loaded.clip.source_path.generic_string().c_str());
                ImGui::SameLine(); ImGui::TextDisabled("(%s)", loaded.clip.source_resolved_by_id ? "stable ID" : "fallback path");
                if (loaded.clip.source_fallback_stale) ImGui::TextColored(ImVec4(1.0f, 0.68f, 0.25f, 1.0f), "Fallback path is stale; stable ID still resolves.");
                ImGui::Text("Volume: %.2f | %s", loaded.clip.volume, loaded.clip.loop ? "loop" : "one-shot");
                ImGui::Text("Spatial: %s", loaded.clip.spatial ? "yes" : "no");
                if (loaded.clip.spatial) ImGui::Text("Distance: %.2f - %.2f", loaded.clip.min_distance, loaded.clip.max_distance);
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.42f, 1.0f), "Invalid audio clip asset");
                ImGui::TextWrapped("%s", loaded.message.c_str());
            }
        }

        ImGui::SeparatorText("Dependencies");
        const auto dependencies = state.asset_catalog.dependencies_of(record->asset_id);
        if (dependencies.empty()) ImGui::TextDisabled("No tracked dependencies.");
        for (const auto* dependency : dependencies) {
            if (dependency->resolved) {
                const auto* target = state.asset_catalog.find_by_id(dependency->target_asset_id);
                const char* mode = dependency->resolved_by_id ? "stable ID" : "path";
                ImGui::BulletText("%s -> %s  [%s]", dependency->reason.c_str(),
                    target ? target->relative_path.generic_string().c_str() : dependency->reference.c_str(), mode);
                if (dependency->stale_fallback_path) {
                    ImGui::Indent();
                    ImGui::TextColored(ImVec4(1.0f, 0.68f, 0.25f, 1.0f), "Fallback path stale: %s", dependency->reference.c_str());
                    ImGui::Unindent();
                }
            } else {
                const std::string missing = dependency->reference.empty() ? dependency->requested_asset_id : dependency->reference;
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.36f, 1.0f), "Missing: %s (%s)", missing.c_str(), dependency->reason.c_str());
            }
        }
        const auto dependents = state.asset_catalog.dependents_of(record->asset_id);
        if (!dependents.empty()) {
            ImGui::TextDisabled("Used by %zu asset(s)", dependents.size());
            for (const auto* dependency : dependents) {
                const auto* source = state.asset_catalog.find_by_id(dependency->source_asset_id);
                if (source) ImGui::BulletText("%s", source->relative_path.generic_string().c_str());
            }
        }

        ImGui::SeparatorText("Stable identity");
        ImGui::TextWrapped("%s", record->asset_id.c_str());
        ImGui::TextDisabled("Content fingerprint: %s", record->source_hash.c_str());

        if (ImGui::Button("Copy Asset ID")) ImGui::SetClipboardText(record->asset_id.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Copy Path")) {
            const auto path_text = record->relative_path.generic_string();
            ImGui::SetClipboardText(path_text.c_str());
        }

        ImGui::SameLine();
        if (ImGui::Button("Move / Rename...")) request_asset_move(state, *record);

        if (record->kind == vespera::AssetKind::Scene) {
            ImGui::Separator();
            if (ImGui::Button("Open Scene", ImVec2(-1.0f, 0.0f))) {
                if (state.dirty) {
                    state.pending_action = PendingAction::OpenScene;
                    state.pending_open_path = record->absolute_path;
                    state.request_unsaved_popup = true;
                } else {
                    open_scene(state, record->absolute_path);
                }
            }
        } else if (record->kind == vespera::AssetKind::EntityPrefab) {
            ImGui::Separator();
            if (ImGui::Button("Instantiate Prefab", ImVec2(-1.0f, 0.0f))) {
                command_instantiate_prefab(state, record->absolute_path);
            }
        }
        if (state.project_loaded) {
            ImGui::SeparatorText("Build inclusion");
            const auto relative = record->relative_path.lexically_normal();
            std::optional<std::size_t> included_index;
            for (std::size_t i = 0; i < state.project.build_includes.size(); ++i) {
                const bool id_match = i < state.project.build_include_asset_ids.size()
                    && !state.project.build_include_asset_ids[i].empty()
                    && state.project.build_include_asset_ids[i] == record->asset_id;
                const bool path_match = state.project.build_includes[i].lexically_normal() == relative;
                if (id_match || path_match) { included_index = i; break; }
            }
            if (!included_index) {
                if (ImGui::Button("Include in Standalone Build", ImVec2(-1.0f, 0.0f))) {
                    state.project.build_includes.push_back(relative);
                    state.project.build_include_asset_ids.push_back(record->asset_id);
                    push_console(state, ConsoleEntry::Level::Info, "Stable build include added: " + relative.generic_string() + " (Save Project to persist)");
                }
            } else {
                if (ImGui::Button("Remove Explicit Build Include", ImVec2(-1.0f, 0.0f))) {
                    const auto index = *included_index;
                    state.project.build_includes.erase(state.project.build_includes.begin() + static_cast<std::ptrdiff_t>(index));
                    if (index < state.project.build_include_asset_ids.size()) {
                        state.project.build_include_asset_ids.erase(state.project.build_include_asset_ids.begin() + static_cast<std::ptrdiff_t>(index));
                    }
                    push_console(state, ConsoleEntry::Level::Info, "Build include removed: " + relative.generic_string() + " (Save Project to persist)");
                }
            }
            ImGui::TextDisabled("Startup-scene dependencies are included automatically.");
            if (record->kind == vespera::AssetKind::Texture) {
                const bool current_icon = state.project.game_icon_asset_id == record->asset_id;
                if (!current_icon && ImGui::Button("Use as Project Game Icon", ImVec2(-1.0f, 0.0f))) {
                    state.project.game_icon = relative;
                    state.project.game_icon_asset_id = record->asset_id;
                    push_console(state, ConsoleEntry::Level::Info, "Project game icon set: " + relative.generic_string() + " (Save Project to persist)");
                } else if (current_icon) {
                    ImGui::TextDisabled("This texture is the current project game icon.");
                }
            }
        }
        if (ImGui::Button("Refresh / Reimport", ImVec2(-1.0f, 0.0f))) {
            refresh_asset_catalog(state, true, true);
        }

        ImGui::End();
        return;
    }

    if (state.selection.kind == SelectionKind::None) {
        ImGui::TextDisabled("Nothing selected.");
        ImGui::TextWrapped("Select a scene object, scene resource, or project asset to inspect it here.");
        ImGui::End();
        return;
    }

    if (state.selection.kind == SelectionKind::Camera) {
        ImGui::TextUnformatted("Camera");
        ImGui::Separator();
        auto& camera = state.scene.camera;

        float pos[3]{camera.position.x, camera.position.y, camera.position.z};
        auto before = capture_snapshot(state);
        const ImGuiID position_id = ImGui::GetID("Position");
        const bool changed_position = ImGui::DragFloat3("Position", pos, 0.05f);
        if (changed_position) camera.position = {pos[0], pos[1], pos[2]};
        track_item_edit(state, std::move(before), "Edit camera position", position_id, changed_position);

        float yaw = camera.yaw * kRadiansToDegrees;
        before = capture_snapshot(state);
        const ImGuiID yaw_id = ImGui::GetID("Yaw");
        const bool changed_yaw = ImGui::DragFloat("Yaw", &yaw, 0.5f, -360.0f, 360.0f, "%.1f deg");
        if (changed_yaw) camera.yaw = yaw * kDegreesToRadians;
        track_item_edit(state, std::move(before), "Edit camera yaw", yaw_id, changed_yaw);

        float pitch = camera.pitch * kRadiansToDegrees;
        before = capture_snapshot(state);
        const ImGuiID pitch_id = ImGui::GetID("Pitch");
        const bool changed_pitch = ImGui::DragFloat("Pitch", &pitch, 0.5f, -89.0f, 89.0f, "%.1f deg");
        if (changed_pitch) camera.pitch = std::clamp(pitch, -89.0f, 89.0f) * kDegreesToRadians;
        track_item_edit(state, std::move(before), "Edit camera pitch", pitch_id, changed_pitch);

        before = capture_snapshot(state);
        const ImGuiID fov_id = ImGui::GetID("Vertical FOV");
        const bool changed_fov = ImGui::DragFloat("Vertical FOV", &camera.vertical_fov_degrees, 0.25f, 30.0f, 130.0f, "%.1f deg");
        track_item_edit(state, std::move(before), "Edit camera FOV", fov_id, changed_fov);
        ImGui::End();
        return;
    }

    if (state.selection.kind == SelectionKind::Sector) {
        if (state.selection.index >= state.scene.world.sectors().size()) {
            state.selection = {};
            ImGui::End();
            return;
        }

        const std::size_t sector_index = state.selection.index;
        vespera::Sector edited = state.scene.world.sectors()[sector_index];
        ImGui::Text("Sector %zu", sector_index);
        ImGui::Separator();

        std::string proposed_name = edited.name;
        auto before = capture_snapshot(state);
        const ImGuiID sector_name_id = ImGui::GetID("Name");
        const bool requested_name_change = ImGui::InputText("Name", &proposed_name);
        bool applied_name_change = false;
        if (requested_name_change && !proposed_name.empty()
            && !sector_name_exists(state.scene, proposed_name, sector_index)) {
            edited.name = proposed_name;
            applied_name_change = state.scene.world.set_sector(sector_index, edited);
        }
        track_item_edit(state, std::move(before), "Rename sector", sector_name_id, applied_name_change);
        if (requested_name_change && !applied_name_change) {
            ImGui::TextColored(ImVec4(1.0f, 0.48f, 0.32f, 1.0f), "Sector names must be non-empty and unique.");
        }

        edited = state.scene.world.sectors()[sector_index];
        before = capture_snapshot(state);
        const ImGuiID floor_height_id = ImGui::GetID("Floor Height");
        bool changed = ImGui::DragFloat("Floor Height", &edited.floor_height, 0.05f);
        if (changed) {
            if (edited.ceiling_height < edited.floor_height + 0.05f) edited.ceiling_height = edited.floor_height + 0.05f;
            state.scene.world.set_sector(sector_index, edited);
        }
        track_item_edit(state, std::move(before), "Edit sector floor height", floor_height_id, changed);

        edited = state.scene.world.sectors()[sector_index];
        before = capture_snapshot(state);
        const ImGuiID ceiling_height_id = ImGui::GetID("Ceiling Height");
        changed = ImGui::DragFloat("Ceiling Height", &edited.ceiling_height, 0.05f);
        if (changed) {
            if (edited.ceiling_height < edited.floor_height + 0.05f) edited.ceiling_height = edited.floor_height + 0.05f;
            state.scene.world.set_sector(sector_index, edited);
        }
        track_item_edit(state, std::move(before), "Edit sector ceiling height", ceiling_height_id, changed);

        edited = state.scene.world.sectors()[sector_index];
        before = capture_snapshot(state);
        if (material_combo("Floor Material", edited.floor_material, state.scene.world)) {
            state.scene.world.set_sector(sector_index, edited);
            record_immediate_edit(state, std::move(before), "Change floor material");
        }
        edited = state.scene.world.sectors()[sector_index];
        before = capture_snapshot(state);
        if (material_combo("Ceiling Material", edited.ceiling_material, state.scene.world)) {
            state.scene.world.set_sector(sector_index, edited);
            record_immediate_edit(state, std::move(before), "Change ceiling material");
        }
        edited = state.scene.world.sectors()[sector_index];
        before = capture_snapshot(state);
        if (material_combo("Default Wall Material", edited.wall_material, state.scene.world)) {
            state.scene.world.set_sector(sector_index, edited);
            record_immediate_edit(state, std::move(before), "Change wall material");
        }

        const auto& sector = state.scene.world.sectors()[sector_index];
        ImGui::SeparatorText("Geometry");
        ImGui::TextWrapped("Drag vertex handles in Sector. Click a wall edge to edit its material or portal target. Coincident vertices in neighboring sectors stay welded.");
        if (state.selection.sub_index != kNoSubSelection && state.selection.sub_index < sector.vertices.size()) {
            const auto& vertex = sector.vertices[state.selection.sub_index];
            ImGui::Text("Selected vertex %zu: X %.3f  Z %.3f", state.selection.sub_index, vertex.x, vertex.z);
        }
        if (ImGui::BeginTable("vertices", 3, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 28.0f);
            ImGui::TableSetupColumn("X");
            ImGui::TableSetupColumn("Z");
            ImGui::TableHeadersRow();
            for (std::size_t i = 0; i < sector.vertices.size(); ++i) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const std::string row_label = std::to_string(i) + "##vertexrow" + std::to_string(i);
                if (ImGui::Selectable(row_label.c_str(), state.selection.sub_index == i, ImGuiSelectableFlags_SpanAllColumns)) {
                    state.selection.sub_index = i;
                    state.selection.side_index = kNoSubSelection;
                }
                ImGui::TableSetColumnIndex(1); ImGui::Text("%.3f", sector.vertices[i].x);
                ImGui::TableSetColumnIndex(2); ImGui::Text("%.3f", sector.vertices[i].z);
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Sides / Portals");
        if (ImGui::BeginTable("sector_sides", 3, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Side", ImGuiTableColumnFlags_WidthFixed, 42.0f);
            ImGui::TableSetupColumn("Material");
            ImGui::TableSetupColumn("Target");
            ImGui::TableHeadersRow();
            for (std::size_t side_i = 0; side_i < sector.sides.size(); ++side_i) {
                const auto& side = sector.sides[side_i];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const std::string side_label = std::to_string(side_i) + "##siderow" + std::to_string(side_i);
                if (ImGui::Selectable(side_label.c_str(), state.selection.side_index == side_i, ImGuiSelectableFlags_SpanAllColumns)) {
                    state.selection.side_index = side_i;
                    state.selection.sub_index = kNoSubSelection;
                }
                ImGui::TableSetColumnIndex(1);
                if (side.material != vespera::kInvalidMaterial && side.material < state.scene.world.materials().size()) {
                    ImGui::TextUnformatted(state.scene.world.materials()[side.material].name.c_str());
                } else {
                    ImGui::TextDisabled("<default>");
                }
                ImGui::TableSetColumnIndex(2);
                if (side.adjacent_sector >= 0 && side.adjacent_sector < static_cast<int>(state.scene.world.sectors().size())) {
                    ImGui::TextUnformatted(state.scene.world.sectors()[static_cast<std::size_t>(side.adjacent_sector)].name.c_str());
                } else {
                    ImGui::TextDisabled("Solid");
                }
            }
            ImGui::EndTable();
        }

        if (state.selection.side_index < state.scene.world.sectors()[sector_index].sides.size()) {
            const std::size_t side_index = state.selection.side_index;
            const auto& current_sector = state.scene.world.sectors()[sector_index];
            const auto a = current_sector.vertices[side_index];
            const auto b = current_sector.vertices[(side_index + 1u) % current_sector.vertices.size()];
            ImGui::Text("Selected side %zu: (%.2f, %.2f) -> (%.2f, %.2f)", side_index, a.x, a.z, b.x, b.z);

            edited = current_sector;
            before = capture_snapshot(state);
            if (material_combo("Side Material", edited.sides[side_index].material, state.scene.world)) {
                state.scene.world.set_sector(sector_index, edited);
                record_immediate_edit(state, std::move(before), "Change sector side material");
            }

            const auto& portal_sector = state.scene.world.sectors()[sector_index];
            const int current_target = portal_sector.sides[side_index].adjacent_sector;
            const char* portal_preview = "<solid wall>";
            std::string portal_preview_storage;
            if (current_target >= 0 && current_target < static_cast<int>(state.scene.world.sectors().size())) {
                portal_preview_storage = state.scene.world.sectors()[static_cast<std::size_t>(current_target)].name;
                portal_preview = portal_preview_storage.c_str();
            }
            int requested_target = current_target;
            bool requested_target_change = false;
            if (ImGui::BeginCombo("Portal Target", portal_preview)) {
                if (ImGui::Selectable("<solid wall>", current_target < 0)) {
                    requested_target = -1;
                    requested_target_change = requested_target != current_target;
                }
                for (std::size_t target_i = 0; target_i < state.scene.world.sectors().size(); ++target_i) {
                    if (target_i == sector_index) continue;
                    const bool selected_target = current_target == static_cast<int>(target_i);
                    if (ImGui::Selectable(state.scene.world.sectors()[target_i].name.c_str(), selected_target)) {
                        requested_target = static_cast<int>(target_i);
                        requested_target_change = requested_target != current_target;
                    }
                }
                ImGui::EndCombo();
            }
            if (requested_target_change) {
                command_set_sector_portal_target(state, sector_index, side_index, requested_target);
            }

            const auto& after_sector = state.scene.world.sectors()[sector_index];
            const int after_target = after_sector.sides[side_index].adjacent_sector;
            if (after_target >= 0 && after_target < static_cast<int>(state.scene.world.sectors().size())) {
                const auto matching = vespera::find_matching_sector_side(
                    state.scene.world, sector_index, side_index, static_cast<std::size_t>(after_target));
                if (!matching) {
                    ImGui::TextColored(ImVec4(1.0f, 0.62f, 0.25f, 1.0f), "No matching shared edge in target sector.");
                } else {
                    const auto& target = state.scene.world.sectors()[static_cast<std::size_t>(after_target)];
                    const bool reciprocal = *matching < target.sides.size()
                        && target.sides[*matching].adjacent_sector == static_cast<int>(sector_index);
                    if (reciprocal) {
                        ImGui::TextDisabled("Reciprocal portal linked to target side %zu.", *matching);
                    } else {
                        ImGui::TextColored(ImVec4(1.0f, 0.62f, 0.25f, 1.0f), "Matching edge exists, but target is not reciprocal.");
                    }
                }
            }
        }

        int portal_count = 0;
        for (const auto& side : state.scene.world.sectors()[sector_index].sides) {
            if (side.adjacent_sector >= 0) ++portal_count;
        }
        ImGui::Text("Portal edges: %d", portal_count);

        ImGui::Separator();
        if (ImGui::Button("Duplicate", ImVec2(110.0f, 0.0f))) {
            command_duplicate_selection(state);
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete", ImVec2(110.0f, 0.0f))) {
            command_delete_selection(state);
        }
        ImGui::End();
        return;
    }

    if (state.selection.kind == SelectionKind::Material) {
        if (state.selection.index >= state.scene.world.materials().size()) {
            state.selection = {};
            ImGui::End();
            return;
        }

        const std::size_t material_index = state.selection.index;
        vespera::WorldMaterial material = state.scene.world.materials()[material_index];
        ImGui::Text("Material %zu", material_index);
        ImGui::Separator();

        std::string proposed_name = material.name;
        auto before = capture_snapshot(state);
        const ImGuiID material_name_id = ImGui::GetID("Name");
        const bool requested_name_change = ImGui::InputText("Name", &proposed_name);
        bool applied_name_change = false;
        if (requested_name_change && !proposed_name.empty()
            && !material_name_exists(state.scene, proposed_name, material_index)) {
            material.name = proposed_name;
            applied_name_change = state.scene.world.set_material(material_index, material);
        }
        track_item_edit(state, std::move(before), "Rename material", material_name_id, applied_name_change);
        if (requested_name_change && !applied_name_change) {
            ImGui::TextColored(ImVec4(1.0f, 0.48f, 0.32f, 1.0f), "Material names must be non-empty and unique.");
        }

        material = state.scene.world.materials()[material_index];
        before = capture_snapshot(state);
        const ImGuiID material_color_id = ImGui::GetID("Tint");
        const bool changed_color = ImGui::ColorEdit4("Tint", material.color.data());
        if (changed_color) state.scene.world.set_material(material_index, material);
        track_item_edit(state, std::move(before), "Edit material tint", material_color_id, changed_color);

        material = state.scene.world.materials()[material_index];
        before = capture_snapshot(state);
        if (texture_combo("Texture", material.texture, state.scene.world)) {
            state.scene.world.set_material(material_index, material);
            record_immediate_edit(state, std::move(before), "Change material texture");
        }

        material = state.scene.world.materials()[material_index];
        float uv[2]{material.uv_scale.x, material.uv_scale.z};
        before = capture_snapshot(state);
        const ImGuiID uv_id = ImGui::GetID("UV Scale");
        const bool changed_uv = ImGui::DragFloat2("UV Scale", uv, 0.025f, -16.0f, 16.0f, "%.3f");
        if (changed_uv) {
            material.uv_scale = {uv[0], uv[1]};
            state.scene.world.set_material(material_index, material);
        }
        track_item_edit(state, std::move(before), "Edit material UV scale", uv_id, changed_uv);

        ImGui::Separator();
        ImGui::TextWrapped("Deleting a material clears direct references to it and remaps higher material ids safely.");
        if (ImGui::Button("Duplicate", ImVec2(110.0f, 0.0f))) {
            command_duplicate_selection(state);
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete", ImVec2(110.0f, 0.0f))) {
            command_delete_selection(state);
        }
        ImGui::End();
        return;
    }

    if (state.selection.kind == SelectionKind::SpriteClip) {
        if (state.selection.index >= state.scene.sprite_clips.size()) {
            state.selection = {};
            ImGui::End();
            return;
        }

        auto& clip = state.scene.sprite_clips[state.selection.index];
        ImGui::Text("Sprite Clip %zu", state.selection.index);
        ImGui::Separator();

        std::string proposed_name = clip.name;
        auto before = capture_snapshot(state);
        const ImGuiID clip_name_id = ImGui::GetID("Name");
        const bool requested_name_change = ImGui::InputText("Name", &proposed_name);
        bool applied_name_change = false;
        if (requested_name_change && !proposed_name.empty()
            && !clip_name_exists(state.scene, proposed_name, state.selection.index)) {
            const std::string old_name = clip.name;
            clip.name = proposed_name;
            for (auto& entity : state.scene.entities) {
                if (entity.sprite_renderer && entity.sprite_renderer->animation_clip == old_name) {
                    entity.sprite_renderer->animation_clip = clip.name;
                }
            }
            applied_name_change = true;
        }
        track_item_edit(state, std::move(before), "Rename sprite clip", clip_name_id, applied_name_change);
        if (requested_name_change && !applied_name_change) {
            ImGui::TextColored(ImVec4(1.0f, 0.48f, 0.32f, 1.0f), "Clip names must be non-empty and unique.");
        }

        int direction_choice = clip.direction_count == 8u ? 2 : (clip.direction_count == 4u ? 1 : 0);
        static const char* direction_labels[] = {"1 direction", "4 directions", "8 directions"};
        before = capture_snapshot(state);
        if (ImGui::Combo("Directions", &direction_choice, direction_labels, 3)) {
            const std::uint32_t directions = direction_choice == 2 ? 8u : (direction_choice == 1 ? 4u : 1u);
            resize_clip_frames(clip, directions, clip.frame_count, default_sprite_texture(state.scene));
            record_immediate_edit(state, std::move(before), "Change sprite clip direction count");
        }

        int frame_count = static_cast<int>(clip.frame_count);
        before = capture_snapshot(state);
        const ImGuiID frame_count_id = ImGui::GetID("Frames");
        const bool changed_frames = ImGui::DragInt("Frames", &frame_count, 0.1f, 1, 16);
        if (changed_frames) {
            frame_count = std::clamp(frame_count, 1, 16);
            resize_clip_frames(clip, clip.direction_count, static_cast<std::uint32_t>(frame_count), default_sprite_texture(state.scene));
        }
        track_item_edit(state, std::move(before), "Change sprite clip frame count", frame_count_id, changed_frames);

        before = capture_snapshot(state);
        const ImGuiID fps_id = ImGui::GetID("Frames Per Second");
        const bool changed_fps = ImGui::DragFloat("Frames Per Second", &clip.frames_per_second, 0.1f, 0.0f, 120.0f, "%.2f");
        if (changed_fps) clip.frames_per_second = std::max(clip.frames_per_second, 0.0f);
        track_item_edit(state, std::move(before), "Change sprite clip FPS", fps_id, changed_fps);

        before = capture_snapshot(state);
        const bool changed_loop = ImGui::Checkbox("Loop", &clip.loop);
        if (changed_loop) {
            record_immediate_edit(state, std::move(before), "Toggle sprite clip looping");
        }

        ImGui::SeparatorText("Frames");
        ImGui::TextWrapped("Direction 0 is the actor front. Higher directions rotate clockwise around the actor.");
        for (std::uint32_t direction = 0; direction < clip.direction_count; ++direction) {
            ImGui::PushID(static_cast<int>(direction));
            const std::string header = std::format("Direction {}", direction);
            if (ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                for (std::uint32_t frame = 0; frame < clip.frame_count; ++frame) {
                    const std::size_t texture_index = static_cast<std::size_t>(direction) * clip.frame_count + frame;
                    if (texture_index >= clip.textures.size()) continue;
                    ImGui::PushID(static_cast<int>(frame));
                    const std::string label = std::format("Frame {}", frame);
                    before = capture_snapshot(state);
                    if (texture_combo(label.c_str(), clip.textures[texture_index], state.scene.world)) {
                        record_immediate_edit(state, std::move(before), "Change sprite clip frame texture");
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }

        ImGui::Separator();
        if (ImGui::Button("Duplicate", ImVec2(110.0f, 0.0f))) {
            command_duplicate_selection(state);
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete", ImVec2(110.0f, 0.0f))) {
            command_delete_selection(state);
        }
        ImGui::End();
        return;
    }

    if (state.selection.kind == SelectionKind::Entity) {
        const auto resolved_entity_index = selected_entity_index(state);
        if (!resolved_entity_index) {
            state.selection = {};
            state.selected_entity_ids.clear();
            ImGui::End();
            return;
        }
        state.selection.index = *resolved_entity_index;
        auto& entity = state.scene.entities[*resolved_entity_index];
        if (state.selected_entity_ids.size() > 1) {
            ImGui::TextColored(ImVec4(0.96f, 0.78f, 0.32f, 1.0f), "%zu entities selected", state.selected_entity_ids.size());
            ImGui::SameLine();
            ImGui::TextDisabled("(Inspector edits primary; Scene gizmo transforms the group)");
            ImGui::Separator();
        }
        ImGui::Text("Entity %zu", *resolved_entity_index);
        ImGui::SameLine();
        ImGui::TextDisabled("ID %llu", static_cast<unsigned long long>(entity.id));
        ImGui::Separator();

        auto before = capture_snapshot(state);
        const bool changed_enabled = ImGui::Checkbox("Enabled", &entity.enabled);
        if (changed_enabled) record_immediate_edit(state, std::move(before), "Toggle entity enabled");

        before = capture_snapshot(state);
        const ImGuiID entity_name_id = ImGui::GetID("Name");
        const bool changed_name = ImGui::InputText("Name", &entity.name);
        track_item_edit(state, std::move(before), "Rename entity", entity_name_id, changed_name);

        before = capture_snapshot(state);
        const ImGuiID entity_tag_id = ImGui::GetID("Tag");
        const bool changed_tag = ImGui::InputText("Tag", &entity.tag);
        track_item_edit(state, std::move(before), "Edit entity tag", entity_tag_id, changed_tag);

        before = capture_snapshot(state);
        const ImGuiID entity_layer_id = ImGui::GetID("Layer");
        const bool changed_layer = ImGui::InputText("Layer", &entity.layer);
        track_item_edit(state, std::move(before), "Edit entity layer", entity_layer_id, changed_layer);
        ImGui::TextDisabled("Tag = semantic gameplay query  |  Layer = broad system grouping");

        ImGui::SeparatorText("Hierarchy");
        if (entity.parent_id == vespera::kInvalidSceneObjectId) {
            ImGui::TextDisabled("Parent: <root>");
        } else if (const auto* parent = state.scene.find_entity(entity.parent_id)) {
            ImGui::Text("Parent: %s", parent->name.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Unparent")) command_reparent_entity(state, entity.id, vespera::kInvalidSceneObjectId);
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.38f, 1.0f), "Parent: missing ID %llu", static_cast<unsigned long long>(entity.parent_id));
        }
        ImGui::TextDisabled("Drag entities in Hierarchy to parent/reparent while preserving world position.");

        ImGui::SeparatorText("Prefab");
        if (entity.prefab_source.empty()) {
            ImGui::TextDisabled("Unpacked entity");
            if (ImGui::Button("Create Prefab from Entity")) {
                command_create_prefab_from_selected(state);
            }
        } else {
            ImGui::TextWrapped("Source: %s", entity.prefab_source.path.generic_string().c_str());
            if (!entity.prefab_source.asset_id.empty()) ImGui::TextDisabled("Asset ID: %s", entity.prefab_source.asset_id.c_str());
            ImGui::TextColored(ImVec4(0.52f, 0.78f, 0.66f, 1.0f), "Linked prefab instance (stable-ID first)");
            if (ImGui::Button("Select Source", ImVec2(110.0f, 0.0f))) {
                const auto resolved = state.asset_catalog.resolve_reference(entity.prefab_source);
                if (resolved && resolved.record) {
                    state.selection = {SelectionKind::Asset};
                    state.selection.asset_id = resolved.record->asset_id;
                    state.selected_entity_ids.clear();
                    state.asset_browser_folder = resolved.record->relative_path.parent_path();
                } else {
                    push_console(state, ConsoleEntry::Level::Warning, "Prefab source could not be resolved in the current asset catalog.");
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Apply to Prefab", ImVec2(120.0f, 0.0f))) {
                command_apply_selected_to_prefab(state);
            }
            ImGui::SameLine();
            if (ImGui::Button("Revert", ImVec2(90.0f, 0.0f))) {
                command_revert_selected_from_prefab(state);
            }
            ImGui::SameLine();
            if (ImGui::Button("Unpack", ImVec2(90.0f, 0.0f))) {
                command_unpack_selected_prefab(state);
            }
            ImGui::TextDisabled("Apply writes the whole entity to the prefab; Revert reloads it. Per-property overrides come later.");
        }

        ImGui::SeparatorText(entity.parent_id == vespera::kInvalidSceneObjectId ? "Transform" : "Local Transform");
        ImGui::TextDisabled("Scene tool: %s  |  %s  |  snap %s",
            scene_tool_name(state.scene_view_3d.tool),
            state.scene_view_3d.local_space ? "Local" : "Global",
            state.scene_view_3d.snap_enabled ? "on" : "off");
        auto& transform = entity.transform;

        float pos[3]{transform.position.x, transform.position.y, transform.position.z};
        before = capture_snapshot(state);
        const ImGuiID position_id = ImGui::GetID("Position");
        const bool changed_position = ImGui::DragFloat3("Position", pos, 0.05f);
        if (changed_position) transform.position = {pos[0], pos[1], pos[2]};
        track_item_edit(state, std::move(before), "Edit entity position", position_id, changed_position);

        float rotation[3]{
            transform.rotation.x * kRadiansToDegrees,
            transform.rotation.y * kRadiansToDegrees,
            transform.rotation.z * kRadiansToDegrees
        };
        before = capture_snapshot(state);
        const ImGuiID rotation_id = ImGui::GetID("Rotation");
        const bool changed_rotation = ImGui::DragFloat3("Rotation", rotation, 1.0f, -3600.0f, 3600.0f, "%.1f deg");
        if (changed_rotation) {
            transform.rotation = {
                rotation[0] * kDegreesToRadians,
                rotation[1] * kDegreesToRadians,
                rotation[2] * kDegreesToRadians
            };
        }
        track_item_edit(state, std::move(before), "Edit entity rotation", rotation_id, changed_rotation);

        float scale[3]{transform.scale.x, transform.scale.y, transform.scale.z};
        before = capture_snapshot(state);
        const ImGuiID scale_id = ImGui::GetID("Scale");
        const bool changed_scale = ImGui::DragFloat3("Scale", scale, 0.025f, 0.01f, 100.0f, "%.3f");
        if (changed_scale) {
            transform.scale = {
                std::max(scale[0], 0.01f),
                std::max(scale[1], 0.01f),
                std::max(scale[2], 0.01f)
            };
        }
        track_item_edit(state, std::move(before), "Edit entity scale", scale_id, changed_scale);

        if (entity.parent_id != vespera::kInvalidSceneObjectId) {
            const auto world = editor_world_transform(state, entity);
            ImGui::TextDisabled("World: P %.2f %.2f %.2f | R %.1f %.1f %.1f deg | S %.2f %.2f %.2f",
                world.position.x, world.position.y, world.position.z,
                world.rotation.x * kRadiansToDegrees, world.rotation.y * kRadiansToDegrees, world.rotation.z * kRadiansToDegrees,
                world.scale.x, world.scale.y, world.scale.z);
        }
        ImGui::TextDisabled("Transform is the required component on every entity.");
        ImGui::SeparatorText("Components");

        if (ImGui::Button("Add Component...")) {
            ImGui::OpenPopup("add_component_popup");
        }
        if (ImGui::BeginPopup("add_component_popup")) {
            for (const auto& info : vespera::kBuiltinComponentTypes) {
                if (!info.removable || entity.has_component(info.type)) continue;
                if (ImGui::MenuItem(info.display_name.data())) {
                    before = capture_snapshot(state);
                    if (entity.add_component(info.type)) {
                        if (info.type == vespera::BuiltinComponentType::SpriteRenderer) {
                            auto& sprite = *entity.sprite_renderer;
                            sprite.texture = default_sprite_texture(state.scene);
                            sprite.size = {1.0f, 1.5f};
                        }
                        record_immediate_edit(state, std::move(before),
                            std::string("Add ") + std::string(info.display_name) + " component");
                    }
                }
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("C# Script")) {
                if (state.managed_metadata.loaded && !state.managed_metadata.scripts.empty()) {
                    for (const auto& meta : state.managed_metadata.scripts) {
                        if (ImGui::MenuItem(meta.class_name.c_str())) {
                            before = capture_snapshot(state);
                            entity.add_managed_script(meta.class_name);
                            record_immediate_edit(state, std::move(before), "Add C# Script");
                        }
                    }
                } else if (ImGui::MenuItem("Manual / unresolved type")) {
                    before = capture_snapshot(state); entity.add_managed_script();
                    record_immediate_edit(state, std::move(before), "Add C# Script");
                }
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }

        bool optional_component_present = entity.sprite_renderer.has_value() || entity.mesh_renderer.has_value() || entity.cylinder_collider.has_value() || entity.point_light.has_value() || !entity.managed_scripts.empty();
        if (!optional_component_present) {
            ImGui::TextDisabled("No optional components attached.");
        }

        if (entity.sprite_renderer) {
            auto& sprite = *entity.sprite_renderer;
            if (ImGui::TreeNodeEx("Sprite Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextDisabled("sectorline.sprite_renderer");
                ImGui::TextDisabled("Directional facing uses Transform rotation Y.");

                float size[2]{sprite.size.x, sprite.size.z};
                before = capture_snapshot(state);
                const ImGuiID sprite_size_id = ImGui::GetID("Size");
                const bool changed_size = ImGui::DragFloat2("Size", size, 0.025f, 0.05f, 100.0f);
                if (changed_size) sprite.size = {std::max(size[0], 0.05f), std::max(size[1], 0.05f)};
                track_item_edit(state, std::move(before), "Edit Sprite Renderer size", sprite_size_id, changed_size);

                before = capture_snapshot(state);
                bool changed_sprite_texture = texture_combo("Fallback Texture", sprite.texture, state.scene.world);
                changed_sprite_texture = accept_texture_asset_drop(state, sprite.texture, "Sprite Renderer") || changed_sprite_texture;
                if (changed_sprite_texture) {
                    record_immediate_edit(state, std::move(before), "Change Sprite Renderer texture");
                }

                before = capture_snapshot(state);
                if (sprite_clip_combo("Clip", sprite.animation_clip, state.scene)) {
                    record_immediate_edit(state, std::move(before), "Change Sprite Renderer animation clip");
                }

                before = capture_snapshot(state);
                const ImGuiID speed_id = ImGui::GetID("Animation Speed");
                const bool changed_speed = ImGui::DragFloat("Animation Speed", &sprite.animation_speed, 0.025f, 0.0f, 10.0f, "%.2fx");
                if (changed_speed) sprite.animation_speed = std::max(sprite.animation_speed, 0.0f);
                track_item_edit(state, std::move(before), "Edit Sprite Renderer animation speed", speed_id, changed_speed);

                before = capture_snapshot(state);
                const ImGuiID offset_id = ImGui::GetID("Time Offset");
                const bool changed_offset = ImGui::DragFloat("Time Offset", &sprite.animation_time_offset, 0.01f, -60.0f, 60.0f, "%.2f s");
                track_item_edit(state, std::move(before), "Edit Sprite Renderer animation time offset", offset_id, changed_offset);

                before = capture_snapshot(state);
                const bool changed_paused = ImGui::Checkbox("Pause Animation", &sprite.animation_paused);
                if (changed_paused) record_immediate_edit(state, std::move(before), "Toggle Sprite Renderer animation pause");

                if (!sprite.animation_clip.empty()) {
                    if (const auto* clip = state.scene.find_sprite_clip(sprite.animation_clip)) {
                        ImGui::TextDisabled("%u directions  |  %u frames  |  %.2f fps  |  %s",
                            clip->direction_count, clip->frame_count, clip->frames_per_second, clip->loop ? "loop" : "once");
                    } else {
                        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "Missing clip: %s", sprite.animation_clip.c_str());
                    }
                } else {
                    ImGui::TextDisabled("Static sprite: fallback texture is drawn directly.");
                }

                before = capture_snapshot(state);
                const ImGuiID tint_id = ImGui::GetID("Tint");
                const bool changed_tint = ImGui::ColorEdit4("Tint", sprite.color.data());
                track_item_edit(state, std::move(before), "Edit Sprite Renderer tint", tint_id, changed_tint);

                if (ImGui::Button("Remove Sprite Renderer")) {
                    before = capture_snapshot(state);
                    entity.remove_component(vespera::BuiltinComponentType::SpriteRenderer);
                    record_immediate_edit(state, std::move(before), "Remove Sprite Renderer component");
                }
                ImGui::TreePop();
            }
        }

        if (entity.mesh_renderer) {
            auto& mesh = *entity.mesh_renderer;
            if (ImGui::TreeNodeEx("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextDisabled("sectorline.mesh_renderer");
                ImGui::TextDisabled("Built-in blockout primitive. Transform Scale controls dimensions.");
                const char* primitives[] = {"Cube", "Plane", "Cylinder", "Sphere"};
                int primitive = static_cast<int>(mesh.primitive);
                before = capture_snapshot(state);
                if (ImGui::Combo("Primitive", &primitive, primitives, 4)) {
                    mesh.primitive = static_cast<vespera::PrimitiveMeshType>(std::clamp(primitive,0,3));
                    record_immediate_edit(state, std::move(before), "Change Mesh Renderer primitive");
                }
                before = capture_snapshot(state);
                const auto material_resolution = state.asset_catalog.resolve_reference(mesh.material);
                std::string material_label = "None";
                if (material_resolution && material_resolution.record->kind == vespera::AssetKind::Material)
                    material_label = material_resolution.record->display_name;
                else if (!mesh.material.path.empty()) material_label = mesh.material.path.filename().string();
                ImGui::TextUnformatted("Material");
                ImGui::SameLine(92.0f);
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::Button((material_label + "##MeshMaterial").c_str(), ImVec2(-28.0f, 0.0f))) {
                    if (material_resolution && material_resolution.record->kind == vespera::AssetKind::Material) {
                        state.selection = {SelectionKind::Asset, 0, kNoSubSelection, vespera::kInvalidSceneObjectId, kNoSubSelection, material_resolution.record->asset_id};
                    }
                }
                const bool dropped_material = accept_material_asset_drop(state, mesh);
                ImGui::SameLine();
                if (ImGui::SmallButton("X##ClearMeshMaterial")) {
                    mesh.material = {};
                    mesh.material_resolved = false;
                    mesh.resolved_material_texture = vespera::kInvalidTexture;
                    record_immediate_edit(state, std::move(before), "Clear Mesh Renderer material");
                } else if (dropped_material) {
                    record_immediate_edit(state, std::move(before), "Assign Mesh Renderer material");
                }
                if (mesh.material_resolved) {
                    ImGui::TextDisabled("Shader: %s", vespera::builtin_material_shader_name(mesh.resolved_material.shader).data());
                } else if (!mesh.material.empty()) {
                    ImGui::TextColored(ImVec4(1.0f,0.55f,0.42f,1.0f), "Material reference is unresolved.");
                }

                before = capture_snapshot(state);
                bool changed_mesh_texture = texture_combo("Fallback Texture", mesh.texture, state.scene.world);
                changed_mesh_texture = accept_texture_asset_drop(state, mesh.texture, "Mesh Renderer") || changed_mesh_texture;
                if (changed_mesh_texture) {
                    record_immediate_edit(state, std::move(before), "Change Mesh Renderer texture");
                }
                before = capture_snapshot(state);
                const ImGuiID mesh_tint_id = ImGui::GetID("Mesh Tint");
                const bool changed_mesh_tint = ImGui::ColorEdit4("Instance Tint", mesh.color.data());
                track_item_edit(state, std::move(before), "Edit Mesh Renderer tint", mesh_tint_id, changed_mesh_tint);
                if (ImGui::Button("Remove Mesh Renderer")) {
                    before = capture_snapshot(state); entity.remove_component(vespera::BuiltinComponentType::MeshRenderer);
                    record_immediate_edit(state, std::move(before), "Remove Mesh Renderer component");
                }
                ImGui::TreePop();
            }
        }

        if (entity.cylinder_collider) {
            auto& collider = *entity.cylinder_collider;
            if (ImGui::TreeNodeEx("Cylinder Collider", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextDisabled("sectorline.cylinder_collider");
                ImGui::TextDisabled("2.5D X/Z collision primitive; triggers do not block movement.");

                before = capture_snapshot(state);
                const ImGuiID radius_id = ImGui::GetID("Radius");
                const bool changed_radius = ImGui::DragFloat("Radius", &collider.radius, 0.025f, 0.01f, 100.0f, "%.3f");
                if (changed_radius) collider.radius = std::max(collider.radius, 0.01f);
                track_item_edit(state, std::move(before), "Edit Cylinder Collider radius", radius_id, changed_radius);

                before = capture_snapshot(state);
                const ImGuiID height_id = ImGui::GetID("Height");
                const bool changed_height = ImGui::DragFloat("Height", &collider.height, 0.025f, 0.01f, 100.0f, "%.3f");
                if (changed_height) collider.height = std::max(collider.height, 0.01f);
                track_item_edit(state, std::move(before), "Edit Cylinder Collider height", height_id, changed_height);

                float center[3]{collider.center.x, collider.center.y, collider.center.z};
                before = capture_snapshot(state);
                const ImGuiID center_id = ImGui::GetID("Center");
                const bool changed_center = ImGui::DragFloat3("Center", center, 0.025f);
                if (changed_center) collider.center = {center[0], center[1], center[2]};
                track_item_edit(state, std::move(before), "Edit Cylinder Collider center", center_id, changed_center);

                before = capture_snapshot(state);
                const bool changed_trigger = ImGui::Checkbox("Is Trigger", &collider.is_trigger);
                if (changed_trigger) record_immediate_edit(state, std::move(before), "Toggle Cylinder Collider trigger");

                if (ImGui::Button("Remove Cylinder Collider")) {
                    before = capture_snapshot(state);
                    entity.remove_component(vespera::BuiltinComponentType::CylinderCollider);
                    record_immediate_edit(state, std::move(before), "Remove Cylinder Collider component");
                }
                ImGui::TreePop();
            }
        }

        if (entity.point_light) {
            auto& light = *entity.point_light;
            if (ImGui::TreeNodeEx("Point Light", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextDisabled("sectorline.point_light");
                ImGui::TextDisabled("Unshadowed radial light. D3D12 evaluates up to 32 active point lights per view.");

                before = capture_snapshot(state);
                const ImGuiID light_color_id = ImGui::GetID("Light Color");
                const bool changed_light_color = ImGui::ColorEdit4("Color", light.color.data());
                track_item_edit(state, std::move(before), "Edit Point Light color", light_color_id, changed_light_color);

                before = capture_snapshot(state);
                const ImGuiID intensity_id = ImGui::GetID("Light Intensity");
                const bool changed_intensity = ImGui::DragFloat("Intensity", &light.intensity, 0.025f, 0.0f, 16.0f, "%.2f");
                if (changed_intensity) light.intensity = std::max(light.intensity, 0.0f);
                track_item_edit(state, std::move(before), "Edit Point Light intensity", intensity_id, changed_intensity);

                before = capture_snapshot(state);
                const ImGuiID light_radius_id = ImGui::GetID("Light Radius");
                const bool changed_light_radius = ImGui::DragFloat("Radius", &light.radius, 0.05f, 0.05f, 100.0f, "%.2f");
                if (changed_light_radius) light.radius = std::max(light.radius, 0.05f);
                track_item_edit(state, std::move(before), "Edit Point Light radius", light_radius_id, changed_light_radius);

                if (ImGui::Button("Remove Point Light")) {
                    before = capture_snapshot(state);
                    entity.remove_component(vespera::BuiltinComponentType::PointLight);
                    record_immediate_edit(state, std::move(before), "Remove Point Light component");
                }
                ImGui::TreePop();
            }
        }

        for (std::size_t script_index = 0; script_index < entity.managed_scripts.size();) {
            auto& script = entity.managed_scripts[script_index];
            ImGui::PushID(static_cast<int>(script_index) + 700000);
            const std::string header = std::format("C# Script {}##managed", script_index + 1);
            bool remove_script = false;
            bool move_script_up = false;
            bool move_script_down = false;
            bool duplicate_script = false;
            if (ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                if (state.managed_metadata.loaded && !state.managed_metadata.scripts.empty()) {
                    const char* preview = script.class_name.empty() ? "<select component>" : script.class_name.c_str();
                    if (ImGui::BeginCombo("Component Type", preview)) {
                        for (const auto& candidate : state.managed_metadata.scripts) {
                            const bool selected = candidate.class_name == script.class_name;
                            if (ImGui::Selectable(candidate.class_name.c_str(), selected) && !selected) {
                                before = capture_snapshot(state);
                                script.class_name = candidate.class_name;
                                record_immediate_edit(state, std::move(before), "Select C# component type");
                            }
                            if (selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                }
                before = capture_snapshot(state);
                const ImGuiID class_id = ImGui::GetID("Class Name");
                const bool changed_class = ImGui::InputText("Class Name", &script.class_name);
                track_item_edit(state, std::move(before), "Edit C# script class", class_id, changed_class);
                ImGui::TextDisabled("Class Name stays editable so unresolved scripts are never destroyed.");
                before = capture_snapshot(state);
                const bool changed_script_enabled = ImGui::Checkbox("Script Enabled", &script.enabled);
                if (changed_script_enabled) record_immediate_edit(state, std::move(before), "Toggle C# script");

                const auto* meta = state.managed_metadata.find(script.class_name);
                if (meta) {
                    ImGui::SeparatorText("Exposed Fields");
                    for (const auto& field_meta : meta->fields) {
                        ImGui::PushID(field_meta.name.c_str());
                        auto matches_field = [&](const auto& stored) {
                            if (stored.field_name == field_meta.name) return true;
                            return std::find(field_meta.aliases.begin(), field_meta.aliases.end(), stored.field_name) != field_meta.aliases.end();
                        };
                        auto it = std::find_if(script.fields.begin(), script.fields.end(), matches_field);
                        bool overridden = it != script.fields.end();
                        if (ImGui::Checkbox("##override", &overridden)) {
                            before = capture_snapshot(state);
                            if (overridden) script.fields.push_back({field_meta.name, field_meta.type, default_managed_value(field_meta)});
                            else if (it != script.fields.end()) script.fields.erase(it);
                            record_immediate_edit(state, std::move(before), std::string(overridden ? "Override " : "Reset ") + field_meta.display_name);
                            it = std::find_if(script.fields.begin(), script.fields.end(), matches_field);
                        }
                        ImGui::SameLine(); ImGui::TextUnformatted(field_meta.display_name.c_str());
                        if (it != script.fields.end() && it->field_name != field_meta.name) {
                            ImGui::SameLine();
                            ImGui::TextDisabled("(legacy key: %s)", it->field_name.c_str());
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Migrate Name")) {
                                before = capture_snapshot(state);
                                it->field_name = field_meta.name;
                                it->type_name = field_meta.type;
                                record_immediate_edit(state, std::move(before), "Migrate C# serialized field name");
                            }
                        }
                        if (field_meta.type == "unsupported") { ImGui::SameLine(); ImGui::TextDisabled("(unsupported: %s)", field_meta.clr_type.c_str()); }
                        else if (it != script.fields.end()) {
                            bool changed = false;
                            if (field_meta.type == "bool") {
                                before = capture_snapshot(state);
                                const ImGuiID value_id = ImGui::GetID("Value");
                                bool value = it->serialized_value == "true" || it->serialized_value == "1";
                                changed = ImGui::Checkbox("Value", &value);
                                if (changed) it->serialized_value = value ? "true" : "false";
                                track_item_edit(state, std::move(before), "Edit exposed C# field", value_id, changed);
                            } else if (field_meta.type == "int") {
                                int value = 0; std::istringstream(it->serialized_value) >> value;
                                if (field_meta.has_range) {
                                    const int min_value = static_cast<int>(field_meta.range_min);
                                    const int max_value = static_cast<int>(field_meta.range_max);
                                    before = capture_snapshot(state);
                                    ImGui::SetNextItemWidth((std::max)(120.0f, ImGui::GetContentRegionAvail().x - 105.0f));
                                    const ImGuiID slider_id = ImGui::GetID("##range_slider");
                                    const bool slider_changed = ImGui::SliderInt("##range_slider", &value, min_value, max_value);
                                    if (slider_changed) it->serialized_value = std::to_string(value);
                                    track_item_edit(state, std::move(before), "Edit exposed C# field", slider_id, slider_changed);
                                    ImGui::SameLine();
                                    int precise = value;
                                    before = capture_snapshot(state);
                                    ImGui::SetNextItemWidth(96.0f);
                                    const ImGuiID precise_id = ImGui::GetID("##range_precise");
                                    const bool precise_changed = ImGui::InputInt("##range_precise", &precise, 0, 0);
                                    if (precise_changed) { precise = std::clamp(precise, min_value, max_value); it->serialized_value = std::to_string(precise); }
                                    track_item_edit(state, std::move(before), "Enter precise exposed C# field value", precise_id, precise_changed);
                                    changed = slider_changed || precise_changed;
                                } else {
                                    before = capture_snapshot(state);
                                    const ImGuiID value_id = ImGui::GetID("Value");
                                    changed = ImGui::InputInt("Value", &value);
                                    if (changed) it->serialized_value = std::to_string(value);
                                    track_item_edit(state, std::move(before), "Edit exposed C# field", value_id, changed);
                                }
                            } else if (field_meta.type == "float") {
                                float value = 0.0f; std::istringstream(it->serialized_value) >> value;
                                if (field_meta.has_range) {
                                    before = capture_snapshot(state);
                                    ImGui::SetNextItemWidth((std::max)(120.0f, ImGui::GetContentRegionAvail().x - 105.0f));
                                    const ImGuiID slider_id = ImGui::GetID("##range_slider");
                                    const bool slider_changed = ImGui::SliderFloat("##range_slider", &value, field_meta.range_min, field_meta.range_max, "%.3f");
                                    if (slider_changed) it->serialized_value = std::format("{}", value);
                                    track_item_edit(state, std::move(before), "Edit exposed C# field", slider_id, slider_changed);
                                    ImGui::SameLine();
                                    float precise = value;
                                    before = capture_snapshot(state);
                                    ImGui::SetNextItemWidth(96.0f);
                                    const ImGuiID precise_id = ImGui::GetID("##range_precise");
                                    const bool precise_changed = ImGui::InputFloat("##range_precise", &precise, 0.0f, 0.0f, "%.6g");
                                    if (precise_changed) { precise = std::clamp(precise, field_meta.range_min, field_meta.range_max); it->serialized_value = std::format("{}", precise); }
                                    track_item_edit(state, std::move(before), "Enter precise exposed C# field value", precise_id, precise_changed);
                                    changed = slider_changed || precise_changed;
                                } else {
                                    before = capture_snapshot(state);
                                    const ImGuiID value_id = ImGui::GetID("Value");
                                    changed = ImGui::DragFloat("Value", &value, 0.025f);
                                    if (changed) it->serialized_value = std::format("{}", value);
                                    track_item_edit(state, std::move(before), "Edit exposed C# field", value_id, changed);
                                }
                            } else if (field_meta.type == "enum") {
                                const char* preview_value = it->serialized_value.empty() ? "<select>" : it->serialized_value.c_str();
                                if (ImGui::BeginCombo("Value", preview_value)) {
                                    for (const auto& enum_value : field_meta.enum_values) {
                                        const bool selected = enum_value == it->serialized_value;
                                        if (ImGui::Selectable(enum_value.c_str(), selected) && !selected) {
                                            before = capture_snapshot(state);
                                            it->serialized_value = enum_value;
                                            record_immediate_edit(state, std::move(before), "Edit exposed C# enum field");
                                            changed = true;
                                        }
                                        if (selected) ImGui::SetItemDefaultFocus();
                                    }
                                    ImGui::EndCombo();
                                }
                            } else if (field_meta.type == "vec2") {
                                float value[2]{0.0f,0.0f}; std::istringstream parse(it->serialized_value); parse >> value[0] >> value[1];
                                before = capture_snapshot(state);
                                const ImGuiID value_id = ImGui::GetID("Value");
                                changed = ImGui::DragFloat2("Value", value, 0.025f);
                                if (changed) it->serialized_value = std::format("{} {}", value[0], value[1]);
                                track_item_edit(state, std::move(before), "Edit exposed C# field", value_id, changed);
                            } else if (field_meta.type == "vec3") {
                                float value[3]{0.0f,0.0f,0.0f}; std::istringstream parse(it->serialized_value); parse >> value[0] >> value[1] >> value[2];
                                before = capture_snapshot(state);
                                const ImGuiID value_id = ImGui::GetID("Value");
                                changed = ImGui::DragFloat3("Value", value, 0.025f);
                                if (changed) it->serialized_value = std::format("{} {} {}", value[0], value[1], value[2]);
                                track_item_edit(state, std::move(before), "Edit exposed C# field", value_id, changed);
                            } else if (field_meta.type == "color") {
                                float value[4]{1.0f,1.0f,1.0f,1.0f}; std::istringstream parse(it->serialized_value); parse >> value[0] >> value[1] >> value[2] >> value[3];
                                before = capture_snapshot(state);
                                const ImGuiID value_id = ImGui::GetID("Value");
                                changed = ImGui::ColorEdit4("Value", value);
                                if (changed) it->serialized_value = std::format("{} {} {} {}", value[0], value[1], value[2], value[3]);
                                track_item_edit(state, std::move(before), "Edit exposed C# field", value_id, changed);
                            } else {
                                before = capture_snapshot(state);
                                const ImGuiID value_id = ImGui::GetID("Value");
                                changed = ImGui::InputText("Value", &it->serialized_value);
                                track_item_edit(state, std::move(before), "Edit exposed C# field", value_id, changed);
                            }
                            ImGui::TextDisabled("%s%s", field_meta.type.c_str(), field_meta.has_range ? " / slider + precise entry" : "");
                        } else { ImGui::SameLine(); ImGui::TextDisabled("<code default>"); }
                        if (!field_meta.tooltip.empty()) {
                            ImGui::SameLine();
                            ImGui::TextDisabled("(?)");
                            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", field_meta.tooltip.c_str());
                        }
                        ImGui::PopID();
                    }
                    bool unresolved_header = false;
                    for (auto& stored : script.fields) {
                        const bool known = std::any_of(meta->fields.begin(), meta->fields.end(), [&](const auto& f){
                            return f.name == stored.field_name
                                || std::find(f.aliases.begin(), f.aliases.end(), stored.field_name) != f.aliases.end();
                        });
                        if (known) continue;
                        if (!unresolved_header) { ImGui::SeparatorText("Unresolved Overrides"); unresolved_header = true; }
                        ImGui::PushID(stored.field_name.c_str());
                        ImGui::TextDisabled("%s (%s)", stored.field_name.c_str(), stored.type_name.c_str());
                        before = capture_snapshot(state);
                        const ImGuiID raw_id = ImGui::GetID("Raw Value");
                        const bool changed_raw = ImGui::InputText("Raw Value", &stored.serialized_value);
                        track_item_edit(state, std::move(before), "Edit unresolved C# field", raw_id, changed_raw);
                        ImGui::PopID();
                    }
                } else {
                    ImGui::TextColored(ImVec4(1.0f,0.65f,0.25f,1.0f), "Type not found in current C# metadata; serialized data is preserved.");
                    for (auto& stored : script.fields) {
                        ImGui::PushID(stored.field_name.c_str());
                        ImGui::TextDisabled("%s (%s)", stored.field_name.c_str(), stored.type_name.c_str());
                        before = capture_snapshot(state);
                        const ImGuiID raw_id = ImGui::GetID("Raw Value");
                        const bool changed_raw = ImGui::InputText("Raw Value", &stored.serialized_value);
                        track_item_edit(state, std::move(before), "Edit unresolved C# field", raw_id, changed_raw);
                        ImGui::PopID();
                    }
                }
                if (!script.fields.empty()) {
                    if (ImGui::Button("Reset All Overrides")) {
                        before = capture_snapshot(state);
                        script.fields.clear();
                        record_immediate_edit(state, std::move(before), "Reset C# script overrides");
                    }
                    ImGui::SameLine();
                }
                ImGui::SeparatorText("Attachment");
                ImGui::TextDisabled("Execution order follows the attachment order on this Entity.");
                ImGui::BeginDisabled(script_index == 0);
                if (ImGui::Button("Move Up")) move_script_up = true;
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(script_index + 1 >= entity.managed_scripts.size());
                if (ImGui::Button("Move Down")) move_script_down = true;
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button("Duplicate")) duplicate_script = true;
                ImGui::SameLine();
                if (ImGui::Button("Remove C# Script")) remove_script = true;
                ImGui::TreePop();
            }
            ImGui::PopID();
            if (remove_script) {
                before = capture_snapshot(state);
                entity.remove_managed_script(script_index);
                record_immediate_edit(state, std::move(before), "Remove C# Script");
            } else if (duplicate_script) {
                before = capture_snapshot(state);
                entity.managed_scripts.insert(entity.managed_scripts.begin() + static_cast<std::ptrdiff_t>(script_index + 1), script);
                record_immediate_edit(state, std::move(before), "Duplicate C# Script");
                break;
            } else if (move_script_up && script_index > 0) {
                before = capture_snapshot(state);
                std::swap(entity.managed_scripts[script_index], entity.managed_scripts[script_index - 1]);
                record_immediate_edit(state, std::move(before), "Move C# Script up");
                break;
            } else if (move_script_down && script_index + 1 < entity.managed_scripts.size()) {
                before = capture_snapshot(state);
                std::swap(entity.managed_scripts[script_index], entity.managed_scripts[script_index + 1]);
                record_immediate_edit(state, std::move(before), "Move C# Script down");
                break;
            } else ++script_index;
        }

        ImGui::Separator();
        if (ImGui::Button("Duplicate", ImVec2(110.0f, 0.0f))) command_duplicate_selection(state);
        ImGui::SameLine();
        if (ImGui::Button("Delete", ImVec2(110.0f, 0.0f))) command_delete_selection(state);
        ImGui::End();
    }

}

const AssetThumbnail* texture_asset_thumbnail(EditorState& state, const vespera::AssetRecord& record) {
    if (record.kind != vespera::AssetKind::Texture || record.asset_id.empty()) return nullptr;
    auto& thumbnail = state.asset_thumbnails[record.asset_id];
    if (thumbnail.source_hash != record.source_hash) {
        thumbnail = {};
        thumbnail.source_hash = record.source_hash;
    }
    if (!thumbnail.attempted) {
        thumbnail.attempted = true;
        const auto imported = vespera::import_texture(record.absolute_path, record.display_name);
        if (imported && imported.texture.valid()) {
            constexpr std::uint32_t max_side = 24;
            const float aspect = static_cast<float>(imported.texture.width) / static_cast<float>(imported.texture.height);
            thumbnail.width = aspect >= 1.0f ? max_side : std::max(1u, static_cast<std::uint32_t>(max_side * aspect));
            thumbnail.height = aspect >= 1.0f ? std::max(1u, static_cast<std::uint32_t>(max_side / aspect)) : max_side;
            thumbnail.pixels.resize(static_cast<std::size_t>(thumbnail.width) * thumbnail.height);
            for (std::uint32_t y = 0; y < thumbnail.height; ++y) {
                const std::uint32_t sy = std::min(imported.texture.height - 1u,
                    static_cast<std::uint32_t>((static_cast<std::uint64_t>(y) * imported.texture.height) / thumbnail.height));
                for (std::uint32_t x = 0; x < thumbnail.width; ++x) {
                    const std::uint32_t sx = std::min(imported.texture.width - 1u,
                        static_cast<std::uint32_t>((static_cast<std::uint64_t>(x) * imported.texture.width) / thumbnail.width));
                    const std::size_t src = (static_cast<std::size_t>(sy) * imported.texture.width + sx) * 4u;
                    thumbnail.pixels[static_cast<std::size_t>(y) * thumbnail.width + x] = IM_COL32(
                        imported.texture.rgba8[src], imported.texture.rgba8[src+1],
                        imported.texture.rgba8[src+2], imported.texture.rgba8[src+3]);
                }
            }
            thumbnail.valid = true;
        }
    }
    return thumbnail.valid ? &thumbnail : nullptr;
}

void draw_cpu_thumbnail(ImDrawList* draw, const AssetThumbnail& thumbnail, ImVec2 min, ImVec2 max) {
    if (!thumbnail.valid || thumbnail.width == 0 || thumbnail.height == 0) return;
    const float width = max.x - min.x;
    const float height = max.y - min.y;
    const float cell = std::max(1.0f, std::min(width / static_cast<float>(thumbnail.width), height / static_cast<float>(thumbnail.height)));
    const float draw_w = cell * thumbnail.width;
    const float draw_h = cell * thumbnail.height;
    const ImVec2 origin{min.x + (width - draw_w) * 0.5f, min.y + (height - draw_h) * 0.5f};
    const ImU32 checker_a = ImGui::GetColorU32(ImVec4(0.16f,0.17f,0.20f,1.0f));
    const ImU32 checker_b = ImGui::GetColorU32(ImVec4(0.24f,0.25f,0.28f,1.0f));
    const float checker = std::max(4.0f, cell * 4.0f);
    for (float y = min.y; y < max.y; y += checker) for (float x = min.x; x < max.x; x += checker) {
        const int parity = (static_cast<int>((x-min.x)/checker) + static_cast<int>((y-min.y)/checker)) & 1;
        draw->AddRectFilled({x,y},{std::min(x+checker,max.x),std::min(y+checker,max.y)}, parity ? checker_a : checker_b);
    }
    for (std::uint32_t y = 0; y < thumbnail.height; ++y) for (std::uint32_t x = 0; x < thumbnail.width; ++x) {
        const ImVec2 a{origin.x + x * cell, origin.y + y * cell};
        draw->AddRectFilled(a, {a.x + cell + 0.5f, a.y + cell + 0.5f}, thumbnail.pixels[static_cast<std::size_t>(y)*thumbnail.width+x]);
    }
}

void draw_assets(EditorState& state) {
    ImGui::Begin("Project");

    // Lightweight source watching. Metadata v2 stores source mtimes, so
    // unchanged assets take the no-hash fast path and this remains cheap for
    // ordinary project sizes while dependency edges are rebuilt from the refreshed source catalog.
    if (state.asset_auto_refresh && !state.assets_root.empty()) {
        const double now = ImGui::GetTime();
        if (now >= state.next_asset_refresh_time) {
            state.next_asset_refresh_time = now + 2.5;
            refresh_asset_catalog(state, false);
        }
    }

    if (state.project_loaded) {
        ImGui::TextUnformatted(state.project.name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("| %s", state.scene_path.empty() ? "no scene" : state.scene_path.filename().string().c_str());
    } else {
        ImGui::TextDisabled("Legacy scene-only workspace");
    }

    if (ImGui::BeginTabBar("ProjectTabs", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem("Assets")) {
            // Unity-familiar Project rail: folders are primary; asset type filtering
            // lives in the browser toolbar rather than competing with the folder tree.
            ImGui::BeginChild("AssetSourceRail", ImVec2(176.0f, 0.0f), true);
            ImGui::TextColored(ImVec4(0.58f, 0.63f, 1.0f, 1.0f), "PROJECT");
            ImGui::TextDisabled("%zu assets", state.asset_catalog.records().size());
            ImGui::Spacing();
            if (ImGui::Selectable("Assets", state.asset_browser_folder.empty(), 0, ImVec2(0.0f, 26.0f))) state.asset_browser_folder.clear();
            std::vector<std::filesystem::path> folders;
            for (const auto& record : state.asset_catalog.records()) {
                auto folder_path = record.relative_path.parent_path().lexically_normal();
                while (!folder_path.empty() && folder_path != ".") {
                    if (std::find(folders.begin(), folders.end(), folder_path) == folders.end()) folders.push_back(folder_path);
                    folder_path = folder_path.parent_path();
                }
            }
            std::sort(folders.begin(), folders.end(), [](const auto& a, const auto& b) {
                return a.generic_string() < b.generic_string();
            });
            for (const auto& folder_path : folders) {
                int depth = 0;
                for (const auto& part : folder_path) { (void)part; ++depth; }
                ImGui::Indent(static_cast<float>((std::max)(0, depth - 1)) * 10.0f);
                const auto label = folder_path.filename().string() + "##folder_" + folder_path.generic_string();
                if (ImGui::Selectable(label.c_str(), state.asset_browser_folder == folder_path)) state.asset_browser_folder = folder_path;
                ImGui::Unindent(static_cast<float>((std::max)(0, depth - 1)) * 10.0f);
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::Checkbox("Auto refresh", &state.asset_auto_refresh);
            if (ImGui::Button("Reimport All", ImVec2(-1.0f, 0.0f))) refresh_asset_catalog(state, true, true);
            if (state.last_asset_report.orphaned_metadata != 0) {
                ImGui::TextColored(ImVec4(1.0f, 0.68f, 0.25f, 1.0f), "%zu orphaned meta", state.last_asset_report.orphaned_metadata);
            }
            ImGui::EndChild();

            ImGui::SameLine();
            ImGui::BeginChild("AssetBrowserMain", ImVec2(0.0f, 0.0f), false);
            if (ImGui::SmallButton("Assets##breadcrumb_root")) state.asset_browser_folder.clear();
            std::filesystem::path breadcrumb_path;
            for (const auto& part : state.asset_browser_folder) {
                if (part == ".") continue;
                breadcrumb_path /= part;
                ImGui::SameLine();
                ImGui::TextDisabled(">");
                ImGui::SameLine();
                const std::string breadcrumb_label = part.string() + "##breadcrumb_" + breadcrumb_path.generic_string();
                if (ImGui::SmallButton(breadcrumb_label.c_str())) state.asset_browser_folder = breadcrumb_path;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("  %zu deps | %zu stable | %zu broken | %zu stale",
                state.last_asset_report.dependency_edges, state.last_asset_report.stable_reference_edges,
                state.last_asset_report.broken_dependencies, state.last_asset_report.stale_fallback_paths);

            ImGui::SetNextItemWidth(std::min(320.0f, ImGui::GetContentRegionAvail().x * 0.38f));
            state.asset_filter.Draw("Search##assets");
            ImGui::SameLine();
            const char* kind_names[] = {"All", "Scenes", "Prefabs", "Sprite Clips", "Sprite Sheets", "Textures", "Audio", "Audio Clips", "Fonts", "Materials", "Legacy UI", "RmlUi", "RCSS"};
            ImGui::SetNextItemWidth(126.0f);
            ImGui::Combo("##asset_kind_filter", &state.asset_kind_filter, kind_names, IM_ARRAYSIZE(kind_names));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Asset type filter");
            ImGui::SameLine();
            if (ImGui::SmallButton(state.asset_grid_view ? "Grid" : "List")) state.asset_grid_view = !state.asset_grid_view;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle Unity-style asset grid/list view");
            ImGui::SameLine();
            if (state.asset_grid_view) {
                ImGui::SetNextItemWidth(84.0f);
                ImGui::SliderFloat("##asset_tile_size", &state.asset_tile_size, 72.0f, 132.0f, "%.0f px");
                ImGui::SameLine();
            }
            if (ImGui::Button("Refresh")) refresh_asset_catalog(state, true, false);
            ImGui::SameLine();
            ImGui::TextDisabled("%zu fast / %zu hashed",
                state.last_asset_report.fast_path_hits, state.last_asset_report.hashes_computed);

            std::optional<std::filesystem::path> asset_open_scene;
            std::optional<std::filesystem::path> asset_instantiate_prefab;
            std::optional<std::string> asset_open_ui;
            auto asset_matches_filters = [&](const vespera::AssetRecord& record) {
                int filter_kind = 0;
                switch (record.kind) {
                    case vespera::AssetKind::Scene: filter_kind = 1; break;
                    case vespera::AssetKind::EntityPrefab: filter_kind = 2; break;
                    case vespera::AssetKind::SpriteClip: filter_kind = 3; break;
                    case vespera::AssetKind::SpriteSheet: filter_kind = 4; break;
                    case vespera::AssetKind::Texture: filter_kind = 5; break;
                    case vespera::AssetKind::Audio: filter_kind = 6; break;
                    case vespera::AssetKind::AudioClip: filter_kind = 7; break;
                    case vespera::AssetKind::Font: filter_kind = 8; break;
                    case vespera::AssetKind::Material: filter_kind = 9; break;
                    case vespera::AssetKind::UiDocument: filter_kind = 10; break;
                    case vespera::AssetKind::RmlDocument: filter_kind = 11; break;
                    case vespera::AssetKind::RmlStyleSheet: filter_kind = 12; break;
                }
                if (state.asset_kind_filter != 0 && state.asset_kind_filter != filter_kind) return false;
                if (!state.asset_browser_folder.empty()) {
                    const auto folder = record.relative_path.parent_path().lexically_normal();
                    if (folder != state.asset_browser_folder.lexically_normal()) return false;
                }
                const std::string search_text = record.relative_path.generic_string() + " " + record.importer;
                return state.asset_filter.PassFilter(search_text.c_str());
            };

            if (state.asset_grid_view) {
                const float tile = std::clamp(state.asset_tile_size, 72.0f, 132.0f);
                const float spacing = 10.0f;
                const float cell_width = tile + spacing;
                const int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cell_width));
                if (ImGui::BeginTable("AssetGrid", columns, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings)) {
                    int column = 0;
                    for (const auto& record : state.asset_catalog.records()) {
                        if (!asset_matches_filters(record)) continue;
                        if (column == 0) ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(column);
                        ImGui::PushID(record.asset_id.c_str());
                        const bool selected = state.selection.kind == SelectionKind::Asset && state.selection.asset_id == record.asset_id;
                        const ImVec2 pos = ImGui::GetCursorScreenPos();
                        const ImVec2 card_size{tile, tile + 36.0f};
                        if (ImGui::Selectable("##asset_tile", selected, ImGuiSelectableFlags_AllowDoubleClick, card_size)) {
                            commit_active_edit(state);
                            state.selection = {SelectionKind::Asset};
                            state.selection.asset_id = record.asset_id;
                            state.selected_entity_ids.clear();
                            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                                if (record.kind == vespera::AssetKind::Scene) asset_open_scene = record.absolute_path;
                                else if (record.kind == vespera::AssetKind::EntityPrefab) asset_instantiate_prefab = record.absolute_path;
                                else if (record.kind == vespera::AssetKind::UiDocument) asset_open_ui = record.asset_id;
                            }
                        }
                        ImVec4 type_color(0.62f, 0.66f, 0.74f, 1.0f);
                        const char* type_short = "ASSET";
                        switch (record.kind) {
                            case vespera::AssetKind::Scene: type_short="SCENE"; type_color=ImVec4(0.48f,0.72f,1.0f,1.0f); break;
                            case vespera::AssetKind::EntityPrefab: type_short="PREFAB"; type_color=ImVec4(0.67f,0.57f,1.0f,1.0f); break;
                            case vespera::AssetKind::SpriteClip: type_short="CLIP"; type_color=ImVec4(0.78f,0.64f,1.0f,1.0f); break;
                            case vespera::AssetKind::SpriteSheet: type_short="SHEET"; type_color=ImVec4(0.55f,0.78f,1.0f,1.0f); break;
                            case vespera::AssetKind::Texture: type_short="TEX"; type_color=ImVec4(0.46f,0.86f,0.73f,1.0f); break;
                            case vespera::AssetKind::Audio: type_short="AUDIO"; type_color=ImVec4(1.0f,0.72f,0.43f,1.0f); break;
                            case vespera::AssetKind::AudioClip: type_short="A-CLIP"; type_color=ImVec4(1.0f,0.60f,0.35f,1.0f); break;
                            case vespera::AssetKind::Font: type_short="FONT"; type_color=ImVec4(0.94f,0.58f,0.77f,1.0f); break;
                            case vespera::AssetKind::Material: type_short="MAT"; type_color=ImVec4(0.78f,0.70f,0.42f,1.0f); break;
                            case vespera::AssetKind::UiDocument: type_short="SLUI"; type_color=ImVec4(0.55f,0.82f,0.95f,1.0f); break;
                            case vespera::AssetKind::RmlDocument: type_short="RML"; type_color=ImVec4(0.45f,0.78f,1.0f,1.0f); break;
                            case vespera::AssetKind::RmlStyleSheet: type_short="RCSS"; type_color=ImVec4(0.56f,0.72f,1.0f,1.0f); break;
                        }
                        auto* draw = ImGui::GetWindowDrawList();
                        const ImU32 bg = ImGui::GetColorU32(ImVec4(type_color.x*0.22f, type_color.y*0.22f, type_color.z*0.22f, 0.95f));
                        const ImU32 border = ImGui::GetColorU32(type_color);
                        const ImVec2 icon_min{pos.x + 8.0f, pos.y + 8.0f};
                        const ImVec2 icon_max{pos.x + tile - 8.0f, pos.y + tile - 22.0f};
                        draw->AddRectFilled(icon_min, icon_max, bg, 6.0f);
                        if (record.kind == vespera::AssetKind::Texture) {
                            if (const auto* thumbnail = texture_asset_thumbnail(state, record)) {
                                draw_cpu_thumbnail(draw, *thumbnail, {icon_min.x + 2.0f, icon_min.y + 2.0f}, {icon_max.x - 2.0f, icon_max.y - 2.0f});
                            } else {
                                const ImVec2 type_size = ImGui::CalcTextSize(type_short);
                                draw->AddText({(icon_min.x+icon_max.x-type_size.x)*0.5f, (icon_min.y+icon_max.y-type_size.y)*0.5f}, border, type_short);
                            }
                        } else {
                            const ImVec2 type_size = ImGui::CalcTextSize(type_short);
                            draw->AddText({(icon_min.x+icon_max.x-type_size.x)*0.5f, (icon_min.y+icon_max.y-type_size.y)*0.5f}, border, type_short);
                        }
                        draw->AddRect(icon_min, icon_max, border, 6.0f, 0, selected ? 2.0f : 1.0f);
                        const std::string display = record.display_name;
                        draw->AddText({pos.x + 6.0f, pos.y + tile - 14.0f}, ImGui::GetColorU32(ImGuiCol_Text), display.c_str());
                        const auto folder = record.relative_path.parent_path().filename().generic_string();
                        draw->AddText({pos.x + 6.0f, pos.y + tile + 4.0f}, ImGui::GetColorU32(ImGuiCol_TextDisabled), folder.c_str());
                        begin_project_asset_drag(record);
                        if (ImGui::BeginPopupContextItem("asset_grid_context")) {
                            if (record.kind == vespera::AssetKind::Scene && ImGui::MenuItem("Open Scene")) asset_open_scene = record.absolute_path;
                            if (record.kind == vespera::AssetKind::EntityPrefab && ImGui::MenuItem("Instantiate Prefab")) asset_instantiate_prefab = record.absolute_path;
                            if (record.kind == vespera::AssetKind::UiDocument && ImGui::MenuItem("Open UI Authoring")) asset_open_ui = record.asset_id;
                            ImGui::Separator();
                            if (ImGui::MenuItem("Copy Asset ID")) ImGui::SetClipboardText(record.asset_id.c_str());
                            if (ImGui::MenuItem("Copy Relative Path")) { const auto text = record.relative_path.generic_string(); ImGui::SetClipboardText(text.c_str()); }
                            ImGui::Separator();
                            if (ImGui::MenuItem("Move / Rename...")) request_asset_move(state, record);
                            if (ImGui::MenuItem("Check Delete Safety")) {
                                const auto preflight = vespera::preflight_delete_project_asset(state.asset_catalog, state.project, record.asset_id);
                                if (preflight) push_console(state, ConsoleEntry::Level::Info, "Delete preflight: safe to delete " + record.relative_path.generic_string() + " (no mutation performed)");
                                else {
                                    push_console(state, ConsoleEntry::Level::Warning, "Delete preflight blocked for " + record.relative_path.generic_string());
                                    for (const auto& blocker : preflight.blockers) push_console(state, ConsoleEntry::Level::Warning, "  - " + blocker);
                                }
                            }
                            ImGui::EndPopup();
                        }
                        ImGui::PopID();
                        column = (column + 1) % columns;
                    }
                    ImGui::EndTable();
                }
            } else {
                const float asset_table_height = std::max(110.0f, ImGui::GetContentRegionAvail().y - 38.0f);
                if (ImGui::BeginTable("AssetBrowser", 5,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH
                        | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY
                        | ImGuiTableFlags_SizingStretchProp,
                        ImVec2(0.0f, asset_table_height))) {
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 82.0f);
                    ImGui::TableSetupColumn("Asset", ImGuiTableColumnFlags_WidthStretch, 0.40f);
                    ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch, 0.32f);
                    ImGui::TableSetupColumn("Import", ImGuiTableColumnFlags_WidthFixed, 92.0f);
                    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 72.0f);
                    ImGui::TableHeadersRow();

                    for (const auto& record : state.asset_catalog.records()) {
                        int filter_kind = 0;
                        switch (record.kind) {
                            case vespera::AssetKind::Scene: filter_kind = 1; break;
                            case vespera::AssetKind::EntityPrefab: filter_kind = 2; break;
                            case vespera::AssetKind::SpriteClip: filter_kind = 3; break;
                            case vespera::AssetKind::SpriteSheet: filter_kind = 4; break;
                            case vespera::AssetKind::Texture: filter_kind = 5; break;
                            case vespera::AssetKind::Audio: filter_kind = 6; break;
                            case vespera::AssetKind::AudioClip: filter_kind = 7; break;
                            case vespera::AssetKind::Font: filter_kind = 8; break;
                            case vespera::AssetKind::Material: filter_kind = 9; break;
                            case vespera::AssetKind::UiDocument: filter_kind = 10; break;
                            case vespera::AssetKind::RmlDocument: filter_kind = 11; break;
                            case vespera::AssetKind::RmlStyleSheet: filter_kind = 12; break;
                        }
                        if (state.asset_kind_filter != 0 && state.asset_kind_filter != filter_kind) continue;
                        if (!state.asset_browser_folder.empty()) {
                            const auto folder = record.relative_path.parent_path().lexically_normal();
                            const auto selected_folder = state.asset_browser_folder.lexically_normal();
                            const auto folder_text = folder.generic_string();
                            const auto selected_text = selected_folder.generic_string();
                            if (folder_text != selected_text && !(folder_text.size() > selected_text.size()
                                && folder_text.compare(0, selected_text.size(), selected_text) == 0
                                && folder_text[selected_text.size()] == '/')) continue;
                        }
                        const std::string search_text = record.relative_path.generic_string() + " " + record.importer;
                        if (!state.asset_filter.PassFilter(search_text.c_str())) continue;

                        ImGui::PushID(record.asset_id.c_str());
                        ImGui::TableNextRow(ImGuiTableRowFlags_None, 29.0f);
                        ImGui::TableSetColumnIndex(0);
                        const char* type_short = "ASSET";
                        ImVec4 type_color(0.62f, 0.66f, 0.74f, 1.0f);
                        switch (record.kind) {
                            case vespera::AssetKind::Scene: type_short = "SCENE"; type_color = ImVec4(0.48f,0.72f,1.0f,1.0f); break;
                            case vespera::AssetKind::EntityPrefab: type_short = "PREFAB"; type_color = ImVec4(0.67f,0.57f,1.0f,1.0f); break;
                            case vespera::AssetKind::SpriteClip: type_short = "CLIP"; type_color = ImVec4(0.78f,0.64f,1.0f,1.0f); break;
                            case vespera::AssetKind::SpriteSheet: type_short = "SHEET"; type_color = ImVec4(0.55f,0.78f,1.0f,1.0f); break;
                            case vespera::AssetKind::Texture: type_short = "TEXTURE"; type_color = ImVec4(0.46f,0.86f,0.73f,1.0f); break;
                            case vespera::AssetKind::Audio: type_short = "AUDIO"; type_color = ImVec4(1.0f,0.72f,0.43f,1.0f); break;
                            case vespera::AssetKind::AudioClip: type_short = "A-CLIP"; type_color = ImVec4(1.0f,0.60f,0.35f,1.0f); break;
                            case vespera::AssetKind::Font: type_short = "FONT"; type_color = ImVec4(0.94f,0.58f,0.77f,1.0f); break;
                            case vespera::AssetKind::Material: type_short = "MATERIAL"; type_color = ImVec4(0.78f,0.70f,0.42f,1.0f); break;
                            case vespera::AssetKind::UiDocument: type_short = "SLUI"; type_color = ImVec4(0.55f,0.82f,0.95f,1.0f); break;
                            case vespera::AssetKind::RmlDocument: type_short = "RML"; type_color = ImVec4(0.45f,0.78f,1.0f,1.0f); break;
                            case vespera::AssetKind::RmlStyleSheet: type_short = "RCSS"; type_color = ImVec4(0.56f,0.72f,1.0f,1.0f); break;
                        }
                        ImGui::TextColored(type_color, "%s", type_short);

                        ImGui::TableSetColumnIndex(1);
                        const bool selected = state.selection.kind == SelectionKind::Asset && state.selection.asset_id == record.asset_id;
                        const bool active_scene = record.kind == vespera::AssetKind::Scene
                            && !state.scene_path.empty()
                            && record.absolute_path.lexically_normal() == state.scene_path.lexically_normal();
                        std::string row_name = record.display_name;
                        if (active_scene) row_name += state.dirty ? "  *" : "  (open)";
                        const bool clicked = ImGui::Selectable(row_name.c_str(), selected,
                            ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick,
                            ImVec2(0.0f, 24.0f));
                        if (clicked) {
                            commit_active_edit(state);
                            state.selection = {SelectionKind::Asset};
                            state.selection.asset_id = record.asset_id;
                            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                                if (record.kind == vespera::AssetKind::Scene) asset_open_scene = record.absolute_path;
                                else if (record.kind == vespera::AssetKind::EntityPrefab) asset_instantiate_prefab = record.absolute_path;
                                else if (record.kind == vespera::AssetKind::UiDocument) asset_open_ui = record.asset_id;
                            }
                        }
                        begin_project_asset_drag(record);
                        if (ImGui::BeginPopupContextItem("asset_context")) {
                            if (record.kind == vespera::AssetKind::Scene && ImGui::MenuItem("Open Scene")) asset_open_scene = record.absolute_path;
                            if (record.kind == vespera::AssetKind::EntityPrefab && ImGui::MenuItem("Instantiate Prefab")) asset_instantiate_prefab = record.absolute_path;
                            if (record.kind == vespera::AssetKind::UiDocument && ImGui::MenuItem("Open UI Authoring")) asset_open_ui = record.asset_id;
                            ImGui::Separator();
                            if (ImGui::MenuItem("Copy Asset ID")) ImGui::SetClipboardText(record.asset_id.c_str());
                            if (ImGui::MenuItem("Copy Relative Path")) {
                                const auto text = record.relative_path.generic_string();
                                ImGui::SetClipboardText(text.c_str());
                            }
                            ImGui::Separator();
                            if (ImGui::MenuItem("Move / Rename...")) request_asset_move(state, record);
                            if (ImGui::MenuItem("Check Delete Safety")) {
                                const auto preflight = vespera::preflight_delete_project_asset(state.asset_catalog, state.project, record.asset_id);
                                if (preflight) {
                                    push_console(state, ConsoleEntry::Level::Info, "Delete preflight: safe to delete " + record.relative_path.generic_string() + " (no mutation performed)");
                                } else {
                                    push_console(state, ConsoleEntry::Level::Warning, "Delete preflight blocked for " + record.relative_path.generic_string());
                                    for (const auto& blocker : preflight.blockers) push_console(state, ConsoleEntry::Level::Warning, "  - " + blocker);
                                }
                            }
                            ImGui::EndPopup();
                        }

                        ImGui::TableSetColumnIndex(2);
                        const auto folder = record.relative_path.parent_path().generic_string();
                        ImGui::TextDisabled("%s", folder.empty() ? "." : folder.c_str());
                        ImGui::TableSetColumnIndex(3);
                        const bool import_ok = record.import_state == vespera::AssetImportState::Ready;
                        ImGui::TextColored(import_ok ? ImVec4(0.48f,0.82f,0.62f,1.0f) : ImVec4(1.0f,0.68f,0.25f,1.0f),
                            "%s", vespera::asset_import_state_name(record.import_state).data());
                        ImGui::TableSetColumnIndex(4);
                        if (record.source_size >= 1024u * 1024u) ImGui::Text("%.1f MB", static_cast<double>(record.source_size) / (1024.0 * 1024.0));
                        else if (record.source_size >= 1024u) ImGui::Text("%.1f KB", static_cast<double>(record.source_size) / 1024.0);
                        else ImGui::Text("%llu B", static_cast<unsigned long long>(record.source_size));
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }

            }

            if (asset_open_scene) {
                if (state.dirty) {
                    state.pending_action = PendingAction::OpenScene;
                    state.pending_open_path = *asset_open_scene;
                    state.request_unsaved_popup = true;
                } else {
                    open_scene(state, *asset_open_scene);
                }
            } else if (asset_instantiate_prefab) {
                command_instantiate_prefab(state, *asset_instantiate_prefab);
            }
            if (asset_open_ui) {
                if (const auto* record = state.asset_catalog.find_by_id(*asset_open_ui)) open_ui_authoring(state, *record);
            }

            const bool entity_selected_for_prefab = selected_entity_index(state).has_value();
            ImGui::BeginDisabled(!entity_selected_for_prefab);
            if (ImGui::Button("Create Prefab from Selection")) command_create_prefab_from_selected(state);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("Double-click scenes to open, prefabs to instantiate, and UI documents to author.");
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Scene Resources")) {
            ImGui::TextDisabled("Resources serialized inside the currently open scene.");
            if (ImGui::Button("+ Material")) command_create_material(state);
            ImGui::SameLine();
            if (ImGui::Button("+ Sprite Clip")) command_create_clip(state);
            ImGui::Separator();

            std::optional<std::size_t> duplicate_material;
            std::optional<std::size_t> delete_material;
            if (ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_DefaultOpen)) {
                const auto& materials = state.scene.world.materials();
                if (materials.empty()) ImGui::TextDisabled("No materials in this scene.");
                for (std::size_t i = 0; i < materials.size(); ++i) {
                    ImGui::PushID(static_cast<int>(i) + 200000);
                    const bool selected = state.selection.kind == SelectionKind::Material && state.selection.index == i;
                    if (ImGui::Selectable(materials[i].name.c_str(), selected)) {
                        commit_active_edit(state);
                        state.selection = {SelectionKind::Material, i};
                    }
                    if (ImGui::BeginPopupContextItem("material_context")) {
                        if (ImGui::MenuItem("Duplicate", "Ctrl+D")) duplicate_material = i;
                        if (ImGui::MenuItem("Delete", "Del")) delete_material = i;
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }
            }
            if (duplicate_material && *duplicate_material < state.scene.world.materials().size()) {
                state.selection = {SelectionKind::Material, *duplicate_material}; command_duplicate_selection(state);
            } else if (delete_material && *delete_material < state.scene.world.materials().size()) {
                state.selection = {SelectionKind::Material, *delete_material}; command_delete_selection(state);
            }

            std::optional<std::size_t> duplicate_clip;
            std::optional<std::size_t> delete_clip;
            if (ImGui::CollapsingHeader("Sprite Clips", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (state.scene.sprite_clips.empty()) ImGui::TextDisabled("No sprite clips in this scene.");
                for (std::size_t i = 0; i < state.scene.sprite_clips.size(); ++i) {
                    const auto& clip = state.scene.sprite_clips[i];
                    ImGui::PushID(static_cast<int>(i) + 300000);
                    const bool selected = state.selection.kind == SelectionKind::SpriteClip && state.selection.index == i;
                    if (ImGui::Selectable(clip.name.c_str(), selected)) {
                        commit_active_edit(state);
                        state.selection = {SelectionKind::SpriteClip, i};
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%u directions x %u frames | %.2f fps | %s",
                        clip.direction_count, clip.frame_count, clip.frames_per_second, clip.loop ? "loop" : "once");
                    if (ImGui::BeginPopupContextItem("clip_context")) {
                        if (ImGui::MenuItem("Duplicate", "Ctrl+D")) duplicate_clip = i;
                        if (ImGui::MenuItem("Delete", "Del")) delete_clip = i;
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }
            }
            if (duplicate_clip && *duplicate_clip < state.scene.sprite_clips.size()) {
                state.selection = {SelectionKind::SpriteClip, *duplicate_clip}; command_duplicate_selection(state);
            } else if (delete_clip && *delete_clip < state.scene.sprite_clips.size()) {
                state.selection = {SelectionKind::SpriteClip, *delete_clip}; command_delete_selection(state);
            }

            if (ImGui::CollapsingHeader("Registered Textures")) {
                for (const auto& texture : state.scene.world.textures()) ImGui::BulletText("%s", texture.name.c_str());
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("C# Scripts")) {
            if (ImGui::Button("Build C#")) build_managed_scripts(state);
            ImGui::SameLine();
            if (ImGui::Button("Refresh Metadata")) load_managed_metadata(state, true);
            ImGui::Separator();
            if (!state.managed_metadata.loaded) {
                ImGui::TextDisabled("No managed metadata loaded. Build C# or run build.ps1.");
            } else {
                ImGui::TextDisabled("Assembly: %s", state.managed_metadata.assembly.c_str());
                if (ImGui::BeginTable("ManagedScripts", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
                    ImGui::TableSetupColumn("Component");
                    ImGui::TableSetupColumn("Exposed", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                    ImGui::TableHeadersRow();
                    for (const auto& meta : state.managed_metadata.scripts) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(meta.class_name.c_str());
                        ImGui::TableSetColumnIndex(1); ImGui::Text("%zu", meta.fields.size());
                    }
                    ImGui::EndTable();
                }
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Project Settings")) {
            if (!state.project_loaded) {
                ImGui::TextDisabled("Open a .vesperaproject workspace to edit project settings.");
            } else {
                ImGui::SeparatorText("General");
                ImGui::InputText("Project Name", &state.project.name);
                ImGui::TextDisabled("Project file: %s", state.project_path.string().c_str());
                ImGui::TextDisabled("Assets root: %s", state.project.assets_root().string().c_str());

                ImGui::SeparatorText("Startup & Runtime");
                std::string startup_scene = state.project.startup_scene.generic_string();
                if (ImGui::InputText("Startup Scene", &startup_scene)) {
                    state.project.startup_scene = startup_scene;
                    state.project.startup_scene_asset_id.clear();
                }
                if (!state.project.startup_scene_asset_id.empty()) {
                    ImGui::TextDisabled("Stable asset ID: %s", state.project.startup_scene_asset_id.c_str());
                }
                if (ImGui::Button("Use Current Scene") && !state.scene_path.empty()) {
                    std::error_code ec;
                    const auto relative = std::filesystem::relative(state.scene_path, state.project.assets_root(), ec);
                    if (!ec && !relative.empty()) {
                        state.project.startup_scene = relative.lexically_normal();
                        if (const auto* startup_record = state.asset_catalog.find(state.project.startup_scene.generic_string())) {
                            state.project.startup_scene_asset_id = startup_record->asset_id;
                        } else {
                            state.project.startup_scene_asset_id.clear();
                        }
                    }
                }

                std::string startup_ui = state.project.startup_ui.generic_string();
                if (ImGui::InputText("Startup RML UI", &startup_ui)) {
                    state.project.startup_ui = startup_ui;
                    state.project.startup_ui_asset_id.clear();
                }
                if (!state.project.startup_ui_asset_id.empty()) {
                    ImGui::TextDisabled("Stable asset ID: %s", state.project.startup_ui_asset_id.c_str());
                }
                if (ImGui::Button("Use Selected RML")) {
                    const auto* selected_record = state.selection.kind == SelectionKind::Asset
                        ? state.asset_catalog.find_by_id(state.selection.asset_id) : nullptr;
                    if (!selected_record) {
                        push_console(state, ConsoleEntry::Level::Warning,
                            "Startup UI: select an RML document in Project/Assets first.");
                    } else if (selected_record->kind != vespera::AssetKind::RmlDocument) {
                        push_console(state, ConsoleEntry::Level::Warning,
                            "Startup UI: selected asset is not an RML document: " + selected_record->relative_path.generic_string());
                    } else {
                        state.project.startup_ui = selected_record->relative_path.lexically_normal();
                        state.project.startup_ui_asset_id = selected_record->asset_id;
                        push_console(state, ConsoleEntry::Level::Info,
                            "Startup UI set to " + selected_record->relative_path.generic_string());
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear Startup UI")) {
                    state.project.startup_ui.clear();
                    state.project.startup_ui_asset_id.clear();
                }
                ImGui::TextDisabled("Project v8+: exported/shared-player startup RML. Leave empty for projects with no startup UI.");

                std::string lua_entry = state.project.lua_entry.generic_string();
                if (ImGui::InputText("Lua Entry Script", &lua_entry)) {
                    state.project.lua_entry = lua_entry;
                    state.project.lua_entry_asset_id.clear();
                }
                if (!state.project.lua_entry_asset_id.empty()) {
                    ImGui::TextDisabled("Lua stable asset ID: %s", state.project.lua_entry_asset_id.c_str());
                }
                if (ImGui::Button("Use Selected Lua")) {
                    const auto* selected_record = state.selection.kind == SelectionKind::Asset
                        ? state.asset_catalog.find_by_id(state.selection.asset_id) : nullptr;
                    if (!selected_record) {
                        push_console(state, ConsoleEntry::Level::Warning,
                            "Lua entry: select a .lua asset in Project/Assets first.");
                    } else if (selected_record->kind != vespera::AssetKind::LuaScript) {
                        push_console(state, ConsoleEntry::Level::Warning,
                            "Lua entry: selected asset is not a Lua script: " + selected_record->relative_path.generic_string());
                    } else {
                        state.project.lua_entry = selected_record->relative_path.lexically_normal();
                        state.project.lua_entry_asset_id = selected_record->asset_id;
                        push_console(state, ConsoleEntry::Level::Info,
                            "Lua entry set to " + selected_record->relative_path.generic_string());
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear Lua Entry")) {
                    state.project.lua_entry.clear();
                    state.project.lua_entry_asset_id.clear();
                }
                ImGui::TextDisabled("Project v9: optional lightweight runtime/mod script. C# and Lua may coexist.");

                ImGui::InputText("Window Title", &state.project.window_title);
                ImGui::InputInt("Window Width", &state.project.window_width, 16, 128);
                ImGui::InputInt("Window Height", &state.project.window_height, 16, 128);
                ImGui::Checkbox("Resizable", &state.project.window_resizable);
                ImGui::SameLine(); ImGui::Checkbox("Relative Mouse", &state.project.relative_mouse);
                ImGui::SameLine(); ImGui::Checkbox("Escape Quits", &state.project.escape_quits);
                ImGui::SameLine(); ImGui::Checkbox("VSync", &state.project.vsync);

                ImGui::SeparatorText("Input Actions");
                ImGui::TextDisabled("Project input bindings drive both standalone runtime and in-editor Play Mode.");
                std::optional<std::size_t> remove_input_binding;
                if (state.project.input_bindings.empty()) {
                    ImGui::TextDisabled("No input bindings authored. C# Input actions will remain unbound.");
                } else if (ImGui::BeginTable("ProjectInputBindings", 6,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 1.5f);
                    ImGui::TableSetupColumn("Device", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                    ImGui::TableSetupColumn("Code", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                    ImGui::TableSetupColumn("Scale", ImGuiTableColumnFlags_WidthFixed, 75.0f);
                    ImGui::TableSetupColumn("Deadzone", ImGuiTableColumnFlags_WidthFixed, 85.0f);
                    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 26.0f);
                    ImGui::TableHeadersRow();
                    for (std::size_t i = 0; i < state.project.input_bindings.size(); ++i) {
                        auto& binding = state.project.input_bindings[i];
                        ImGui::PushID(static_cast<int>(i) + 920000);
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0); ImGui::SetNextItemWidth(-1); ImGui::InputText("##action", &binding.action);
                        ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1); ImGui::InputText("##device", &binding.device);
                        ImGui::TableSetColumnIndex(2); ImGui::SetNextItemWidth(-1); ImGui::InputText("##code", &binding.code);
                        ImGui::TableSetColumnIndex(3); ImGui::SetNextItemWidth(-1); ImGui::DragFloat("##scale", &binding.scale, 0.05f, -100.0f, 100.0f, "%.2f");
                        ImGui::TableSetColumnIndex(4); ImGui::SetNextItemWidth(-1); ImGui::DragFloat("##deadzone", &binding.deadzone, 0.01f, 0.0f, 0.95f, "%.2f");
                        ImGui::TableSetColumnIndex(5);
                        if (ImGui::SmallButton("x")) remove_input_binding = i;
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                if (remove_input_binding && *remove_input_binding < state.project.input_bindings.size()) {
                    state.project.input_bindings.erase(
                        state.project.input_bindings.begin() + static_cast<std::ptrdiff_t>(*remove_input_binding));
                }
                if (ImGui::Button("+ Key Binding")) {
                    state.project.input_bindings.push_back({"new_action", "key", "W", 1.0f, 0.0f});
                }
                ImGui::SameLine();
                if (ImGui::Button("+ Gamepad Button")) {
                    state.project.input_bindings.push_back({"new_action", "gamepad_button", "South", 1.0f, 0.0f});
                }
                ImGui::SameLine();
                if (ImGui::Button("+ Gamepad Axis")) {
                    state.project.input_bindings.push_back({"new_action", "gamepad_axis", "LeftX", 1.0f, 0.18f});
                }

                ImGui::SeparatorText("Build & Package");
                ImGui::InputText("Company", &state.project.company_name);
                ImGui::InputText("Product Version", &state.project.product_version);
                ImGui::InputText("Package Name", &state.project.package_name);
                int deployment_index = state.project.managed_deployment == vespera::ManagedDeploymentMode::Portable ? 1 : 0;
                const char* deployment_items[] = {"Framework-dependent", "Portable (.NET bundled)"};
                if (ImGui::Combo("Managed Deployment", &deployment_index, deployment_items, IM_ARRAYSIZE(deployment_items))) {
                    state.project.managed_deployment = deployment_index == 1
                        ? vespera::ManagedDeploymentMode::Portable
                        : vespera::ManagedDeploymentMode::FrameworkDependent;
                }
                ImGui::TextDisabled("Portable export copies a private compatible .NET runtime beside the game executable.");
                ImGui::InputText("Executable Name", &state.project.executable_name);
                std::string build_output = state.project.build_output_directory.generic_string();
                if (ImGui::InputText("Build Output Directory", &build_output)) state.project.build_output_directory = build_output;
                std::string game_icon = state.project.game_icon.generic_string();
                if (ImGui::InputText("Game Icon Asset", &game_icon)) {
                    state.project.game_icon = game_icon;
                    state.project.game_icon_asset_id.clear();
                }
                if (!state.project.game_icon_asset_id.empty()) {
                    ImGui::TextDisabled("Game icon stable ID: %s", state.project.game_icon_asset_id.c_str());
                }
                ImGui::Checkbox("Development Diagnostics / Symbols", &state.project.development_diagnostics);
                ImGui::TextDisabled("Debug = Debug native + symbols | Development = RelWithDebInfo | Release = optimized Release.");
                ImGui::TextDisabled("Game icon is packaged as stable project metadata; executable resource stamping remains later 0.9.x work.");
                ImGui::TextDisabled("Resolved output root: %s", state.project.build_output_root().string().c_str());
                ImGui::BeginDisabled(state.build_job.running || editor_is_playing(state));
                if (ImGui::Button("Build Game...", ImVec2(130.0f, 0.0f))) {
                    state.build_job.window_open = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Build & Run", ImVec2(120.0f, 0.0f))) {
                    state.build_job.window_open = true;
                    (void)start_editor_build_job(state, "Development", true);
                }
                ImGui::EndDisabled();
                ImGui::TextDisabled("Builds run inside Vespera; PowerShell remains an internal/automation implementation detail.");

                ImGui::SeparatorText("Managed C#");
                std::string managed_project = state.project.managed_project.generic_string();
                if (ImGui::InputText("Managed Project", &managed_project)) state.project.managed_project = managed_project;
                ImGui::InputText("Managed Assembly", &state.project.managed_assembly);
                ImGui::InputText("Game Target", &state.project.game_target);

                ImGui::SeparatorText("Standalone Build Assets");
                ImGui::TextDisabled("Startup scene dependencies are automatic. Explicit includes cover dynamically loaded assets.");
                std::optional<std::size_t> remove_build_include;
                if (state.project.build_includes.empty()) ImGui::TextDisabled("No explicit build includes.");
                for (std::size_t i = 0; i < state.project.build_includes.size(); ++i) {
                    ImGui::PushID(static_cast<int>(i) + 810000);
                    const bool stable = i < state.project.build_include_asset_ids.size() && !state.project.build_include_asset_ids[i].empty();
                    ImGui::BulletText("%s%s", state.project.build_includes[i].generic_string().c_str(), stable ? "  [stable ID]" : "");
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Remove")) remove_build_include = i;
                    ImGui::PopID();
                }
                if (remove_build_include && *remove_build_include < state.project.build_includes.size()) {
                    const auto index = *remove_build_include;
                    state.project.build_includes.erase(state.project.build_includes.begin() + static_cast<std::ptrdiff_t>(index));
                    if (index < state.project.build_include_asset_ids.size()) {
                        state.project.build_include_asset_ids.erase(state.project.build_include_asset_ids.begin() + static_cast<std::ptrdiff_t>(index));
                    }
                }
                const auto manifest = vespera::build_project_asset_manifest(state.project, state.asset_catalog);
                const ImVec4 manifest_color = manifest.valid()
                    ? ImVec4(0.48f, 0.82f, 0.62f, 1.0f) : ImVec4(1.0f, 0.55f, 0.42f, 1.0f);
                ImGui::TextColored(manifest_color, "Build manifest: %zu asset(s) | %zu missing root(s) | %zu broken reference(s) | %zu stale root path(s)",
                    manifest.assets.size(), manifest.missing_roots.size(), manifest.broken_dependencies.size(), manifest.stale_root_paths.size());
                if (state.last_asset_report.stale_fallback_paths != 0 || !manifest.stale_root_paths.empty()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.68f, 0.25f, 1.0f),
                        "Readable fallback paths are stale, but stable IDs still resolve.");
                }
                if (ImGui::Button("Repair Stable Fallback Paths", ImVec2(220.0f, 0.0f))) {
                    repair_asset_fallback_paths(state, true);
                }

                ImGui::SeparatorText("Validation");
                const auto issues = vespera::validate_vespera_project(state.project);
                std::size_t errors = 0, warnings = 0;
                for (const auto& issue : issues) {
                    if (issue.severity == vespera::ProjectValidationSeverity::Error) ++errors; else ++warnings;
                }
                if (errors == 0 && warnings == 0) ImGui::TextDisabled("Project validation: clean");
                else ImGui::Text("Project validation: %zu error(s), %zu warning(s)", errors, warnings);
                for (const auto& issue : issues) {
                    const ImVec4 color = issue.severity == vespera::ProjectValidationSeverity::Error
                        ? ImVec4(0.95f, 0.38f, 0.34f, 1.0f) : ImVec4(0.95f, 0.72f, 0.28f, 1.0f);
                    ImGui::TextColored(color, "%s", issue.message.c_str());
                }

                if (ImGui::Button("Save Project", ImVec2(130.0f, 0.0f))) {
                    const auto result = vespera::save_vespera_project(state.project, state.project_path);
                    push_console(state, result ? ConsoleEntry::Level::Info : ConsoleEntry::Level::Error, result.message);
                }
                ImGui::SameLine();
                if (ImGui::Button("Validate to Console", ImVec2(150.0f, 0.0f))) {
                    if (issues.empty()) push_console(state, ConsoleEntry::Level::Info, "Project validation passed.");
                    for (const auto& issue : issues) push_console(state,
                        issue.severity == vespera::ProjectValidationSeverity::Error
                            ? ConsoleEntry::Level::Error : ConsoleEntry::Level::Warning,
                        "Project: " + issue.message);
                }
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

std::string console_export_text(const EditorState& state, bool visible_only) {
    std::string out;
    for (const auto& entry : state.console) {
        if (visible_only) {
            if (entry.level == ConsoleEntry::Level::Info && !state.console_show_info) continue;
            if (entry.level == ConsoleEntry::Level::Warning && !state.console_show_warnings) continue;
            if (entry.level == ConsoleEntry::Level::Error && !state.console_show_errors) continue;
        }
        const char* prefix = entry.level == ConsoleEntry::Level::Error
            ? "[Error]" : (entry.level == ConsoleEntry::Level::Warning ? "[Warn]" : "[Info]");
        if (!out.empty()) out.push_back('\n');
        out += prefix;
        out.push_back(' ');
        out += entry.text;
    }
    return out;
}

void draw_console(EditorState& state) {
    ImGui::Begin("Console");
    std::size_t info_count = 0, warn_count = 0, error_count = 0;
    for (const auto& entry : state.console) {
        if (entry.level == ConsoleEntry::Level::Error) ++error_count;
        else if (entry.level == ConsoleEntry::Level::Warning) ++warn_count;
        else ++info_count;
    }
    ImGui::TextColored(ImVec4(0.58f, 0.63f, 1.0f, 1.0f), "CONSOLE");
    ImGui::SameLine();
    ImGui::Checkbox("Info", &state.console_show_info);
    ImGui::SameLine(); ImGui::TextDisabled("%zu", info_count);
    ImGui::SameLine();
    ImGui::Checkbox("Warnings", &state.console_show_warnings);
    ImGui::SameLine(); ImGui::TextColored(ImVec4(0.95f,0.72f,0.28f,1.0f), "%zu", warn_count);
    ImGui::SameLine();
    ImGui::Checkbox("Errors", &state.console_show_errors);
    ImGui::SameLine(); ImGui::TextColored(ImVec4(0.95f,0.38f,0.34f,1.0f), "%zu", error_count);
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy Visible")) {
        const auto text = console_export_text(state, true);
        ImGui::SetClipboardText(text.c_str());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy All")) {
        const auto text = console_export_text(state, false);
        ImGui::SetClipboardText(text.c_str());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) state.console.clear();
    ImGui::Separator();

    // Render the filtered log as one read-only text surface so normal desktop
    // selection works: click-drag arbitrary text and press Ctrl+C.  The toolbar
    // copy actions remain useful for bulk export, but they are no longer the
    // only way to copy Console content.
    std::string selectable_console_text = console_export_text(state, true);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 6.0f));
    ImGui::InputTextMultiline(
        "##ConsoleSelectableText",
        &selectable_console_text,
        ImVec2(-FLT_MIN, -FLT_MIN),
        ImGuiInputTextFlags_ReadOnly
    );
    ImGui::PopStyleVar();
    ImGui::End();
}

void setup_default_docking(ImGuiID dockspace_id, ImVec2 size, bool force_reset = false) {
    if (!force_reset && ImGui::DockBuilderGetNode(dockspace_id) != nullptr) {
        return;
    }

    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, size);

    ImGuiID center = dockspace_id;
    ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18f, nullptr, &center);
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24f, nullptr, &center);
    ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.28f, nullptr, &center);

    ImGui::DockBuilderDockWindow("Hierarchy", left);
    ImGui::DockBuilderDockWindow("Project", bottom);
    ImGui::DockBuilderDockWindow("Console", bottom);
    ImGui::DockBuilderDockWindow("Inspector", right);
    ImGui::DockBuilderDockWindow("Scene", center);
    ImGui::DockBuilderDockWindow("Game", center);
    ImGui::DockBuilderDockWindow("Sector", center);
    ImGui::DockBuilderFinish(dockspace_id);
}

void draw_workspace_toolbar(EditorState& state) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.032f, 0.036f, 0.047f, 1.0f));
    ImGui::BeginChild("VesperaWorkspaceToolbar", ImVec2(0.0f, 46.0f), false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::SetCursorPosY(9.0f);
    ImGui::TextColored(ImVec4(0.58f, 0.63f, 1.0f, 1.0f), "VESPERA");
    ImGui::SameLine(0.0f, 14.0f);
    if (state.project_loaded) {
        ImGui::TextUnformatted(state.project.name.c_str());
    } else {
        ImGui::TextDisabled("No Project");
    }
    ImGui::SameLine(0.0f, 12.0f);
    if (!state.scene_path.empty()) {
        ImGui::TextDisabled("/  %s%s", state.scene_path.filename().string().c_str(), state.dirty ? "  *" : "");
    }

    const float right_width = 780.0f;
    if (ImGui::GetContentRegionAvail().x > right_width) {
        ImGui::SameLine(ImGui::GetWindowWidth() - right_width);
    } else {
        ImGui::SameLine();
    }
    if (state.play_state == EditorPlayState::Editing) {
        if (ImGui::Button("Play", ImVec2(62.0f, 27.0f))) start_play_mode(state);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.28f, 0.72f, 1.0f));
        if (ImGui::Button("Stop", ImVec2(62.0f, 27.0f))) stop_play_mode(state);
        ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!editor_is_playing(state));
    if (ImGui::Button(state.play_state == EditorPlayState::Paused ? "Resume" : "Pause", ImVec2(68.0f, 27.0f))) toggle_play_pause(state);
    ImGui::SameLine();
    if (ImGui::Button("Step", ImVec2(54.0f, 27.0f))) step_play_mode(state);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(state.scene_path.empty() || editor_is_playing(state));
    if (ImGui::Button("Save", ImVec2(70.0f, 27.0f))) save_scene(state, state.scene_path);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Build C#", ImVec2(86.0f, 27.0f))) build_managed_scripts(state);
    ImGui::SameLine();
    ImGui::BeginDisabled(!state.project_loaded || editor_is_playing(state));
    if (ImGui::Button("Build Game", ImVec2(92.0f, 27.0f))) state.build_job.window_open = true;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Validate", ImVec2(82.0f, 27.0f))) validate_scene_to_console(state);
    ImGui::SameLine();
    if (ImGui::Button("Frame", ImVec2(70.0f, 27.0f))) {
        state.scene_view.frame_all_pending = true;
        state.scene_view_3d.frame_selection_pending = true;
    }
    ImGui::SameLine();
    if (editor_is_playing(state)) {
        ImGui::TextColored(ImVec4(0.69f, 0.58f, 1.0f, 1.0f), "%s %.2fs",
            state.play_state == EditorPlayState::Paused ? "PAUSED" : "PLAY", state.play_time_seconds);
    } else {
        ImGui::TextDisabled("%s", state.dirty ? "UNSAVED" : "READY");
    }
    if (!state.command_log.empty() && ImGui::IsItemHovered()) {
        const auto& last = state.command_log.back();
        ImGui::SetTooltip("Last command #%llu: %.*s",
            static_cast<unsigned long long>(last.sequence),
            static_cast<int>(vespera::editor::command_name(last.kind).size()),
            vespera::editor::command_name(last.kind).data());
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void draw_main_dockspace(EditorState& state) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking
        | ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoBringToFrontOnFocus
        | ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("VesperaDockHost", nullptr, flags);
    ImGui::PopStyleVar(3);

    draw_workspace_toolbar(state);

    const ImGuiID dockspace_id = ImGui::GetID("VesperaDockSpace");
    setup_default_docking(dockspace_id, ImGui::GetContentRegionAvail(), state.request_reset_layout);
    state.request_reset_layout = false;
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();
}

void request_open_scene(EditorState& state, std::filesystem::path path) {
    commit_active_edit(state);
    if (state.dirty) {
        state.pending_action = PendingAction::OpenScene;
        state.pending_open_path = std::move(path);
        state.request_unsaved_popup = true;
        return;
    }
    open_scene(state, path);
}

void request_exit(EditorState& state) {
    commit_active_edit(state);
    if (state.dirty) {
        state.pending_action = PendingAction::Exit;
        state.request_unsaved_popup = true;
    } else {
        state.pending_action = PendingAction::Exit;
    }
}

void execute_pending_action(EditorState& state, bool& running) {
    const PendingAction action = state.pending_action;
    const auto open_path = state.pending_open_path;
    state.pending_action = PendingAction::None;
    state.pending_open_path.clear();
    if (action == PendingAction::OpenScene) {
        open_scene(state, open_path);
    } else if (action == PendingAction::OpenProject) {
        open_project(state, open_path);
    } else if (action == PendingAction::Exit) {
        running = false;
    }
}

void draw_path_popups(EditorState& state, bool& running) {
    if (state.request_open_popup) {
        ImGui::OpenPopup("Open Scene");
        state.request_open_popup = false;
    }
    if (state.request_open_project_popup) {
        ImGui::OpenPopup("Open Project");
        state.request_open_project_popup = false;
    }
    if (state.request_save_as_popup) {
        ImGui::OpenPopup("Save Scene As");
        state.request_save_as_popup = false;
    }
    if (state.request_unsaved_popup) {
        ImGui::OpenPopup("Unsaved Changes");
        state.request_unsaved_popup = false;
    }

    if (ImGui::BeginPopupModal("Open Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Scene path (.slscene)");
        ImGui::SetNextItemWidth(620.0f);
        ImGui::InputText("##open_path", &state.open_path_text);
        if (ImGui::Button("Open", ImVec2(120.0f, 0.0f))) {
            const auto path = std::filesystem::path(state.open_path_text);
            if (state.dirty) {
                state.pending_action = PendingAction::OpenScene;
                state.pending_open_path = path;
                state.request_unsaved_popup = true;
                ImGui::CloseCurrentPopup();
            } else if (open_scene(state, path)) {
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Open Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Vespera project path (.vesperaproject)");
        ImGui::SetNextItemWidth(620.0f);
        ImGui::InputText("##project_path", &state.project_path_text);
        if (ImGui::Button("Open", ImVec2(120.0f, 0.0f))) {
            const auto path = std::filesystem::path(state.project_path_text);
            if (state.dirty) {
                state.pending_action = PendingAction::OpenProject;
                state.pending_open_path = path;
                state.request_unsaved_popup = true;
                ImGui::CloseCurrentPopup();
            } else if (open_project(state, path)) {
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Destination path (.slscene)");
        ImGui::SetNextItemWidth(620.0f);
        ImGui::InputText("##save_path", &state.save_path_text);
        if (ImGui::Button("Save", ImVec2(120.0f, 0.0f))) {
            if (save_scene(state, state.save_path_text)) {
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("This scene has unsaved changes.");
        ImGui::TextDisabled("Save them before continuing?");
        ImGui::Separator();
        if (ImGui::Button("Save", ImVec2(120.0f, 0.0f))) {
            if (!state.scene_path.empty() && save_scene(state, state.scene_path)) {
                ImGui::CloseCurrentPopup();
                execute_pending_action(state, running);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard", ImVec2(120.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
            execute_pending_action(state, running);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            state.pending_action = PendingAction::None;
            state.pending_open_path.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}


std::string automation_json_escape(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (const unsigned char c : value) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) out += std::format("\\u{:04x}", static_cast<unsigned int>(c));
                else out.push_back(static_cast<char>(c));
                break;
        }
    }
    return out;
}

const std::string* automation_arg(const vespera::editor::AutomationRequest& request, std::string_view key) {
    const auto it = request.args.find(std::string(key));
    return it == request.args.end() ? nullptr : &it->second;
}

std::optional<std::uint64_t> automation_u64(std::string_view value) {
    std::uint64_t out = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), out);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) return std::nullopt;
    return out;
}

std::optional<int> automation_int(std::string_view value) {
    int out = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), out);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) return std::nullopt;
    return out;
}

std::optional<vespera::Vec3> automation_vec3(std::string_view value) {
    vespera::Vec3 result{};
    float* fields[] = {&result.x, &result.y, &result.z};
    std::size_t start = 0;
    for (int i = 0; i < 3; ++i) {
        const std::size_t comma = value.find(',', start);
        const std::string_view token = i < 2
            ? (comma == std::string_view::npos ? std::string_view{} : value.substr(start, comma - start))
            : value.substr(start);
        if (token.empty()) return std::nullopt;
        std::string temp(token);
        char* end = nullptr;
        const float parsed = std::strtof(temp.c_str(), &end);
        if (!end || *end != '\0' || !std::isfinite(parsed)) return std::nullopt;
        *fields[i] = parsed;
        if (i < 2) start = comma + 1;
    }
    return result;
}


std::optional<bool> automation_bool(std::string_view value) {
    if (value == "true" || value == "1") return true;
    if (value == "false" || value == "0") return false;
    return std::nullopt;
}

std::optional<float> automation_float(std::string_view value) {
    std::string temp(value);
    char* end = nullptr;
    const float parsed = std::strtof(temp.c_str(), &end);
    if (!end || *end != '\0' || !std::isfinite(parsed)) return std::nullopt;
    return parsed;
}

std::optional<vespera::Vec2> automation_vec2(std::string_view value) {
    const std::size_t comma = value.find(',');
    if (comma == std::string_view::npos) return std::nullopt;
    const auto x = automation_float(value.substr(0, comma));
    const auto z = automation_float(value.substr(comma + 1));
    if (!x || !z) return std::nullopt;
    return vespera::Vec2{*x, *z};
}

std::optional<std::array<float, 4>> automation_color4(std::string_view value) {
    std::array<float, 4> result{};
    std::size_t start = 0;
    for (int i = 0; i < 4; ++i) {
        const std::size_t comma = value.find(',', start);
        const std::string_view token = i < 3
            ? (comma == std::string_view::npos ? std::string_view{} : value.substr(start, comma - start))
            : value.substr(start);
        if (token.empty()) return std::nullopt;
        const auto parsed = automation_float(token);
        if (!parsed) return std::nullopt;
        result[static_cast<std::size_t>(i)] = *parsed;
        if (i < 3) start = comma + 1;
    }
    return result;
}


std::optional<vespera::UiNodeType> automation_ui_node_type(std::string_view value) {
    if (value == "canvas") return vespera::UiNodeType::Canvas;
    if (value == "panel") return vespera::UiNodeType::Panel;
    if (value == "text") return vespera::UiNodeType::Text;
    if (value == "image") return vespera::UiNodeType::Image;
    if (value == "button") return vespera::UiNodeType::Button;
    if (value == "progress" || value == "progress_bar") return vespera::UiNodeType::ProgressBar;
    if (value == "scroll" || value == "scroll_view") return vespera::UiNodeType::ScrollView;
    if (value == "list") return vespera::UiNodeType::List;
    if (value == "grid") return vespera::UiNodeType::Grid;
    if (value == "tabs") return vespera::UiNodeType::Tabs;
    if (value == "modal") return vespera::UiNodeType::Modal;
    if (value == "tooltip") return vespera::UiNodeType::Tooltip;
    if (value == "text_input") return vespera::UiNodeType::TextInput;
    return std::nullopt;
}

std::optional<vespera::UiLayoutMode> automation_ui_layout_mode(std::string_view value) {
    if (value == "none") return vespera::UiLayoutMode::None;
    if (value == "horizontal") return vespera::UiLayoutMode::Horizontal;
    if (value == "vertical") return vespera::UiLayoutMode::Vertical;
    if (value == "grid") return vespera::UiLayoutMode::Grid;
    return std::nullopt;
}

std::optional<vespera::UiImageFit> automation_ui_image_fit(std::string_view value) {
    if (value == "stretch") return vespera::UiImageFit::Stretch;
    if (value == "contain") return vespera::UiImageFit::Contain;
    if (value == "cover") return vespera::UiImageFit::Cover;
    return std::nullopt;
}

std::optional<vespera::UiHorizontalAlignment> automation_ui_horizontal_alignment(std::string_view value) {
    if (value == "left") return vespera::UiHorizontalAlignment::Left;
    if (value == "center") return vespera::UiHorizontalAlignment::Center;
    if (value == "right") return vespera::UiHorizontalAlignment::Right;
    return std::nullopt;
}

std::optional<vespera::UiVerticalAlignment> automation_ui_vertical_alignment(std::string_view value) {
    if (value == "top") return vespera::UiVerticalAlignment::Top;
    if (value == "middle") return vespera::UiVerticalAlignment::Middle;
    if (value == "bottom") return vespera::UiVerticalAlignment::Bottom;
    return std::nullopt;
}

const vespera::BuiltinPropertyInfo* automation_property_info(
    std::string_view component_key,
    std::string_view property_key
) {
    const auto* component = vespera::builtin_component_info(component_key);
    if (!component) return nullptr;
    for (const auto& property : vespera::builtin_component_properties(component->type)) {
        if (property.key == property_key) return &property;
    }
    return nullptr;
}

std::optional<vespera::BuiltinPropertyValue> automation_parse_property_value(
    vespera::BuiltinPropertyType type,
    std::string_view value
) {
    switch (type) {
        case vespera::BuiltinPropertyType::Bool: {
            const auto parsed = automation_bool(value);
            if (parsed) return vespera::BuiltinPropertyValue{*parsed};
            break;
        }
        case vespera::BuiltinPropertyType::Float: {
            const auto parsed = automation_float(value);
            if (parsed) return vespera::BuiltinPropertyValue{*parsed};
            break;
        }
        case vespera::BuiltinPropertyType::String:
            return vespera::BuiltinPropertyValue{std::string(value)};
        case vespera::BuiltinPropertyType::Vec2: {
            const auto parsed = automation_vec2(value);
            if (parsed) return vespera::BuiltinPropertyValue{*parsed};
            break;
        }
        case vespera::BuiltinPropertyType::Vec3: {
            const auto parsed = automation_vec3(value);
            if (parsed) return vespera::BuiltinPropertyValue{*parsed};
            break;
        }
        case vespera::BuiltinPropertyType::Color4: {
            const auto parsed = automation_color4(value);
            if (parsed) return vespera::BuiltinPropertyValue{*parsed};
            break;
        }
        case vespera::BuiltinPropertyType::Texture: {
            const auto parsed = automation_u64(value);
            if (parsed && *parsed <= std::numeric_limits<vespera::TextureId>::max()) {
                return vespera::BuiltinPropertyValue{static_cast<vespera::TextureId>(*parsed)};
            }
            break;
        }
    }
    return std::nullopt;
}

std::string automation_property_json(const vespera::BuiltinPropertyValue& value) {
    return std::visit([](const auto& typed) -> std::string {
        using T = std::decay_t<decltype(typed)>;
        if constexpr (std::is_same_v<T, bool>) {
            return typed ? "true" : "false";
        } else if constexpr (std::is_same_v<T, float>) {
            return std::format("{}", typed);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return std::format("\"{}\"", automation_json_escape(typed));
        } else if constexpr (std::is_same_v<T, vespera::Vec2>) {
            return std::format("[{},{}]", typed.x, typed.z);
        } else if constexpr (std::is_same_v<T, vespera::Vec3>) {
            return std::format("[{},{},{}]", typed.x, typed.y, typed.z);
        } else if constexpr (std::is_same_v<T, std::array<float, 4>>) {
            return std::format("[{},{},{},{}]", typed[0], typed[1], typed[2], typed[3]);
        } else if constexpr (std::is_same_v<T, vespera::TextureId>) {
            return std::format("{}", typed);
        }
        return "null";
    }, value);
}

std::filesystem::path automation_source_root() {
    if (const char* value = std::getenv("VESPERA_SOURCE_ROOT"); value && *value) {
        return std::filesystem::path(value).lexically_normal();
    }
    return {};
}

std::filesystem::path automation_find_runtime_executable(const EditorState& state, std::string_view configuration) {
    const auto root = automation_source_root();
    if (root.empty()) return {};
    const std::string target_name = state.project.game_target.empty() ? "vespera_player" : state.project.game_target;
    const auto build_root = root / "build";
    std::error_code ec;
    if (!std::filesystem::is_directory(build_root, ec) || ec) return {};
#if defined(_WIN32)
    const std::string filename = target_name + ".exe";
#else
    const std::string filename = target_name;
#endif
    const std::string native_config(vespera::native_configuration_for_package(configuration));
    for (std::filesystem::recursive_directory_iterator it(build_root, ec), end; it != end && !ec; it.increment(ec)) {
        if (!it->is_regular_file(ec) || ec) continue;
        if (it->path().filename() != filename) continue;
        bool config_match = false;
        for (const auto& part : it->path().parent_path()) {
            if (part.string() == native_config) { config_match = true; break; }
        }
        if (config_match) return it->path().lexically_normal();
    }
    return {};
}

std::filesystem::path automation_find_managed_directory(const EditorState& state) {
    const auto root = automation_source_root();
    if (root.empty() || state.project.managed_assembly.empty()) return {};
    const auto managed_root = root / "build" / "managed";
    std::error_code ec;
    if (!std::filesystem::is_directory(managed_root, ec) || ec) return {};
    const std::string filename = state.project.managed_assembly + ".dll";
    for (std::filesystem::recursive_directory_iterator it(managed_root, ec), end; it != end && !ec; it.increment(ec)) {
        if (!it->is_regular_file(ec) || ec) continue;
        if (it->path().filename() == filename) return it->path().parent_path().lexically_normal();
    }
    return {};
}

const vespera::AssetRecord* automation_asset_record(
    const EditorState& state,
    const vespera::editor::AutomationRequest& request
) {
    if (const auto* id = automation_arg(request, "asset_id"); id && !id->empty()) {
        return state.asset_catalog.find_by_id(*id);
    }
    if (const auto* path = automation_arg(request, "path"); path && !path->empty()) {
        return state.asset_catalog.find(*path);
    }
    return nullptr;
}

std::optional<std::filesystem::path> automation_managed_source_path(
    const EditorState& state,
    std::string_view requested,
    std::string* error = nullptr
) {
    if (!state.project_loaded || state.project.managed_project.empty()) {
        if (error) *error = "the current project has no managed C# project";
        return std::nullopt;
    }
    if (requested.empty()) {
        if (error) *error = "path is required";
        return std::nullopt;
    }
    std::filesystem::path relative(requested);
    if (relative.is_absolute() || relative.has_root_name() || relative.has_root_directory()) {
        if (error) *error = "managed source path must be relative to the managed project directory";
        return std::nullopt;
    }
    relative = relative.lexically_normal();
    const auto text = relative.generic_string();
    if (text.empty() || text == "." || text.starts_with("../") || text == "..") {
        if (error) *error = "managed source path must stay inside the managed project directory";
        return std::nullopt;
    }
    if (relative.extension() != ".cs") {
        if (error) *error = "managed source path must end in .cs";
        return std::nullopt;
    }
    const auto base = state.project.managed_project_path().parent_path().lexically_normal();
    return (base / relative).lexically_normal();
}

bool automation_guarded_replace_text(
    const std::filesystem::path& path,
    std::string_view text,
    std::string* error = nullptr
) {
    if (text.size() > 1024 * 1024) {
        if (error) *error = "managed source is limited to 1 MiB through automation";
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        if (error) *error = "could not create managed source directory: " + ec.message();
        return false;
    }
    auto temp = path; temp += ".vespera_tmp";
    auto backup = path; backup += ".vespera_bak";
    std::filesystem::remove(temp, ec); ec.clear();
    std::filesystem::remove(backup, ec); ec.clear();
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) { if (error) *error = "could not open temporary managed source for writing"; return false; }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) { std::filesystem::remove(temp, ec); if (error) *error = "failed while writing temporary managed source"; return false; }
    }
    const bool existed = std::filesystem::exists(path, ec) && !ec; ec.clear();
    if (existed) {
        std::filesystem::rename(path, backup, ec);
        if (ec) { std::filesystem::remove(temp, ec); if (error) *error = "could not back up existing managed source before replacement"; return false; }
    }
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        const auto commit_error = ec.message();
        std::string restore_error;
        ec.clear();
        if (existed) {
            std::filesystem::rename(backup, path, ec);
            if (ec) restore_error = "; backup restore also failed: " + ec.message();
        }
        if (error) *error = "could not commit managed source replacement: " + commit_error + restore_error;
        return false;
    }
    if (existed) { ec.clear(); std::filesystem::remove(backup, ec); }
    return true;
}

bool launch_packaged_runtime(EditorState& state, const std::filesystem::path& runtime, bool runtime_automation = false);

vespera::editor::AutomationResponse automation_response(std::uint64_t id, bool ok, std::string text) {
    vespera::editor::AutomationResponse response;
    response.id = id;
    response.ok = ok;
    response.fields["text"] = std::move(text);
    return response;
}

vespera::editor::AutomationResponse dispatch_automation_request(
    EditorState& state,
    const vespera::editor::AutomationRequest& request) {
    using vespera::editor::AutomationResponse;
    const auto fail = [&](std::string message) { return automation_response(request.id, false, std::move(message)); };
    const auto ok = [&](std::string message) { return automation_response(request.id, true, std::move(message)); };

    if (request.command == "vespera_get_state") {
        const auto selected = state.selection.kind == SelectionKind::Entity
            ? state.selection.object_id : vespera::kInvalidSceneObjectId;
        const std::string play_state = state.play_state == EditorPlayState::Editing
            ? "editing" : (state.play_state == EditorPlayState::Paused ? "paused" : "playing");
        const std::uint32_t assembly_generation = state.play_runtime
            ? state.play_runtime->assembly_generation() : state.last_runtime_assembly_generation;
        const std::uint64_t runtime_event_sequence = state.play_runtime
            ? state.play_runtime->latest_runtime_event_sequence() : state.last_runtime_event_sequence;
        const auto& visible_scene = editor_is_playing(state) ? state.play_scene : state.scene;
        const auto scene_stats = vespera::collect_scene_stats(visible_scene);
        const auto& perf = state.performance;
        return ok(std::format(
            "{{\"engine_version\":\"{}\",\"project\":\"{}\",\"scene\":\"{}\",\"entities\":{},\"sectors\":{},\"dirty\":{},\"play_state\":\"{}\",\"selected_entity_id\":{},\"managed_assembly_generation\":{},\"runtime_event_sequence\":{},"
            "\"scene_stats\":{{\"enabled_entities\":{},\"sprites\":{},\"meshes\":{},\"colliders\":{},\"triggers\":{},\"point_lights\":{},\"managed_scripts\":{},\"sprite_clips\":{}}},"
            "\"asset_stats\":{{\"records\":{},\"dependency_edges\":{},\"broken_dependencies\":{}}},"
            "\"performance\":{{\"frame_index\":{},\"last_frame_ms\":{:.3f},\"smoothed_frame_ms\":{:.3f},\"max_frame_ms\":{:.3f},\"play_update_ms\":{:.3f},\"render_ms\":{:.3f},\"live_resize_redraws\":{}}}}}",
            vespera::kEngineVersion,
            automation_json_escape(state.project_loaded ? state.project.name : std::string{}),
            automation_json_escape(state.scene_path.generic_string()),
            scene_stats.entities, scene_stats.sectors,
            state.dirty ? "true" : "false", play_state, selected, assembly_generation, runtime_event_sequence,
            scene_stats.enabled_entities, scene_stats.sprite_renderers, scene_stats.mesh_renderers,
            scene_stats.colliders, scene_stats.triggers, scene_stats.point_lights, scene_stats.managed_scripts, scene_stats.sprite_clips,
            state.asset_catalog.records().size(), state.asset_catalog.dependencies().size(), state.asset_catalog.broken_dependencies().size(),
            perf.frame_index, perf.last_frame_ms, perf.smoothed_frame_ms, perf.max_frame_ms,
            state.play_update_ms, perf.render_ms, perf.live_resize_redraws));
    }

    if (request.command == "vespera_list_entities") {
        std::string json = "[";
        for (std::size_t i = 0; i < state.scene.entities.size(); ++i) {
            const auto& entity = state.scene.entities[i];
            if (i != 0) json += ',';
            json += std::format(
                "{{\"id\":{},\"name\":\"{}\",\"parent_id\":{},\"tag\":\"{}\",\"layer\":\"{}\",\"components\":[",
                entity.id, automation_json_escape(entity.name), entity.parent_id,
                automation_json_escape(entity.tag), automation_json_escape(entity.layer));
            bool first = true;
            for (const auto& component : vespera::kBuiltinComponentTypes) {
                if (!entity.has_component(component.type)) continue;
                if (!first) json += ',';
                first = false;
                json += '"'; json += component.key; json += '"';
            }
            for (const auto& script : entity.managed_scripts) {
                if (!first) json += ',';
                first = false;
                json += "\"csharp:" + automation_json_escape(script.class_name) + "\"";
            }
            json += "]}";
        }
        json += "]";
        return ok(std::move(json));
    }

    if (request.command == "vespera_get_entity") {
        const auto* raw_id = automation_arg(request, "entity_id");
        const auto id = raw_id ? automation_u64(*raw_id) : std::nullopt;
        if (!id) return fail("entity_id is required and must be an unsigned integer");
        const auto* entity = state.scene.find_entity(*id);
        if (!entity) return fail("entity_id does not exist in the edit scene");
        const auto world = editor_world_transform(state, *entity);
        std::string components = "[";
        bool first = true;
        for (const auto& component : vespera::kBuiltinComponentTypes) {
            if (!entity->has_component(component.type)) continue;
            if (!first) components += ',';
            first = false;
            components += std::format("\"{}\"", component.key);
        }
        components += "]";
        return ok(std::format(
            "{{\"id\":{},\"name\":\"{}\",\"enabled\":{},\"parent_id\":{},\"tag\":\"{}\",\"layer\":\"{}\","
            "\"local_position\":[{},{},{}],\"local_rotation_degrees\":[{},{},{}],\"local_scale\":[{},{},{}],"
            "\"world_position\":[{},{},{}],\"components\":{}}}",
            entity->id, automation_json_escape(entity->name), entity->enabled ? "true" : "false", entity->parent_id,
            automation_json_escape(entity->tag), automation_json_escape(entity->layer),
            entity->transform.position.x, entity->transform.position.y, entity->transform.position.z,
            entity->transform.rotation.x * kRadiansToDegrees, entity->transform.rotation.y * kRadiansToDegrees, entity->transform.rotation.z * kRadiansToDegrees,
            entity->transform.scale.x, entity->transform.scale.y, entity->transform.scale.z,
            world.position.x, world.position.y, world.position.z, components));
    }

    if (request.command == "vespera_list_assets") {
        std::string json = "[";
        bool first = true;
        const std::string kind_filter = automation_arg(request, "kind") ? *automation_arg(request, "kind") : std::string{};
        for (const auto& asset : state.asset_catalog.records()) {
            if (!kind_filter.empty() && vespera::asset_kind_name(asset.kind) != kind_filter) continue;
            if (!first) json += ',';
            first = false;
            json += std::format("{{\"asset_id\":\"{}\",\"kind\":\"{}\",\"path\":\"{}\",\"name\":\"{}\"}}",
                automation_json_escape(asset.asset_id), vespera::asset_kind_name(asset.kind),
                automation_json_escape(asset.relative_path.generic_string()), automation_json_escape(asset.display_name));
        }
        json += "]";
        return ok(std::move(json));
    }

    if (request.command == "vespera_get_build_manifest") {
        if (!state.project_loaded) return fail("no Vespera project is open");
        const auto manifest = vespera::build_project_asset_manifest(state.project, state.asset_catalog);
        std::string json = std::format(
            "{{\"valid\":{},\"asset_count\":{},\"missing_roots\":{},\"broken_dependencies\":{},\"stale_root_paths\":{},\"assets\":[",
            manifest.valid() ? "true" : "false", manifest.assets.size(), manifest.missing_roots.size(),
            manifest.broken_dependencies.size(), manifest.stale_root_paths.size());
        bool first = true;
        for (const auto* asset : manifest.assets) {
            if (!asset) continue;
            if (!first) json += ',';
            first = false;
            json += std::format("{{\"asset_id\":\"{}\",\"kind\":\"{}\",\"path\":\"{}\"}}",
                automation_json_escape(asset->asset_id), vespera::asset_kind_name(asset->kind),
                automation_json_escape(asset->relative_path.generic_string()));
        }
        json += "]}";
        return ok(std::move(json));
    }

    if (request.command == "vespera_export_project") {
        if (editor_is_playing(state)) return fail("project export is disabled during Play Mode");
        if (!state.project_loaded) return fail("no Vespera project is open");
        const auto* raw_output = automation_arg(request, "output_directory");
        const std::string configuration = automation_arg(request, "configuration")
            ? *automation_arg(request, "configuration") : "Development";
        if (!vespera::valid_package_configuration(configuration)) {
            return fail("configuration must be Debug, Development, or Release");
        }
        vespera::ProjectPackageOptions package_options;
        package_options.configuration = configuration;
        package_options.managed_deployment = state.project.managed_deployment;
        if (const auto* deployment = automation_arg(request, "managed_deployment"); deployment && !deployment->empty()) {
            const auto parsed = vespera::managed_deployment_mode_from_name(*deployment);
            if (!parsed) return fail("managed_deployment must be framework-dependent or portable");
            package_options.managed_deployment = *parsed;
        }
        package_options.clean_output = automation_arg(request, "clean")
            ? automation_bool(*automation_arg(request, "clean")).value_or(true) : true;
        package_options.include_debug_symbols = configuration == "Debug"
            || (configuration == "Development" && state.project.development_diagnostics);
        if (raw_output && !raw_output->empty()) {
            package_options.output_directory = std::filesystem::path(*raw_output);
            if (package_options.output_directory.is_relative()) {
                package_options.output_directory = state.project.root_directory / package_options.output_directory;
            }
        } else {
            std::string safe_name = state.project.name.empty() ? "VesperaGame" : state.project.name;
            for (char& c : safe_name) {
                const bool allowed = std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-';
                if (!allowed) c = '-';
            }
            package_options.output_directory = state.project.build_output_root() / (safe_name + "-" + configuration);
        }
        if (const auto* runtime = automation_arg(request, "runtime_executable"); runtime && !runtime->empty()) {
            package_options.runtime_executable = *runtime;
        } else {
            package_options.runtime_executable = automation_find_runtime_executable(state, configuration);
        }
        if (const auto* managed = automation_arg(request, "managed_directory"); managed && !managed->empty()) {
            package_options.managed_directory = *managed;
        } else {
            package_options.managed_directory = automation_find_managed_directory(state);
        }
        const auto packaged = vespera::export_project_package(state.project, state.asset_catalog, package_options);
        append_command_audit(state, vespera::editor::EditorCommandKind::ExportProject,
            "Automation export project", packaged.ok, state.current_state_id, state.current_state_id);
        if (!packaged) return fail(packaged.message);
        const bool launch_requested = automation_arg(request, "launch")
            ? automation_bool(*automation_arg(request, "launch")).value_or(false) : false;
        const bool runtime_automation = automation_arg(request, "runtime_automation")
            ? automation_bool(*automation_arg(request, "runtime_automation")).value_or(false) : false;
        if (runtime_automation && !launch_requested) return fail("runtime_automation requires launch=true");
        const bool launched = launch_requested
            ? launch_packaged_runtime(state, packaged.packaged_runtime, runtime_automation) : false;
        if (launch_requested && !launched) return fail("package succeeded but exported runtime launch failed; inspect vespera_get_console");
        return ok(std::format(
            "{{\"output_directory\":\"{}\",\"runtime\":\"{}\",\"report\":\"{}\",\"assets\":{},\"asset_bytes\":{},"
            "\"metadata\":{},\"managed_files\":{},\"runtime_files\":{},\"warnings\":{},\"launched\":{},\"runtime_automation\":{}}}",
            automation_json_escape(packaged.package_directory.generic_string()),
            automation_json_escape(packaged.packaged_runtime.generic_string()), automation_json_escape(packaged.package_report.generic_string()),
            packaged.copied_assets, packaged.asset_bytes, packaged.copied_metadata, packaged.copied_managed_files,
            packaged.copied_runtime_files, packaged.warnings.size(), launched ? "true" : "false",
            (launched && runtime_automation) ? "true" : "false"));
    }

    if (request.command == "vespera_list_capabilities") {
        std::string json = std::format("{{\"extension_api_version\":{},\"extensions\":[", vespera::editor::kEditorExtensionApiVersion);
        bool first = true;
        for (const auto& extension : state.extension_registry.extensions()) {
            if (!first) json += ',';
            first = false;
            json += std::format("{{\"id\":\"{}\",\"name\":\"{}\",\"version\":\"{}\"}}",
                automation_json_escape(extension.id), automation_json_escape(extension.display_name), automation_json_escape(extension.version));
        }
        json += "],\"commands\":[";
        first = true;
        for (const auto& command : state.extension_registry.commands()) {
            if (!first) json += ',';
            first = false;
            json += std::format("{{\"name\":\"{}\",\"mutating\":{},\"undoable\":{},\"allowed_during_play\":{}}}",
                automation_json_escape(command.name), command.mutating ? "true" : "false",
                command.undoable ? "true" : "false", command.allowed_during_play ? "true" : "false");
        }
        json += "]}";
        return ok(std::move(json));
    }

    if (request.command == "vespera_get_console") {
        std::size_t limit = 50;
        if (const auto* raw_limit = automation_arg(request, "limit")) {
            const auto parsed = automation_u64(*raw_limit);
            if (!parsed || *parsed == 0) return fail("limit must be a positive integer");
            limit = static_cast<std::size_t>(std::min<std::uint64_t>(*parsed, 100));
        }
        const std::size_t begin = state.console.size() > limit ? state.console.size() - limit : 0;
        std::string json = "[";
        for (std::size_t i = begin; i < state.console.size(); ++i) {
            if (i != begin) json += ',';
            const auto& entry = state.console[i];
            const char* level = entry.level == ConsoleEntry::Level::Error
                ? "error" : (entry.level == ConsoleEntry::Level::Warning ? "warning" : "info");
            json += std::format("{{\"level\":\"{}\",\"text\":\"{}\"}}",
                level, automation_json_escape(entry.text));
        }
        json += "]";
        return ok(std::move(json));
    }

    if (request.command == "vespera_get_command_log") {
        std::size_t limit = 50;
        if (const auto* raw_limit = automation_arg(request, "limit")) {
            const auto parsed = automation_u64(*raw_limit);
            if (!parsed || *parsed == 0) return fail("limit must be a positive integer");
            limit = static_cast<std::size_t>(std::min<std::uint64_t>(*parsed, 100));
        }
        const std::size_t begin = state.command_log.size() > limit ? state.command_log.size() - limit : 0;
        std::string json = "[";
        for (std::size_t i = begin; i < state.command_log.size(); ++i) {
            if (i != begin) json += ',';
            const auto& record = state.command_log[i];
            json += std::format(
                "{{\"sequence\":{},\"command\":\"{}\",\"label\":\"{}\",\"succeeded\":{},\"before_state_id\":{},\"after_state_id\":{},\"target_entity_id\":{},\"target_asset_id\":\"{}\"}}",
                record.sequence, vespera::editor::command_name(record.kind), automation_json_escape(record.label),
                record.succeeded ? "true" : "false", record.before_state_id, record.after_state_id,
                record.target_entity_id, automation_json_escape(record.target_asset_id));
        }
        json += "]";
        return ok(std::move(json));
    }

    if (request.command == "vespera_select_entity") {
        if (editor_is_playing(state)) return fail("selection mutation is disabled during Play Mode");
        const auto* raw_id = automation_arg(request, "entity_id");
        const auto id = raw_id ? automation_u64(*raw_id) : std::nullopt;
        if (!id) return fail("entity_id is required and must be an unsigned integer");
        const auto index = entity_index_from_id(state, *id);
        if (!index) return fail("entity_id does not exist in the edit scene");
        select_entity(state, *index);
        append_command_audit(state, vespera::editor::EditorCommandKind::SelectEntity,
            "Automation select entity", true, 0, 0, *id);
        return ok(std::format("{{\"selected_entity_id\":{}}}", *id));
    }

    if (request.command == "vespera_create_entity") {
        if (editor_is_playing(state)) return fail("scene authoring is disabled during Play Mode");
        const std::string kind = automation_arg(request, "kind") ? *automation_arg(request, "kind") : "empty";
        const std::string requested_name = automation_arg(request, "name") ? *automation_arg(request, "name") : std::string{};
        bool created = false;
        if (kind == "empty") created = command_create_entity(state, requested_name);
        else if (kind == "sprite") created = command_create_sprite_entity(state, requested_name);
        else if (kind == "trigger") created = command_create_trigger_entity(state, requested_name);
        else if (kind == "point_light") created = command_create_point_light_entity(state, requested_name);
        else return fail("kind must be empty, sprite, trigger or point_light");
        if (!created) return fail("entity creation failed");
        const auto selected = selected_entity_index(state);
        if (!selected) return fail("entity was created but selection could not be resolved");
        auto& entity = state.scene.entities[*selected];
        if (!state.command_log.empty()) state.command_log.back().target_entity_id = entity.id;
        return ok(std::format("{{\"entity_id\":{},\"name\":\"{}\"}}", entity.id, automation_json_escape(entity.name)));
    }

    if (request.command == "vespera_duplicate_entity") {
        if (editor_is_playing(state)) return fail("scene authoring is disabled during Play Mode");
        const auto* raw_id = automation_arg(request, "entity_id");
        const auto id = raw_id ? automation_u64(*raw_id) : std::nullopt;
        if (!id) return fail("entity_id is required and must be an unsigned integer");
        const auto index = entity_index_from_id(state, *id);
        if (!index) return fail("entity_id does not exist in the edit scene");
        select_entity(state, *index);
        if (!command_duplicate_selected_entity(state)) return fail("duplicate command failed");
        const auto selected = selected_entity_index(state);
        if (!selected) return fail("duplicate completed but selection could not be resolved");
        const auto new_id = state.scene.entities[*selected].id;
        if (!state.command_log.empty()) state.command_log.back().target_entity_id = new_id;
        return ok(std::format("{{\"source_entity_id\":{},\"entity_id\":{}}}", *id, new_id));
    }

    if (request.command == "vespera_delete_entity") {
        if (editor_is_playing(state)) return fail("scene authoring is disabled during Play Mode");
        const auto* raw_id = automation_arg(request, "entity_id");
        const auto id = raw_id ? automation_u64(*raw_id) : std::nullopt;
        if (!id) return fail("entity_id is required and must be an unsigned integer");
        const auto index = entity_index_from_id(state, *id);
        if (!index) return fail("entity_id does not exist in the edit scene");
        select_entity(state, *index);
        if (!command_delete_selected_entity(state)) return fail("delete command failed");
        if (!state.command_log.empty()) state.command_log.back().target_entity_id = *id;
        return ok(std::format("{{\"deleted_entity_id\":{}}}", *id));
    }

    if (request.command == "vespera_reparent_entity") {
        if (editor_is_playing(state)) return fail("scene authoring is disabled during Play Mode");
        const auto* raw_child = automation_arg(request, "entity_id");
        const auto child = raw_child ? automation_u64(*raw_child) : std::nullopt;
        if (!child) return fail("entity_id is required and must be an unsigned integer");
        if (!state.scene.find_entity(*child)) return fail("entity_id does not exist in the edit scene");
        vespera::SceneObjectId parent = vespera::kInvalidSceneObjectId;
        if (const auto* raw_parent = automation_arg(request, "parent_id"); raw_parent && !raw_parent->empty() && *raw_parent != "0") {
            const auto parsed = automation_u64(*raw_parent);
            if (!parsed) return fail("parent_id must be an unsigned integer");
            parent = *parsed;
            if (!state.scene.find_entity(parent)) return fail("parent_id does not exist in the edit scene");
        }
        if (!command_reparent_entity(state, *child, parent)) return fail("reparent command failed; the relationship may be unchanged or cyclic");
        if (!state.command_log.empty()) state.command_log.back().target_entity_id = *child;
        return ok(std::format("{{\"entity_id\":{},\"parent_id\":{}}}", *child, parent));
    }

    if (request.command == "vespera_create_primitive") {
        if (editor_is_playing(state)) return fail("scene authoring is disabled during Play Mode");
        const std::string primitive = automation_arg(request, "primitive")
            ? *automation_arg(request, "primitive") : "cube";
        std::optional<vespera::PrimitiveMeshType> type = vespera::primitive_mesh_from_name(primitive);
        if (!type) return fail("primitive must be cube, plane, cylinder or sphere");
        if (!command_create_primitive_entity(state, *type)) return fail("primitive creation failed");
        const auto selected = selected_entity_index(state);
        if (!selected) return fail("primitive was created but selection could not be resolved");
        const auto id = state.scene.entities[*selected].id;
        if (!state.command_log.empty()) state.command_log.back().target_entity_id = id;
        return ok(std::format("{{\"entity_id\":{},\"name\":\"{}\"}}",
            id, automation_json_escape(state.scene.entities[*selected].name)));
    }

    if (request.command == "vespera_set_transform") {
        if (editor_is_playing(state)) return fail("scene authoring is disabled during Play Mode");
        const auto* raw_id = automation_arg(request, "entity_id");
        const auto id = raw_id ? automation_u64(*raw_id) : std::nullopt;
        if (!id) return fail("entity_id is required and must be an unsigned integer");
        if (!state.scene.find_entity(*id)) return fail("entity_id does not exist in the edit scene");
        const bool world_space = !automation_arg(request, "space") || *automation_arg(request, "space") != "local";
        const auto position = automation_arg(request, "position") ? automation_vec3(*automation_arg(request, "position")) : std::optional<vespera::Vec3>{};
        const auto rotation_degrees = automation_arg(request, "rotation_degrees") ? automation_vec3(*automation_arg(request, "rotation_degrees")) : std::optional<vespera::Vec3>{};
        const auto scale = automation_arg(request, "scale") ? automation_vec3(*automation_arg(request, "scale")) : std::optional<vespera::Vec3>{};
        if (automation_arg(request, "position") && !position) return fail("position must be x,y,z");
        if (automation_arg(request, "rotation_degrees") && !rotation_degrees) return fail("rotation_degrees must be x,y,z");
        if (automation_arg(request, "scale") && !scale) return fail("scale must be x,y,z");
        if (!position && !rotation_degrees && !scale) return fail("provide position, rotation_degrees and/or scale");

        const bool changed = execute_editor_command(state, vespera::editor::EditorCommandKind::SetEntityTransform,
            "Automation set entity transform", [&]() {
                auto* entity = state.scene.find_entity(*id);
                if (!entity) return false;
                vespera::TransformComponent transform = world_space ? editor_world_transform(state, *entity) : entity->transform;
                if (position) transform.position = *position;
                if (rotation_degrees) transform.rotation = {
                    rotation_degrees->x * kDegreesToRadians,
                    rotation_degrees->y * kDegreesToRadians,
                    rotation_degrees->z * kDegreesToRadians};
                if (scale) transform.scale = *scale;
                if (world_space) set_editor_world_transform(state, *id, transform);
                else entity->transform = transform;
                return true;
            });
        if (!changed) return fail("transform command failed");
        if (!state.command_log.empty()) state.command_log.back().target_entity_id = *id;
        return ok(std::format("{{\"entity_id\":{},\"space\":\"{}\"}}", *id, world_space ? "world" : "local"));
    }

    if (request.command == "vespera_add_component") {
        if (editor_is_playing(state)) return fail("scene authoring is disabled during Play Mode");
        const auto* raw_id = automation_arg(request, "entity_id");
        const auto id = raw_id ? automation_u64(*raw_id) : std::nullopt;
        const auto* component_key = automation_arg(request, "component");
        if (!id || !component_key || component_key->empty()) return fail("entity_id and component are required");
        const auto* info = vespera::builtin_component_info(*component_key);
        if (!info) return fail("unknown built-in component key");
        if (info->type == vespera::BuiltinComponentType::Transform) return fail("Transform always exists and cannot be added");
        if (!state.scene.find_entity(*id)) return fail("entity_id does not exist in the edit scene");
        const bool changed = execute_editor_command(state, vespera::editor::EditorCommandKind::AddComponent,
            "Automation add component", [&]() {
                auto* entity = state.scene.find_entity(*id);
                return entity && entity->add_component(info->type);
            });
        if (!changed) return fail("component already exists or could not be added");
        if (!state.command_log.empty()) state.command_log.back().target_entity_id = *id;
        return ok(std::format("{{\"entity_id\":{},\"component\":\"{}\"}}", *id, automation_json_escape(*component_key)));
    }

    if (request.command == "vespera_remove_component") {
        if (editor_is_playing(state)) return fail("scene authoring is disabled during Play Mode");
        const auto* raw_id = automation_arg(request, "entity_id");
        const auto id = raw_id ? automation_u64(*raw_id) : std::nullopt;
        const auto* component_key = automation_arg(request, "component");
        if (!id || !component_key || component_key->empty()) return fail("entity_id and component are required");
        const auto* info = vespera::builtin_component_info(*component_key);
        if (!info) return fail("unknown built-in component key");
        if (!info->removable) return fail("component is not removable");
        if (!state.scene.find_entity(*id)) return fail("entity_id does not exist in the edit scene");
        const bool changed = execute_editor_command(state, vespera::editor::EditorCommandKind::RemoveComponent,
            "Automation remove component", [&]() {
                auto* entity = state.scene.find_entity(*id);
                return entity && entity->remove_component(info->type);
            });
        if (!changed) return fail("component is not present or could not be removed");
        if (!state.command_log.empty()) state.command_log.back().target_entity_id = *id;
        return ok(std::format("{{\"entity_id\":{},\"component\":\"{}\"}}", *id, automation_json_escape(*component_key)));
    }

    if (request.command == "vespera_get_component_property") {
        const auto* raw_id = automation_arg(request, "entity_id");
        const auto id = raw_id ? automation_u64(*raw_id) : std::nullopt;
        const auto* component_key = automation_arg(request, "component");
        const auto* property_key = automation_arg(request, "property");
        if (!id || !component_key || !property_key) return fail("entity_id, component and property are required");
        const auto* entity = state.scene.find_entity(*id);
        if (!entity) return fail("entity_id does not exist in the edit scene");
        const auto* property_info = automation_property_info(*component_key, *property_key);
        if (!property_info) return fail("unknown reflected component property");
        const auto value = vespera::get_builtin_component_property(*entity, *component_key, *property_key);
        if (!value) return fail("component is missing or property could not be read");
        return ok(std::format("{{\"entity_id\":{},\"component\":\"{}\",\"property\":\"{}\",\"value\":{}}}",
            *id, automation_json_escape(*component_key), automation_json_escape(*property_key), automation_property_json(*value)));
    }

    if (request.command == "vespera_set_component_property") {
        if (editor_is_playing(state)) return fail("scene authoring is disabled during Play Mode");
        const auto* raw_id = automation_arg(request, "entity_id");
        const auto id = raw_id ? automation_u64(*raw_id) : std::nullopt;
        const auto* component_key = automation_arg(request, "component");
        const auto* property_key = automation_arg(request, "property");
        const auto* raw_value = automation_arg(request, "value");
        if (!id || !component_key || !property_key || !raw_value) return fail("entity_id, component, property and value are required");
        const auto* property_info = automation_property_info(*component_key, *property_key);
        if (!property_info) return fail("unknown reflected component property");
        const auto parsed_value = automation_parse_property_value(property_info->type, *raw_value);
        if (!parsed_value) return fail("value could not be parsed for the reflected property type");
        if (!state.scene.find_entity(*id)) return fail("entity_id does not exist in the edit scene");
        const bool changed = execute_editor_command(state, vespera::editor::EditorCommandKind::SetComponentProperty,
            "Automation set component property", [&]() {
                auto* entity = state.scene.find_entity(*id);
                if (!entity) return false;
                const bool set = vespera::set_builtin_component_property(*entity, *component_key, *property_key, *parsed_value);
                if (set && *component_key == "sectorline.mesh_renderer"
                    && (*property_key == "material_asset_id" || *property_key == "material_path")) {
                    (void)vespera::hydrate_scene_materials(state.scene, state.asset_catalog);
                }
                return set;
            });
        if (!changed) return fail("property mutation was rejected by component validation");
        if (!state.command_log.empty()) state.command_log.back().target_entity_id = *id;
        return ok(std::format("{{\"entity_id\":{},\"component\":\"{}\",\"property\":\"{}\"}}",
            *id, automation_json_escape(*component_key), automation_json_escape(*property_key)));
    }

    if (request.command == "vespera_instantiate_prefab") {
        if (editor_is_playing(state)) return fail("scene authoring is disabled during Play Mode");
        const auto* record = automation_asset_record(state, request);
        if (!record) return fail("provide a valid prefab asset_id or project-relative path");
        if (record->kind != vespera::AssetKind::EntityPrefab) return fail("asset is not an Entity Prefab");
        std::optional<vespera::Vec3> position;
        if (const auto* raw_position = automation_arg(request, "position")) {
            position = automation_vec3(*raw_position);
            if (!position) return fail("position must be x,y,z");
        }
        if (!command_instantiate_prefab(state, record->absolute_path, position)) return fail("prefab instantiation failed");
        const auto selected = selected_entity_index(state);
        if (!selected) return fail("prefab was instantiated but selection could not be resolved");
        const auto id = state.scene.entities[*selected].id;
        if (!state.command_log.empty()) {
            state.command_log.back().target_entity_id = id;
            state.command_log.back().target_asset_id = record->asset_id;
        }
        return ok(std::format("{{\"entity_id\":{},\"asset_id\":\"{}\"}}", id, automation_json_escape(record->asset_id)));
    }

    if (request.command == "vespera_assign_material") {
        if (editor_is_playing(state)) return fail("scene authoring is disabled during Play Mode");
        const auto* raw_id = automation_arg(request, "entity_id");
        const auto id = raw_id ? automation_u64(*raw_id) : std::nullopt;
        if (!id) return fail("entity_id is required");
        const auto* record = automation_asset_record(state, request);
        if (!record) return fail("provide a valid Material asset_id or project-relative path");
        if (record->kind != vespera::AssetKind::Material) return fail("asset is not a Material");
        auto* entity = state.scene.find_entity(*id);
        if (!entity) return fail("entity_id does not exist in the edit scene");
        if (!entity->mesh_renderer) return fail("entity does not have a Mesh Renderer");
        const bool changed = execute_editor_command(state, vespera::editor::EditorCommandKind::AssignMaterialAsset,
            "Automation assign Material", [&]() {
                auto* target = state.scene.find_entity(*id);
                if (!target || !target->mesh_renderer) return false;
                target->mesh_renderer->material = {record->asset_id, record->relative_path};
                target->mesh_renderer->material_resolved = false;
                (void)vespera::hydrate_scene_materials(state.scene, state.asset_catalog);
                return true;
            });
        if (!changed) return fail("Material assignment failed");
        if (!state.command_log.empty()) {
            state.command_log.back().target_entity_id = *id;
            state.command_log.back().target_asset_id = record->asset_id;
        }
        return ok(std::format("{{\"entity_id\":{},\"asset_id\":\"{}\"}}", *id, automation_json_escape(record->asset_id)));
    }

    if (request.command == "vespera_create_material") {
        if (editor_is_playing(state)) return fail("asset authoring is disabled during Play Mode");
        if (state.assets_root.empty()) return fail("no project Assets root is open");
        std::filesystem::path relative = automation_arg(request, "path")
            ? std::filesystem::path(*automation_arg(request, "path")) : std::filesystem::path{};
        if (relative.empty()) {
            relative = std::filesystem::relative(unique_material_asset_path(state), state.assets_root).lexically_normal();
        }
        if (relative.is_absolute() || relative.extension() != ".slmat" || relative.lexically_normal().generic_string().starts_with("..")) {
            return fail("path must be a project-relative .slmat path inside Assets");
        }
        const auto absolute = (state.assets_root / relative).lexically_normal();
        std::error_code ec;
        if (std::filesystem::exists(absolute, ec)) return fail("Material path already exists");
        vespera::MaterialAsset material;
        material.name = automation_arg(request, "name") && !automation_arg(request, "name")->empty()
            ? *automation_arg(request, "name") : relative.stem().string();
        const std::string shader = automation_arg(request, "shader") ? *automation_arg(request, "shader") : "lit";
        if (shader == "lit") material.properties.shader = vespera::BuiltinMaterialShader::Lit;
        else if (shader == "unlit") material.properties.shader = vespera::BuiltinMaterialShader::Unlit;
        else return fail("shader must be lit or unlit");
        std::filesystem::create_directories(absolute.parent_path(), ec);
        if (ec) return fail("could not create Material folder: " + ec.message());
        const auto saved = vespera::save_material_asset(absolute, material);
        if (!saved) return fail(saved.message);
        refresh_asset_catalog(state, false, true);
        const auto* record = state.asset_catalog.find(relative.generic_string());
        append_command_audit(state, vespera::editor::EditorCommandKind::CreateMaterialAsset,
            "Automation create Material asset", record != nullptr, state.current_state_id, state.current_state_id,
            vespera::kInvalidSceneObjectId, record ? record->asset_id : std::string{});
        if (!record) return fail("Material was written but catalog refresh did not register it");
        return ok(std::format("{{\"asset_id\":\"{}\",\"path\":\"{}\"}}",
            automation_json_escape(record->asset_id), automation_json_escape(record->relative_path.generic_string())));
    }

    if (request.command == "vespera_refresh_assets") {
        if (editor_is_playing(state)) return fail("asset refresh is disabled during Play Mode");
        const bool rehash = automation_arg(request, "force_rehash")
            ? automation_bool(*automation_arg(request, "force_rehash")).value_or(false) : false;
        refresh_asset_catalog(state, true, rehash);
        append_command_audit(state, vespera::editor::EditorCommandKind::RefreshAssets,
            "Automation refresh assets", true, state.current_state_id, state.current_state_id);
        return ok(std::format("{{\"assets\":{},\"dependencies\":{},\"broken\":{},\"stale\":{}}}",
            state.asset_catalog.records().size(), state.last_asset_report.dependency_edges,
            state.last_asset_report.broken_dependencies, state.last_asset_report.stale_fallback_paths));
    }

    if (request.command == "vespera_build_csharp") {
        const auto before_console = state.console.size();
        build_managed_scripts(state);
        bool saw_error = false;
        for (std::size_t i = before_console; i < state.console.size(); ++i) {
            if (state.console[i].level == ConsoleEntry::Level::Error) saw_error = true;
        }
        append_command_audit(state, vespera::editor::EditorCommandKind::BuildManagedScripts,
            "Automation Build C#", !saw_error, state.current_state_id, state.current_state_id);
        return saw_error ? fail("C# build reported errors; inspect vespera_get_console")
            : ok("{\"build_requested\":true,\"errors_observed\":false}");
    }

    if (request.command == "vespera_open_scene") {
        if (editor_is_playing(state)) return fail("scene opening is disabled during Play Mode");
        const auto* record = automation_asset_record(state, request);
        if (!record) return fail("provide a valid Scene asset_id or project-relative path");
        if (record->kind != vespera::AssetKind::Scene) return fail("asset is not a Scene");
        const bool opened = open_scene(state, record->absolute_path);
        append_command_audit(state, vespera::editor::EditorCommandKind::OpenScene,
            "Automation open Scene", opened, state.current_state_id, state.current_state_id,
            vespera::kInvalidSceneObjectId, record->asset_id);
        return opened ? ok(std::format("{{\"asset_id\":\"{}\",\"path\":\"{}\"}}",
            automation_json_escape(record->asset_id), automation_json_escape(record->relative_path.generic_string())))
            : fail("Scene open failed; inspect vespera_get_console");
    }

    if (request.command == "vespera_get_project_settings") {
        if (!state.project_loaded) return fail("no Vespera project is open");
        std::string build_includes = "[";
        for (std::size_t i = 0; i < state.project.build_includes.size(); ++i) {
            if (i != 0) build_includes += ',';
            const std::string asset_id = i < state.project.build_include_asset_ids.size()
                ? state.project.build_include_asset_ids[i] : std::string{};
            build_includes += std::format("{{\"asset_id\":\"{}\",\"path\":\"{}\"}}",
                automation_json_escape(asset_id),
                automation_json_escape(state.project.build_includes[i].generic_string()));
        }
        build_includes += ']';
        return ok(std::format(
            "{{\"name\":\"{}\",\"company_name\":\"{}\",\"product_version\":\"{}\",\"package_name\":\"{}\","
            "\"managed_deployment\":\"{}\",\"executable_name\":\"{}\",\"build_output_directory\":\"{}\","
            "\"game_icon\":\"{}\",\"game_icon_asset_id\":\"{}\","
            "\"startup_ui\":\"{}\",\"startup_ui_asset_id\":\"{}\","
            "\"lua_entry\":\"{}\",\"lua_entry_asset_id\":\"{}\",\"development_diagnostics\":{},"
            "\"window_title\":\"{}\",\"window_width\":{},\"window_height\":{},"
            "\"window_resizable\":{},\"relative_mouse\":{},\"escape_quits\":{},\"vsync\":{},\"game_target\":\"{}\","
            "\"build_includes\":{}}}",
            automation_json_escape(state.project.name), automation_json_escape(state.project.company_name),
            automation_json_escape(state.project.product_version), automation_json_escape(state.project.package_name),
            vespera::managed_deployment_mode_name(state.project.managed_deployment),
            automation_json_escape(state.project.executable_name), automation_json_escape(state.project.build_output_directory.generic_string()),
            automation_json_escape(state.project.game_icon.generic_string()), automation_json_escape(state.project.game_icon_asset_id),
            automation_json_escape(state.project.startup_ui.generic_string()), automation_json_escape(state.project.startup_ui_asset_id),
            automation_json_escape(state.project.lua_entry.generic_string()), automation_json_escape(state.project.lua_entry_asset_id),
            state.project.development_diagnostics ? "true" : "false",
            automation_json_escape(state.project.window_title), state.project.window_width, state.project.window_height,
            state.project.window_resizable ? "true" : "false", state.project.relative_mouse ? "true" : "false",
            state.project.escape_quits ? "true" : "false", state.project.vsync ? "true" : "false",
            automation_json_escape(state.project.game_target), build_includes));
    }

    if (request.command == "vespera_set_project_setting") {
        if (editor_is_playing(state)) return fail("project settings are disabled during Play Mode");
        if (!state.project_loaded) return fail("no Vespera project is open");
        const auto* key = automation_arg(request, "key");
        const auto* value = automation_arg(request, "value");
        if (!key || !value) return fail("key and value are required");
        const auto previous_project = state.project;
        bool changed = true;
        if (*key == "window_title") state.project.window_title = *value;
        else if (*key == "window_width" || *key == "window_height") {
            const auto parsed = automation_int(*value);
            if (!parsed) return fail("window size value must be an integer");
            if (*key == "window_width") state.project.window_width = *parsed; else state.project.window_height = *parsed;
        } else if (*key == "window_resizable" || *key == "relative_mouse" || *key == "escape_quits" || *key == "vsync") {
            const auto parsed = automation_bool(*value);
            if (!parsed) return fail("boolean project setting must be true or false");
            if (*key == "window_resizable") state.project.window_resizable = *parsed;
            else if (*key == "relative_mouse") state.project.relative_mouse = *parsed;
            else if (*key == "escape_quits") state.project.escape_quits = *parsed;
            else state.project.vsync = *parsed;
        } else if (*key == "company_name") state.project.company_name = *value;
        else if (*key == "product_version") {
            if (value->empty()) return fail("product_version cannot be empty");
            state.project.product_version = *value;
        } else if (*key == "package_name") state.project.package_name = *value;
        else if (*key == "executable_name") state.project.executable_name = *value;
        else if (*key == "build_output_directory") state.project.build_output_directory = *value;
        else if (*key == "game_icon") { state.project.game_icon = *value; state.project.game_icon_asset_id.clear(); }
        else if (*key == "startup_ui") {
            if (value->empty()) {
                state.project.startup_ui.clear();
                state.project.startup_ui_asset_id.clear();
            } else {
                const vespera::AssetRecord* record = state.asset_catalog.find(*value);
                if (!record) record = state.asset_catalog.find_by_id(*value);
                if (!record) return fail("startup_ui must identify an existing project RML asset by path or stable ID");
                if (record->kind != vespera::AssetKind::RmlDocument) return fail("startup_ui must identify an RML document");
                state.project.startup_ui = record->relative_path;
                state.project.startup_ui_asset_id = record->asset_id;
            }
        }
        else if (*key == "lua_entry") {
            if (value->empty()) {
                state.project.lua_entry.clear();
                state.project.lua_entry_asset_id.clear();
            } else {
                const vespera::AssetRecord* record = state.asset_catalog.find(*value);
                if (!record) record = state.asset_catalog.find_by_id(*value);
                if (!record) return fail("lua_entry must identify an existing project Lua asset by path or stable ID");
                if (record->kind != vespera::AssetKind::LuaScript) return fail("lua_entry must identify a .lua script");
                state.project.lua_entry = record->relative_path;
                state.project.lua_entry_asset_id = record->asset_id;
            }
        }
        else if (*key == "development_diagnostics") {
            const auto parsed = automation_bool(*value);
            if (!parsed) return fail("development_diagnostics must be true or false");
            state.project.development_diagnostics = *parsed;
        }
        else if (*key == "managed_deployment") {
            const auto parsed = vespera::managed_deployment_mode_from_name(*value);
            if (!parsed) return fail("managed_deployment must be framework-dependent or portable");
            state.project.managed_deployment = *parsed;
        } else changed = false;
        if (!changed) return fail("project setting is not allow-listed for automation");
        vespera::ProjectValidationOptions validation_options;
        validation_options.require_managed_source = false;
        const auto issues = vespera::validate_vespera_project(state.project, validation_options);
        for (const auto& issue : issues) {
            if (issue.severity == vespera::ProjectValidationSeverity::Error) {
                state.project = previous_project;
                return fail("project setting produced validation error: " + issue.message);
            }
        }
        const bool persist = automation_arg(request, "save")
            ? automation_bool(*automation_arg(request, "save")).value_or(true) : true;
        if (persist) {
            const auto saved = vespera::save_vespera_project(state.project, state.project_path);
            if (!saved) {
                state.project = previous_project;
                return fail(saved.message);
            }
        }
        append_command_audit(state, vespera::editor::EditorCommandKind::SetProjectSetting,
            "Automation set project setting " + *key, true, state.current_state_id, state.current_state_id);
        return ok(std::format("{{\"key\":\"{}\",\"saved\":{}}}", automation_json_escape(*key), persist ? "true" : "false"));
    }

    if (request.command == "vespera_set_build_include") {
        if (editor_is_playing(state)) return fail("build-root authoring is disabled during Play Mode");
        if (!state.project_loaded) return fail("no Vespera project is open");
        const auto* raw_operation = automation_arg(request, "operation");
        if (!raw_operation || (*raw_operation != "add" && *raw_operation != "remove")) {
            return fail("operation must be add or remove");
        }
        const auto previous_project = state.project;
        const auto* record = automation_asset_record(state, request);
        if (!record) return fail("provide a valid asset_id or project-relative path");
        if (state.project.build_include_asset_ids.size() < state.project.build_includes.size()) {
            state.project.build_include_asset_ids.resize(state.project.build_includes.size());
        }
        bool changed = false;
        if (*raw_operation == "add") {
            for (std::size_t i = 0; i < state.project.build_includes.size(); ++i) {
                if ((!record->asset_id.empty() && state.project.build_include_asset_ids[i] == record->asset_id)
                    || state.project.build_includes[i].lexically_normal() == record->relative_path.lexically_normal()) {
                    return ok(std::format("{{\"operation\":\"add\",\"changed\":false,\"asset_id\":\"{}\",\"path\":\"{}\"}}",
                        automation_json_escape(record->asset_id), automation_json_escape(record->relative_path.generic_string())));
                }
            }
            state.project.build_includes.push_back(record->relative_path.lexically_normal());
            state.project.build_include_asset_ids.push_back(record->asset_id);
            changed = true;
        } else {
            for (std::size_t i = state.project.build_includes.size(); i-- > 0;) {
                const bool matches = (!record->asset_id.empty() && state.project.build_include_asset_ids[i] == record->asset_id)
                    || state.project.build_includes[i].lexically_normal() == record->relative_path.lexically_normal();
                if (!matches) continue;
                state.project.build_includes.erase(state.project.build_includes.begin() + static_cast<std::ptrdiff_t>(i));
                state.project.build_include_asset_ids.erase(state.project.build_include_asset_ids.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
            }
            if (!changed) {
                return ok(std::format("{{\"operation\":\"remove\",\"changed\":false,\"asset_id\":\"{}\",\"path\":\"{}\"}}",
                    automation_json_escape(record->asset_id), automation_json_escape(record->relative_path.generic_string())));
            }
        }
        const bool persist = automation_arg(request, "save")
            ? automation_bool(*automation_arg(request, "save")).value_or(true) : true;
        if (persist) {
            const auto saved = vespera::save_vespera_project(state.project, state.project_path);
            if (!saved) {
                state.project = previous_project;
                return fail("build include update could not be saved: " + saved.message);
            }
        }
        append_command_audit(state, vespera::editor::EditorCommandKind::SetProjectSetting,
            "Automation " + *raw_operation + " build include", true, state.current_state_id, state.current_state_id,
            vespera::kInvalidSceneObjectId, record->asset_id);
        return ok(std::format("{{\"operation\":\"{}\",\"changed\":true,\"saved\":{},\"asset_id\":\"{}\",\"path\":\"{}\"}}",
            automation_json_escape(*raw_operation), persist ? "true" : "false",
            automation_json_escape(record->asset_id), automation_json_escape(record->relative_path.generic_string())));
    }

    if (request.command == "vespera_check_asset_delete") {
        if (!state.project_loaded) return fail("no Vespera project is open");
        const auto* record = automation_asset_record(state, request);
        if (!record) return fail("provide a valid asset_id or project-relative path");
        const auto preflight = vespera::preflight_delete_project_asset(state.asset_catalog, state.project, record->asset_id);
        std::string blockers = "[";
        for (std::size_t i = 0; i < preflight.blockers.size(); ++i) {
            if (i) blockers += ',';
            blockers += std::format("\"{}\"", automation_json_escape(preflight.blockers[i]));
        }
        blockers += "]";
        return ok(std::format("{{\"allowed\":{},\"asset_id\":\"{}\",\"path\":\"{}\",\"blockers\":{}}}",
            preflight.allowed ? "true" : "false", automation_json_escape(record->asset_id),
            automation_json_escape(record->relative_path.generic_string()), blockers));
    }

    if (request.command == "vespera_move_asset") {
        if (editor_is_playing(state)) return fail("asset authoring is disabled during Play Mode");
        const auto* record = automation_asset_record(state, request);
        const auto* destination = automation_arg(request, "destination");
        if (!record) return fail("provide a valid asset_id or project-relative path");
        if (!destination || destination->empty()) return fail("destination is required");
        const auto old_absolute = record->absolute_path;
        const auto moved = vespera::move_project_asset(state.asset_catalog, record->asset_id, *destination);
        if (!moved) return fail(moved.message);
        if (!state.scene_path.empty() && old_absolute.lexically_normal() == state.scene_path.lexically_normal()) {
            state.scene_path = (state.assets_root / moved.new_path).lexically_normal();
        }
        refresh_asset_catalog(state, false, false);
        auto repaired = vespera::repair_stable_asset_fallbacks(state.project, state.asset_catalog);
        if (repaired.project_references_repaired != 0) {
            const auto saved = vespera::save_vespera_project(state.project, state.project_path);
            if (!saved) return fail("asset moved, but project fallback repair save failed: " + saved.message);
        }
        if (repaired.descriptor_files_rewritten != 0) refresh_asset_catalog(state, false, false);
        append_command_audit(state, vespera::editor::EditorCommandKind::MoveAsset,
            "Automation move asset", true, state.current_state_id, state.current_state_id,
            vespera::kInvalidSceneObjectId, moved.asset_id);
        return ok(std::format("{{\"asset_id\":\"{}\",\"old_path\":\"{}\",\"new_path\":\"{}\",\"repaired_references\":{},\"rml_references_rewritten\":{},\"rml_files_rewritten\":{}}}",
            automation_json_escape(moved.asset_id), automation_json_escape(moved.old_path.generic_string()),
            automation_json_escape(moved.new_path.generic_string()), repaired.total_references_repaired(),
            moved.rml_references_rewritten, moved.rml_files_rewritten));
    }

    if (request.command == "vespera_repair_asset_fallbacks") {
        if (editor_is_playing(state)) return fail("asset authoring is disabled during Play Mode");
        if (!state.project_loaded) return fail("no Vespera project is open");
        auto report = vespera::repair_stable_asset_fallbacks(state.project, state.asset_catalog);
        const bool persist = automation_arg(request, "save_project")
            ? automation_bool(*automation_arg(request, "save_project")).value_or(true) : true;
        if (persist && report.project_references_repaired != 0) {
            const auto saved = vespera::save_vespera_project(state.project, state.project_path);
            if (!saved) return fail(saved.message);
        }
        if (report.descriptor_files_rewritten != 0) refresh_asset_catalog(state, false, false);
        append_command_audit(state, vespera::editor::EditorCommandKind::RepairAssetFallbacks,
            "Automation repair stable fallbacks", report.warnings.empty(), state.current_state_id, state.current_state_id);
        std::string warning_json = "[";
        for (std::size_t i = 0; i < report.warnings.size(); ++i) {
            if (i != 0) warning_json += ',';
            warning_json += "\"" + automation_json_escape(report.warnings[i]) + "\"";
        }
        warning_json += "]";
        return ok(std::format("{{\"project_references\":{},\"descriptor_references\":{},\"files_rewritten\":{},\"warnings\":{},\"warning_messages\":{}}}",
            report.project_references_repaired, report.descriptor_references_repaired,
            report.descriptor_files_rewritten, report.warnings.size(), warning_json));
    }

    if (request.command == "vespera_create_ui_document") {
        if (editor_is_playing(state)) return fail("UI asset authoring is disabled during Play Mode");
        if (state.assets_root.empty()) return fail("no project Assets root is open");
        const auto* raw_path = automation_arg(request, "path");
        if (!raw_path || raw_path->empty()) return fail("path is required");
        const std::filesystem::path relative = std::filesystem::path(*raw_path).lexically_normal();
        if (relative.is_absolute() || relative.extension() != ".slui" || relative.generic_string().starts_with("..")) {
            return fail("path must be a project-relative .slui path inside Assets");
        }
        const auto absolute = (state.assets_root / relative).lexically_normal();
        std::error_code ec;
        if (std::filesystem::exists(absolute, ec)) return fail("UI document path already exists");
        std::filesystem::create_directories(absolute.parent_path(), ec);
        if (ec) return fail("could not create UI document folder: " + ec.message());
        vespera::UiDocument document;
        auto& canvas = document.create_node(vespera::UiNodeType::Canvas,
            automation_arg(request, "name") && !automation_arg(request, "name")->empty()
                ? *automation_arg(request, "name") : "Canvas");
        canvas.rect.anchor_max = {1.0f, 1.0f};
        const auto saved = vespera::save_ui_document(document, absolute);
        if (!saved) return fail(saved.message);
        refresh_asset_catalog(state, false, true);
        const auto* record = state.asset_catalog.find(relative.generic_string());
        append_command_audit(state, vespera::editor::EditorCommandKind::CreateUiDocument,
            "Automation create UI document", record != nullptr, state.current_state_id, state.current_state_id,
            vespera::kInvalidSceneObjectId, record ? record->asset_id : std::string{});
        if (!record) return fail("UI document was written but catalog refresh did not register it");
        return ok(std::format("{{\"asset_id\":\"{}\",\"path\":\"{}\",\"canvas_id\":{}}}",
            automation_json_escape(record->asset_id), automation_json_escape(record->relative_path.generic_string()), canvas.id));
    }

    if (request.command == "vespera_get_ui_document") {
        const auto* record = automation_asset_record(state, request);
        if (!record) return fail("provide a valid UI asset_id or project-relative path");
        if (record->kind != vespera::AssetKind::UiDocument) return fail("asset is not a UI Document");
        vespera::UiDocument document;
        const auto loaded = vespera::load_ui_document(document, record->absolute_path);
        if (!loaded) return fail(loaded.message);
        const auto layout = vespera::resolve_ui_layout(document, 1280.0f, 720.0f);
        std::string nodes = "[";
        bool first = true;
        for (const auto& node : document.nodes()) {
            if (!first) nodes += ','; first = false;
            const auto* resolved = layout.find(node.id);
            nodes += std::format("{{\"id\":{},\"parent_id\":{},\"type\":\"{}\",\"name\":\"{}\",\"enabled\":{},\"z_order\":{}",
                node.id, node.parent_id, vespera::ui_node_type_name(node.type), automation_json_escape(node.name),
                node.enabled ? "true" : "false", node.z_order);
            if (resolved) nodes += std::format(",\"rect\":[{},{},{},{}]", resolved->rect.x, resolved->rect.y, resolved->rect.width, resolved->rect.height);
            if (node.type == vespera::UiNodeType::Text || node.type == vespera::UiNodeType::Button || node.type == vespera::UiNodeType::TextInput)
                nodes += std::format(",\"text\":\"{}\"", automation_json_escape(node.text.text));
            if (node.type == vespera::UiNodeType::ProgressBar)
                nodes += std::format(",\"value\":{},\"minimum\":{},\"maximum\":{}", node.progress.value, node.progress.minimum, node.progress.maximum);
            nodes += std::format(",\"opacity\":{},\"corner_radius\":{},\"border_width\":{}",
                node.surface.opacity, node.surface.corner_radius, node.surface.border_width);
            if (node.type == vespera::UiNodeType::Text || node.type == vespera::UiNodeType::Button || node.type == vespera::UiNodeType::TextInput
                || node.type == vespera::UiNodeType::ProgressBar || node.type == vespera::UiNodeType::Modal || node.type == vespera::UiNodeType::Tooltip)
                nodes += std::format(",\"font_family\":\"{}\",\"font_weight\":{},\"wrap\":{}", automation_json_escape(node.text.font_family), node.text.font_weight, node.text.wrap ? "true" : "false");
            if (node.layout.mode != vespera::UiLayoutMode::None)
                nodes += std::format(",\"layout_mode\":{}", static_cast<int>(node.layout.mode));
            nodes += "}";
        }
        nodes += "]";
        return ok(std::format("{{\"asset_id\":\"{}\",\"path\":\"{}\",\"node_count\":{},\"layout_warnings\":{},\"nodes\":{}}}",
            automation_json_escape(record->asset_id), automation_json_escape(record->relative_path.generic_string()),
            document.nodes().size(), layout.warnings.size(), nodes));
    }

    if (request.command == "vespera_add_ui_node") {
        if (editor_is_playing(state)) return fail("UI asset authoring is disabled during Play Mode");
        const auto* record = automation_asset_record(state, request);
        if (!record || record->kind != vespera::AssetKind::UiDocument) return fail("provide a valid UI Document asset");
        const auto* type_text = automation_arg(request, "type");
        if (!type_text) return fail("type is required");
        const auto type = automation_ui_node_type(*type_text);
        if (!type) return fail("type is not a supported Vespera UI node type");
        vespera::UiDocument document;
        const auto loaded = vespera::load_ui_document(document, record->absolute_path);
        if (!loaded) return fail(loaded.message);
        auto& node = document.create_node(*type, automation_arg(request, "name") ? *automation_arg(request, "name") : std::string{});
        if (const auto* raw_parent = automation_arg(request, "parent_id")) {
            const auto parent = automation_u64(*raw_parent);
            if (!parent) return fail("parent_id must be an integer");
            std::string reparent_error;
            if (!document.reparent(node.id, *parent, &reparent_error)) return fail(reparent_error);
        }
        const auto node_id = node.id;
        const std::string asset_id = record->asset_id;
        const auto saved = vespera::save_ui_document(document, record->absolute_path);
        if (!saved) return fail(saved.message);
        refresh_asset_catalog(state, false, true);
        append_command_audit(state, vespera::editor::EditorCommandKind::AddUiNode,
            "Automation add UI node", true, state.current_state_id, state.current_state_id,
            vespera::kInvalidSceneObjectId, asset_id);
        return ok(std::format("{{\"asset_id\":\"{}\",\"node_id\":{},\"type\":\"{}\"}}",
            automation_json_escape(asset_id), node_id, vespera::ui_node_type_name(*type)));
    }

    if (request.command == "vespera_set_ui_node") {
        if (editor_is_playing(state)) return fail("UI asset authoring is disabled during Play Mode");
        const auto* record = automation_asset_record(state, request);
        if (!record || record->kind != vespera::AssetKind::UiDocument) return fail("provide a valid UI Document asset");
        const auto* raw_node = automation_arg(request, "node_id");
        const auto node_id = raw_node ? automation_u64(*raw_node) : std::nullopt;
        if (!node_id) return fail("node_id is required");
        vespera::UiDocument document;
        const auto loaded = vespera::load_ui_document(document, record->absolute_path);
        if (!loaded) return fail(loaded.message);
        auto* node = document.find(*node_id);
        if (!node) return fail("UI node does not exist");
        if (const auto* value = automation_arg(request, "name")) node->name = *value;
        if (const auto* value = automation_arg(request, "enabled")) {
            const auto parsed = automation_bool(*value); if (!parsed) return fail("enabled must be true or false"); node->enabled = *parsed;
        }
        if (const auto* value = automation_arg(request, "z_order")) {
            const auto parsed = automation_int(*value); if (!parsed) return fail("z_order must be an integer"); node->z_order = *parsed;
        }
        if (const auto* value = automation_arg(request, "parent_id")) {
            const auto parsed = automation_u64(*value); if (!parsed) return fail("parent_id must be an integer");
            std::string error; if (!document.reparent(node->id, *parsed, &error)) return fail(error);
            node = document.find(*node_id);
        }
        const auto set_vec2 = [&](std::string_view key, vespera::UiVec2& target) -> std::optional<std::string> {
            const auto* raw = automation_arg(request, key); if (!raw) return std::nullopt;
            const auto parsed = automation_vec2(*raw); if (!parsed) return std::string(key) + " must contain two numbers";
            target = {parsed->x, parsed->z}; return std::nullopt;
        };
        if (const auto error = set_vec2("anchor_min", node->rect.anchor_min)) return fail(*error);
        if (const auto error = set_vec2("anchor_max", node->rect.anchor_max)) return fail(*error);
        if (const auto error = set_vec2("offset_min", node->rect.offset_min)) return fail(*error);
        if (const auto error = set_vec2("offset_max", node->rect.offset_max)) return fail(*error);
        if (const auto* value = automation_arg(request, "text")) node->text.text = *value;
        if (const auto* value = automation_arg(request, "font_size")) {
            const auto parsed = automation_float(*value); if (!parsed || *parsed < 1.0f) return fail("font_size must be >= 1"); node->text.font_size = *parsed;
        }
        if (const auto* value = automation_arg(request, "font_family")) {
            if (value->empty()) return fail("font_family must not be empty");
            node->text.font_family = *value;
        }
        if (const auto* value = automation_arg(request, "font_weight")) {
            const auto parsed = automation_int(*value); if (!parsed || *parsed < 100 || *parsed > 900) return fail("font_weight must be between 100 and 900");
            node->text.font_weight = static_cast<std::uint16_t>(*parsed);
        }
        if (const auto* value = automation_arg(request, "italic")) {
            const auto parsed = automation_bool(*value); if (!parsed) return fail("italic must be true or false"); node->text.italic = *parsed;
        }
        if (const auto* value = automation_arg(request, "wrap")) {
            const auto parsed = automation_bool(*value); if (!parsed) return fail("wrap must be true or false"); node->text.wrap = *parsed;
        }
        if (const auto* value = automation_arg(request, "line_spacing")) {
            const auto parsed = automation_float(*value); if (!parsed || *parsed < 0.5f || *parsed > 4.0f) return fail("line_spacing must be between 0.5 and 4.0"); node->text.line_spacing = *parsed;
        }
        if (const auto* value = automation_arg(request, "horizontal_alignment")) {
            const auto parsed = automation_ui_horizontal_alignment(*value); if (!parsed) return fail("horizontal_alignment must be left, center or right"); node->text.horizontal_alignment = *parsed;
        }
        if (const auto* value = automation_arg(request, "vertical_alignment")) {
            const auto parsed = automation_ui_vertical_alignment(*value); if (!parsed) return fail("vertical_alignment must be top, middle or bottom"); node->text.vertical_alignment = *parsed;
        }
        if (automation_arg(request, "image_asset_id") || automation_arg(request, "image_path")) {
            const std::string id = automation_arg(request, "image_asset_id") ? *automation_arg(request, "image_asset_id") : std::string{};
            const std::string path = automation_arg(request, "image_path") ? *automation_arg(request, "image_path") : std::string{};
            vespera::AssetReference image{id, path};
            if (!image.empty()) {
                const auto resolved = state.asset_catalog.resolve_reference(image);
                if (!resolved || resolved.record->kind != vespera::AssetKind::Texture) return fail("UI image reference must resolve to a Texture asset");
                image = {resolved.record->asset_id, resolved.record->relative_path};
            }
            node->visual.image = std::move(image);
        }
        if (automation_arg(request, "font_asset_id") || automation_arg(request, "font_path")) {
            const std::string id = automation_arg(request, "font_asset_id") ? *automation_arg(request, "font_asset_id") : std::string{};
            const std::string path = automation_arg(request, "font_path") ? *automation_arg(request, "font_path") : std::string{};
            vespera::AssetReference font{id, path};
            if (!font.empty()) {
                const auto resolved = state.asset_catalog.resolve_reference(font);
                if (!resolved || resolved.record->kind != vespera::AssetKind::Font) return fail("UI font reference must resolve to a Font asset");
                font = {resolved.record->asset_id, resolved.record->relative_path};
            }
            node->text.font = std::move(font);
        }
        if (const auto* value = automation_arg(request, "opacity")) {
            const auto parsed = automation_float(*value); if (!parsed || *parsed < 0.0f || *parsed > 1.0f) return fail("opacity must be between 0 and 1"); node->surface.opacity = *parsed;
        }
        if (const auto* value = automation_arg(request, "corner_radius")) {
            const auto parsed = automation_float(*value); if (!parsed || *parsed < 0.0f) return fail("corner_radius must be >= 0"); node->surface.corner_radius = *parsed;
        }
        if (const auto* value = automation_arg(request, "border_width")) {
            const auto parsed = automation_float(*value); if (!parsed || *parsed < 0.0f) return fail("border_width must be >= 0"); node->surface.border_width = *parsed;
        }
        if (const auto error = set_vec2("shadow_offset", node->surface.shadow_offset)) return fail(*error);
        if (const auto* value = automation_arg(request, "shadow_softness")) {
            const auto parsed = automation_float(*value); if (!parsed || *parsed < 0.0f || *parsed > 32.0f) return fail("shadow_softness must be between 0 and 32"); node->surface.shadow_softness = *parsed;
        }
        if (const auto error = set_vec2("text_shadow_offset", node->text.shadow_offset)) return fail(*error);
        if (const auto* value = automation_arg(request, "image_fit")) {
            const auto parsed = automation_ui_image_fit(*value); if (!parsed) return fail("image_fit must be stretch, contain or cover"); node->surface.image_fit = *parsed;
        }
        if (const auto* value = automation_arg(request, "nine_slice")) {
            const auto parsed = automation_color4(*value); if (!parsed) return fail("nine_slice must contain four numbers");
            if ((*parsed)[0] < 0 || (*parsed)[1] < 0 || (*parsed)[2] < 0 || (*parsed)[3] < 0) return fail("nine_slice values must be >= 0");
            node->surface.nine_slice = {(*parsed)[0],(*parsed)[1],(*parsed)[2],(*parsed)[3]};
        }
        if (const auto* value = automation_arg(request, "interactable")) {
            const auto parsed = automation_bool(*value); if (!parsed) return fail("interactable must be true or false"); node->button.interactable = *parsed;
        }
        if (const auto* value = automation_arg(request, "progress_value")) {
            const auto parsed = automation_float(*value); if (!parsed) return fail("progress_value must be a number"); node->progress.value = *parsed;
        }
        if (const auto* value = automation_arg(request, "progress_min")) {
            const auto parsed = automation_float(*value); if (!parsed) return fail("progress_min must be a number"); node->progress.minimum = *parsed;
        }
        if (const auto* value = automation_arg(request, "progress_max")) {
            const auto parsed = automation_float(*value); if (!parsed) return fail("progress_max must be a number"); node->progress.maximum = *parsed;
        }
        if (node->progress.maximum < node->progress.minimum) std::swap(node->progress.minimum, node->progress.maximum);
        node->progress.value = std::clamp(node->progress.value, node->progress.minimum, node->progress.maximum);
        if (const auto* value = automation_arg(request, "layout_mode")) {
            const auto parsed = automation_ui_layout_mode(*value); if (!parsed) return fail("layout_mode must be none, horizontal, vertical or grid"); node->layout.mode = *parsed;
        }
        if (const auto error = set_vec2("layout_spacing", node->layout.spacing)) return fail(*error);
        if (const auto error = set_vec2("cell_size", node->layout.cell_size)) return fail(*error);
        if (const auto* value = automation_arg(request, "columns")) {
            const auto parsed = automation_int(*value); if (!parsed || *parsed < 1) return fail("columns must be >= 1"); node->layout.columns = static_cast<std::uint32_t>(*parsed);
        }
        if (const auto* value = automation_arg(request, "clip_children")) {
            const auto parsed = automation_bool(*value); if (!parsed) return fail("clip_children must be true or false"); node->layout.clip_children = *parsed;
        }
        if (const auto error = set_vec2("scroll_offset", node->scroll.offset)) return fail(*error);
        if (const auto* value = automation_arg(request, "active_tab")) {
            const auto parsed = automation_int(*value); if (!parsed || *parsed < 0) return fail("active_tab must be >= 0"); node->tabs.active_index = static_cast<std::uint32_t>(*parsed);
        }
        if (const auto* value = automation_arg(request, "placeholder")) node->input.placeholder = *value;
        if (const auto* value = automation_arg(request, "max_length")) {
            const auto parsed = automation_int(*value); if (!parsed || *parsed < 1) return fail("max_length must be >= 1"); node->input.max_length = static_cast<std::uint32_t>(*parsed);
        }
        if (const auto* value = automation_arg(request, "read_only")) {
            const auto parsed = automation_bool(*value); if (!parsed) return fail("read_only must be true or false"); node->input.read_only = *parsed;
        }
        const auto set_color = [&](std::string_view key, std::array<float,4>& target) -> std::optional<std::string> {
            const auto* raw = automation_arg(request, key); if (!raw) return std::nullopt;
            const auto parsed = automation_color4(*raw); if (!parsed) return std::string(key) + " must contain four numbers";
            target = *parsed; return std::nullopt;
        };
        if (const auto error = set_color("color", node->visual.color)) return fail(*error);
        if (const auto error = set_color("text_color", node->text.color)) return fail(*error);
        if (const auto error = set_color("border_color", node->surface.border_color)) return fail(*error);
        if (const auto error = set_color("shadow_color", node->surface.shadow_color)) return fail(*error);
        if (const auto error = set_color("text_shadow_color", node->text.shadow_color)) return fail(*error);
        if (const auto error = set_color("button_normal_color", node->button.normal_color)) return fail(*error);
        if (const auto error = set_color("button_hovered_color", node->button.hovered_color)) return fail(*error);
        if (const auto error = set_color("button_pressed_color", node->button.pressed_color)) return fail(*error);
        if (const auto error = set_color("button_disabled_color", node->button.disabled_color)) return fail(*error);
        if (const auto error = set_color("progress_fill_color", node->progress.fill_color)) return fail(*error);
        if (const auto error = set_color("progress_background_color", node->progress.background_color)) return fail(*error);
        const std::string asset_id = record->asset_id;
        const auto saved = vespera::save_ui_document(document, record->absolute_path);
        if (!saved) return fail(saved.message);
        refresh_asset_catalog(state, false, true);
        append_command_audit(state, vespera::editor::EditorCommandKind::SetUiNode,
            "Automation set UI node", true, state.current_state_id, state.current_state_id,
            vespera::kInvalidSceneObjectId, asset_id);
        return ok(std::format("{{\"asset_id\":\"{}\",\"node_id\":{}}}", automation_json_escape(asset_id), *node_id));
    }

    if (request.command == "vespera_delete_ui_node") {
        if (editor_is_playing(state)) return fail("UI asset authoring is disabled during Play Mode");
        const auto* record = automation_asset_record(state, request);
        if (!record || record->kind != vespera::AssetKind::UiDocument) return fail("provide a valid UI Document asset");
        const auto* raw_node = automation_arg(request, "node_id");
        const auto node_id = raw_node ? automation_u64(*raw_node) : std::nullopt;
        if (!node_id) return fail("node_id is required");
        vespera::UiDocument document;
        const auto loaded = vespera::load_ui_document(document, record->absolute_path);
        if (!loaded) return fail(loaded.message);
        const auto* node = document.find(*node_id);
        if (!node) return fail("UI node does not exist");
        if (node->type == vespera::UiNodeType::Canvas) return fail("Canvas deletion is blocked; a UI document must retain a Canvas root");
        if (!document.destroy_node(*node_id)) return fail("UI node could not be deleted");
        const std::string asset_id = record->asset_id;
        const auto saved = vespera::save_ui_document(document, record->absolute_path);
        if (!saved) return fail(saved.message);
        refresh_asset_catalog(state, false, true);
        append_command_audit(state, vespera::editor::EditorCommandKind::DeleteUiNode,
            "Automation delete UI node", true, state.current_state_id, state.current_state_id,
            vespera::kInvalidSceneObjectId, asset_id);
        return ok(std::format("{{\"asset_id\":\"{}\",\"deleted_node_id\":{},\"remaining_nodes\":{}}}",
            automation_json_escape(asset_id), *node_id, document.nodes().size()));
    }

    if (request.command == "vespera_reparent_ui_node") {
        if (editor_is_playing(state)) return fail("UI asset authoring is disabled during Play Mode");
        const auto* record = automation_asset_record(state, request);
        if (!record || record->kind != vespera::AssetKind::UiDocument) return fail("provide a valid UI Document asset");
        const auto* raw_node = automation_arg(request, "node_id");
        const auto* raw_parent = automation_arg(request, "parent_id");
        const auto node_id = raw_node ? automation_u64(*raw_node) : std::nullopt;
        const auto parent_id = raw_parent ? automation_u64(*raw_parent) : std::nullopt;
        if (!node_id || !parent_id) return fail("node_id and parent_id are required integers; use parent_id=0 to unparent");
        vespera::UiDocument document;
        const auto loaded = vespera::load_ui_document(document, record->absolute_path);
        if (!loaded) return fail(loaded.message);
        std::string reparent_error;
        if (!document.reparent(*node_id, *parent_id, &reparent_error)) return fail(reparent_error);
        const std::string asset_id = record->asset_id;
        const auto saved = vespera::save_ui_document(document, record->absolute_path);
        if (!saved) return fail(saved.message);
        refresh_asset_catalog(state, false, true);
        append_command_audit(state, vespera::editor::EditorCommandKind::ReparentUiNode,
            "Automation reparent UI node", true, state.current_state_id, state.current_state_id,
            vespera::kInvalidSceneObjectId, asset_id);
        return ok(std::format("{{\"asset_id\":\"{}\",\"node_id\":{},\"parent_id\":{}}}",
            automation_json_escape(asset_id), *node_id, *parent_id));
    }

    if (request.command == "vespera_validate_ui_document") {
        const auto* record = automation_asset_record(state, request);
        if (!record || record->kind != vespera::AssetKind::UiDocument) return fail("provide a valid UI Document asset");
        vespera::UiDocument document;
        const auto loaded = vespera::load_ui_document(document, record->absolute_path);
        if (!loaded) return fail(loaded.message);
        std::size_t canvas_roots = 0;
        std::size_t focusable = 0;
        for (const auto& node : document.nodes()) {
            if (node.type == vespera::UiNodeType::Canvas && node.parent_id == vespera::kInvalidUiNodeId) ++canvas_roots;
            if ((node.type == vespera::UiNodeType::Button && node.button.interactable) ||
                (node.type == vespera::UiNodeType::TextInput && !node.input.read_only)) ++focusable;
        }
        const auto layout720 = vespera::resolve_ui_layout(document, 1280.0f, 720.0f);
        const auto layout1080 = vespera::resolve_ui_layout(document, 1920.0f, 1080.0f);
        const std::size_t warnings = layout720.warnings.size() + layout1080.warnings.size();
        return ok(std::format("{{\"asset_id\":\"{}\",\"nodes\":{},\"canvas_roots\":{},\"focusable\":{},\"warnings_720p\":{},\"warnings_1080p\":{},\"valid\":{}}}",
            automation_json_escape(record->asset_id), document.nodes().size(), canvas_roots, focusable,
            layout720.warnings.size(), layout1080.warnings.size(), (canvas_roots == 1 && warnings == 0) ? "true" : "false"));
    }

    if (request.command == "vespera_get_ui_layout") {
        const auto* record = automation_asset_record(state, request);
        if (!record || record->kind != vespera::AssetKind::UiDocument) return fail("provide a valid UI Document asset");
        int width = 1280;
        int height = 720;
        if (const auto* raw = automation_arg(request, "width")) {
            const auto parsed = automation_int(*raw); if (!parsed || *parsed < 1 || *parsed > 16384) return fail("width must be 1..16384"); width = *parsed;
        }
        if (const auto* raw = automation_arg(request, "height")) {
            const auto parsed = automation_int(*raw); if (!parsed || *parsed < 1 || *parsed > 16384) return fail("height must be 1..16384"); height = *parsed;
        }
        vespera::UiDocument document;
        const auto loaded = vespera::load_ui_document(document, record->absolute_path);
        if (!loaded) return fail(loaded.message);
        const auto layout = vespera::resolve_ui_layout(document, static_cast<float>(width), static_cast<float>(height));
        std::string nodes = "[";
        bool first = true;
        for (const auto& resolved : layout.nodes) {
            if (!first) nodes += ',';
            first = false;
            nodes += std::format("{{\"id\":{},\"type\":\"{}\",\"enabled\":{},\"rect\":[{},{},{},{}],\"clip_enabled\":{},\"clip\":[{},{},{},{}]}}",
                resolved.id, automation_json_escape(vespera::ui_node_type_name(resolved.type)), resolved.enabled ? "true" : "false",
                resolved.rect.x, resolved.rect.y, resolved.rect.width, resolved.rect.height,
                resolved.clip_enabled ? "true" : "false", resolved.clip_rect.x, resolved.clip_rect.y,
                resolved.clip_rect.width, resolved.clip_rect.height);
        }
        nodes += "]";
        return ok(std::format("{{\"asset_id\":\"{}\",\"viewport\":[{},{}],\"canvas_scale\":{},\"warnings\":{},\"nodes\":{}}}",
            automation_json_escape(record->asset_id), width, height, layout.canvas_scale, layout.warnings.size(), nodes));
    }

    if (request.command == "vespera_set_entity_metadata") {
        if (editor_is_playing(state)) return fail("scene authoring is disabled during Play Mode");
        const auto* raw_id = automation_arg(request, "entity_id");
        const auto id = raw_id ? automation_u64(*raw_id) : std::nullopt;
        if (!id) return fail("entity_id is required and must be an unsigned integer");
        if (!state.scene.find_entity(*id)) return fail("entity_id does not exist in the edit scene");
        const auto* name = automation_arg(request, "name");
        const auto* tag = automation_arg(request, "tag");
        const auto* layer = automation_arg(request, "layer");
        const auto* raw_enabled = automation_arg(request, "enabled");
        const auto enabled = raw_enabled ? automation_bool(*raw_enabled) : std::optional<bool>{};
        if (raw_enabled && !enabled) return fail("enabled must be true or false");
        if (!name && !tag && !layer && !raw_enabled) return fail("supply at least one of name, tag, layer or enabled");
        const bool changed = execute_editor_command(state, vespera::editor::EditorCommandKind::SetEntityMetadata,
            "Automation set entity metadata", [&]() {
                auto* entity = state.scene.find_entity(*id);
                if (!entity) return false;
                if (name) entity->name = *name;
                if (tag) entity->tag = *tag;
                if (layer) entity->layer = *layer;
                if (enabled) entity->enabled = *enabled;
                return true;
            });
        if (!changed) return fail("no valid metadata field was supplied or the edit failed");
        if (!state.command_log.empty()) state.command_log.back().target_entity_id = *id;
        const auto* entity = state.scene.find_entity(*id);
        return ok(std::format("{{\"entity_id\":{},\"name\":\"{}\",\"tag\":\"{}\",\"layer\":\"{}\",\"enabled\":{}}}",
            *id, automation_json_escape(entity->name), automation_json_escape(entity->tag), automation_json_escape(entity->layer), entity->enabled ? "true" : "false"));
    }

    if (request.command == "vespera_reorder_entity") {
        if (editor_is_playing(state)) return fail("scene authoring is disabled during Play Mode");
        const auto id = automation_arg(request, "entity_id") ? automation_u64(*automation_arg(request, "entity_id")) : std::nullopt;
        const auto before = automation_arg(request, "before_entity_id") ? automation_u64(*automation_arg(request, "before_entity_id")) : std::nullopt;
        if (!id || !before) return fail("entity_id and before_entity_id are required unsigned integers");
        if (!state.scene.find_entity(*id) || !state.scene.find_entity(*before)) return fail("entity_id or before_entity_id does not exist");
        if (!command_reorder_entity(state, *id, *before)) return fail("reorder command failed or would not change order");
        if (!state.command_log.empty()) state.command_log.back().target_entity_id = *id;
        return ok(std::format("{{\"entity_id\":{},\"before_entity_id\":{}}}", *id, *before));
    }

    if (request.command == "vespera_get_asset_dependencies") {
        const auto* record = automation_asset_record(state, request);
        if (!record) return fail("provide a valid asset_id or project-relative asset path");
        std::string dependencies = "["; bool first = true;
        for (const auto* dep : state.asset_catalog.dependencies_of(record->asset_id)) {
            if (!dep) continue; if (!first) dependencies += ','; first = false;
            dependencies += std::format("{{\"target_asset_id\":\"{}\",\"reference\":\"{}\",\"reason\":\"{}\",\"resolved\":{},\"stable\":{},\"stale_fallback\":{}}}",
                automation_json_escape(dep->target_asset_id), automation_json_escape(dep->reference), automation_json_escape(dep->reason), dep->resolved ? "true" : "false", dep->requested_asset_id.empty() ? "false" : "true", dep->stale_fallback_path ? "true" : "false");
        }
        dependencies += "]";
        std::string dependents = "["; first = true;
        for (const auto* dep : state.asset_catalog.dependents_of(record->asset_id)) {
            if (!dep) continue; if (!first) dependents += ','; first = false;
            dependents += std::format("{{\"source_asset_id\":\"{}\",\"reference\":\"{}\",\"reason\":\"{}\",\"resolved\":{},\"stable\":{},\"stale_fallback\":{}}}",
                automation_json_escape(dep->source_asset_id), automation_json_escape(dep->reference), automation_json_escape(dep->reason), dep->resolved ? "true" : "false", dep->requested_asset_id.empty() ? "false" : "true", dep->stale_fallback_path ? "true" : "false");
        }
        dependents += "]";
        return ok(std::format("{{\"asset_id\":\"{}\",\"path\":\"{}\",\"dependencies\":{},\"dependents\":{}}}", automation_json_escape(record->asset_id), automation_json_escape(record->relative_path.generic_string()), dependencies, dependents));
    }

    if (request.command == "vespera_check_project_integrity") {
        if (!state.project_loaded) return fail("no Vespera project is open");
        const auto project_issues = vespera::validate_vespera_project(state.project);
        std::size_t project_errors = 0, project_warnings = 0;
        for (const auto& issue : project_issues) { if (issue.severity == vespera::ProjectValidationSeverity::Error) ++project_errors; else ++project_warnings; }
        const auto scene_issues = vespera::validate_scene(state.scene);
        std::size_t scene_errors = 0, scene_warnings = 0;
        for (const auto& issue : scene_issues) { if (issue.severity == vespera::SceneValidationSeverity::Error) ++scene_errors; else ++scene_warnings; }
        const auto build = vespera::build_project_asset_manifest(state.project, state.asset_catalog);
        std::size_t ui_documents = 0, ui_invalid = 0, ui_warnings = 0;
        for (const auto* asset : state.asset_catalog.records_of_kind(vespera::AssetKind::UiDocument)) {
            if (!asset) continue; ++ui_documents;
            vespera::UiDocument document; const auto loaded = vespera::load_ui_document(document, asset->absolute_path);
            if (!loaded) { ++ui_invalid; continue; }
            const auto a = vespera::resolve_ui_layout(document, 1280.0f, 720.0f); const auto b = vespera::resolve_ui_layout(document, 1920.0f, 1080.0f);
            ui_warnings += a.warnings.size() + b.warnings.size();
            std::size_t roots = 0; for (const auto& node : document.nodes()) if (node.type == vespera::UiNodeType::Canvas && node.parent_id == vespera::kInvalidUiNodeId) ++roots;
            if (roots != 1) ++ui_invalid;
        }
        const std::size_t broken = state.asset_catalog.broken_dependencies().size(); std::size_t stale = 0;
        for (const auto& dep : state.asset_catalog.dependencies()) if (dep.stale_fallback_path) ++stale;
        const bool clean = project_errors == 0 && scene_errors == 0 && broken == 0 && stale == 0 && build.valid() && ui_invalid == 0 && ui_warnings == 0;
        const auto report = std::format("{{\"clean\":{},\"project_errors\":{},\"project_warnings\":{},\"scene_errors\":{},\"scene_warnings\":{},\"broken_dependencies\":{},\"stale_fallbacks\":{},\"build_valid\":{},\"build_assets\":{},\"build_missing_roots\":{},\"build_broken\":{},\"ui_documents\":{},\"ui_invalid\":{},\"ui_layout_warnings\":{},\"managed_metadata_loaded\":{}}}",
            clean ? "true" : "false", project_errors, project_warnings, scene_errors, scene_warnings, broken, stale, build.valid() ? "true" : "false", build.assets.size(), build.missing_roots.size(), build.broken_dependencies.size(), ui_documents, ui_invalid, ui_warnings, state.managed_metadata.loaded ? "true" : "false");
        return clean ? ok(report) : fail(report);
    }

    if (request.command == "vespera_read_csharp_source") {
        const auto* raw_path = automation_arg(request, "path"); std::string path_error;
        const auto path = raw_path ? automation_managed_source_path(state, *raw_path, &path_error) : std::nullopt;
        if (!path) return fail(path_error.empty() ? "path is required" : path_error);
        std::ifstream in(*path, std::ios::binary); if (!in) return fail("managed source file does not exist or could not be opened");
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (content.size() > 1024 * 1024) return fail("managed source is larger than the 1 MiB automation read limit");
        return ok(std::format("{{\"path\":\"{}\",\"bytes\":{},\"content\":\"{}\"}}", automation_json_escape(std::filesystem::relative(*path, state.project.managed_project_path().parent_path()).generic_string()), content.size(), automation_json_escape(content)));
    }

    if (request.command == "vespera_write_csharp_source") {
        const auto* raw_path = automation_arg(request, "path"); const auto* content = automation_arg(request, "content");
        if (!raw_path || !content) return fail("path and content are required");
        std::string path_error; const auto path = automation_managed_source_path(state, *raw_path, &path_error); if (!path) return fail(path_error);
        std::string write_error; const bool wrote = automation_guarded_replace_text(*path, *content, &write_error);
        append_command_audit(state, vespera::editor::EditorCommandKind::WriteManagedSource, "Automation write C# source", wrote, state.current_state_id, state.current_state_id);
        if (!wrote) return fail(write_error);
        return ok(std::format("{{\"path\":\"{}\",\"bytes\":{}}}", automation_json_escape(*raw_path), content->size()));
    }

    if (request.command == "vespera_attach_csharp_script") {
        if (editor_is_playing(state)) return fail("scene authoring is disabled during Play Mode");
        const auto id = automation_arg(request, "entity_id") ? automation_u64(*automation_arg(request, "entity_id")) : std::nullopt;
        const auto* class_name = automation_arg(request, "class_name");
        if (!id || !class_name || class_name->empty()) return fail("entity_id and class_name are required");
        if (!state.scene.find_entity(*id)) return fail("entity_id does not exist in the edit scene");
        if (state.managed_metadata.loaded && !state.managed_metadata.find(*class_name)) return fail("class_name is not present in current managed metadata; build C# first");
        const bool attached = execute_editor_command(state, vespera::editor::EditorCommandKind::AttachManagedScript, "Automation attach C# script", [&]() {
            auto* entity = state.scene.find_entity(*id); if (!entity) return false;
            for (const auto& script : entity->managed_scripts) if (script.class_name == *class_name) return false;
            entity->add_managed_script(*class_name); return true;
        });
        if (!attached) return fail("script attachment failed or class is already attached");
        if (!state.command_log.empty()) state.command_log.back().target_entity_id = *id;
        return ok(std::format("{{\"entity_id\":{},\"class_name\":\"{}\"}}", *id, automation_json_escape(*class_name)));
    }

    if (request.command == "vespera_run_qa_scenario") {
        if (!state.project_loaded) return fail("no Vespera project is open");
        const std::string scenario = automation_arg(request, "scenario") ? *automation_arg(request, "scenario") : std::string{};
        std::size_t count = 24;
        if (const auto* raw = automation_arg(request, "count")) { const auto parsed = automation_u64(*raw); if (!parsed || *parsed < 2 || *parsed > 128) return fail("count must be 2..128"); count = static_cast<std::size_t>(*parsed); }
        const bool cleanup = automation_arg(request, "cleanup") ? automation_bool(*automation_arg(request, "cleanup")).value_or(true) : true;
        const auto before_entities = state.scene.entities.size(); const auto before_undo = state.undo_stack.size(); bool passed = true; std::string detail;
        if (scenario == "hierarchy") {
            if (editor_is_playing(state)) return fail("hierarchy QA requires edit mode");
            std::vector<vespera::SceneObjectId> ids; ids.reserve(count);
            for (std::size_t i = 0; i < count; ++i) {
                if (!command_create_entity(state, std::format("QA Node {:03}", i))) { passed = false; detail = "entity creation failed"; break; }
                const auto selected = selected_entity_index(state); if (!selected) { passed = false; detail = "created entity selection missing"; break; }
                ids.push_back(state.scene.entities[*selected].id);
                if (i > 0 && !command_reparent_entity(state, ids[i], ids[i - 1])) { passed = false; detail = "deep reparent failed"; break; }
            }
            if (passed && ids.size() >= 2 && command_reparent_entity(state, ids.front(), ids.back())) { passed = false; detail = "hierarchy cycle was incorrectly accepted"; }
            if (passed && ids.size() >= 4) {
                const auto middle = ids[ids.size()/2]; const auto index = entity_index_from_id(state, middle);
                if (!index) { passed = false; detail = "middle hierarchy node missing"; }
                else { select_entity(state, *index); if (!command_delete_selected_entity(state)) { passed = false; detail = "middle hierarchy delete failed"; } else if (!undo(state)) { passed = false; detail = "undo after delete failed"; } else if (!redo(state)) { passed = false; detail = "redo after delete failed"; } else if (!undo(state)) { passed = false; detail = "second undo after delete failed"; } }
            }
            for (const auto& issue : vespera::validate_scene(state.scene)) if (issue.severity == vespera::SceneValidationSeverity::Error) passed = false;
            if (cleanup) while (state.undo_stack.size() > before_undo) { if (!undo(state)) { passed = false; detail = "cleanup undo failed"; break; } }
            if (detail.empty()) detail = "deep hierarchy/cycle rejection/delete/undo/redo completed";
        } else if (scenario == "play_cycle") {
            if (editor_is_playing(state)) stop_play_mode(state);
            for (std::size_t i = 0; i < std::min<std::size_t>(count, 64); ++i) {
                start_play_mode(state); if (!editor_is_playing(state)) { passed = false; detail = "Play Mode did not start"; break; }
                toggle_play_pause(state); step_play_mode(state); if (state.play_state == EditorPlayState::Paused) toggle_play_pause(state); stop_play_mode(state);
                if (editor_is_playing(state) || state.scene.entities.size() != before_entities) { passed = false; detail = "Play/Stop did not restore edit scene entity count"; break; }
            }
            if (detail.empty()) detail = "repeated Play/Pause/Step/Stop restoration completed";
        } else if (scenario == "ui_layout") {
            const auto* record = automation_asset_record(state, request); if (!record || record->kind != vespera::AssetKind::UiDocument) return fail("ui_layout scenario requires a valid UI Document asset_id or path");
            vespera::UiDocument document; const auto loaded = vespera::load_ui_document(document, record->absolute_path); if (!loaded) return fail(loaded.message);
            const std::array<std::pair<int,int>,6> viewports{{{320,200},{640,360},{1280,720},{1600,900},{1920,1080},{3440,1440}}}; std::size_t warnings = 0;
            for (const auto [w,h] : viewports) warnings += vespera::resolve_ui_layout(document, static_cast<float>(w), static_cast<float>(h)).warnings.size();
            passed = warnings == 0; detail = std::format("resolved {} UI nodes at {} viewports with {} warning(s)", document.nodes().size(), viewports.size(), warnings);
        } else if (scenario == "managed_build_recovery") {
            if (editor_is_playing(state)) return fail("managed_build_recovery QA requires edit mode");
            const auto* raw_path = automation_arg(request, "path");
            if (!raw_path || raw_path->empty()) return fail("managed_build_recovery requires a managed source path");
            std::string path_error;
            const auto path = automation_managed_source_path(state, *raw_path, &path_error);
            if (!path) return fail(path_error);
            std::ifstream source_in(*path, std::ios::binary);
            if (!source_in) return fail("managed_build_recovery source file could not be opened");
            const std::string original((std::istreambuf_iterator<char>(source_in)), std::istreambuf_iterator<char>());
            source_in.close();
            std::string write_error;
            const std::string broken = original + "\n// Vespera managed-build recovery QA sentinel\nVESPERA_QA_INTENTIONAL_COMPILER_ERROR\n";
            if (!automation_guarded_replace_text(*path, broken, &write_error)) return fail("could not inject managed compiler error: " + write_error);
            const auto failed_build_console = state.console.size();
            build_managed_scripts(state);
            bool expected_failure = false;
            for (std::size_t i = failed_build_console; i < state.console.size(); ++i) {
                if (state.console[i].level == ConsoleEntry::Level::Error) { expected_failure = true; break; }
            }
            write_error.clear();
            if (!automation_guarded_replace_text(*path, original, &write_error)) {
                return fail("managed source restore failed after intentional compiler error: " + write_error);
            }
            const auto restored_build_console = state.console.size();
            build_managed_scripts(state);
            bool restored_failed = false;
            for (std::size_t i = restored_build_console; i < state.console.size(); ++i) {
                if (state.console[i].level == ConsoleEntry::Level::Error) { restored_failed = true; break; }
            }
            passed = expected_failure && !restored_failed;
            detail = std::format("intentional failure observed={} exact restore rebuild succeeded={}",
                expected_failure ? "true" : "false", restored_failed ? "false" : "true");
        } else if (scenario == "asset_move_save_reopen") {
            if (editor_is_playing(state)) return fail("asset_move_save_reopen QA requires edit mode");
            if (!cleanup) return fail("asset_move_save_reopen requires cleanup=true because it mutates a real asset and scene");
            if (state.scene_path.empty()) return fail("asset_move_save_reopen requires an open saved scene");
            const auto* record = automation_asset_record(state, request);
            if (!record || record->kind != vespera::AssetKind::EntityPrefab) {
                return fail("asset_move_save_reopen requires a valid Prefab asset_id or path");
            }
            const std::string prefab_id = record->asset_id;
            const std::filesystem::path original_relative = record->relative_path.lexically_normal();
            const std::filesystem::path original_scene = state.scene_path.lexically_normal();
            const bool scene_references_prefab = std::any_of(state.scene.entities.begin(), state.scene.entities.end(), [&](const auto& entity) {
                return !entity.prefab_source.asset_id.empty() && entity.prefab_source.asset_id == prefab_id;
            });
            if (!scene_references_prefab) return fail("the currently open scene does not reference the selected Prefab by stable ID");

            std::filesystem::path destination = automation_arg(request, "destination")
                ? std::filesystem::path(*automation_arg(request, "destination")).lexically_normal()
                : (original_relative.parent_path() / ("__vespera_qa_move_" + original_relative.filename().string())).lexically_normal();
            if (destination.empty() || destination.is_absolute() || destination.extension() != original_relative.extension()
                || destination == original_relative || destination.generic_string().starts_with("..")) {
                return fail("destination must be a different project-relative Prefab path with the same extension");
            }
            for (const auto& part : destination) if (part == "..") return fail("destination may not escape Assets");
            if (state.asset_catalog.find(destination.generic_string())) return fail("asset_move_save_reopen destination already exists");

            bool moved_away = false;
            auto repair_after_catalog_change = [&]() -> bool {
                auto repair = vespera::repair_stable_asset_fallbacks(state.project, state.asset_catalog);
                if (repair.project_references_repaired != 0) {
                    const auto saved_project = vespera::save_vespera_project(state.project, state.project_path);
                    if (!saved_project) {
                        detail = "project fallback repair save failed: " + saved_project.message;
                        return false;
                    }
                }
                if (repair.descriptor_files_rewritten != 0) refresh_asset_catalog(state, false, false);
                if (!repair.warnings.empty()) {
                    detail = "fallback repair produced warning: " + repair.warnings.front();
                    return false;
                }
                return true;
            };

            const auto moved = vespera::move_project_asset(state.asset_catalog, prefab_id, destination.generic_string());
            if (!moved) {
                passed = false;
                detail = "prefab move failed: " + moved.message;
            } else {
                moved_away = true;
                refresh_asset_catalog(state, false, false);
                if (!repair_after_catalog_change()) passed = false;
            }

            if (passed && !save_scene(state, original_scene)) {
                passed = false;
                detail = "scene save after prefab move failed";
            }
            if (passed) {
                refresh_asset_catalog(state, false, false);
                if (state.last_asset_report.stale_fallback_paths != 0) {
                    passed = false;
                    detail = std::format("scene save reintroduced {} stale fallback path(s)", state.last_asset_report.stale_fallback_paths);
                }
            }
            if (passed && !open_scene(state, original_scene)) {
                passed = false;
                detail = "scene reopen after prefab move failed";
            }
            if (passed) {
                const bool canonicalized = std::any_of(state.scene.entities.begin(), state.scene.entities.end(), [&](const auto& entity) {
                    return entity.prefab_source.asset_id == prefab_id
                        && entity.prefab_source.path.lexically_normal() == destination;
                });
                if (!canonicalized) {
                    passed = false;
                    detail = "reopened scene did not retain the moved Prefab's canonical fallback path";
                }
            }

            // Always attempt to restore the authored asset and scene before reporting.
            if (moved_away) {
                const auto restore = vespera::move_project_asset(state.asset_catalog, prefab_id, original_relative.generic_string());
                if (!restore) {
                    passed = false;
                    detail = detail.empty() ? "cleanup move-back failed: " + restore.message : detail + "; cleanup move-back failed: " + restore.message;
                } else {
                    refresh_asset_catalog(state, false, false);
                    if (!repair_after_catalog_change()) passed = false;
                    if (!save_scene(state, original_scene)) {
                        passed = false;
                        detail = detail.empty() ? "cleanup scene save failed" : detail + "; cleanup scene save failed";
                    }
                    refresh_asset_catalog(state, false, false);
                    if (!open_scene(state, original_scene)) {
                        passed = false;
                        detail = detail.empty() ? "cleanup scene reopen failed" : detail + "; cleanup scene reopen failed";
                    }
                }
            }
            if (passed && state.last_asset_report.stale_fallback_paths != 0) {
                passed = false;
                detail = std::format("cleanup left {} stale fallback path(s)", state.last_asset_report.stale_fallback_paths);
            }
            if (detail.empty()) detail = "Prefab move -> repair -> save -> refresh -> reopen preserved stable ID/canonical fallback and cleanup restored original path";
        } else return fail("scenario must be hierarchy, play_cycle, ui_layout, managed_build_recovery or asset_move_save_reopen");
        append_command_audit(state, vespera::editor::EditorCommandKind::RunQaScenario, "Automation QA scenario: " + scenario, passed, state.current_state_id, state.current_state_id);
        const auto after_entities = state.scene.entities.size();
        return passed ? ok(std::format("{{\"scenario\":\"{}\",\"passed\":true,\"detail\":\"{}\",\"entities_before\":{},\"entities_after\":{},\"cleanup\":{}}}", automation_json_escape(scenario), automation_json_escape(detail), before_entities, after_entities, cleanup ? "true" : "false")) : fail(std::format("QA scenario '{}' failed: {}", scenario, detail));
    }

    if (request.command == "vespera_validate_scene") {
        const auto issues = vespera::validate_scene(editor_is_playing(state) ? state.play_scene : state.scene);
        int errors = 0;
        int warnings = 0;
        for (const auto& issue : issues) {
            if (issue.severity == vespera::SceneValidationSeverity::Error) ++errors;
            else ++warnings;
        }
        validate_scene_to_console(state);
        return ok(std::format("{{\"errors\":{},\"warnings\":{},\"issue_count\":{}}}", errors, warnings, issues.size()));
    }

    if (request.command == "vespera_undo") {
        if (editor_is_playing(state)) return fail("undo is disabled during Play Mode");
        const bool succeeded = undo(state);
        append_command_audit(state, vespera::editor::EditorCommandKind::Undo, "Automation undo", succeeded);
        return succeeded ? ok("{\"undone\":true}") : fail("nothing to undo");
    }

    if (request.command == "vespera_redo") {
        if (editor_is_playing(state)) return fail("redo is disabled during Play Mode");
        const bool succeeded = redo(state);
        append_command_audit(state, vespera::editor::EditorCommandKind::Redo, "Automation redo", succeeded);
        return succeeded ? ok("{\"redone\":true}") : fail("nothing to redo");
    }

    if (request.command == "vespera_play") {
        const std::string action = automation_arg(request, "action") ? *automation_arg(request, "action") : "enter";
        if (action == "enter") {
            if (!editor_is_playing(state)) start_play_mode(state);
        } else if (action == "stop") {
            if (editor_is_playing(state)) stop_play_mode(state);
        } else if (action == "pause") {
            if (!editor_is_playing(state)) return fail("Play Mode is not active");
            if (state.play_state != EditorPlayState::Paused) toggle_play_pause(state);
        } else if (action == "resume") {
            if (!editor_is_playing(state)) return fail("Play Mode is not active");
            if (state.play_state == EditorPlayState::Paused) toggle_play_pause(state);
        } else if (action == "step") {
            if (!editor_is_playing(state)) return fail("Play Mode is not active");
            step_play_mode(state);
        } else {
            return fail("action must be enter, stop, pause, resume or step");
        }
        const std::string current = state.play_state == EditorPlayState::Editing
            ? "editing" : (state.play_state == EditorPlayState::Paused ? "paused" : "playing");
        return ok(std::format("{{\"play_state\":\"{}\"}}", current));
    }

    if (request.command == "vespera_inject_input_action") {
        if (!editor_is_playing(state) || !state.play_runtime) return fail("runtime input injection requires active Play Mode");
        const auto* action = automation_arg(request, "action");
        if (!action || action->empty()) return fail("action is required");
        const std::string phase = automation_arg(request, "phase") ? *automation_arg(request, "phase") : "set";
        float value = 1.0f;
        if (const auto* raw = automation_arg(request, "value")) {
            const auto parsed = automation_float(*raw); if (!parsed) return fail("value must be numeric"); value = std::clamp(*parsed, -1.0f, 1.0f);
        }
        if (phase == "press" || phase == "set") state.play_runtime->queue_action_override(*action, phase == "press" ? std::max(0.5f, value) : value);
        else if (phase == "release") state.play_runtime->queue_action_override(*action, 0.0f);
        else if (phase == "clear") state.play_runtime->queue_clear_action_override(*action);
        else return fail("phase must be set, press, release or clear");
        append_command_audit(state, vespera::editor::EditorCommandKind::InjectRuntimeInput,
            "Automation inject Input action", true, state.current_state_id, state.current_state_id);
        return ok(std::format("{{\"action\":\"{}\",\"phase\":\"{}\",\"value\":{},\"queued\":true}}",
            automation_json_escape(*action), automation_json_escape(phase), value));
    }

    if (request.command == "vespera_pointer_event") {
        if (!editor_is_playing(state) || !state.play_runtime || (!state.play_ui_loaded && !state.play_rml_ui_loaded)) {
            return fail("pointer injection requires active Play Mode with a runtime UI surface");
        }
        const auto* raw_x = automation_arg(request, "x"); const auto* raw_y = automation_arg(request, "y");
        const auto x = raw_x ? automation_float(*raw_x) : std::nullopt; const auto y = raw_y ? automation_float(*raw_y) : std::nullopt;
        if (!x || !y) return fail("x and y are required numeric coordinates");
        int width = state.play_rml_ui_loaded
            ? std::max(1, state.play_rml_view_width)
            : std::max(1, state.project_loaded ? state.project.window_width : 1280);
        int height = state.play_rml_ui_loaded
            ? std::max(1, state.play_rml_view_height)
            : std::max(1, state.project_loaded ? state.project.window_height : 720);
        if (const auto* raw = automation_arg(request, "width")) { const auto parsed=automation_int(*raw); if (!parsed || *parsed<1) return fail("width must be >= 1"); width=*parsed; }
        if (const auto* raw = automation_arg(request, "height")) { const auto parsed=automation_int(*raw); if (!parsed || *parsed<1) return fail("height must be >= 1"); height=*parsed; }
        const bool normalized = automation_arg(request, "normalized") ? automation_bool(*automation_arg(request, "normalized")).value_or(false) : false;
        const float point_x = normalized ? *x * static_cast<float>(width) : *x;
        const float point_y = normalized ? *y * static_cast<float>(height) : *y;
        const std::string phase = automation_arg(request, "phase") ? *automation_arg(request, "phase") : "move";

        if (state.play_rml_ui_loaded && state.play_rml_ui) {
            state.play_rml_view_width = width;
            state.play_rml_view_height = height;
            state.play_rml_ui->resize(width, height);
            if (!state.play_runtime->inject_rml_pointer(point_x, point_y, phase)) {
                return fail("RmlUi pointer phase must be move, down, up or click");
            }
            append_command_audit(state, vespera::editor::EditorCommandKind::InjectRuntimeUi,
                "Automation inject RmlUi pointer", true, state.current_state_id, state.current_state_id);
            return ok(std::format(
                "{{\"backend\":\"rmlui\",\"phase\":\"{}\",\"hovered_id\":\"{}\",\"focused_id\":\"{}\"}}",
                automation_json_escape(phase), automation_json_escape(state.play_rml_ui->hovered_id()),
                automation_json_escape(state.play_rml_ui->focused_id())));
        }

        vespera::UiVec2 point{point_x, point_y};
        auto apply = [&](bool down, bool pressed, bool released) {
            vespera::UiPointerState pointer{}; pointer.available=true; pointer.position=point; pointer.primary_down=down; pointer.primary_pressed=pressed; pointer.primary_released=released;
            (void)state.play_ui_cache.build_packet(state.play_ui_document, static_cast<float>(width), static_cast<float>(height), pointer,
                &state.play_runtime->ui_runtime_state());
        };
        if (phase == "move") apply(false,false,false);
        else if (phase == "down") apply(true,true,false);
        else if (phase == "up") apply(false,false,true);
        else if (phase == "click") { apply(true,true,false); apply(false,false,true); }
        else return fail("phase must be move, down, up or click");
        append_command_audit(state, vespera::editor::EditorCommandKind::InjectRuntimeUi,
            "Automation inject legacy runtime UI pointer", true, state.current_state_id, state.current_state_id);
        const auto& ui = state.play_runtime->ui_runtime_state();
        return ok(std::format("{{\"backend\":\"slui\",\"phase\":\"{}\",\"hovered\":{},\"pressed\":{},\"focused\":{},\"pending_clicks\":{}}}",
            automation_json_escape(phase), ui.hovered.value_or(0), ui.pressed.value_or(0), ui.focused.value_or(0), ui.pending_clicks.size()));
    }

    if (request.command == "vespera_ui_navigation") {
        if (!editor_is_playing(state) || !state.play_runtime || (!state.play_ui_loaded && !state.play_rml_ui_loaded)) {
            return fail("UI navigation injection requires active Play Mode with a runtime UI surface");
        }
        const auto* raw_action = automation_arg(request, "action"); if (!raw_action) return fail("action is required");
        if (*raw_action != "next" && *raw_action != "previous" && *raw_action != "activate") {
            return fail("action must be next, previous or activate");
        }
        if (state.play_rml_ui_loaded && state.play_rml_ui) {
            if (!state.play_runtime->inject_rml_navigation(*raw_action)) return fail("RmlUi navigation injection failed");
            append_command_audit(state, vespera::editor::EditorCommandKind::InjectRuntimeUi,
                "Automation inject RmlUi navigation", true, state.current_state_id, state.current_state_id);
            return ok(std::format(
                "{{\"backend\":\"rmlui\",\"action\":\"{}\",\"focused_id\":\"{}\"}}",
                automation_json_escape(*raw_action), automation_json_escape(state.play_rml_ui->focused_id())));
        }
        vespera::UiNavigationState nav{};
        if (*raw_action == "next") nav.focus_next = true;
        else if (*raw_action == "previous") nav.focus_previous = true;
        else nav.activate_pressed = true;
        const float width = static_cast<float>(std::max(1, state.project_loaded ? state.project.window_width : 1280));
        const float height = static_cast<float>(std::max(1, state.project_loaded ? state.project.window_height : 720));
        (void)state.play_ui_cache.build_packet(state.play_ui_document, width, height, {}, &state.play_runtime->ui_runtime_state(), nav);
        append_command_audit(state, vespera::editor::EditorCommandKind::InjectRuntimeUi,
            "Automation inject legacy runtime UI navigation", true, state.current_state_id, state.current_state_id);
        const auto& ui = state.play_runtime->ui_runtime_state();
        return ok(std::format("{{\"backend\":\"slui\",\"action\":\"{}\",\"focused\":{},\"pending_clicks\":{}}}",
            automation_json_escape(*raw_action), ui.focused.value_or(0), ui.pending_clicks.size()));
    }

    if (request.command == "vespera_text_input") {
        if (!editor_is_playing(state) || !state.play_runtime || (!state.play_ui_loaded && !state.play_rml_ui_loaded)) {
            return fail("text input injection requires active Play Mode with a runtime UI surface");
        }
        const std::string text = automation_arg(request, "text") ? *automation_arg(request, "text") : std::string{};
        const bool backspace = automation_arg(request, "backspace") ? automation_bool(*automation_arg(request, "backspace")).value_or(false) : false;
        const bool clear = automation_arg(request, "clear") ? automation_bool(*automation_arg(request, "clear")).value_or(false) : false;
        if (state.play_rml_ui_loaded && state.play_rml_ui) {
            if (!state.play_runtime->inject_rml_text(text, backspace, clear)) {
                return fail("RmlUi text input requires a focused editable element");
            }
            const std::string focused = state.play_rml_ui->focused_id();
            bool read_only = false;
            (void)state.play_rml_ui->read_only(focused, read_only);
            append_command_audit(state, vespera::editor::EditorCommandKind::InjectRuntimeUi,
                "Automation inject RmlUi text", true, state.current_state_id, state.current_state_id);
            return ok(std::format(
                "{{\"backend\":\"rmlui\",\"focused_id\":\"{}\",\"value\":\"{}\",\"read_only\":{}}}",
                automation_json_escape(focused), automation_json_escape(state.play_rml_ui->value(focused)),
                read_only ? "true" : "false"));
        }
        vespera::UiTextInputState input{};
        input.text = text;
        input.backspace = backspace;
        input.clear = clear;
        const float width = static_cast<float>(std::max(1, state.project_loaded ? state.project.window_width : 1280));
        const float height = static_cast<float>(std::max(1, state.project_loaded ? state.project.window_height : 720));
        (void)state.play_ui_cache.build_packet(state.play_ui_document, width, height, {}, &state.play_runtime->ui_runtime_state(), {}, input);
        const auto focused = state.play_runtime->ui_runtime_state().focused;
        const auto* node = focused ? state.play_ui_document.find(*focused) : nullptr;
        if (!node || node->type != vespera::UiNodeType::TextInput) return fail("no TextInput node is currently focused");
        append_command_audit(state, vespera::editor::EditorCommandKind::InjectRuntimeUi,
            "Automation inject legacy runtime UI text", true, state.current_state_id, state.current_state_id);
        return ok(std::format("{{\"backend\":\"slui\",\"focused\":{},\"text\":\"{}\",\"read_only\":{},\"max_length\":{}}}",
            node->id, automation_json_escape(node->text.text), node->input.read_only ? "true" : "false", node->input.max_length));
    }

    if (request.command == "vespera_get_ui_runtime_state") {
        if (!editor_is_playing(state) || !state.play_runtime || (!state.play_ui_loaded && !state.play_rml_ui_loaded)) {
            return fail("runtime UI state requires active Play Mode with a runtime UI surface");
        }
        if (state.play_rml_ui_loaded && state.play_rml_ui) {
            const std::string hovered = state.play_rml_ui->hovered_id();
            const std::string focused = state.play_rml_ui->focused_id();
            bool read_only = false;
            if (!focused.empty()) (void)state.play_rml_ui->read_only(focused, read_only);
            return ok(std::format(
                "{{\"backend\":\"rmlui\",\"hovered_id\":\"{}\",\"focused_id\":\"{}\",\"focused_value\":\"{}\",\"focused_read_only\":{}}}",
                automation_json_escape(hovered), automation_json_escape(focused),
                automation_json_escape(focused.empty() ? std::string{} : state.play_rml_ui->value(focused)),
                read_only ? "true" : "false"));
        }
        const auto& ui = state.play_runtime->ui_runtime_state();
        std::string clicks = "[";
        for (std::size_t i=0;i<ui.pending_clicks.size();++i) { if (i) clicks += ','; clicks += std::to_string(ui.pending_clicks[i]); }
        clicks += "]";
        std::string focused_name, focused_type, focused_text; bool focused_read_only=false;
        if (ui.focused) if (const auto* node = state.play_ui_document.find(*ui.focused)) {
            focused_name=node->name; focused_type=std::string(vespera::ui_node_type_name(node->type)); focused_text=node->text.text; focused_read_only=node->input.read_only;
        }
        return ok(std::format("{{\"backend\":\"slui\",\"hovered\":{},\"pressed\":{},\"focused\":{},\"pointer_capture\":{},\"pending_clicks\":{},\"focused_name\":\"{}\",\"focused_type\":\"{}\",\"focused_text\":\"{}\",\"focused_read_only\":{}}}",
            ui.hovered.value_or(0), ui.pressed.value_or(0), ui.focused.value_or(0), ui.pointer_capture, clicks,
            automation_json_escape(focused_name), automation_json_escape(focused_type), automation_json_escape(focused_text), focused_read_only ? "true" : "false"));
    }

    if (request.command == "vespera_get_runtime_events") {
        std::uint64_t after = 0; std::size_t limit = 100;
        if (const auto* raw=automation_arg(request,"after_sequence")) { const auto parsed=automation_u64(*raw); if (!parsed) return fail("after_sequence must be an unsigned integer"); after=*parsed; }
        if (const auto* raw=automation_arg(request,"limit")) { const auto parsed=automation_u64(*raw); if (!parsed || *parsed==0) return fail("limit must be positive"); limit=static_cast<std::size_t>(std::min<std::uint64_t>(*parsed,500)); }
        std::vector<vespera::ManagedRuntimeEvent> events;
        std::uint32_t generation = state.last_runtime_assembly_generation;
        std::uint64_t latest = state.last_runtime_event_sequence;
        const bool active = editor_is_playing(state) && state.play_runtime;
        if (active) {
            events = state.play_runtime->runtime_events_since(after);
            generation = state.play_runtime->assembly_generation();
            latest = state.play_runtime->latest_runtime_event_sequence();
        } else {
            for (const auto& event : state.last_runtime_events) if (event.sequence > after) events.push_back(event);
        }
        if (events.size() > limit) events.erase(events.begin(), events.end()-static_cast<std::ptrdiff_t>(limit));
        std::string json="["; bool first=true;
        for (const auto& event:events) { if(!first) json+=','; first=false; json += std::format("{{\"sequence\":{},\"frame\":{},\"generation\":{},\"entity_id\":{},\"component\":\"{}\",\"callback\":\"{}\"}}", event.sequence,event.frame,event.assembly_generation,event.entity_id,automation_json_escape(event.component),automation_json_escape(event.callback)); }
        json += "]";
        return ok(std::format("{{\"active\":{},\"assembly_generation\":{},\"latest_sequence\":{},\"events\":{}}}", active ? "true" : "false", generation, latest, json));
    }

    if (request.command == "vespera_clear_runtime_events") {
        if (editor_is_playing(state) && state.play_runtime) {
            state.play_runtime->clear_runtime_events();
        } else {
            state.last_runtime_events.clear();
        }
        append_command_audit(state, vespera::editor::EditorCommandKind::ClearRuntimeEvents,
            "Automation clear runtime events", true, state.current_state_id, state.current_state_id);
        const auto latest = state.play_runtime ? state.play_runtime->latest_runtime_event_sequence() : state.last_runtime_event_sequence;
        return ok(std::format("{{\"cleared\":true,\"latest_sequence\":{}}}", latest));
    }

    if (request.command == "vespera_save_scene") {
        if (editor_is_playing(state)) return fail("saving is disabled during Play Mode");
        std::filesystem::path path = state.scene_path;
        if (const auto* requested_path = automation_arg(request, "path"); requested_path && !requested_path->empty()) {
            path = *requested_path;
        }
        const bool succeeded = save_scene(state, path);
        append_command_audit(state, vespera::editor::EditorCommandKind::SaveScene,
            "Automation save scene", succeeded);
        return succeeded
            ? ok(std::format("{{\"path\":\"{}\"}}", automation_json_escape(std::filesystem::absolute(path).lexically_normal().generic_string())))
            : fail("scene save failed; inspect the editor Console for details");
    }

    return fail("unknown automation command: " + request.command);
}

void poll_automation_server(EditorState& state) {
    if (!state.automation_server || !state.automation_server->running()) return;
    for (const auto& incoming : state.automation_server->poll()) {
        auto response = dispatch_automation_request(state, incoming.request);
        if (!state.automation_server->respond(incoming.client_id, response)) {
            push_console(state, ConsoleEntry::Level::Warning,
                "Automation response could not be delivered to the local client.");
        }
    }
}

bool start_automation_server(EditorState& state, std::uint16_t port) {
    if (!state.automation_server) state.automation_server = std::make_unique<vespera::editor::AutomationServer>();
    if (state.automation_server->running()) state.automation_server->stop();
    std::string error;
    if (!state.automation_server->start(port, &error)) {
        push_console(state, ConsoleEntry::Level::Error, "Automation server failed: " + error);
        return false;
    }
    state.automation_port = port;
    push_console(state, ConsoleEntry::Level::Info,
        std::format("Local editor automation server listening on 127.0.0.1:{} (main-thread command dispatch).", port));
    return true;
}

bool launch_packaged_runtime(EditorState& state, const std::filesystem::path& runtime, bool runtime_automation) {
    if (runtime.empty()) {
        push_console(state, ConsoleEntry::Level::Error, "Exported package has no runtime executable to launch.");
        return false;
    }
#if defined(_WIN32)
    std::wstring command = L"\"" + runtime.wstring() + L"\"";
    if (runtime_automation) command += L" --automation";
    std::vector<wchar_t> command_line(command.begin(), command.end());
    command_line.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const auto working = runtime.parent_path().wstring();
    if (!CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, FALSE, 0, nullptr, working.c_str(), &startup, &process)) {
        push_console(state, ConsoleEntry::Level::Error, "Could not launch exported runtime: " + runtime.string());
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    push_console(state, ConsoleEntry::Level::Info, "Launched exported game: " + runtime.string()
        + (runtime_automation ? " with runtime automation on 127.0.0.1:46788" : ""));
    return true;
#else
    push_console(state, ConsoleEntry::Level::Warning, "Launching exported games from the editor is currently implemented for Windows only.");
    return false;
#endif
}

std::string editor_build_configuration(const EditorBuildJobState& job) {
    static constexpr std::array<std::string_view, 3> names{"Debug", "Development", "Release"};
    const int index = std::clamp(job.configuration_index, 0, static_cast<int>(names.size()) - 1);
    return std::string(names[static_cast<std::size_t>(index)]);
}

std::string safe_project_build_name(const EditorState& state) {
    std::string safe_name = state.project.name.empty() ? "VesperaGame" : state.project.name;
    for (char& c : safe_name) {
        const bool allowed = std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-';
        if (!allowed) c = '-';
    }
    while (!safe_name.empty() && safe_name.back() == '-') safe_name.pop_back();
    if (safe_name.empty()) safe_name = "VesperaGame";
    return safe_name;
}

std::filesystem::path editor_build_output_directory(const EditorState& state, std::string_view configuration) {
    return (state.project.build_output_root() / (safe_project_build_name(state) + "-" + std::string(configuration))).lexically_normal();
}

#if defined(_WIN32)
int run_hidden_process_to_log(
    std::wstring command,
    const std::filesystem::path& working_directory,
    const std::filesystem::path& log_path
) {
    std::error_code ec;
    std::filesystem::create_directories(log_path.parent_path(), ec);

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE log = CreateFileW(
        log_path.wstring().c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (log == INVALID_HANDLE_VALUE) return -1001;

    HANDLE null_input = CreateFileW(
        L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = null_input == INVALID_HANDLE_VALUE ? nullptr : null_input;
    startup.hStdOutput = log;
    startup.hStdError = log;
    PROCESS_INFORMATION process{};
    std::vector<wchar_t> command_line(command.begin(), command.end());
    command_line.push_back(L'\0');
    const auto working = working_directory.wstring();
    const BOOL created = CreateProcessW(
        nullptr,
        command_line.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        working.empty() ? nullptr : working.c_str(),
        &startup,
        &process);
    CloseHandle(log);
    if (null_input != INVALID_HANDLE_VALUE) CloseHandle(null_input);
    if (!created) return -1002;

    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 1;
    if (!GetExitCodeProcess(process.hProcess, &exit_code)) exit_code = 1;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<int>(exit_code);
}
#endif

void append_editor_build_log(EditorState& state) {
    auto& job = state.build_job;
    if (job.log_path.empty()) return;
    std::ifstream input(job.log_path, std::ios::binary);
    if (!input) return;
    input.seekg(static_cast<std::streamoff>(job.log_bytes_consumed));
    if (!input) return;
    std::ostringstream buffer;
    buffer << input.rdbuf();
    std::string fresh = buffer.str();
    if (fresh.empty()) return;
    job.log_bytes_consumed += fresh.size();
    job.log_partial += fresh;

    std::size_t start = 0;
    for (;;) {
        const auto newline = job.log_partial.find('\n', start);
        if (newline == std::string::npos) break;
        std::string line = job.log_partial.substr(start, newline - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) {
            job.status = line;
            job.log_tail.push_back(std::move(line));
            if (job.log_tail.size() > 32) job.log_tail.erase(job.log_tail.begin());
        }
        start = newline + 1;
    }
    if (start != 0) job.log_partial.erase(0, start);
}

bool start_editor_build_job(EditorState& state, std::string_view configuration, bool launch_after) {
#if defined(_WIN32)
    auto& job = state.build_job;
    if (job.running) {
        push_console(state, ConsoleEntry::Level::Warning, "A Vespera game build is already running.");
        return false;
    }
    if (!state.project_loaded) {
        push_console(state, ConsoleEntry::Level::Warning, "Open a Vespera project before building a game.");
        return false;
    }
    if (editor_is_playing(state)) {
        push_console(state, ConsoleEntry::Level::Warning, "Stop Play Mode before building a standalone game.");
        return false;
    }
    if (configuration != "Debug" && configuration != "Development" && configuration != "Release") {
        push_console(state, ConsoleEntry::Level::Error, "Build configuration must be Debug, Development, or Release.");
        return false;
    }

    if (state.dirty && !state.scene_path.empty() && !save_scene(state, state.scene_path)) {
        push_console(state, ConsoleEntry::Level::Error, "Build stopped because the current scene could not be saved.");
        return false;
    }
    const auto saved_project = vespera::save_vespera_project(state.project, state.project_path);
    if (!saved_project) {
        push_console(state, ConsoleEntry::Level::Error, "Build stopped because the project could not be saved: " + saved_project.message);
        return false;
    }
    refresh_asset_catalog(state);

    const auto issues = vespera::validate_vespera_project(state.project);
    std::size_t errors = 0;
    for (const auto& issue : issues) {
        if (issue.severity == vespera::ProjectValidationSeverity::Error) ++errors;
    }
    const auto manifest = vespera::build_project_asset_manifest(state.project, state.asset_catalog);
    if (errors != 0 || !manifest.valid()) {
        push_console(state, ConsoleEntry::Level::Error,
            std::format("Build preflight failed: {} project error(s), {} missing build root(s), {} broken dependency reference(s).",
                errors, manifest.missing_roots.size(), manifest.broken_dependencies.size()));
        job.window_open = true;
        job.status = "Build preflight failed - fix Project Settings / asset errors first.";
        job.last_succeeded = false;
        return false;
    }

    const auto source_root = automation_source_root();
    const auto export_script = source_root / "export.ps1";
    if (source_root.empty() || !std::filesystem::is_regular_file(export_script)) {
        push_console(state, ConsoleEntry::Level::Error,
            "Build Game cannot locate Vespera's export pipeline. Launch the editor through the Project Hub/run.ps1.");
        return false;
    }

    const std::string config(configuration);
    const auto log_dir = source_root / "build" / "editor-logs";
    job.log_path = log_dir / (safe_project_build_name(state) + "-" + config + ".log");
    job.log_bytes_consumed = 0;
    job.log_partial.clear();
    job.log_tail.clear();
    job.last_output_directory = editor_build_output_directory(state, configuration);
    job.last_launch_requested = launch_after;
    job.last_succeeded = false;
    job.running = true;
    job.window_open = true;
    job.status = launch_after ? "Building and preparing to launch..." : "Building game...";

    const auto quote = [](const std::filesystem::path& value) {
        return std::wstring(L"\"") + value.wstring() + L"\"";
    };
    std::wstring command = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File "
        + quote(export_script)
        + L" -Project " + quote(state.project_path)
        + L" -Configuration " + std::wstring(config.begin(), config.end());
    if (launch_after) command += L" -Launch";

    const auto working = source_root;
    const auto log_path = job.log_path;
    job.future = std::async(std::launch::async, [command = std::move(command), working, log_path]() mutable {
        return run_hidden_process_to_log(std::move(command), working, log_path);
    });
    push_console(state, ConsoleEntry::Level::Info,
        std::format("{} build started inside Vespera. Output: {}",
            config, job.last_output_directory.string()));
    return true;
#else
    (void)configuration;
    (void)launch_after;
    push_console(state, ConsoleEntry::Level::Warning, "In-editor standalone builds are currently implemented for Windows.");
    return false;
#endif
}

void update_editor_build_job(EditorState& state) {
    auto& job = state.build_job;
    if (!job.running) return;
    append_editor_build_log(state);
    if (!job.future.valid() || job.future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return;

    const int result = job.future.get();
    append_editor_build_log(state);
    if (!job.log_partial.empty()) {
        if (!job.log_partial.empty() && job.log_partial.back() == '\r') job.log_partial.pop_back();
        if (!job.log_partial.empty()) {
            job.status = job.log_partial;
            job.log_tail.push_back(job.log_partial);
        }
        job.log_partial.clear();
    }
    job.running = false;
    job.last_succeeded = result == 0;
    if (job.last_succeeded) {
        job.status = job.last_launch_requested ? "Build succeeded and game launched." : "Build succeeded.";
        push_console(state, ConsoleEntry::Level::Info,
            "Standalone build succeeded: " + job.last_output_directory.string());
    } else {
        job.status = std::format("Build failed (exit code {}).", result);
        push_console(state, ConsoleEntry::Level::Error,
            std::format("Standalone build failed (exit code {}). Build log: {}", result, job.log_path.string()));
        const std::size_t begin = job.log_tail.size() > 8 ? job.log_tail.size() - 8 : 0;
        for (std::size_t i = begin; i < job.log_tail.size(); ++i) {
            push_console(state, ConsoleEntry::Level::Error, "Build: " + job.log_tail[i]);
        }
    }
}

void open_editor_build_output(EditorState& state) {
#if defined(_WIN32)
    const auto& output = state.build_job.last_output_directory;
    if (output.empty() || !std::filesystem::exists(output)) return;
    std::wstring command = L"explorer.exe \"" + output.wstring() + L"\"";
    std::vector<wchar_t> command_line(command.begin(), command.end());
    command_line.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    } else {
        push_console(state, ConsoleEntry::Level::Warning, "Could not open build output folder: " + output.string());
    }
#else
    (void)state;
#endif
}

void draw_build_game_window(EditorState& state) {
    auto& job = state.build_job;
    if (!job.window_open && !job.running) return;
    if (!job.window_open) return;

    ImGui::SetNextWindowSize(ImVec2(690.0f, 700.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Build Game", &job.window_open)) {
        ImGui::End();
        return;
    }

    if (!state.project_loaded) {
        ImGui::TextDisabled("Open a Vespera project to build a standalone game.");
        ImGui::End();
        return;
    }

    ImGui::Text("Project: %s", state.project.name.c_str());
    ImGui::TextDisabled("%s", state.project_path.string().c_str());
    ImGui::Separator();

    const char* configurations[] = {"Debug", "Development", "Release"};
    ImGui::BeginDisabled(job.running);
    ImGui::Combo("Configuration", &job.configuration_index, configurations, IM_ARRAYSIZE(configurations));
    ImGui::InputText("Executable Name", &state.project.executable_name);
    std::string build_output = state.project.build_output_directory.generic_string();
    if (ImGui::InputText("Build Output Directory", &build_output)) state.project.build_output_directory = build_output;
    int deployment_index = state.project.managed_deployment == vespera::ManagedDeploymentMode::Portable ? 1 : 0;
    const char* deployment_items[] = {"Framework-dependent", "Portable (.NET bundled)"};
    if (ImGui::Combo("Managed Deployment", &deployment_index, deployment_items, IM_ARRAYSIZE(deployment_items))) {
        state.project.managed_deployment = deployment_index == 1
            ? vespera::ManagedDeploymentMode::Portable
            : vespera::ManagedDeploymentMode::FrameworkDependent;
    }
    ImGui::Checkbox("Development Diagnostics / Symbols", &state.project.development_diagnostics);
    ImGui::EndDisabled();

    const std::string configuration = editor_build_configuration(job);
    const auto output = editor_build_output_directory(state, configuration);
    ImGui::TextDisabled("Output: %s", output.string().c_str());
    ImGui::TextDisabled("Build Game uses Vespera's deterministic export pipeline; no terminal is required.");

    const auto issues = vespera::validate_vespera_project(state.project);
    const auto manifest = vespera::build_project_asset_manifest(state.project, state.asset_catalog);
    std::size_t errors = 0, warnings = 0;
    for (const auto& issue : issues) {
        if (issue.severity == vespera::ProjectValidationSeverity::Error) ++errors; else ++warnings;
    }
    const bool preflight_ok = errors == 0 && manifest.valid();
    const ImVec4 validation_color = preflight_ok
        ? ImVec4(0.48f, 0.82f, 0.62f, 1.0f) : ImVec4(1.0f, 0.55f, 0.42f, 1.0f);
    ImGui::TextColored(validation_color,
        "Preflight: %zu error(s), %zu warning(s) | %zu assets | %zu broken | %zu missing roots",
        errors, warnings, manifest.assets.size(), manifest.broken_dependencies.size(), manifest.missing_roots.size());

    if (ImGui::CollapsingHeader("Preflight Details", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BeginChild("BuildPreflightDetails", ImVec2(0.0f, 132.0f), true);
        if (issues.empty() && manifest.valid() && manifest.stale_root_paths.empty()) {
            ImGui::TextColored(ImVec4(0.48f, 0.82f, 0.62f, 1.0f),
                "Ready to build. Project validation and asset closure are clean.");
        }
        for (const auto& issue : issues) {
            const bool is_error = issue.severity == vespera::ProjectValidationSeverity::Error;
            const ImVec4 color = is_error
                ? ImVec4(1.0f, 0.48f, 0.42f, 1.0f)
                : ImVec4(0.95f, 0.74f, 0.35f, 1.0f);
            ImGui::TextColored(color, "%s", is_error ? "ERROR" : "WARNING");
            ImGui::SameLine();
            ImGui::TextWrapped("%s", issue.message.c_str());
        }
        for (const auto& root : manifest.missing_roots) {
            ImGui::TextColored(ImVec4(1.0f, 0.48f, 0.42f, 1.0f), "ERROR");
            ImGui::SameLine();
            ImGui::TextWrapped("Missing build root: %s", root.c_str());
        }
        for (const auto* dependency : manifest.broken_dependencies) {
            if (!dependency) continue;
            ImGui::TextColored(ImVec4(1.0f, 0.48f, 0.42f, 1.0f), "ERROR");
            ImGui::SameLine();
            ImGui::TextWrapped("Broken asset reference: %s (%s)",
                dependency->reference.c_str(), dependency->reason.c_str());
        }
        for (const auto& path : manifest.stale_root_paths) {
            ImGui::TextColored(ImVec4(0.95f, 0.74f, 0.35f, 1.0f), "WARNING");
            ImGui::SameLine();
            ImGui::TextWrapped("Stable build root has a stale fallback path: %s", path.c_str());
        }
        if (configuration == "Release" && state.project.game_icon.empty()) {
            ImGui::TextColored(ImVec4(0.68f, 0.76f, 0.90f, 1.0f), "NOTE");
            ImGui::SameLine();
            ImGui::TextWrapped("No project game icon is authored; Release will use the Vespera fallback icon.");
        }
        ImGui::EndChild();
    }

    ImGui::BeginDisabled(job.running || !preflight_ok || editor_is_playing(state));
    if (ImGui::Button("Build", ImVec2(120.0f, 34.0f))) {
        (void)start_editor_build_job(state, configuration, false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Build & Run", ImVec2(140.0f, 34.0f))) {
        (void)start_editor_build_job(state, configuration, true);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(job.running || job.last_output_directory.empty() || !std::filesystem::exists(job.last_output_directory));
    if (ImGui::Button("Open Output Folder", ImVec2(155.0f, 34.0f))) open_editor_build_output(state);
    ImGui::EndDisabled();

    ImGui::Separator();
    if (job.running) {
        const float activity = static_cast<float>(std::fmod(ImGui::GetTime() * 0.32, 1.0));
        ImGui::ProgressBar(activity, ImVec2(-1.0f, 18.0f), "Building...");
    }
    const ImVec4 status_color = job.running
        ? ImVec4(0.55f, 0.72f, 1.0f, 1.0f)
        : (job.last_succeeded ? ImVec4(0.48f, 0.82f, 0.62f, 1.0f) : ImVec4(0.82f, 0.84f, 0.90f, 1.0f));
    ImGui::TextColored(status_color, "%s", job.status.c_str());
    if (!job.log_path.empty()) ImGui::TextDisabled("Log: %s", job.log_path.string().c_str());

    ImGui::SeparatorText("Recent Build Output");
    ImGui::BeginChild("BuildGameLog", ImVec2(0.0f, 210.0f), true);
    if (job.log_tail.empty()) ImGui::TextDisabled("Build output will appear here.");
    for (const auto& line : job.log_tail) ImGui::TextWrapped("%s", line.c_str());
    if (job.running) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    ImGui::End();
}

bool export_current_project_from_editor(EditorState& state, std::string_view configuration, bool launch_after = false) {
    if (editor_is_playing(state)) {
        push_console(state, ConsoleEntry::Level::Warning, "Stop Play Mode before exporting a standalone package.");
        return false;
    }
    if (!state.project_loaded) {
        push_console(state, ConsoleEntry::Level::Warning, "Open a Vespera project before exporting.");
        return false;
    }
    const auto source_root = automation_source_root();
    if (source_root.empty()) {
        push_console(state, ConsoleEntry::Level::Error,
            "Export cannot locate the Vespera source/build root in this launch. Start the editor with run.ps1 or use export.ps1.");
        return false;
    }
    std::string safe_name = state.project.name.empty() ? "VesperaGame" : state.project.name;
    for (char& c : safe_name) {
        const bool allowed = std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-';
        if (!allowed) c = '-';
    }
    vespera::ProjectPackageOptions options;
    options.configuration = std::string(configuration);
    options.managed_deployment = state.project.managed_deployment;
    options.output_directory = state.project.build_output_root() / (safe_name + "-" + std::string(configuration));
    options.runtime_executable = automation_find_runtime_executable(state, configuration);
    options.managed_directory = automation_find_managed_directory(state);
    options.clean_output = true;
    options.include_debug_symbols = configuration == "Debug"
        || (configuration == "Development" && state.project.development_diagnostics);
    if (options.runtime_executable.empty()) {
        push_console(state, ConsoleEntry::Level::Error,
            "No built " + std::string(configuration) + " runtime target was found for direct packaging. Build that configuration through Vespera Build Game first.");
        return false;
    }
    const auto result = vespera::export_project_package(state.project, state.asset_catalog, options);
    append_command_audit(state, vespera::editor::EditorCommandKind::ExportProject,
        "Export " + std::string(configuration) + " package", result.ok, state.current_state_id, state.current_state_id);
    if (!result) {
        push_console(state, ConsoleEntry::Level::Error, "Export failed: " + result.message);
        return false;
    }
    for (const auto& warning : result.warnings) {
        push_console(state, ConsoleEntry::Level::Warning, "Export: " + warning);
    }
    push_console(state, ConsoleEntry::Level::Info,
        std::format("Exported {} package: {} assets | {} bytes | {} metadata | {} managed files | {} bundled .NET files -> {}",
            configuration, result.copied_assets, result.asset_bytes, result.copied_metadata, result.copied_managed_files, result.copied_dotnet_files,
            result.package_directory.string()));
    push_console(state, ConsoleEntry::Level::Info, "Package report: " + result.package_report.string());
    if (launch_after) return launch_packaged_runtime(state, result.packaged_runtime);
    return true;
}

void stop_automation_server(EditorState& state) {
    if (!state.automation_server || !state.automation_server->running()) return;
    state.automation_server->stop();
    push_console(state, ConsoleEntry::Level::Info, "Local editor automation server stopped.");
}

bool draw_menu(EditorState& state) {
    bool keep_running = true;
    if (!ImGui::BeginMainMenuBar()) {
        return keep_running;
    }

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open Project...")) {
            state.project_path_text = state.project_path.empty() ? std::string{} : state.project_path.string();
            state.request_open_project_popup = true;
        }
        if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) {
            state.request_open_popup = true;
        }
        if (ImGui::MenuItem("Save", "Ctrl+S", false, !state.scene_path.empty())) {
            save_scene(state, state.scene_path);
        }
        if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) {
            state.request_save_as_popup = true;
        }
        ImGui::Separator();
        const bool can_export = state.project_loaded && !editor_is_playing(state);
        if (ImGui::MenuItem("Build Game...", "Ctrl+Shift+B", false, can_export)) {
            state.build_job.window_open = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) {
            request_exit(state);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Build")) {
        const bool can_build = state.project_loaded && !editor_is_playing(state);
        if (ImGui::MenuItem("Build Game...", "Ctrl+Shift+B", false, can_build)) {
            state.build_job.window_open = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Build Development", nullptr, false, can_build && !state.build_job.running)) {
            (void)start_editor_build_job(state, "Development", false);
        }
        if (ImGui::MenuItem("Build & Run Development", nullptr, false, can_build && !state.build_job.running)) {
            (void)start_editor_build_job(state, "Development", true);
        }
        if (ImGui::MenuItem("Build Release", nullptr, false, can_build && !state.build_job.running)) {
            (void)start_editor_build_job(state, "Release", false);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !state.undo_stack.empty())) {
            undo(state);
        }
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !state.redo_stack.empty())) {
            redo(state);
        }
        ImGui::Separator();
        const bool duplicable = (state.selection.kind == SelectionKind::Sector && state.selection.index < state.scene.world.sectors().size())
            || (state.selection.kind == SelectionKind::Entity && selected_entity_index(state).has_value())
            || (state.selection.kind == SelectionKind::Material && state.selection.index < state.scene.world.materials().size())
            || (state.selection.kind == SelectionKind::SpriteClip && state.selection.index < state.scene.sprite_clips.size());
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, duplicable)) {
            command_duplicate_selection(state);
        }
        if (ImGui::MenuItem("Delete", "Del", false, duplicable)) {
            command_delete_selection(state);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("GameObject")) {
        if (ImGui::MenuItem("Create Empty Entity")) {
            command_create_entity(state);
        }
        if (ImGui::BeginMenu("3D Object")) {
            if (ImGui::MenuItem("Cube")) command_create_primitive_entity(state, vespera::PrimitiveMeshType::Cube);
            if (ImGui::MenuItem("Plane")) command_create_primitive_entity(state, vespera::PrimitiveMeshType::Plane);
            if (ImGui::MenuItem("Cylinder")) command_create_primitive_entity(state, vespera::PrimitiveMeshType::Cylinder);
            if (ImGui::MenuItem("Sphere")) command_create_primitive_entity(state, vespera::PrimitiveMeshType::Sphere);
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Create Sprite Entity")) {
            command_create_sprite_entity(state);
        }
        if (ImGui::MenuItem("Create Trigger Volume")) {
            command_create_trigger_entity(state);
        }
        if (ImGui::MenuItem("Create Point Light")) {
            command_create_point_light_entity(state);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Create Sector")) {
            command_create_sector(state);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Assets")) {
        if (ImGui::MenuItem("Create Material Asset")) {
            command_create_material_asset(state);
        }
        if (ImGui::MenuItem("Create Sprite Clip")) {
            command_create_clip(state);
        }
        ImGui::Separator();
        const bool has_selected_entity = selected_entity_index(state).has_value();
        if (ImGui::MenuItem("Create Prefab from Selected", nullptr, false, has_selected_entity)) {
            command_create_prefab_from_selected(state);
        }
        if (ImGui::MenuItem("Refresh Asset Catalog")) {
            refresh_asset_catalog(state, true, true);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Tools")) {
        if (ImGui::MenuItem("Validate Scene")) {
            validate_scene_to_console(state);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Build C# Scripts")) {
            build_managed_scripts(state);
        }
        if (ImGui::MenuItem("Refresh C# Metadata")) {
            load_managed_metadata(state, true);
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("Extensions")) {
            ImGui::TextDisabled("Editor Extension API v%u", vespera::editor::kEditorExtensionApiVersion);
            ImGui::Separator();
            for (const auto& extension : state.extension_registry.extensions()) {
                ImGui::TextUnformatted(extension.display_name.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("%s", extension.version.c_str());
            }
            ImGui::Separator();
            ImGui::TextDisabled("%zu registered semantic commands", state.extension_registry.commands().size());
            ImGui::TextDisabled("Dynamic plugin loading begins after 0.8 command dogfooding.");
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("Automation / MCP")) {
            const bool automation_running = state.automation_server && state.automation_server->running();
            ImGui::TextDisabled("Localhost only | VAP v1 | port %u", static_cast<unsigned int>(state.automation_port));
            if (!automation_running) {
                if (ImGui::MenuItem("Start Local Automation Server")) {
                    start_automation_server(state, state.automation_port);
                }
            } else {
                if (ImGui::MenuItem("Stop Local Automation Server")) {
                    stop_automation_server(state);
                }
            }
            ImGui::Separator();
            ImGui::TextDisabled("%zu typed tools exposed", vespera::editor::kAutomationTools.size());
            ImGui::TextDisabled("MCP bridge: tools/vespera_mcp_server.py");
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Frame Sector View", "F")) {
            state.scene_view.frame_all_pending = true;
        }
        const bool frameable = state.selection.kind == SelectionKind::Camera
            || state.selection.kind == SelectionKind::Sector
            || state.selection.kind == SelectionKind::Entity;
        if (ImGui::MenuItem("Frame Selected", "Shift+F", false, frameable)) {
            state.scene_view.frame_selection_pending = true;
            state.scene_view_3d.frame_selection_pending = true;
        }
        if (ImGui::MenuItem("Reset Scene Camera")) {
            state.scene_view_3d.camera = state.scene.camera;
            state.scene_view_3d.initialized = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Editor Layout")) {
            state.request_reset_layout = true;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        ImGui::TextDisabled("Vespera Editor %s", vespera::kEngineVersion.data());
        ImGui::Separator();
        ImGui::TextWrapped("Unity-familiar Vespera workspace: Hierarchy, Scene/Game/Sector, Inspector, Project and Console. The Scene tab uses the live Direct3D 12 renderer while Sector keeps precise top-down 2.5D authoring available.");
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
    return keep_running;
}

void handle_shortcuts(EditorState& state) {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || state.game_view.input_captured) {
        return;
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O)) {
        state.request_open_popup = true;
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
        if (io.KeyShift) {
            state.request_save_as_popup = true;
        } else if (!state.scene_path.empty()) {
            save_scene(state, state.scene_path);
        }
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
        if (io.KeyShift) redo(state); else undo(state);
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) {
        redo(state);
    }
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_B)) {
        if (state.project_loaded && !editor_is_playing(state)) state.build_job.window_open = true;
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_P)) {
        if (editor_is_playing(state)) stop_play_mode(state); else start_play_mode(state);
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D)) {
        command_duplicate_selection(state);
    }
    if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        command_delete_selection(state);
    }
    if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F)) {
        if (state.scene_view_3d.focused) {
            state.scene_view_3d.frame_selection_pending = true;
        } else if (io.KeyShift) {
            state.scene_view.frame_selection_pending = true;
            state.scene_view_3d.frame_selection_pending = true;
        } else {
            state.scene_view.frame_all_pending = true;
        }
    }
}

std::optional<vespera::RenderViewport> scene_view_3d_pixel_viewport(
    const EditorState& state,
    SDL_Window* window
) {
    const auto& view = state.scene_view_3d;
    if (!view.visible || !window) return std::nullopt;

    int logical_width = 0;
    int logical_height = 0;
    int pixel_width = 0;
    int pixel_height = 0;
    if (!SDL_GetWindowSize(window, &logical_width, &logical_height)
        || !SDL_GetWindowSizeInPixels(window, &pixel_width, &pixel_height)
        || logical_width <= 0 || logical_height <= 0 || pixel_width <= 0 || pixel_height <= 0) {
        return std::nullopt;
    }

    const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    const ImVec2 viewport_origin = main_viewport ? main_viewport->Pos : ImVec2{};
    const float scale_x = static_cast<float>(pixel_width) / static_cast<float>(logical_width);
    const float scale_y = static_cast<float>(pixel_height) / static_cast<float>(logical_height);

    const float local_min_x = view.content_min.x - viewport_origin.x;
    const float local_min_y = view.content_min.y - viewport_origin.y;
    const float local_max_x = view.content_max.x - viewport_origin.x;
    const float local_max_y = view.content_max.y - viewport_origin.y;

    int x0 = static_cast<int>(std::floor(local_min_x * scale_x));
    int y0 = static_cast<int>(std::floor(local_min_y * scale_y));
    int x1 = static_cast<int>(std::ceil(local_max_x * scale_x));
    int y1 = static_cast<int>(std::ceil(local_max_y * scale_y));

    x0 = std::clamp(x0, 0, pixel_width);
    y0 = std::clamp(y0, 0, pixel_height);
    x1 = std::clamp(x1, 0, pixel_width);
    y1 = std::clamp(y1, 0, pixel_height);
    if (x1 <= x0 || y1 <= y0) return std::nullopt;

    return vespera::RenderViewport{x0, y0, x1 - x0, y1 - y0};
}

std::optional<vespera::RenderViewport> game_view_pixel_viewport(
    const EditorState& state,
    SDL_Window* window
) {
    const auto& view = state.game_view;
    if (!view.visible || !window) return std::nullopt;

    int logical_width = 0;
    int logical_height = 0;
    int pixel_width = 0;
    int pixel_height = 0;
    if (!SDL_GetWindowSize(window, &logical_width, &logical_height)
        || !SDL_GetWindowSizeInPixels(window, &pixel_width, &pixel_height)
        || logical_width <= 0 || logical_height <= 0 || pixel_width <= 0 || pixel_height <= 0) {
        return std::nullopt;
    }

    const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    const ImVec2 viewport_origin = main_viewport ? main_viewport->Pos : ImVec2{};
    const float scale_x = static_cast<float>(pixel_width) / static_cast<float>(logical_width);
    const float scale_y = static_cast<float>(pixel_height) / static_cast<float>(logical_height);

    int x0 = static_cast<int>(std::floor((view.content_min.x - viewport_origin.x) * scale_x));
    int y0 = static_cast<int>(std::floor((view.content_min.y - viewport_origin.y) * scale_y));
    int x1 = static_cast<int>(std::ceil((view.content_max.x - viewport_origin.x) * scale_x));
    int y1 = static_cast<int>(std::ceil((view.content_max.y - viewport_origin.y) * scale_y));
    x0 = std::clamp(x0, 0, pixel_width);
    y0 = std::clamp(y0, 0, pixel_height);
    x1 = std::clamp(x1, 0, pixel_width);
    y1 = std::clamp(y1, 0, pixel_height);
    if (x1 <= x0 || y1 <= y0) return std::nullopt;
    return vespera::RenderViewport{x0, y0, x1 - x0, y1 - y0};
}

void apply_editor_style() {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();

    // 0.7.2: a real editor-theme pass rather than only rearranging dock nodes.
    // The goal is a quieter, denser professional shell with obvious hierarchy,
    // stronger selection states, and a consistent Vespera violet/blue accent.
    style.WindowRounding = 7.0f;
    style.ChildRounding = 7.0f;
    style.PopupRounding = 7.0f;
    style.FrameRounding = 5.0f;
    style.TabRounding = 5.0f;
    style.GrabRounding = 5.0f;
    style.ScrollbarRounding = 8.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;
    style.WindowPadding = ImVec2(11.0f, 10.0f);
    style.FramePadding = ImVec2(8.0f, 5.0f);
    style.ItemSpacing = ImVec2(8.0f, 7.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 5.0f);
    style.IndentSpacing = 17.0f;
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 9.0f;
    style.DockingSeparatorSize = 1.0f;
    style.WindowMenuButtonPosition = ImGuiDir_None;
    style.DisabledAlpha = 0.48f;

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text]                 = ImVec4(0.90f, 0.92f, 0.96f, 1.00f);
    c[ImGuiCol_TextDisabled]         = ImVec4(0.47f, 0.51f, 0.60f, 1.00f);
    c[ImGuiCol_WindowBg]             = ImVec4(0.040f, 0.045f, 0.058f, 1.00f);
    c[ImGuiCol_ChildBg]              = ImVec4(0.052f, 0.058f, 0.074f, 1.00f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.055f, 0.061f, 0.078f, 0.99f);
    c[ImGuiCol_Border]               = ImVec4(0.135f, 0.150f, 0.190f, 0.85f);
    c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]              = ImVec4(0.075f, 0.084f, 0.108f, 1.00f);
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.105f, 0.120f, 0.160f, 1.00f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.125f, 0.145f, 0.200f, 1.00f);
    c[ImGuiCol_TitleBg]              = ImVec4(0.040f, 0.045f, 0.058f, 1.00f);
    c[ImGuiCol_TitleBgActive]        = ImVec4(0.050f, 0.056f, 0.072f, 1.00f);
    c[ImGuiCol_MenuBarBg]            = ImVec4(0.032f, 0.036f, 0.047f, 1.00f);
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0.035f, 0.040f, 0.052f, 1.00f);
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.150f, 0.165f, 0.210f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.205f, 0.225f, 0.290f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.260f, 0.285f, 0.365f, 1.00f);
    c[ImGuiCol_CheckMark]            = ImVec4(0.49f, 0.55f, 1.00f, 1.00f);
    c[ImGuiCol_SliderGrab]           = ImVec4(0.41f, 0.48f, 0.92f, 1.00f);
    c[ImGuiCol_SliderGrabActive]     = ImVec4(0.55f, 0.62f, 1.00f, 1.00f);
    c[ImGuiCol_Button]               = ImVec4(0.105f, 0.120f, 0.165f, 1.00f);
    c[ImGuiCol_ButtonHovered]        = ImVec4(0.170f, 0.205f, 0.315f, 1.00f);
    c[ImGuiCol_ButtonActive]         = ImVec4(0.235f, 0.275f, 0.440f, 1.00f);
    c[ImGuiCol_Header]               = ImVec4(0.105f, 0.120f, 0.165f, 0.86f);
    c[ImGuiCol_HeaderHovered]        = ImVec4(0.155f, 0.185f, 0.285f, 0.94f);
    c[ImGuiCol_HeaderActive]         = ImVec4(0.220f, 0.255f, 0.420f, 1.00f);
    c[ImGuiCol_Separator]            = ImVec4(0.120f, 0.135f, 0.170f, 0.85f);
    c[ImGuiCol_SeparatorHovered]     = ImVec4(0.35f, 0.40f, 0.70f, 1.00f);
    c[ImGuiCol_SeparatorActive]      = ImVec4(0.49f, 0.55f, 1.00f, 1.00f);
    c[ImGuiCol_ResizeGrip]           = ImVec4(0.30f, 0.34f, 0.52f, 0.18f);
    c[ImGuiCol_ResizeGripHovered]    = ImVec4(0.42f, 0.48f, 0.80f, 0.45f);
    c[ImGuiCol_ResizeGripActive]     = ImVec4(0.49f, 0.55f, 1.00f, 0.75f);
    c[ImGuiCol_Tab]                  = ImVec4(0.055f, 0.062f, 0.080f, 1.00f);
    c[ImGuiCol_TabHovered]           = ImVec4(0.155f, 0.185f, 0.285f, 1.00f);
    c[ImGuiCol_TabSelected]          = ImVec4(0.115f, 0.135f, 0.220f, 1.00f);
    c[ImGuiCol_TabDimmed]            = ImVec4(0.045f, 0.050f, 0.065f, 1.00f);
    c[ImGuiCol_TabDimmedSelected]    = ImVec4(0.075f, 0.085f, 0.120f, 1.00f);
    c[ImGuiCol_DockingPreview]       = ImVec4(0.40f, 0.47f, 0.95f, 0.55f);
    c[ImGuiCol_DockingEmptyBg]       = ImVec4(0.030f, 0.034f, 0.044f, 1.00f);
    c[ImGuiCol_TableHeaderBg]        = ImVec4(0.065f, 0.074f, 0.096f, 1.00f);
    c[ImGuiCol_TableBorderStrong]    = ImVec4(0.120f, 0.135f, 0.175f, 0.90f);
    c[ImGuiCol_TableBorderLight]     = ImVec4(0.090f, 0.102f, 0.135f, 0.65f);
    c[ImGuiCol_TableRowBg]           = ImVec4(0.045f, 0.051f, 0.066f, 0.35f);
    c[ImGuiCol_TableRowBgAlt]        = ImVec4(0.070f, 0.079f, 0.103f, 0.32f);
    c[ImGuiCol_NavHighlight]         = ImVec4(0.49f, 0.55f, 1.00f, 0.78f);
}


vespera::UiRenderPacket make_editor_startup_splash_packet(
    const vespera::TextureData& texture,
    int viewport_width,
    int viewport_height
) {
    vespera::UiRenderPacket packet;
    packet.viewport_width = std::max(viewport_width, 1);
    packet.viewport_height = std::max(viewport_height, 1);
    packet.atlas = &texture;
    packet.atlas_revision = 1;
    if (!texture.valid()) return packet;

    const float vw = static_cast<float>(packet.viewport_width);
    const float vh = static_cast<float>(packet.viewport_height);
    const float iw = static_cast<float>(texture.width);
    const float ih = static_cast<float>(texture.height);
    const float scale = std::min(vw / iw, vh / ih);
    const float width = iw * scale;
    const float height = ih * scale;
    const float x0 = (vw - width) * 0.5f;
    const float y0 = (vh - height) * 0.5f;
    const float x1 = x0 + width;
    const float y1 = y0 + height;
    constexpr std::uint32_t white = 0xFFFFFFFFu;
    packet.vertices = {
        {x0, y0, 0.0f, 0.0f, white}, {x1, y0, 1.0f, 0.0f, white}, {x1, y1, 1.0f, 1.0f, white},
        {x0, y0, 0.0f, 0.0f, white}, {x1, y1, 1.0f, 1.0f, white}, {x0, y1, 0.0f, 1.0f, white},
    };
    return packet;
}

void present_editor_startup_splash(vespera::RenderBackend& renderer) {
    const auto splash = vespera::import_texture("branding/vespera_splash.png", "Vespera editor startup splash");
    if (!splash || !splash.texture.valid() || !renderer.begin_frame()) return;
    auto packet = make_editor_startup_splash_packet(
        splash.texture,
        std::max(renderer.target_width(), 1),
        std::max(renderer.target_height(), 1));
    renderer.render_ui(packet);
    (void)renderer.end_frame();
}

#if defined(_WIN32)
struct EditorWindowsFrameContext {
    SDL_Window* window = nullptr;
    vespera::RenderBackend* render_backend = nullptr;
    vespera::D3D12NativeAccess* d3d12 = nullptr;
    EditorD3D12DescriptorAllocator* imgui_descriptors = nullptr;
    EditorScenePreviewTarget* scene_preview_target = nullptr;
    EditorState* state = nullptr;
    std::chrono::steady_clock::time_point start_time{};
    bool* running = nullptr;
    bool* first_present_traced = nullptr;
    bool frame_in_progress = false;
    bool live_redraw_in_progress = false;
};

bool render_editor_windows_frame(EditorWindowsFrameContext& context, bool interactive) {
    if (!context.window || !context.render_backend || !context.d3d12
        || !context.imgui_descriptors || !context.scene_preview_target
        || !context.state || !context.running || !context.first_present_traced) {
        return false;
    }

    EditorState& state = *context.state;
    bool& running = *context.running;
    const auto frame_begin = std::chrono::steady_clock::now();
    if (context.frame_in_progress) {
        return running;
    }
    context.frame_in_progress = true;

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    repair_selection(state);
    if (interactive) update_editor_build_job(state);

    running = draw_menu(state) && running;
    if (interactive) {
        if (state.pending_action == PendingAction::Exit && !state.dirty && !state.request_unsaved_popup) {
            execute_pending_action(state, running);
        }
    }
    draw_main_dockspace(state);
    if (interactive) {
        handle_shortcuts(state);
    }
    draw_path_popups(state, running);
    draw_hierarchy(state);
    draw_scene_view(state);
    draw_scene_view_3d(state);
    draw_game_view(state);
    draw_ui_authoring(state);
    draw_rml_source_authoring(state);
    draw_inspector(state);
    draw_assets(state);
    draw_console(state);
    draw_build_game_window(state);

    ImGui::Render();
    SDL_SetWindowTitle(context.window, window_title(state).c_str());

    const auto now = std::chrono::steady_clock::now();
    const double total_seconds = std::chrono::duration<double>(now - context.start_time).count();
    const vespera::RenderFrameConfig frame_config{{0.018f, 0.024f, 0.035f, 1.0f}};
    const auto render_begin = std::chrono::steady_clock::now();
    if (!context.render_backend->begin_frame(frame_config)) {
        context.frame_in_progress = false;
        return running;
    }

    // During a Windows live resize, the OS temporarily blocks the normal application
    // loop. Redraw the editor chrome from SDL's exposed-event watcher, but avoid
    // rebuilding the full-size 3D preview texture for every intermediate mouse pixel.
    // The normal frame immediately refreshes the preview after the resize finishes.
    if (!context.live_redraw_in_progress) {
        int preview_pixel_width = 0;
        int preview_pixel_height = 0;
        const bool have_window_pixels = SDL_GetWindowSizeInPixels(
            context.window, &preview_pixel_width, &preview_pixel_height);
        if (have_window_pixels && preview_pixel_width > 0 && preview_pixel_height > 0
            && context.scene_preview_target->ensure(
                *context.d3d12,
                *context.imgui_descriptors,
                static_cast<UINT>(preview_pixel_width),
                static_cast<UINT>(preview_pixel_height))) {
            state.scene_view_3d.preview_texture = context.scene_preview_target->texture_id();
            state.game_view.preview_texture = context.scene_preview_target->texture_id();
        } else {
            state.scene_view_3d.preview_texture = ImTextureID_Invalid;
            state.game_view.preview_texture = ImTextureID_Invalid;
        }

        const auto play_update_begin = std::chrono::steady_clock::now();
        update_play_clock(state, total_seconds);
        const auto play_update_end = std::chrono::steady_clock::now();
        state.play_update_ms = std::chrono::duration<double, std::milli>(play_update_end - play_update_begin).count();
        bool rendered_preview = false;
        if (const auto viewport = scene_view_3d_pixel_viewport(state, context.window)) {
            context.render_backend->render_scene(
                state.scene,
                total_seconds,
                &state.scene_view_3d.camera,
                &*viewport
            );
            rendered_preview = true;
            if (preview_pixel_width > 0 && preview_pixel_height > 0) {
                state.scene_view_3d.preview_uv0 = {
                    static_cast<float>(viewport->x) / static_cast<float>(preview_pixel_width),
                    static_cast<float>(viewport->y) / static_cast<float>(preview_pixel_height)
                };
                state.scene_view_3d.preview_uv1 = {
                    static_cast<float>(viewport->x + viewport->width) / static_cast<float>(preview_pixel_width),
                    static_cast<float>(viewport->y + viewport->height) / static_cast<float>(preview_pixel_height)
                };
            }
        }

        if (const auto viewport = game_view_pixel_viewport(state, context.window)) {
            const bool playing = editor_is_playing(state);
            const vespera::Scene& game_scene = playing ? state.play_scene : state.scene;
            const double game_time = playing ? state.play_time_seconds : total_seconds;
            context.render_backend->render_scene(
                game_scene,
                game_time,
                &game_scene.camera,
                &*viewport
            );
            if (playing && state.play_rml_ui_loaded && state.play_rml_ui) {
                state.play_rml_view_width = std::max(1, viewport->width);
                state.play_rml_view_height = std::max(1, viewport->height);
                state.play_rml_ui->resize(state.play_rml_view_width, state.play_rml_view_height);
                const auto ui_packet = state.play_rml_ui->build_packet();
                context.render_backend->render_ui(ui_packet, &*viewport);
            } else if (playing && state.play_ui_loaded) {
                vespera::UiPointerState ui_pointer{};
                const ImGuiIO& ui_io = ImGui::GetIO();
                const float view_w = std::max(1.0f, state.game_view.content_max.x - state.game_view.content_min.x);
                const float view_h = std::max(1.0f, state.game_view.content_max.y - state.game_view.content_min.y);
                if (state.game_view.hovered && !state.game_view.input_captured) {
                    ui_pointer.available = true;
                    ui_pointer.position = {
                        std::clamp((ui_io.MousePos.x - state.game_view.content_min.x) / view_w, 0.0f, 1.0f) * static_cast<float>(viewport->width),
                        std::clamp((ui_io.MousePos.y - state.game_view.content_min.y) / view_h, 0.0f, 1.0f) * static_cast<float>(viewport->height)
                    };
                    ui_pointer.primary_down = ui_io.MouseDown[ImGuiMouseButton_Left];
                    ui_pointer.primary_pressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
                    ui_pointer.primary_released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
                }
                vespera::UiNavigationState ui_navigation{};
                if (state.game_view.focused && !state.game_view.input_captured) {
                    const bool tab = ImGui::IsKeyPressed(ImGuiKey_Tab, false);
                    ui_navigation.focus_next = tab && !ui_io.KeyShift;
                    ui_navigation.focus_previous = tab && ui_io.KeyShift;
                    ui_navigation.activate_pressed = ImGui::IsKeyPressed(ImGuiKey_Enter, false);
                }
                vespera::UiTextInputState ui_text_input{};
                if (state.game_view.focused && !state.game_view.input_captured) {
                    ui_text_input.text = state.game_view_text_input;
                    ui_text_input.backspace = state.game_view_backspace_pending;
                }
                auto* ui_runtime = state.play_runtime ? &state.play_runtime->ui_runtime_state() : nullptr;
                const auto ui_packet = state.play_ui_cache.build_packet(
                    state.play_ui_document, static_cast<float>(viewport->width), static_cast<float>(viewport->height),
                    ui_pointer, ui_runtime, ui_navigation, ui_text_input);
                state.game_view_text_input.clear();
                state.game_view_backspace_pending = false;
                context.render_backend->render_ui(ui_packet, &*viewport);
            }
            rendered_preview = true;
            if (preview_pixel_width > 0 && preview_pixel_height > 0) {
                state.game_view.preview_uv0 = {
                    static_cast<float>(viewport->x) / static_cast<float>(preview_pixel_width),
                    static_cast<float>(viewport->y) / static_cast<float>(preview_pixel_height)
                };
                state.game_view.preview_uv1 = {
                    static_cast<float>(viewport->x + viewport->width) / static_cast<float>(preview_pixel_width),
                    static_cast<float>(viewport->y + viewport->height) / static_cast<float>(preview_pixel_height)
                };
            }
        }

        if (rendered_preview && context.scene_preview_target->texture) {
            if (!context.d3d12->d3d12_copy_backbuffer_to_texture(context.scene_preview_target->texture.Get())) {
                editor_startup_trace("WARNING: failed to copy editor Scene/Game preview texture");
            }
        }
    }

    if (!context.d3d12->d3d12_prepare_overlay(context.imgui_descriptors->heap.Get())) {
        editor_startup_trace("ERROR: failed to prepare D3D12 overlay state");
        running = false;
    } else {
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), context.d3d12->d3d12_command_list());
        if (!context.render_backend->end_frame()) {
            editor_startup_trace("ERROR: D3D12 frame presentation failed");
            running = false;
        } else if (!*context.first_present_traced) {
            editor_startup_trace("First editor frame presented successfully");
            *context.first_present_traced = true;
        }
    }

    const auto frame_end = std::chrono::steady_clock::now();
    const auto to_ms = [](auto duration) { return std::chrono::duration<double, std::milli>(duration).count(); };
    state.performance.render = context.render_backend->frame_stats();
    state.performance.record_frame(
        to_ms(frame_end - frame_begin),
        state.play_update_ms,
        to_ms(frame_end - render_begin));
    context.frame_in_progress = false;
    return running;
}

bool SDLCALL editor_live_resize_event_watch(void* userdata, SDL_Event* event) {
    auto* context = static_cast<EditorWindowsFrameContext*>(userdata);
    if (!context || !event || !context->window || !context->render_backend) {
        return true;
    }

    // SDL guarantees WINDOW_EXPOSED is delivered on the main thread and explicitly
    // supports redrawing from an event watcher for Windows live-resize operations.
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
    if (context->state) ++context->state->performance.live_resize_redraws;
    context->render_backend->resize(pixel_width, pixel_height);
    render_editor_windows_frame(*context, false);
    context->live_redraw_in_progress = false;
    return true;
}
#endif


} // namespace

int main(int argc, char** argv) {
#if defined(_WIN32)
    editor_startup_trace(std::format("Vespera Editor {} startup", vespera::kEngineVersion), true);
#endif
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        std::format("Vespera Editor {}", vespera::kEngineVersion).c_str(),
        1440,
        900,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
    );
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // User-approved Vespera brand mark. Keep this SDL-level so editor branding
    // remains independent of D3D12 and survives future renderer backends.
    if (SDL_Surface* icon = SDL_LoadBMP("branding/vespera_icon_window.bmp")) {
        SDL_SetWindowIcon(window, icon);
        SDL_DestroySurface(icon);
    }

#if defined(_WIN32)
    editor_startup_trace("SDL window created");
    auto render_backend = vespera::create_default_render_backend();
    if (!render_backend || !render_backend->initialize(window)) {
        std::fprintf(stderr, "Vespera editor renderer initialization failed.\n");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    editor_startup_trace("Vespera D3D12 renderer initialized");

    int initial_pixel_width = 0;
    int initial_pixel_height = 0;
    if (SDL_GetWindowSizeInPixels(window, &initial_pixel_width, &initial_pixel_height)) {
        render_backend->resize(initial_pixel_width, initial_pixel_height);
    }
    // Present actual Vespera artwork while editor services initialize. Keep the
    // deliberate RC branding window readable on fast machines; initialization
    // work below counts toward it, so only the remaining interval is held.
    constexpr double kEditorStartupSplashMinimumSeconds = 4.0;
    present_editor_startup_splash(*render_backend);
    const auto editor_splash_presented_at = std::chrono::steady_clock::now();
    editor_startup_trace("Vespera startup splash presented");

    vespera::D3D12NativeAccess* d3d12 = vespera::d3d12_native_access(render_backend.get());
    if (!d3d12 || !d3d12->d3d12_device() || !d3d12->d3d12_command_queue() || !d3d12->d3d12_command_list()) {
        std::fprintf(stderr, "Vespera editor requires Direct3D 12 native renderer access on Windows.\n");
        render_backend->shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    EditorD3D12DescriptorAllocator imgui_descriptors;
    constexpr UINT kImGuiDescriptorCount = 64;
    D3D12_DESCRIPTOR_HEAP_DESC imgui_heap_desc{};
    imgui_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    imgui_heap_desc.NumDescriptors = kImGuiDescriptorCount;
    imgui_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(d3d12->d3d12_device()->CreateDescriptorHeap(
            &imgui_heap_desc,
            IID_PPV_ARGS(&imgui_descriptors.heap)))) {
        std::fprintf(stderr, "Failed to create the editor ImGui D3D12 descriptor heap.\n");
        render_backend->shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    imgui_descriptors.descriptor_size = d3d12->d3d12_device()->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    imgui_descriptors.used.assign(kImGuiDescriptorCount, false);
    EditorScenePreviewTarget scene_preview_target;
#else
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);
#endif

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = "vespera_editor_v2.ini";
    apply_editor_style();

#if defined(_WIN32)
    if (!ImGui_ImplSDL3_InitForD3D(window)) {
        std::fprintf(stderr, "ImGui SDL3/D3D backend initialization failed.\n");
        ImGui::DestroyContext();
        render_backend->shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    ImGui_ImplDX12_InitInfo dx12_init{};
    dx12_init.Device = d3d12->d3d12_device();
    dx12_init.CommandQueue = d3d12->d3d12_command_queue();
    dx12_init.NumFramesInFlight = 2;
    dx12_init.RTVFormat = d3d12->d3d12_backbuffer_format();
    dx12_init.DSVFormat = DXGI_FORMAT_UNKNOWN;
    dx12_init.SrvDescriptorHeap = imgui_descriptors.heap.Get();
    dx12_init.SrvDescriptorAllocFn = &EditorD3D12DescriptorAllocator::allocate;
    dx12_init.SrvDescriptorFreeFn = &EditorD3D12DescriptorAllocator::free;
    dx12_init.UserData = &imgui_descriptors;
    if (!ImGui_ImplDX12_Init(&dx12_init)) {
        std::fprintf(stderr, "ImGui Direct3D 12 backend initialization failed.\n");
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        render_backend->shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    editor_startup_trace("Dear ImGui D3D12 backend initialized");
    if (!scene_preview_target.reserve_descriptor(imgui_descriptors)) {
        std::fprintf(stderr, "Failed to reserve a D3D12 descriptor for the editor 3D preview.\n");
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        render_backend->shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
#else
    if (!ImGui_ImplSDL3_InitForSDLRenderer(window, renderer)) {
        std::fprintf(stderr, "ImGui SDL3 backend initialization failed.\n");
        ImGui::DestroyContext();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    if (!ImGui_ImplSDLRenderer3_Init(renderer)) {
        std::fprintf(stderr, "ImGui SDLRenderer3 backend initialization failed.\n");
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
#endif

    EditorState state;
    register_core_extension_capabilities(state);
    push_console(state, ConsoleEntry::Level::Info, std::format("Vespera Editor {} started.", vespera::kEngineVersion));
#if defined(_WIN32)
    push_console(state, ConsoleEntry::Level::Info, std::format("Editor renderer: {}", render_backend->name()));
    push_console(state, ConsoleEntry::Level::Info, "Scene uses the live Vespera renderer. RMB + WASD/QE to navigate; Sector provides precise top-down authoring.");
#endif
    load_managed_metadata(state, true);

    std::filesystem::path initial_target = "VesperaReference.vesperaproject";
    bool startup_target_set = false;
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
        if (!startup_target_set && !arg.empty() && !arg.starts_with("--")) {
            initial_target = std::filesystem::path(arg);
            startup_target_set = true;
        }
    }
    const bool initial_is_project = initial_target.extension() == ".vesperaproject";
    const bool initial_loaded = initial_is_project
        ? open_project(state, initial_target)
        : open_scene(state, initial_target);
    if (!initial_loaded) {
        state.open_path_text = initial_target.string();
        state.save_path_text = initial_target.string();
        push_console(state, ConsoleEntry::Level::Warning,
            "Editor is open without a loaded startup scene. Open a .vesperaproject or use File > Open Scene.");
    }
    if (start_automation) {
        start_automation_server(state, state.automation_port);
    }

#if defined(_WIN32)
    const auto editor_splash_minimum = std::chrono::duration<double>(kEditorStartupSplashMinimumSeconds);
    const auto editor_splash_visible_for = std::chrono::steady_clock::now() - editor_splash_presented_at;
    if (editor_splash_visible_for < editor_splash_minimum) {
        const auto deadline = std::chrono::steady_clock::now() + (editor_splash_minimum - editor_splash_visible_for);
        // Keep the native window responsive during the deliberate RC branding
        // interval rather than sleeping the UI thread for several seconds at once.
        while (std::chrono::steady_clock::now() < deadline) {
            SDL_PumpEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    editor_startup_trace("Initial scene load complete; entering frame loop");
    bool first_present_traced = false;
#endif
    bool running = true;
    const auto start_time = std::chrono::steady_clock::now();
#if defined(_WIN32)
    EditorWindowsFrameContext windows_frame_context{
        window,
        render_backend.get(),
        d3d12,
        &imgui_descriptors,
        &scene_preview_target,
        &state,
        start_time,
        &running,
        &first_present_traced
    };
    const bool live_resize_watch_installed = SDL_AddEventWatch(
        editor_live_resize_event_watch, &windows_frame_context);
    if (!live_resize_watch_installed) {
        push_console(state, ConsoleEntry::Level::Warning,
            std::format("Live-resize redraw watcher unavailable: {}", SDL_GetError()));
    }
#endif
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) {
                request_exit(state);
            }
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window)) {
                request_exit(state);
            }
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST && state.game_view.input_captured) {
                set_game_input_capture(state, false);
            }
            if (editor_is_playing(state) && state.game_view.focused && !state.game_view.input_captured) {
                if (event.type == SDL_EVENT_TEXT_INPUT && event.text.text) {
                    state.game_view_text_input += event.text.text;
                }
                if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_BACKSPACE) {
                    state.game_view_backspace_pending = true;
                }
            }
            if (event.type == SDL_EVENT_KEY_DOWN
                && event.key.scancode == SDL_SCANCODE_ESCAPE
                && state.game_view.input_captured) {
                set_game_input_capture(state, false);
            }
#if defined(_WIN32)
            if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED
                && event.window.windowID == SDL_GetWindowID(window)) {
                render_backend->resize(event.window.data1, event.window.data2);
            }
#endif
        }

        // Automation transport is polled from the editor/main thread only. Socket I/O
        // never mutates Scene, renderer, audio, managed state or undo history directly.
        poll_automation_server(state);

#if defined(_WIN32)
        render_editor_windows_frame(windows_frame_context, true);
#else
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        repair_selection(state);
        update_editor_build_job(state);

        running = draw_menu(state) && running;
        if (state.pending_action == PendingAction::Exit && !state.dirty && !state.request_unsaved_popup) {
            execute_pending_action(state, running);
        }
        draw_main_dockspace(state);
        handle_shortcuts(state);
        draw_path_popups(state, running);
        draw_hierarchy(state);
        draw_scene_view(state);
        draw_scene_view_3d(state);
        draw_game_view(state);
        draw_ui_authoring(state);
        draw_rml_source_authoring(state);
        draw_inspector(state);
        draw_assets(state);
        draw_console(state);
        draw_build_game_window(state);

        ImGui::Render();
        SDL_SetWindowTitle(window, window_title(state).c_str());
        SDL_SetRenderDrawColor(renderer, 18, 20, 24, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
#endif
    }

    // Tear editor-owned play services down while SDL/.NET host dependencies are
    // still alive. EditorState itself outlives SDL_Quit() in this function, so
    // relying on PlayRuntime's destructor here would shut audio down too late.
    if (editor_is_playing(state)) {
        stop_play_mode(state);
    }
    stop_automation_server(state);

#if defined(_WIN32)
    if (live_resize_watch_installed) {
        SDL_RemoveEventWatch(editor_live_resize_event_watch, &windows_frame_context);
    }
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    imgui_descriptors.heap.Reset();
    render_backend->shutdown();
#else
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
#endif
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

