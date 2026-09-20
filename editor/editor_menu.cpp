#include "editor_menu.hpp"

#include "automation_tools.hpp"
#include "editor_asset_interactions.hpp"
#include "editor_assets.hpp"
#include "editor_automation.hpp"
#include "editor_build_pipeline.hpp"
#include "editor_history.hpp"
#include "editor_managed.hpp"
#include "editor_play_controls.hpp"
#include "editor_prefabs.hpp"
#include "editor_project_session.hpp"
#include "editor_scene_commands.hpp"
#include "editor_selection.hpp"
#include "editor_session_dialogs.hpp"
#include "editor_state.hpp"
#include "extension_api.hpp"
#include "editor_style.hpp"

#include <vespera/core/version.hpp>

#include <imgui.h>

namespace vespera::editor {

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
        if (ImGui::MenuItem("Exit")) {
            request_exit(state);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Build")) {
        const bool can_build = state.project_loaded && !editor_is_playing(state);
        if (ImGui::MenuItem("Build C# Scripts", nullptr, false, can_build)) {
            build_managed_scripts(state);
        }
        if (ImGui::MenuItem("Build Game...", "Ctrl+Shift+B", false, can_build)) {
            state.build_job.window_open = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Development", nullptr, false, can_build && !state.build_job.running)) {
            (void)start_editor_build_job(state, "Development", false);
        }
        if (ImGui::MenuItem("Development + Run", nullptr, false, can_build && !state.build_job.running)) {
            (void)start_editor_build_job(state, "Development", true);
        }
        if (ImGui::MenuItem("Release", nullptr, false, can_build && !state.build_job.running)) {
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
        if (ImGui::MenuItem("Refresh C# Metadata")) {
            load_managed_metadata(state, true);
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("Extensions")) {
            const auto& palette = editor_ui_palette();
            ImGui::TextDisabled("Extension API v%u", vespera::editor::kEditorExtensionApiVersion);
            ImGui::Separator();
            if (state.extension_registry.extensions().empty()) {
                ImGui::TextDisabled("No extensions registered.");
            } else {
                for (const auto& extension : state.extension_registry.extensions()) {
                    ImGui::TextUnformatted(extension.display_name.c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", extension.version.c_str());
                }
            }
            ImGui::Separator();
            ImGui::TextColored(palette.info, "%zu editor command(s) available", state.extension_registry.commands().size());
            ImGui::TextDisabled("Built-in extensions are active.");
            ImGui::TextDisabled("Third-party extension loading is not available yet.");
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("Automation / MCP")) {
            const auto& palette = editor_ui_palette();
            const bool automation_running = state.automation_server && state.automation_server->running();
            ImGui::TextColored(automation_running ? palette.success : palette.muted,
                "%s", automation_running ? "Local automation running" : "Local automation stopped");
            ImGui::TextDisabled("127.0.0.1:%u", static_cast<unsigned int>(state.automation_port));
            ImGui::Separator();
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
            ImGui::TextDisabled("%zu MCP tools available", vespera::editor::kAutomationTools.size());
            ImGui::TextDisabled("Localhost only; remote connections are not accepted.");
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
        if (ImGui::BeginMenu("Keyboard & Mouse")) {
            ImGui::TextDisabled("Editor");
            ImGui::TextUnformatted("Ctrl+O          Open scene");
            ImGui::TextUnformatted("Ctrl+S          Save scene");
            ImGui::TextUnformatted("Ctrl+Shift+B    Build Game");
            ImGui::TextUnformatted("Ctrl+P          Play / Stop");
            ImGui::Separator();
            ImGui::TextDisabled("Scene view");
            ImGui::TextUnformatted("RMB + WASD/QE   Navigate");
            ImGui::TextUnformatted("F / Shift+F     Frame view / selection");
            ImGui::Separator();
            ImGui::TextDisabled("Sector view");
            ImGui::TextUnformatted("Drag            Move vertices/entities");
            ImGui::TextUnformatted("Middle drag     Pan");
            ImGui::TextUnformatted("Wheel           Zoom");
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("About Vespera")) {
            ImGui::TextUnformatted("Vespera Engine");
            ImGui::TextDisabled("Version %s", vespera::kEngineVersion.data());
            ImGui::Separator();
            ImGui::TextDisabled("Open-source game engine editor");
            ImGui::EndMenu();
        }
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


} // namespace vespera::editor
