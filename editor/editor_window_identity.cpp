#include "editor_window_identity.hpp"
#include "editor_state.hpp"
#include <vespera/core/version.hpp>
#include <format>
#include <string>
namespace vespera::editor {
std::string window_title(const EditorState& state) {
    if (state.project_loaded) {
        std::string project_name = state.project.name.empty()
            ? (state.project_path.empty() ? std::string("Untitled Project") : state.project_path.stem().string())
            : state.project.name;
        if (state.dirty) project_name += " *";
        return std::format("{} - Vespera Editor {}", project_name, vespera::kEngineVersion);
    }
    std::string name = state.scene_path.empty() ? "Untitled" : state.scene_path.filename().string();
    if (state.dirty) name += " *";
    return std::format("{} - Vespera Editor {}", name, vespera::kEngineVersion);
}


} // namespace vespera::editor
