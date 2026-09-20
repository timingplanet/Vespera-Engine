#pragma once
#include "editor_state.hpp"
#include <cstdint>
#include <string>
namespace vespera::editor {
HistorySnapshot capture_snapshot(const EditorState& state, std::string label = {});
void refresh_dirty(EditorState& state);
void clear_active_edit(EditorState& state);
void push_history_before(EditorState& state, HistorySnapshot before, std::string label);
void begin_edit(EditorState& state, HistorySnapshot before, std::string label, ImGuiID item_id = 0);
void commit_active_edit(EditorState& state);
void track_item_edit(EditorState& state, HistorySnapshot before, std::string label, ImGuiID item_id, bool changed);
void record_immediate_edit(EditorState& state, HistorySnapshot before, std::string label);
void append_command_audit(
    EditorState& state,
    EditorCommandKind kind,
    std::string label,
    bool succeeded = true,
    std::uint64_t before_state_id = 0,
    std::uint64_t after_state_id = 0,
    vespera::SceneObjectId target_entity_id = vespera::kInvalidSceneObjectId,
    std::string target_asset_id = {});
bool undo(EditorState& state);
bool redo(EditorState& state);
void reset_history_after_open(EditorState& state);
}
