#pragma once

#include <vespera/assets/asset_catalog.hpp>

namespace vespera::editor {
struct EditorState;
bool open_ui_authoring(EditorState& state, const vespera::AssetRecord& record);
void draw_ui_authoring(EditorState& state);
void draw_rml_source_authoring(EditorState& state);
}
