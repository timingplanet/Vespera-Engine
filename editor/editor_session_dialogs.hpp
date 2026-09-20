#pragma once

#include <filesystem>

namespace vespera::editor {
struct EditorState;
void request_open_scene(EditorState& state, std::filesystem::path path);
void request_exit(EditorState& state);
void execute_pending_action(EditorState& state, bool& running);
void draw_path_popups(EditorState& state, bool& running);
}
