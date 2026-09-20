#include "editor_extensions.hpp"
#include "automation_tools.hpp"
#include "editor_state.hpp"
#include "extension_api.hpp"
#include <vespera/core/version.hpp>
#include <string>
#include <utility>
namespace vespera::editor {
void register_core_extension_capabilities(EditorState& state) {
    if (!state.extension_registry.extensions().empty()) return;
    std::string error;
    (void)state.extension_registry.register_extension({
        "vespera.core", "Vespera Core Editor", std::string(vespera::kEngineVersion),
        vespera::editor::kEditorExtensionApiVersion}, &error);
    for (const auto& tool : vespera::editor::kAutomationTools) {
        vespera::editor::EditorExtensionCommandDescriptor command;
        command.extension_id = "vespera.core";
        command.name = std::string(tool.name);
        command.description = std::string(tool.description);
        command.mutating = tool.mutating;
        command.undoable = tool.undoable;
        command.allowed_during_play = tool.allowed_during_play;
        (void)state.extension_registry.register_command(std::move(command), &error);
    }
}


} // namespace vespera::editor
