#pragma once

#include <imgui.h>

#include <string_view>

namespace vespera::editor {

// Semantic editor colors live here so panels do not each invent slightly
// different blues/greens/yellows/reds as the UI evolves.
struct EditorUiPalette {
    ImVec4 accent;
    ImVec4 accent_hover;
    ImVec4 accent_dim;
    ImVec4 success;
    ImVec4 warning;
    ImVec4 danger;
    ImVec4 info;
    ImVec4 muted;
    ImVec4 toolbar_bg;
    ImVec4 status_bg;
    ImVec4 canvas_border;
};

[[nodiscard]] const EditorUiPalette& editor_ui_palette();

void apply_editor_style();

// Lightweight chrome helpers shared by the workspace and the most frequently
// visible panels. They intentionally remain visual-only and carry no editor
// state or behavior.
void draw_status_badge(std::string_view text, ImVec4 color);
void draw_toolbar_separator(float height = 20.0f);
void draw_panel_heading(std::string_view title, std::string_view detail = {});

} // namespace vespera::editor
