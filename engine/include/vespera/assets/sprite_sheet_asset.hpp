#pragma once

#include <vespera/assets/asset_catalog.hpp>
#include <vespera/scene/scene.hpp>
#include <vespera/world/sector_world.hpp>

#include <cstdint>
#include <filesystem>
#include <string>

namespace vespera {

struct SpriteSheetAssetResult {
    bool ok = false;
    SpriteAnimationClip clip;
    std::string source_asset_id;
    std::filesystem::path source_path;
    bool source_resolved_by_id = false;
    bool source_fallback_stale = false;
    std::uint32_t sheet_width = 0;
    std::uint32_t sheet_height = 0;
    std::uint32_t frame_width = 0;
    std::uint32_t frame_height = 0;
    std::string message;
    explicit operator bool() const { return ok; }
};

// Authoring descriptor for slicing a regular sprite sheet into a directional
// animation clip. Frames are laid out direction-major by row: each row is a
// facing direction and each column is an animation frame. The loader validates
// and slices the source texture before adding derived frames to the world.
[[nodiscard]] SpriteSheetAssetResult load_sprite_sheet_asset(
    const std::filesystem::path& path,
    const AssetCatalog& catalog,
    SectorWorld& world
);

} // namespace vespera
