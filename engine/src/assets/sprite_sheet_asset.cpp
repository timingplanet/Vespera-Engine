#include <vespera/assets/sprite_sheet_asset.hpp>

#include <vespera/assets/texture_importer.hpp>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

namespace vespera {
namespace {

SpriteSheetAssetResult fail(std::size_t line, std::string message) {
    if (line != 0) message = "sprite sheet line " + std::to_string(line) + ": " + message;
    SpriteSheetAssetResult result;
    result.message = std::move(message);
    return result;
}

TextureData slice_rgba(
    const TextureData& sheet,
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t width,
    std::uint32_t height,
    std::string name
) {
    TextureData frame;
    frame.name = std::move(name);
    frame.width = width;
    frame.height = height;
    frame.rgba8.resize(static_cast<std::size_t>(width) * height * 4u);
    for (std::uint32_t row = 0; row < height; ++row) {
        const std::size_t src = (static_cast<std::size_t>(y + row) * sheet.width + x) * 4u;
        const std::size_t dst = static_cast<std::size_t>(row) * width * 4u;
        std::copy_n(sheet.rgba8.data() + src, static_cast<std::size_t>(width) * 4u, frame.rgba8.data() + dst);
    }
    return frame;
}

} // namespace

SpriteSheetAssetResult load_sprite_sheet_asset(
    const std::filesystem::path& path,
    const AssetCatalog& catalog,
    SectorWorld& world
) {
    std::ifstream input(path);
    if (!input) return fail(0, "could not open sprite sheet asset: " + path.string());

    std::string clip_name;
    AssetReference source_reference;
    int format_version = 0;
    std::uint32_t frame_width = 0;
    std::uint32_t frame_height = 0;
    std::uint32_t directions = 0;
    std::uint32_t frames = 0;
    std::uint32_t margin_x = 0;
    std::uint32_t margin_y = 0;
    std::uint32_t spacing_x = 0;
    std::uint32_t spacing_y = 0;
    float fps = 0.0f;
    bool loop = true;
    bool saw_header = false;
    bool saw_end = false;

    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos || line[first] == '#') continue;
        std::istringstream stream(line.substr(first));
        std::string command;
        stream >> command;
        if (!saw_header) {
            if (command != "vespera_sprite_sheet" || !(stream >> format_version)
                || format_version < 1 || format_version > 2) {
                return fail(line_number, "expected 'vespera_sprite_sheet 1|2' header");
            }
            saw_header = true;
            continue;
        }

