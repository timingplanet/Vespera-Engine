#include "editor_game_view.hpp"

#include "editor_play_controls.hpp"
#include "editor_state.hpp"
#include "editor_style.hpp"

#include <vespera/ui/ui_render.hpp>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>

namespace vespera::editor {

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
    const auto& palette = editor_ui_palette();
    if (playing) {
        if (state.play_state == EditorPlayState::Paused) {
            draw_status_badge("PAUSED", palette.warning);
        } else {
            draw_status_badge("PLAYING", palette.success);
        }
        ImGui::SameLine(0.0f, 8.0f);
        ImGui::TextDisabled("%.2f s", state.play_time_seconds);
        if (state.play_runtime) {
            const auto& runtime_status = state.play_runtime->status();
            ImGui::SameLine(0.0f, 10.0f);
            if (runtime_status.managed_ready) {
                ImGui::TextColored(palette.success, "C# %zu", runtime_status.managed_script_count);
            } else {
                ImGui::TextDisabled("game running");
            }
        }
        ImGui::SameLine(0.0f, 10.0f);
        if (view.input_captured) {
            draw_status_badge("INPUT", palette.accent);
            ImGui::SameLine(0.0f, 7.0f);
            ImGui::TextDisabled("Esc releases");
        } else {
            ImGui::TextDisabled("UI active  |  click empty viewport for mouse look");
        }
    } else {
        ImGui::TextDisabled("Edit preview  |  press Play to run scripts and game logic");
    }

    ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    canvas_size.x = std::max(canvas_size.x, 64.0f);
    canvas_size.y = std::max(canvas_size.y, 64.0f);
    const ImVec2 canvas_min = ImGui::GetCursorScreenPos();
    if (!state.direct_render_previews && view.preview_texture != ImTextureID_Invalid) {
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

    if (state.direct_render_previews && view.visible && state.direct_game_draw_callback) {
        ImDrawList* preview_draw = ImGui::GetWindowDrawList();
        preview_draw->AddCallback(state.direct_game_draw_callback, state.direct_game_draw_user_data);
        preview_draw->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    }

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
        ? ImGui::GetColorU32(palette.accent)
        : ImGui::GetColorU32(palette.canvas_border);
    draw->AddRect(canvas_min, canvas_max, border, 0.0f, 0, view.input_captured ? 2.0f : 1.0f);
    if (!state.direct_render_previews && view.preview_texture == ImTextureID_Invalid) {
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
            ImGui::GetColorU32(ImVec4(0.03f, 0.035f, 0.045f, 0.86f)), 3.0f);
        draw->AddText(pos, ImGui::GetColorU32(view.input_captured ? ImGuiCol_Text : ImGuiCol_TextDisabled), note);
    }

    ImGui::End();
}



} // namespace vespera::editor
