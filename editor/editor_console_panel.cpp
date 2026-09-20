#include "editor_console_panel.hpp"

#include "editor_console.hpp"
#include "editor_state.hpp"
#include "editor_style.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <cfloat>
#include <string>

namespace vespera::editor {

void draw_console(EditorState& state) {
    ImGui::Begin("Console");
    const auto& palette = editor_ui_palette();

    std::size_t info_count = 0, warn_count = 0, error_count = 0;
    for (const auto& entry : state.console) {
        if (entry.level == ConsoleEntry::Level::Error) ++error_count;
        else if (entry.level == ConsoleEntry::Level::Warning) ++warn_count;
        else ++info_count;
    }

    if (ImGui::BeginTable(
            "##ConsoleToolbar", 2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableSetupColumn("Filters", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 232.0f);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::Checkbox("Info", &state.console_show_info);
        ImGui::SameLine();
        ImGui::TextDisabled("%zu", info_count);
        ImGui::SameLine(0.0f, 12.0f);
        ImGui::Checkbox("Warnings", &state.console_show_warnings);
        ImGui::SameLine();
        ImGui::TextColored(palette.warning, "%zu", warn_count);
        ImGui::SameLine(0.0f, 12.0f);
        ImGui::Checkbox("Errors", &state.console_show_errors);
        ImGui::SameLine();
        ImGui::TextColored(palette.danger, "%zu", error_count);

        ImGui::TableSetColumnIndex(1);
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

        ImGui::EndTable();
    }
    ImGui::Separator();

    // Render the filtered log as one read-only text surface so normal desktop
    // selection works: click-drag arbitrary text and press Ctrl+C. The toolbar
    // copy actions remain useful for bulk export.
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

} // namespace vespera::editor
