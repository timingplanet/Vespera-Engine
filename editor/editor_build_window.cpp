#include "editor_build_window.hpp"

#include "editor_build_pipeline.hpp"
#include "editor_play_controls.hpp"
#include "editor_state.hpp"
#include "editor_style.hpp"

#include <vespera/assets/build_manifest.hpp>
#include <vespera/project/project.hpp>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <chrono>
#include <cfloat>
#include <filesystem>
#include <format>
#include <string>

namespace vespera::editor {
namespace {

void build_path_value(const char* label, const std::filesystem::path& path) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", label);
    ImGui::TableSetColumnIndex(1);
    const std::string display = path.filename().empty() ? path.string() : path.filename().string();
    ImGui::TextUnformatted(display.c_str());
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::SetTooltip("%s", path.string().c_str());
    }
}

long long build_elapsed_seconds(const EditorBuildJobState& job) {
    if (job.started_at == std::chrono::steady_clock::time_point{}) return 0;
    const auto end = (!job.running && job.completed_at != std::chrono::steady_clock::time_point{})
        ? job.completed_at
        : std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(end - job.started_at).count();
}

std::string build_log_text(const EditorBuildJobState& job) {
    std::string text;
    for (const auto& line : job.log_tail) {
        if (!text.empty()) text.push_back('\n');
        text += line;
    }
    return text;
}

} // namespace

