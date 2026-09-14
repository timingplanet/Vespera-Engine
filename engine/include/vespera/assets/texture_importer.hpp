#pragma once

#include <vespera/render/texture_data.hpp>

#include <filesystem>
#include <string>

namespace vespera {

struct TextureImportResult {
    bool ok = false;
    TextureData texture;
    std::string message;
    explicit operator bool() const { return ok; }
};

// Vespera keeps source decoding renderer-independent. BMP and TGA currently
// decode into the same RGBA8 TextureData payload. PNG/JPEG decode through
// Windows Imaging Component on the current validated Windows platform.
[[nodiscard]] TextureImportResult import_bmp_texture(
    const std::filesystem::path& path,
    std::string display_name = {}
);

[[nodiscard]] TextureImportResult import_tga_texture(
    const std::filesystem::path& path,
    std::string display_name = {}
);

[[nodiscard]] TextureImportResult import_texture(
    const std::filesystem::path& path,
    std::string display_name = {}
);

// Small renderer-independent checkerboard used when a project texture is missing
// or cannot be decoded. The authored texture name is preserved so Scene Text can
// still round-trip the reference instead of aborting project startup.
[[nodiscard]] TextureData make_missing_texture_placeholder(std::string display_name);

} // namespace vespera
