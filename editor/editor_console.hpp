#pragma once
#include "editor_state.hpp"
#include <string>
namespace vespera::editor {
void push_console(EditorState& state, ConsoleEntry::Level level, std::string text);
std::string console_export_text(const EditorState& state, bool visible_only);
}
