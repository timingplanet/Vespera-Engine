#pragma once

#include "editor_state.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace vespera::editor {

std::filesystem::path automation_source_root();
std::filesystem::path automation_find_runtime_executable(const EditorState& state, std::string_view configuration);
std::filesystem::path automation_find_managed_directory(const EditorState& state);
bool launch_packaged_runtime(EditorState& state, const std::filesystem::path& runtime, bool runtime_automation = false);
std::string editor_build_configuration(const EditorBuildJobState& job);
std::string safe_project_build_name(const EditorState& state);
std::filesystem::path editor_build_output_directory(const EditorState& state, std::string_view configuration);
void append_editor_build_log(EditorState& state);
bool start_editor_build_job(EditorState& state, std::string_view configuration, bool launch_after);
void update_editor_build_job(EditorState& state);
void open_editor_build_output(EditorState& state);
bool export_current_project_from_editor(EditorState& state, std::string_view configuration, bool launch_after = false);

} // namespace vespera::editor
