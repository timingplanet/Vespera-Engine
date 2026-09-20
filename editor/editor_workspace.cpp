#include "editor_workspace.hpp"

#include "editor_command.hpp"
#include "editor_managed.hpp"
#include "editor_play_controls.hpp"
#include "editor_project_session.hpp"
#include "editor_state.hpp"
#include "editor_style.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <format>
#include <string>

namespace vespera::editor {
namespace {

constexpr float kToolbarHeight = 48.0f;
constexpr float kStatusBarHeight = 25.0f;

void set_button_semantic_color(ImVec4 color) {
    ImVec4 hovered = color;
    hovered.x = std::min(1.0f, hovered.x + 0.08f);
    hovered.y = std::min(1.0f, hovered.y + 0.08f);
    hovered.z = std::min(1.0f, hovered.z + 0.08f);
    ImVec4 active = color;
    active.x = std::max(0.0f, active.x - 0.08f);
    active.y = std::max(0.0f, active.y - 0.08f);
    active.z = std::max(0.0f, active.z - 0.08f);
    ImGui::PushStyleColor(ImGuiCol_Button, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
}

void end_button_semantic_color() {
    ImGui::PopStyleColor(3);
}

void tooltip(const char* text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::SetTooltip("%s", text);
    }
}

std::string selection_summary(const EditorState& state) {
    switch (state.selection.kind) {
        case SelectionKind::Camera:
            return "Camera selected";
        case SelectionKind::Sector:
            if (state.selection.index < state.scene.world.sectors().size()) {
                return std::format("Sector: {}", state.scene.world.sectors()[state.selection.index].name);
            }
            return "Sector selected";
        case SelectionKind::Entity:
            if (state.selected_entity_ids.size() > 1) {
                return std::format("{} entities selected", state.selected_entity_ids.size());
            }
            if (state.selection.index < state.scene.entities.size()) {
                return std::format("Entity: {}", state.scene.entities[state.selection.index].name);
            }
            return "Entity selected";
        case SelectionKind::Material:
            if (state.selection.index < state.scene.world.materials().size()) {
                return std::format("Material: {}", state.scene.world.materials()[state.selection.index].name);
            }
            return "Material selected";
        case SelectionKind::SpriteClip:
            if (state.selection.index < state.scene.sprite_clips.size()) {
                return std::format("Sprite clip: {}", state.scene.sprite_clips[state.selection.index].name);
            }
            return "Sprite clip selected";
        case SelectionKind::Asset:
            return state.selection.asset_id.empty() ? "Asset selected" : "Asset selected";
        case SelectionKind::None:
        default:
            return "No selection";
    }
}

void draw_workspace_toolbar(EditorState& state) {
    const auto& palette = editor_ui_palette();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, palette.toolbar_bg);
    ImGui::BeginChild("VesperaWorkspaceToolbar", ImVec2(0.0f, kToolbarHeight), false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::SetCursorPosY(8.0f);
    if (ImGui::BeginTable(
            "##WorkspaceToolbarLayout", 3,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableSetupColumn("Project", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Transport", ImGuiTableColumnFlags_WidthFixed, 210.0f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(palette.accent, "VESPERA");
        ImGui::SameLine(0.0f, 12.0f);
        if (state.project_loaded) {
            ImGui::TextUnformatted(state.project.name.c_str());
        } else {
            ImGui::TextDisabled("No Project");
        }
        if (!state.scene_path.empty()) {
            ImGui::SameLine(0.0f, 10.0f);
            ImGui::TextDisabled("/  %s", state.scene_path.filename().string().c_str());
        }

        ImGui::TableSetColumnIndex(1);
        constexpr float transport_width = 62.0f + 6.0f + 68.0f + 6.0f + 54.0f;
        const float transport_available = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (transport_available - transport_width) * 0.5f));

        if (state.play_state == EditorPlayState::Editing) {
            set_button_semantic_color(palette.accent_dim);
            if (ImGui::Button("Play", ImVec2(62.0f, 28.0f))) start_play_mode(state);
            end_button_semantic_color();
            tooltip("Enter Play Mode (Ctrl+P)");
        } else {
            ImVec4 stop_color = palette.danger;
            stop_color.x *= 0.58f;
            stop_color.y *= 0.58f;
            stop_color.z *= 0.58f;
            set_button_semantic_color(stop_color);
            if (ImGui::Button("Stop", ImVec2(62.0f, 28.0f))) stop_play_mode(state);
            end_button_semantic_color();
            tooltip("Stop Play Mode and restore the edit scene (Ctrl+P)");
        }

        ImGui::SameLine(0.0f, 6.0f);
        ImGui::BeginDisabled(!editor_is_playing(state));
        if (ImGui::Button(state.play_state == EditorPlayState::Paused ? "Resume" : "Pause", ImVec2(68.0f, 28.0f))) {
            toggle_play_pause(state);
        }
        tooltip(state.play_state == EditorPlayState::Paused ? "Resume Play Mode" : "Pause Play Mode");
        ImGui::SameLine(0.0f, 6.0f);
        if (ImGui::Button("Step", ImVec2(54.0f, 28.0f))) step_play_mode(state);
        tooltip("Advance one frame while paused");
        ImGui::EndDisabled();

        ImGui::TableSetColumnIndex(2);
        constexpr float actions_width = 64.0f + 6.0f + 78.0f + 6.0f + 92.0f + 6.0f + 72.0f + 6.0f + 82.0f;
        const float action_available = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, action_available - actions_width));

