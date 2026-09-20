#include "editor_history.hpp"
#include "editor_console.hpp"
#include "editor_selection.hpp"
#include <imgui.h>
#include <utility>
namespace vespera::editor {

void append_command_audit(
    EditorState& state,
    vespera::editor::EditorCommandKind kind,
    std::string label,
    bool succeeded,
    std::uint64_t before_state_id,
    std::uint64_t after_state_id,
    vespera::SceneObjectId target_entity_id,
    std::string target_asset_id) {
    vespera::editor::EditorCommandRecord record;
    record.sequence = state.next_command_sequence++;
    record.kind = kind;
    record.label = std::move(label);
    record.succeeded = succeeded;
    record.before_state_id = before_state_id == 0 ? state.current_state_id : before_state_id;
    record.after_state_id = after_state_id == 0 ? state.current_state_id : after_state_id;
    record.target_entity_id = target_entity_id;
    record.target_asset_id = std::move(target_asset_id);
    state.command_log.push_back(std::move(record));
    if (state.command_log.size() > 512) {
        state.command_log.erase(state.command_log.begin(), state.command_log.begin() + 128);
    }
}
HistorySnapshot capture_snapshot(const EditorState& state, std::string label) {
    return {state.scene, state.selection, state.selected_entity_ids, state.current_state_id, std::move(label)};
}
void refresh_dirty(EditorState& state) {
    state.dirty = state.current_state_id != state.saved_state_id;
}
void clear_active_edit(EditorState& state) {
    state.active_edit_before.reset();
    state.active_edit_item = 0;
    state.active_edit_changed = false;
}
void push_history_before(EditorState& state, HistorySnapshot before, std::string label) {
    before.label = std::move(label);
    state.undo_stack.push_back(std::move(before));
    constexpr std::size_t kHistoryLimit = 128;
    if (state.undo_stack.size() > kHistoryLimit) {
        state.undo_stack.erase(state.undo_stack.begin());
    }
    state.redo_stack.clear();
    state.current_state_id = state.next_state_id++;
    refresh_dirty(state);
}
void begin_edit(EditorState& state, HistorySnapshot before, std::string label, ImGuiID item_id) {
    if (state.active_edit_before) {
        return;
    }
    before.label = std::move(label);
    state.active_edit_before = std::move(before);
    state.active_edit_item = item_id;
    state.active_edit_changed = false;
}
void commit_active_edit(EditorState& state) {
    if (!state.active_edit_before) {
        return;
    }
    if (state.active_edit_changed) {
        HistorySnapshot before = std::move(*state.active_edit_before);
        const std::string label = before.label;
        push_history_before(state, std::move(before), label);
    }
    clear_active_edit(state);
}
void track_item_edit(EditorState& state, HistorySnapshot before, std::string label, ImGuiID item_id, bool changed) {
    if (changed) {
        state.dirty = true; // Reflect in-progress edits before the transaction commits.
    }
    if (changed && !state.active_edit_before) {
        begin_edit(state, std::move(before), std::move(label), item_id);
    }
    if (state.active_edit_before && state.active_edit_item == item_id) {
        state.active_edit_changed = state.active_edit_changed || changed;
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            commit_active_edit(state);
        }
    }
}
void record_immediate_edit(EditorState& state, HistorySnapshot before, std::string label) {
    commit_active_edit(state);
    push_history_before(state, std::move(before), std::move(label));
}
bool undo(EditorState& state) {
    commit_active_edit(state);
    if (state.undo_stack.empty()) {
        return false;
    }
    HistorySnapshot previous = std::move(state.undo_stack.back());
    state.undo_stack.pop_back();
    HistorySnapshot current = capture_snapshot(state, previous.label);
    state.redo_stack.push_back(std::move(current));
    state.scene = std::move(previous.scene);
    state.selection = previous.selection;
    state.selected_entity_ids = std::move(previous.selected_entity_ids);
    repair_selection(state);
    state.current_state_id = previous.state_id;
    refresh_dirty(state);
    push_console(state, ConsoleEntry::Level::Info, "Undo: " + previous.label);
    return true;
}
bool redo(EditorState& state) {
    commit_active_edit(state);
    if (state.redo_stack.empty()) {
        return false;
    }
    HistorySnapshot next = std::move(state.redo_stack.back());
    state.redo_stack.pop_back();
    HistorySnapshot current = capture_snapshot(state, next.label);
    state.undo_stack.push_back(std::move(current));
    state.scene = std::move(next.scene);
    state.selection = next.selection;
    state.selected_entity_ids = std::move(next.selected_entity_ids);
    repair_selection(state);
    state.current_state_id = next.state_id;
    refresh_dirty(state);
    push_console(state, ConsoleEntry::Level::Info, "Redo: " + next.label);
    return true;
}
void reset_history_after_open(EditorState& state) {
    clear_active_edit(state);
    state.undo_stack.clear();
    state.redo_stack.clear();
    state.current_state_id = state.next_state_id++;
    state.saved_state_id = state.current_state_id;
    refresh_dirty(state);
}

} // namespace vespera::editor