void draw_build_game_window(EditorState& state) {
    auto& job = state.build_job;
    if (!job.window_open && !job.running) return;
    if (!job.window_open) return;

    ImGui::SetNextWindowSize(ImVec2(720.0f, 720.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Build Game", &job.window_open)) {
        ImGui::End();
        return;
    }

    if (!state.project_loaded) {
        ImGui::TextDisabled("Open a Vespera project to build a standalone game.");
        ImGui::End();
        return;
    }

    const auto& palette = editor_ui_palette();
    draw_panel_heading("Build Game", state.project.name);
    ImGui::Spacing();

    if (ImGui::BeginTable("BuildProjectSummary", 2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 118.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        build_path_value("Project file", state.project_path);
        build_path_value("Scene", state.scene_path);
        ImGui::EndTable();
    }

    ImGui::SeparatorText("Build Settings");
    const char* configurations[] = {"Debug", "Development", "Release"};
    ImGui::BeginDisabled(job.running);
    if (ImGui::BeginTable("BuildSettings", 2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 170.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("Configuration");
        ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::Combo("##BuildConfiguration", &job.configuration_index, configurations, IM_ARRAYSIZE(configurations));

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("Executable name");
        ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("##BuildExecutableName", &state.project.executable_name);

        std::string build_output = state.project.build_output_directory.generic_string();
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("Output directory");
        ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputText("##BuildOutputDirectory", &build_output)) state.project.build_output_directory = build_output;

        int deployment_index = state.project.managed_deployment == vespera::ManagedDeploymentMode::Portable ? 1 : 0;
        const char* deployment_items[] = {"Framework-dependent", "Portable (.NET bundled)"};
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("Managed deployment");
        ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::Combo("##ManagedDeployment", &deployment_index, deployment_items, IM_ARRAYSIZE(deployment_items))) {
            state.project.managed_deployment = deployment_index == 1
                ? vespera::ManagedDeploymentMode::Portable
                : vespera::ManagedDeploymentMode::FrameworkDependent;
        }

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("Diagnostics");
        ImGui::TableSetColumnIndex(1);
        ImGui::Checkbox("Development symbols / diagnostics", &state.project.development_diagnostics);
        ImGui::EndTable();
    }
    ImGui::EndDisabled();

    const std::string configuration = editor_build_configuration(job);
    const auto output = editor_build_output_directory(state, configuration);
    ImGui::TextDisabled("Package output: %s", output.filename().string().c_str());
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", output.string().c_str());
    ImGui::TextDisabled("Builds run in the background and package the current project.");

    const auto issues = vespera::validate_vespera_project(state.project);
    const auto manifest = vespera::build_project_asset_manifest(state.project, state.asset_catalog);
    std::size_t errors = 0, warnings = 0;
    for (const auto& issue : issues) {
        if (issue.severity == vespera::ProjectValidationSeverity::Error) ++errors; else ++warnings;
    }
    const bool preflight_ok = errors == 0 && manifest.valid();
    const ImVec4 validation_color = preflight_ok ? palette.success : palette.danger;
    ImGui::TextColored(validation_color,
        "Build check  %zu error(s)  |  %zu warning(s)  |  %zu assets  |  %zu broken  |  %zu missing input(s)",
        errors, warnings, manifest.assets.size(), manifest.broken_dependencies.size(), manifest.missing_roots.size());

    if (ImGui::CollapsingHeader("Build Check Details", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BeginChild("BuildPreflightDetails", ImVec2(0.0f, 132.0f), true);
        if (issues.empty() && manifest.valid() && manifest.stale_root_paths.empty()) {
            ImGui::TextColored(palette.success, "Ready to build. Project validation and required assets are clean.");
        }
        for (const auto& issue : issues) {
            const bool is_error = issue.severity == vespera::ProjectValidationSeverity::Error;
            ImGui::TextColored(is_error ? palette.danger : palette.warning, "%s", is_error ? "ERROR" : "WARNING");
            ImGui::SameLine();
            ImGui::TextWrapped("%s", issue.message.c_str());
        }
        for (const auto& root : manifest.missing_roots) {
            ImGui::TextColored(palette.danger, "ERROR");
            ImGui::SameLine();
            ImGui::TextWrapped("Missing required build asset: %s", root.c_str());
        }
        for (const auto* dependency : manifest.broken_dependencies) {
            if (!dependency) continue;
            ImGui::TextColored(palette.danger, "ERROR");
            ImGui::SameLine();
            ImGui::TextWrapped("Broken asset reference: %s (%s)", dependency->reference.c_str(), dependency->reason.c_str());
        }
        for (const auto& path : manifest.stale_root_paths) {
            ImGui::TextColored(palette.warning, "WARNING");
            ImGui::SameLine();
            ImGui::TextWrapped("A saved build asset path is outdated: %s", path.c_str());
        }
        if (configuration == "Release" && state.project.game_icon.empty()) {
            ImGui::TextColored(palette.info, "NOTE");
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

    ImGui::SeparatorText("Build Status");
    if (job.running) {
        const float progress = std::clamp(job.progress, 0.0f, 0.99f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, palette.accent);
        ImGui::ProgressBar(progress, ImVec2(-1.0f, 20.0f), job.stage.c_str());
        ImGui::PopStyleColor();
        ImGui::TextColored(palette.info, "%s", job.status.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%llds elapsed", build_elapsed_seconds(job));
        if (!job.latest_output.empty()) {
            ImGui::TextDisabled("Latest: %s", job.latest_output.c_str());
        }
    } else if (job.last_succeeded) {
        draw_status_badge("SUCCESS", palette.success);
        ImGui::SameLine();
        ImGui::Text("Build completed in %llds", build_elapsed_seconds(job));
        if (!job.last_output_directory.empty()) {
            ImGui::TextDisabled("Package: %s", job.last_output_directory.filename().string().c_str());
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                ImGui::SetTooltip("%s", job.last_output_directory.string().c_str());
            }
        }
    } else if (job.stage == "Build failed" || job.stage == "Preflight failed") {
        draw_status_badge("FAILED", palette.danger);
        ImGui::SameLine();
        ImGui::TextColored(palette.danger, "%s", job.status.c_str());
        if (job.started_at != std::chrono::steady_clock::time_point{}) {
            ImGui::SameLine();
            ImGui::TextDisabled("%llds elapsed", build_elapsed_seconds(job));
        }
    } else {
        ImGui::TextDisabled("Ready to build.");
    }
    if (!job.log_path.empty()) {
        ImGui::TextDisabled("Log: %s", job.log_path.filename().string().c_str());
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", job.log_path.string().c_str());
    }

    ImGui::SeparatorText("Recent Build Output");
    ImGui::BeginDisabled(job.log_tail.empty());
    if (ImGui::SmallButton("Copy Output")) {
        const std::string text = build_log_text(job);
        ImGui::SetClipboardText(text.c_str());
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("Most recent %zu line(s)", job.log_tail.size());
    ImGui::BeginChild("BuildGameLog", ImVec2(0.0f, 210.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
    if (job.log_tail.empty()) ImGui::TextDisabled("Build output will appear here.");
    for (const auto& line : job.log_tail) ImGui::TextUnformatted(line.c_str());
    if (job.running) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    ImGui::End();
}

} // namespace vespera::editor
