#pragma once

#include <vespera/assets/texture_importer.hpp>
#include <vespera/world/sector_world.hpp>

#include <array>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vespera::reference_content {

struct ReferenceTextureAsset {
    std::string name;
    std::filesystem::path relative_path;
};

inline std::vector<ReferenceTextureAsset> reference_texture_assets() {
    std::vector<ReferenceTextureAsset> assets{
        {"Floor Tiles", "textures/floor_tiles.bmp"},
        {"Concrete Brick", "textures/concrete_brick.bmp"},
        {"Metal Panels", "textures/metal_panels.bmp"},
        {"Ceiling Tiles", "textures/ceiling_tiles.bmp"},
        {"Chamber Tiles", "textures/chamber_tiles.bmp"},
        {"Test Sprite", "textures/test_sprite.bmp"},
    };
    for (std::uint32_t direction = 0; direction < 8u; ++direction) {
        for (std::uint32_t frame = 0; frame < 2u; ++frame) {
            assets.push_back({
                std::format("Watcher D{} F{}", direction, frame),
                std::format("textures/watcher_d{}_f{}.bmp", direction, frame)
            });
        }
    }
    return assets;
}

inline bool register_textures(
    SectorWorld& world,
    const std::filesystem::path& assets_root = "assets",
    std::string* error_message = nullptr
) {
    world.clear();
    for (const auto& asset : reference_texture_assets()) {
        const auto imported = vespera::import_texture(assets_root / asset.relative_path, asset.name);
        if (!imported) {
            if (error_message) *error_message = imported.message;
            world.clear();
            return false;
        }
        world.add_texture(imported.texture);
    }
    if (error_message) error_message->clear();
    return true;
}

} // namespace vespera::reference_content
