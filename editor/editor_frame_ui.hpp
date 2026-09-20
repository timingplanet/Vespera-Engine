#pragma once

namespace vespera::editor {
struct EditorState;
void draw_editor_ui_frame(EditorState& state, bool& running, bool interactive);
}