        ImGui::BeginDisabled(state.scene_path.empty() || editor_is_playing(state));
        if (state.dirty) {
            ImVec4 save_color = palette.warning;
            save_color.x *= 0.42f;
            save_color.y *= 0.42f;
            save_color.z *= 0.42f;
            set_button_semantic_color(save_color);
        }
        if (ImGui::Button("Save", ImVec2(64.0f, 28.0f))) save_scene(state, state.scene_path);
        if (state.dirty) end_button_semantic_color();
        tooltip("Save Scene (Ctrl+S)");
        ImGui::EndDisabled();

        ImGui::SameLine(0.0f, 6.0f);
        ImGui::BeginDisabled(!state.project_loaded || editor_is_playing(state));
        if (ImGui::Button("Build C#", ImVec2(78.0f, 28.0f))) build_managed_scripts(state);
        tooltip("Compile the project C# scripts");
        ImGui::SameLine(0.0f, 6.0f);
        if (ImGui::Button("Build Game", ImVec2(92.0f, 28.0f))) state.build_job.window_open = true;
        tooltip("Open standalone game build settings (Ctrl+Shift+B)");
        ImGui::EndDisabled();

        ImGui::SameLine(0.0f, 6.0f);
        if (ImGui::Button("Validate", ImVec2(72.0f, 28.0f))) validate_scene_to_console(state);
        tooltip("Validate the current scene");
        ImGui::SameLine(0.0f, 6.0f);
        if (ImGui::Button("Frame View", ImVec2(82.0f, 28.0f))) {
            state.scene_view.frame_all_pending = true;
            state.scene_view_3d.frame_selection_pending = true;
        }
        tooltip("Frame the active view/selection (F / Shift+F)");

        ImGui::EndTable();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void draw_workspace_status_bar(EditorState& state) {
    const auto& palette = editor_ui_palette();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, palette.status_bg);
    ImGui::BeginChild("VesperaWorkspaceStatus", ImVec2(0.0f, kStatusBarHeight), false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::SetCursorPosY(3.0f);

    if (state.play_state == EditorPlayState::Paused) {
        draw_status_badge("PAUSED", palette.warning);
    } else if (editor_is_playing(state)) {
        draw_status_badge("PLAY", palette.success);
    } else {
        draw_status_badge("EDIT", palette.info);
    }

    ImGui::SameLine(0.0f, 8.0f);
    if (editor_is_playing(state)) {
        ImGui::TextDisabled("%.2f s", state.play_time_seconds);
    } else if (state.dirty) {
        ImGui::TextColored(palette.warning, "Unsaved changes");
    } else {
        ImGui::TextDisabled("Saved");
    }

    draw_toolbar_separator(16.0f);
    const std::string selection = selection_summary(state);
    ImGui::TextDisabled("%s", selection.c_str());

    std::string right_text;
    if (state.build_job.running) {
        right_text = state.build_job.stage.empty() ? "Build running" : std::format("Build: {}", state.build_job.stage);
    } else if (state.last_asset_report.broken_dependencies != 0 || state.last_asset_report.stale_fallback_paths != 0) {
        right_text = std::format(
            "Assets: {} broken / {} stale",
            state.last_asset_report.broken_dependencies,
            state.last_asset_report.stale_fallback_paths);
    } else if (state.project_loaded) {
        right_text = std::format("Assets: {}", state.asset_catalog.records().size());
    }

    const bool automation_running = state.automation_server && state.automation_server->running();
    if (automation_running) {
        if (!right_text.empty()) right_text += "   |   ";
        right_text += std::format("MCP :{}", static_cast<unsigned int>(state.automation_port));
    }

    if (!right_text.empty()) {
        const float width = ImGui::CalcTextSize(right_text.c_str()).x;
        const float target_x = ImGui::GetWindowWidth() - width - 10.0f;
        if (target_x > ImGui::GetCursorPosX() + 12.0f) {
            ImGui::SameLine(target_x);
            const bool asset_problem = state.last_asset_report.broken_dependencies != 0
                || state.last_asset_report.stale_fallback_paths != 0;
            if (asset_problem && !state.build_job.running) {
                ImGui::TextColored(palette.warning, "%s", right_text.c_str());
            } else {
                ImGui::TextDisabled("%s", right_text.c_str());
            }
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

} // namespace

void setup_default_docking(ImGuiID dockspace_id, ImVec2 size, bool force_reset = false) {
    if (!force_reset && ImGui::DockBuilderGetNode(dockspace_id) != nullptr) {
        return;
    }

    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, size);

    ImGuiID center = dockspace_id;
    // 1.1 UI cleanup: keep Hierarchy compact, give Inspector enough width for
    // component/property editing, and make Project/Console a useful work shelf.
    ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.17f, nullptr, &center);
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.30f, nullptr, &center);
    ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.30f, nullptr, &center);

    ImGui::DockBuilderDockWindow("Hierarchy", left);
    ImGui::DockBuilderDockWindow("Project", bottom);
    ImGui::DockBuilderDockWindow("Console", bottom);
    ImGui::DockBuilderDockWindow("Inspector", right);
    ImGui::DockBuilderDockWindow("Scene", center);
    ImGui::DockBuilderDockWindow("Game", center);
    ImGui::DockBuilderDockWindow("Sector", center);
    ImGui::DockBuilderFinish(dockspace_id);
}

void draw_main_dockspace(EditorState& state) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking
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

    const ImGuiID dockspace_id = ImGui::GetID("VesperaDockSpace##Layout2");
    ImVec2 dock_size = ImGui::GetContentRegionAvail();
    dock_size.y = std::max(0.0f, dock_size.y - kStatusBarHeight);
    setup_default_docking(dockspace_id, dock_size, state.request_reset_layout);
    state.request_reset_layout = false;
    ImGui::DockSpace(dockspace_id, dock_size, ImGuiDockNodeFlags_PassthruCentralNode);

    draw_workspace_status_bar(state);
    ImGui::End();
}

} // namespace vespera::editor
