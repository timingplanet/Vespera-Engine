#pragma once

namespace vespera::editor {
struct EditorState;
bool draw_menu(EditorState& state);
void handle_shortcuts(EditorState& state);
}
