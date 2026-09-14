#pragma once

#include <filesystem>
#include <string>

namespace vespera::editor {

struct RmlSourceEditorState {
    bool window_open = false;
    std::filesystem::path path;
    std::string asset_id;
    std::string text;
    std::string saved_text;
    std::string status;

    [[nodiscard]] bool dirty() const { return text != saved_text; }
};

// Opens an authored .rml/.rcss source asset in a lightweight text editor. If
// the same dirty asset is reopened, the in-memory edits are preserved.
bool open_rml_source_editor(
    RmlSourceEditorState& state,
    const std::filesystem::path& path,
    std::string asset_id,
    std::string* error = nullptr);

// Draws the source editor. Returns true only when a save reached disk so the
// caller can refresh AssetCatalog / reload an active runtime document.
bool draw_rml_source_editor(RmlSourceEditorState& state);

} // namespace vespera::editor
