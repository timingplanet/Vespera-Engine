#include "editor_style.hpp"

#include <algorithm>
#include <string>

namespace vespera::editor {

const EditorUiPalette& editor_ui_palette() {
    static const EditorUiPalette palette{
        .accent = ImVec4(0.50f, 0.57f, 1.00f, 1.00f),
        .accent_hover = ImVec4(0.60f, 0.66f, 1.00f, 1.00f),
        .accent_dim = ImVec4(0.24f, 0.29f, 0.52f, 1.00f),
        .success = ImVec4(0.47f, 0.82f, 0.62f, 1.00f),
        .warning = ImVec4(0.95f, 0.72f, 0.28f, 1.00f),
        .danger = ImVec4(0.96f, 0.43f, 0.38f, 1.00f),
        .info = ImVec4(0.55f, 0.73f, 1.00f, 1.00f),
        .muted = ImVec4(0.50f, 0.54f, 0.62f, 1.00f),
        .toolbar_bg = ImVec4(0.031f, 0.035f, 0.046f, 1.00f),
        .status_bg = ImVec4(0.028f, 0.032f, 0.042f, 1.00f),
        .canvas_border = ImVec4(0.35f, 0.39f, 0.48f, 0.92f),
    };
    return palette;
}

void apply_editor_style() {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    const auto& p = editor_ui_palette();

    // 1.1 UI hardening: a compact tooling shell with restrained rounding and
    // consistent spacing. Panels keep their existing workflows; this pass only
    // unifies presentation and chrome.
    style.WindowRounding = 4.0f;
    style.ChildRounding = 4.0f;
    style.PopupRounding = 5.0f;
    style.FrameRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;
    style.WindowPadding = ImVec2(9.0f, 8.0f);
    style.FramePadding = ImVec2(8.0f, 4.0f);
    style.ItemSpacing = ImVec2(7.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
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
    c[ImGuiCol_ChildBg]              = ImVec4(0.050f, 0.056f, 0.071f, 1.00f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.052f, 0.058f, 0.075f, 0.99f);
    c[ImGuiCol_Border]               = ImVec4(0.125f, 0.140f, 0.180f, 0.88f);
    c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]              = ImVec4(0.073f, 0.082f, 0.106f, 1.00f);
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.102f, 0.116f, 0.155f, 1.00f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.124f, 0.142f, 0.198f, 1.00f);
    c[ImGuiCol_TitleBg]              = ImVec4(0.039f, 0.044f, 0.056f, 1.00f);
    c[ImGuiCol_TitleBgActive]        = ImVec4(0.050f, 0.056f, 0.072f, 1.00f);
    c[ImGuiCol_MenuBarBg]            = p.toolbar_bg;
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0.033f, 0.038f, 0.050f, 1.00f);
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.145f, 0.160f, 0.205f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.205f, 0.225f, 0.290f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.260f, 0.285f, 0.365f, 1.00f);
    c[ImGuiCol_CheckMark]            = p.accent;
    c[ImGuiCol_SliderGrab]           = ImVec4(0.41f, 0.48f, 0.92f, 1.00f);
    c[ImGuiCol_SliderGrabActive]     = p.accent_hover;
    c[ImGuiCol_Button]               = ImVec4(0.100f, 0.115f, 0.158f, 1.00f);
    c[ImGuiCol_ButtonHovered]        = ImVec4(0.165f, 0.198f, 0.300f, 1.00f);
    c[ImGuiCol_ButtonActive]         = ImVec4(0.225f, 0.265f, 0.430f, 1.00f);
    c[ImGuiCol_Header]               = ImVec4(0.100f, 0.115f, 0.158f, 0.86f);
    c[ImGuiCol_HeaderHovered]        = ImVec4(0.150f, 0.180f, 0.278f, 0.94f);
    c[ImGuiCol_HeaderActive]         = ImVec4(0.215f, 0.250f, 0.410f, 1.00f);
    c[ImGuiCol_Separator]            = ImVec4(0.115f, 0.130f, 0.165f, 0.88f);
    c[ImGuiCol_SeparatorHovered]     = ImVec4(0.35f, 0.40f, 0.70f, 1.00f);
    c[ImGuiCol_SeparatorActive]      = p.accent;
    c[ImGuiCol_ResizeGrip]           = ImVec4(0.30f, 0.34f, 0.52f, 0.18f);
    c[ImGuiCol_ResizeGripHovered]    = ImVec4(0.42f, 0.48f, 0.80f, 0.45f);
    c[ImGuiCol_ResizeGripActive]     = ImVec4(p.accent.x, p.accent.y, p.accent.z, 0.75f);
    c[ImGuiCol_Tab]                  = ImVec4(0.052f, 0.059f, 0.077f, 1.00f);
    c[ImGuiCol_TabHovered]           = ImVec4(0.150f, 0.180f, 0.278f, 1.00f);
    c[ImGuiCol_TabSelected]          = ImVec4(0.118f, 0.138f, 0.225f, 1.00f);
    c[ImGuiCol_TabDimmed]            = ImVec4(0.043f, 0.048f, 0.063f, 1.00f);
    c[ImGuiCol_TabDimmedSelected]    = ImVec4(0.072f, 0.082f, 0.117f, 1.00f);
    c[ImGuiCol_DockingPreview]       = ImVec4(p.accent.x, p.accent.y, p.accent.z, 0.55f);
    c[ImGuiCol_DockingEmptyBg]       = ImVec4(0.029f, 0.033f, 0.043f, 1.00f);
    c[ImGuiCol_TableHeaderBg]        = ImVec4(0.063f, 0.072f, 0.093f, 1.00f);
    c[ImGuiCol_TableBorderStrong]    = ImVec4(0.115f, 0.130f, 0.170f, 0.90f);
    c[ImGuiCol_TableBorderLight]     = ImVec4(0.088f, 0.100f, 0.132f, 0.65f);
    c[ImGuiCol_TableRowBg]           = ImVec4(0.044f, 0.050f, 0.065f, 0.35f);
    c[ImGuiCol_TableRowBgAlt]        = ImVec4(0.068f, 0.077f, 0.100f, 0.32f);
    c[ImGuiCol_NavHighlight]         = ImVec4(p.accent.x, p.accent.y, p.accent.z, 0.78f);
}

