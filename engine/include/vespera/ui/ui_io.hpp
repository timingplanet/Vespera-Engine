#pragma once

#include <vespera/ui/ui.hpp>

#include <filesystem>
#include <string>

namespace vespera {

struct UiIoResult {
    bool ok = false;
    std::string message;
    explicit operator bool() const { return ok; }
};

// Renderer-independent UI document serialization. .slui v3 adds modern surface/text
// styling while preserving stable node IDs, hierarchy/layout/widget properties and
// stable AssetReferences for images/fonts. The loader keeps v1/v2 compatibility.
// No ImGui/D3D12/native
// pointer state is persisted.
[[nodiscard]] UiIoResult load_ui_document(UiDocument& document, const std::filesystem::path& path);
[[nodiscard]] UiIoResult save_ui_document(const UiDocument& document, const std::filesystem::path& path);

} // namespace vespera
