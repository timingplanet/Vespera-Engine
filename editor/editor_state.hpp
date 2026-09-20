#pragma once

#include <vespera/assets/asset_catalog.hpp>
#include <vespera/assets/material_asset.hpp>
#include <vespera/assets/texture_importer.hpp>
#include <vespera/core/game.hpp>
#include <vespera/project/project.hpp>
#include <vespera/render/render_backend.hpp>
#include <vespera/scene/scene.hpp>
#include <vespera/ui/rmlui_surface.hpp>
#include <vespera/ui/ui_render.hpp>

#include "automation_server.hpp"
#include "editor_command.hpp"
#include "extension_api.hpp"
#include "play_runtime.hpp"
#include "rml_source_editor.hpp"

#include <imgui.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <future>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vespera::editor {

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
    // SDL relative mouse mode is window-scoped. Keep the window ID that
    // successfully captured input so focus-loss/Alt-Tab can release that same
    // window even after SDL_GetKeyboardFocus() has already become null.
    std::uint32_t capture_window_id = 0;
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
    std::string stage = "Ready";
    std::string latest_output;
    float progress = 0.0f;
    std::chrono::steady_clock::time_point started_at{};
    std::chrono::steady_clock::time_point completed_at{};
};

struct EditorState {
    vespera::Scene scene;
    Selection selection;
    std::vector<vespera::SceneObjectId> selected_entity_ids;
    vespera::SceneObjectId hierarchy_anchor_id = vespera::kInvalidSceneObjectId;
    SceneViewState scene_view;
    SceneView3DState scene_view_3d;
    GameViewState game_view;
    // Vulkan/Linux renders Scene/Game canvases inside Dear ImGui draw order via
    // backend callbacks so dock/window backgrounds cannot paint over them. D3D12
    // continues to use the copied preview texture path.
    bool direct_render_previews = false;
    ImDrawCallback direct_scene_draw_callback = nullptr;
    void* direct_scene_draw_user_data = nullptr;
    ImDrawCallback direct_game_draw_callback = nullptr;
    void* direct_game_draw_user_data = nullptr;
    std::string game_view_text_input;
    bool game_view_backspace_pending = false;
    EditorPlayState play_state = EditorPlayState::Editing;
    vespera::Scene play_scene;
    std::unique_ptr<vespera::editor::PlayRuntime> play_runtime;
    std::vector<vespera::ManagedRuntimeEvent> last_runtime_events;
    std::uint64_t last_runtime_event_sequence = 0;
    std::uint32_t last_runtime_assembly_generation = 0;
    // Runtime UI backends during Play Mode. New projects prefer project startup RML;
    // legacy .slui remains a compatibility fallback until migration is complete.
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

} // namespace vespera::editor