        if (command == "name") {
            if (!(stream >> std::quoted(clip_name)) || clip_name.empty()) return fail(line_number, "invalid clip name");
        } else if (command == "source") {
            std::string source_path;
            if (!(stream >> std::quoted(source_path)) || source_path.empty()) return fail(line_number, "invalid source texture path");
            source_reference.path = source_path;
        } else if (command == "source_asset") {
            if (format_version < 2) return fail(line_number, "source_asset requires sprite sheet format 2");
            std::string source_path;
            if (!(stream >> std::quoted(source_reference.asset_id) >> std::quoted(source_path))
                || source_reference.asset_id.empty()) return fail(line_number, "invalid stable source asset reference");
            source_reference.path = source_path;
        } else if (command == "frame_size") {
            if (!(stream >> frame_width >> frame_height) || frame_width == 0 || frame_height == 0) {
                return fail(line_number, "frame_size requires positive width and height");
            }
        } else if (command == "directions") {
            if (!(stream >> directions) || (directions != 1u && directions != 4u && directions != 8u)) {
                return fail(line_number, "directions must be 1, 4, or 8");
            }
        } else if (command == "frames") {
            if (!(stream >> frames) || frames == 0u) return fail(line_number, "frames must be greater than zero");
        } else if (command == "fps") {
            if (!(stream >> fps) || fps < 0.0f) return fail(line_number, "fps must be non-negative");
        } else if (command == "loop") {
            int value = 0;
            if (!(stream >> value) || (value != 0 && value != 1)) return fail(line_number, "loop must be 0 or 1");
            loop = value != 0;
        } else if (command == "margin") {
            if (!(stream >> margin_x >> margin_y)) return fail(line_number, "margin requires x and y");
        } else if (command == "spacing") {
            if (!(stream >> spacing_x >> spacing_y)) return fail(line_number, "spacing requires x and y");
        } else if (command == "end_sprite_sheet") {
            saw_end = true;
            break;
        } else {
            return fail(line_number, "unknown command '" + command + "'");
        }
    }

    if (!saw_header) return fail(0, "missing sprite sheet header");
    if (!saw_end) return fail(0, "missing end_sprite_sheet");
    if (clip_name.empty()) return fail(0, "name is missing");
    if (source_reference.empty()) return fail(0, "source texture is missing");
    if (frame_width == 0 || frame_height == 0) return fail(0, "frame_size is missing");
    if (directions == 0 || frames == 0) return fail(0, "directions/frames are missing");

    const auto source_resolution = catalog.resolve_reference(source_reference);
    const auto* source_asset = source_resolution.record;
    if (!source_asset || source_asset->kind != AssetKind::Texture) {
        const std::string authored = source_reference.path.empty() ? source_reference.asset_id : source_reference.path.generic_string();
        return fail(0, "source texture asset is missing: " + authored);
    }
    const auto imported = import_texture(source_asset->absolute_path, source_asset->display_name);
    if (!imported) return fail(0, imported.message);

    const std::uint64_t required_width = static_cast<std::uint64_t>(margin_x) * 2u
        + static_cast<std::uint64_t>(frames) * frame_width
        + static_cast<std::uint64_t>(frames - 1u) * spacing_x;
    const std::uint64_t required_height = static_cast<std::uint64_t>(margin_y) * 2u
        + static_cast<std::uint64_t>(directions) * frame_height
        + static_cast<std::uint64_t>(directions - 1u) * spacing_y;
    if (required_width > imported.texture.width || required_height > imported.texture.height) {
        return fail(0, "sheet is too small for declared grid (need " + std::to_string(required_width)
            + "x" + std::to_string(required_height) + ", got "
            + std::to_string(imported.texture.width) + "x" + std::to_string(imported.texture.height) + ")");
    }

    std::vector<TextureData> derived;
    derived.reserve(static_cast<std::size_t>(directions) * frames);
    for (std::uint32_t direction = 0; direction < directions; ++direction) {
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            const std::uint32_t x = margin_x + frame * (frame_width + spacing_x);
            const std::uint32_t y = margin_y + direction * (frame_height + spacing_y);
            derived.push_back(slice_rgba(imported.texture, x, y, frame_width, frame_height,
                clip_name + " D" + std::to_string(direction) + " F" + std::to_string(frame)));
        }
    }

    SpriteAnimationClip clip;
    clip.name = clip_name;
    clip.direction_count = directions;
    clip.frame_count = frames;
    clip.frames_per_second = fps;
    clip.loop = loop;
    clip.textures.reserve(derived.size());
    for (auto& frame : derived) clip.textures.push_back(world.add_texture(std::move(frame)));
    if (!clip.valid()) return fail(0, "generated clip failed validation");

    SpriteSheetAssetResult result;
    result.ok = true;
    result.clip = std::move(clip);
    result.source_asset_id = source_asset->asset_id;
    result.source_path = source_asset->relative_path;
    result.source_resolved_by_id = source_resolution.resolved_by_id;
    result.source_fallback_stale = source_resolution.stale_fallback_path;
    result.sheet_width = imported.texture.width;
    result.sheet_height = imported.texture.height;
    result.frame_width = frame_width;
    result.frame_height = frame_height;
    result.message = "loaded sprite sheet asset: " + path.string();
    return result;
}

} // namespace vespera
