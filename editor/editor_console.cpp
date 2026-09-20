#include "editor_console.hpp"

#include <utility>

namespace vespera::editor {

void push_console(EditorState& state, ConsoleEntry::Level level, std::string text) {
    state.console.push_back({level, std::move(text)});
    constexpr std::size_t kMaxEntries = 500;
    if (state.console.size() > kMaxEntries) {
        state.console.erase(state.console.begin(), state.console.begin() + (state.console.size() - kMaxEntries));
    }
}

std::string console_export_text(const EditorState& state, bool visible_only) {
    std::string out;
    for (const auto& entry : state.console) {
        if (visible_only) {
            if (entry.level == ConsoleEntry::Level::Info && !state.console_show_info) continue;
            if (entry.level == ConsoleEntry::Level::Warning && !state.console_show_warnings) continue;
            if (entry.level == ConsoleEntry::Level::Error && !state.console_show_errors) continue;
        }
        const char* prefix = entry.level == ConsoleEntry::Level::Error
            ? "[Error]" : (entry.level == ConsoleEntry::Level::Warning ? "[Warn]" : "[Info]");
        if (!out.empty()) out.push_back('\n');
        out += prefix;
        out.push_back(' ');
        out += entry.text;
    }
    return out;
}

} // namespace vespera::editor
