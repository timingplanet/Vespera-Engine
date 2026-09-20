#include "editor_session_dialogs.hpp"

#include "editor_history.hpp"
#include "editor_project_session.hpp"
#include "editor_state.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <utility>

namespace vespera::editor {

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





































} // namespace vespera::editor
