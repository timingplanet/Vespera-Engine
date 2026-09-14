#pragma once

#include <vespera/assets/asset_catalog.hpp>
#include <vespera/scene/scene.hpp>
#include <vespera/world/sector_world.hpp>

#include <filesystem>
#include <string>

namespace vespera {

struct SpriteClipAssetResult {
    bool ok = false;
    SpriteAnimationClip clip;
    std::string message;
    explicit operator bool() const { return ok; }
};

// External sprite-animation authoring asset. 0.7.x keeps scene-v11 embedded
// clips compatible, but this descriptor lets projects author/reuse a clip as a
// first-class asset and then apply it to a scene/runtime resource set.
[[nodiscard]] SpriteClipAssetResult load_sprite_clip_asset(
    const std::filesystem::path& path,
    const AssetCatalog& catalog,
    const SectorWorld& world
);

} // namespace vespera
