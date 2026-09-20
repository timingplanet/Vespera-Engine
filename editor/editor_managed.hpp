#pragma once
#include "editor_state.hpp"
#include <filesystem>
#include <optional>
#include <string>
namespace vespera::editor {
bool load_managed_metadata(EditorState& state, bool report = true);
std::string default_managed_value(const ManagedFieldMetadata& field);
std::optional<std::filesystem::path> managed_build_helper_path();
void load_managed_build_diagnostics(EditorState& state, const std::filesystem::path& path, int process_result);
void build_managed_scripts(EditorState& state);
}