void draw_status_badge(std::string_view text, ImVec4 color) {
    const std::string label{text};
    const ImVec2 text_size = ImGui::CalcTextSize(label.c_str());
    const ImVec2 padding{6.0f, 2.0f};
    const ImVec2 size{text_size.x + padding.x * 2.0f, text_size.y + padding.y * 2.0f};
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    ImVec4 bg = color;
    bg.w = 0.14f;
    ImVec4 outline = color;
    outline.w = 0.38f;
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(pos, {pos.x + size.x, pos.y + size.y}, ImGui::GetColorU32(bg), 4.0f);
    draw->AddRect(pos, {pos.x + size.x, pos.y + size.y}, ImGui::GetColorU32(outline), 4.0f);
    draw->AddText({pos.x + padding.x, pos.y + padding.y}, ImGui::GetColorU32(color), label.c_str());
    ImGui::Dummy(size);
}

void draw_toolbar_separator(float height) {
    const float actual_height = std::max(height, 8.0f);
    ImGui::SameLine(0.0f, 7.0f);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(1.0f, actual_height));
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddLine(
        {pos.x, pos.y + 2.0f},
        {pos.x, pos.y + actual_height - 2.0f},
        ImGui::GetColorU32(ImGuiCol_Separator));
    ImGui::SameLine(0.0f, 7.0f);
}

void draw_panel_heading(std::string_view title, std::string_view detail) {
    const auto& p = editor_ui_palette();
    const std::string title_text{title};
    ImGui::TextColored(p.accent, "%s", title_text.c_str());
    if (!detail.empty()) {
        const std::string detail_text{detail};
        ImGui::SameLine();
        ImGui::TextDisabled("%s", detail_text.c_str());
    }
}

} // namespace vespera::editor
