#include "editor_build_pipeline.hpp"

#include "editor_assets.hpp"
#include "editor_automation_values.hpp"
#include "editor_console.hpp"
#include "editor_history.hpp"
#include "editor_installation.hpp"
#include "editor_play_controls.hpp"
#include "editor_project_session.hpp"

#include <vespera/assets/project_package.hpp>
#include <vespera/project/project.hpp>
#include <vespera/platform/process.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace vespera::editor {

std::filesystem::path automation_source_root() {
    return editor_installation_root();
}

std::filesystem::path automation_find_runtime_executable(const EditorState& state, std::string_view configuration) {
    return editor_find_runtime_executable(state.project.game_target, configuration);
}
std::filesystem::path automation_find_managed_directory(const EditorState& state) {
    if (state.project.managed_assembly.empty()) return {};
    const std::string filename = state.project.managed_assembly + ".dll";

    // Live managed output belongs to the project, never to the Vespera install
    // directory. Installed builds commonly live under Program Files or another
    // read-only location, and two projects must not overwrite each other's game
    // assembly. Source-build legacy stages remain as read-only fallbacks below.
    const auto project_managed = (state.project.root_directory / ".vespera" / "managed").lexically_normal();
    {
        std::error_code ec;
        if (std::filesystem::is_regular_file(project_managed / filename, ec) && !ec) return project_managed;
    }

    const auto root = automation_source_root();
    if (root.empty()) return {};
    for (const auto& managed_root : {root / "build" / "managed", root / "build-linux" / "managed"}) {
        std::error_code ec;
        if (!std::filesystem::is_directory(managed_root, ec) || ec) continue;
        for (std::filesystem::recursive_directory_iterator it(managed_root, ec), end; it != end && !ec; it.increment(ec)) {
            if (!it->is_regular_file(ec) || ec) continue;
            if (it->path().filename() == filename) return it->path().parent_path().lexically_normal();
        }
    }
    return {};
}
bool launch_packaged_runtime(EditorState& state, const std::filesystem::path& runtime, bool runtime_automation) {
    if (runtime.empty()) {
        push_console(state, ConsoleEntry::Level::Error, "Exported package has no game executable to launch.");
        return false;
    }
    vespera::platform::ProcessOptions process;
    process.executable = runtime;
    process.working_directory = runtime.parent_path();
    if (runtime_automation) process.arguments.push_back("--automation");
    std::string error;
    if (!vespera::platform::launch_process(process, &error)) {
        push_console(state, ConsoleEntry::Level::Error, "Could not launch exported game: " + error);
        return false;
    }
    push_console(state, ConsoleEntry::Level::Info, "Launched exported game: " + runtime.string()
        + (runtime_automation ? " with runtime automation on 127.0.0.1:46788" : ""));
    return true;
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

namespace {

void advance_build_stage(EditorBuildJobState& job, std::string_view line) {
    const auto set_stage = [&](std::string_view label, float progress) {
        if (progress >= job.progress) {
            job.progress = progress;
            job.stage = std::string(label);
        }
    };

    if (line.find("Vespera Engine build") != std::string_view::npos
        || line.find("Configuring done") != std::string_view::npos) {
        set_stage("Preparing build tools", 0.10f);
    }
    if (line.find("Building required native export targets") != std::string_view::npos
        || line.find("Checking Build System") != std::string_view::npos) {
        set_stage("Building native runtime", 0.22f);
    }
    if (line.find("vespera_packager") != std::string_view::npos
        && line.find("->") != std::string_view::npos) {
        set_stage("Finishing native targets", 0.48f);
    }
    if (line.find("Building project C# assembly for export") != std::string_view::npos) {
        set_stage("Building C# scripts", 0.56f);
    }
    if (line.find("Vespera C# build succeeded") != std::string_view::npos) {
        set_stage("C# scripts ready", 0.72f);
    }
    if (line.find("Stage: Packaging game") != std::string_view::npos
        || (line.find("Vespera Engine") != std::string_view::npos && line.find(" export") != std::string_view::npos)) {
        set_stage("Packaging game", 0.80f);
    }
    if (line.find("Stage: Resolving assets") != std::string_view::npos) {
        set_stage("Resolving assets", 0.74f);
    }
    if (line.find("Export succeeded") != std::string_view::npos) {
        set_stage("Finalizing package", 0.96f);
    }
    if (line.find("Launching:") != std::string_view::npos) {
        set_stage("Launching game", 0.98f);
    }
}

} // namespace

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
            advance_build_stage(job, line);
            job.latest_output = line;
            job.log_tail.push_back(std::move(line));
            if (job.log_tail.size() > 32) job.log_tail.erase(job.log_tail.begin());
        }
        start = newline + 1;
    }
    if (start != 0) job.log_partial.erase(0, start);
}
bool start_editor_build_job(EditorState& state, std::string_view configuration, bool launch_after) {
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
    for (const auto& issue : issues) if (issue.severity == vespera::ProjectValidationSeverity::Error) ++errors;
    const auto manifest = vespera::build_project_asset_manifest(state.project, state.asset_catalog);
    if (errors != 0 || !manifest.valid()) {
        push_console(state, ConsoleEntry::Level::Error,
            std::format("Build preflight failed: {} project error(s), {} missing build root(s), {} broken dependency reference(s).",
                errors, manifest.missing_roots.size(), manifest.broken_dependencies.size()));
        job.window_open = true;
        job.status = "Build preflight failed - fix Project Settings / asset errors first.";
        job.stage = "Preflight failed";
        job.progress = 0.0f;
        job.latest_output.clear();
        job.last_succeeded = false;
        job.started_at = {};
        job.completed_at = {};
        return false;
    }

    const auto root = automation_source_root();
    const auto builder = editor_find_builder_executable(configuration);
    const auto runtime = automation_find_runtime_executable(state, configuration);
    if (root.empty() || builder.empty() || runtime.empty()) {
        push_console(state, ConsoleEntry::Level::Error,
            "Build Game cannot locate the Vespera builder/runtime for this installation. Rebuild or reinstall Vespera Engine.");
        return false;
    }

    const std::string config(configuration);
    const auto log_dir = state.project.root_directory / ".vespera" / "logs";
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
    job.stage = "Preparing export";
    job.latest_output.clear();
    job.progress = 0.05f;
    job.started_at = std::chrono::steady_clock::now();
    job.completed_at = {};

    vespera::platform::ProcessOptions process;
    process.executable = builder;
    process.working_directory = root;
    process.arguments = {
        "--project", state.project_path.string(),
        "--output", job.last_output_directory.string(),
        "--runtime", runtime.string(),
        "--configuration", config,
    };
    const auto dotnet = editor_find_dotnet_executable();
    const auto sdk_project = editor_find_sdk_project();
    const auto script_tool_project = editor_find_script_tool_project();
    if (!dotnet.empty()) {
        process.arguments.push_back("--dotnet");
        process.arguments.push_back(dotnet.string());
    }
    if (!sdk_project.empty()) {
        process.arguments.push_back("--sdk-project");
        process.arguments.push_back(sdk_project.string());
    }
    if (!script_tool_project.empty()) {
        process.arguments.push_back("--script-tool-project");
        process.arguments.push_back(script_tool_project.string());
    }
    if (launch_after) process.arguments.push_back("--launch");
    process.timeout_seconds = 3600;

    const auto log_path = job.log_path;
    job.future = std::async(std::launch::async, [process = std::move(process), log_path]() mutable {
        std::string error;
        const int result = vespera::platform::run_process_to_file(process, log_path, &error);
        if (result < 0 && !error.empty()) {
            std::ofstream append(log_path, std::ios::app);
            if (append) append << "ERROR: " << error << '\n';
        }
        return result;
    });
    push_console(state, ConsoleEntry::Level::Info,
        std::format("{} build started inside Vespera. Output: {}", config, job.last_output_directory.string()));
    return true;
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
            advance_build_stage(job, job.log_partial);
            job.latest_output = job.log_partial;
            job.log_tail.push_back(job.log_partial);
        }
        job.log_partial.clear();
    }
    job.running = false;
    job.completed_at = std::chrono::steady_clock::now();
    job.last_succeeded = result == 0;
    if (job.last_succeeded) {
        job.progress = 1.0f;
        job.stage = job.last_launch_requested ? "Build complete - game launched" : "Build complete";
        job.status = job.last_launch_requested ? "Build succeeded and game launched." : "Build succeeded.";
        push_console(state, ConsoleEntry::Level::Info,
            "Standalone build succeeded: " + job.last_output_directory.string());
    } else {
        job.stage = "Build failed";
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
    const auto& output = state.build_job.last_output_directory;
    if (output.empty() || !std::filesystem::exists(output)) return;
    std::string error;
    if (!vespera::platform::reveal_directory(output, &error)) {
        push_console(state, ConsoleEntry::Level::Warning, "Could not open build output folder: " + error);
    }
}
bool export_current_project_from_editor(EditorState& state, std::string_view configuration, bool launch_after) {
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
            "Export cannot locate the Vespera Engine installation. Reopen the project through Vespera Hub or reinstall/rebuild Vespera Engine.");
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

} // namespace vespera::editor
