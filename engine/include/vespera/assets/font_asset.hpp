#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace vespera {

enum class FontContainerKind {
    TrueType,
    OpenTypeCff,
};

struct FontAssetInfo {
    std::filesystem::path source_path;
    FontContainerKind container = FontContainerKind::TrueType;
    std::uint16_t table_count = 0;
    std::uintmax_t source_size = 0;
};

struct FontAssetInspectResult {
    bool ok = false;
    FontAssetInfo font;
    std::string message;
    explicit operator bool() const { return ok; }
};

// Validates the SFNT container used by .ttf/.otf sources. Rasterization,
// shaping, atlas policy and runtime Text rendering intentionally remain later
// UI-layer decisions; project font identity/import is established here first.
[[nodiscard]] FontAssetInspectResult inspect_font_asset(const std::filesystem::path& path);
[[nodiscard]] const char* font_container_name(FontContainerKind kind);

} // namespace vespera
