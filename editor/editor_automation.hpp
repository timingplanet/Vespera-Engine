#pragma once

#include "automation_protocol.hpp"
#include "editor_state.hpp"

#include <cstdint>

namespace vespera::editor {

AutomationResponse dispatch_automation_request(EditorState& state, const AutomationRequest& request);
void poll_automation_server(EditorState& state);
bool start_automation_server(EditorState& state, std::uint16_t port);
void stop_automation_server(EditorState& state);

} // namespace vespera::editor
