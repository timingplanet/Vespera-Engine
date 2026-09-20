#pragma once
#include "editor_state.hpp"
namespace vespera::editor {
bool editor_is_playing(const EditorState& state);
void set_game_input_capture(EditorState& state, bool captured);
void feed_play_runtime_input(EditorState& state);
void start_play_mode(EditorState& state);
void stop_play_mode(EditorState& state);
void toggle_play_pause(EditorState& state);
void step_play_mode(EditorState& state);
void update_play_clock(EditorState& state, double wall_seconds);
}
