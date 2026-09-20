#pragma once
#include "editor_state.hpp"
#include <filesystem>
namespace vespera::editor {
void validate_scene_to_console(EditorState& state);
bool open_project(EditorState& state, const std::filesystem::path& path);
bool open_scene(EditorState& state, const std::filesystem::path& path);
bool save_scene(EditorState& state, const std::filesystem::path& path);
}
