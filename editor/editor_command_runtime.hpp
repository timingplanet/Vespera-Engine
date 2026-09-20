#pragma once
#include "editor_console.hpp"
#include "editor_history.hpp"
#include "editor_state.hpp"
#include <cstdint>
#include <string>
#include <utility>
namespace vespera::editor {
template <typename Mutator>
bool execute_editor_command(EditorState& state, EditorCommandKind kind, std::string label, Mutator&& mutator) {
    commit_active_edit(state);
    const std::uint64_t before_state_id = state.current_state_id;
    HistorySnapshot before = capture_snapshot(state);
    const bool succeeded = mutator();
    if (!succeeded) {
        append_command_audit(state, kind, label, false, before_state_id, state.current_state_id);
        return false;
    }
    const std::string console_label = label;
    record_immediate_edit(state, std::move(before), std::move(label));
    append_command_audit(state, kind, console_label, true, before_state_id, state.current_state_id);
    push_console(state, ConsoleEntry::Level::Info, console_label);
    return true;
}
}
