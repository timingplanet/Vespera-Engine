#include <vespera/assets/sprite_clip_asset.hpp>

#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

namespace vespera {
namespace {

SpriteClipAssetResult fail(std::size_t line, std::string message) {
    if (line != 0) message = "sprite clip line " + std::to_string(line) + ": " + message;
    return {false, {}, std::move(message)};
}

TextureId texture_id_by_name(const SectorWorld& world, std::string_view name) {
    const auto& textures = world.textures();
    for (std::size_t i = 0; i < textures.size(); ++i) {
        if (textures[i].name == name) return static_cast<TextureId>(i);
    }
    return kInvalidTexture;
}

} // namespace

SpriteClipAssetResult load_sprite_clip_asset(
    const std::filesystem::path& path,
    const AssetCatalog& catalog,
    const SectorWorld& world
) {
    std::ifstream input(path);
    if (!input) return fail(0, "could not open sprite clip asset: " + path.string());

    SpriteAnimationClip clip;
    std::uint32_t declared_directions = 0;
    std::uint32_t declared_frames = 0;
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
            int version = 0;
            if (command != "vespera_sprite_clip" || !(stream >> version) || version != 1) {
                return fail(line_number, "expected 'vespera_sprite_clip 1' header");
            }
            saw_header = true;
            continue;
        }
        if (command == "name") {
            if (!(stream >> std::quoted(clip.name)) || clip.name.empty()) return fail(line_number, "invalid clip name");
        } else if (command == "directions") {
            if (!(stream >> declared_directions) || (declared_directions != 1u && declared_directions != 4u && declared_directions != 8u)) {
                return fail(line_number, "directions must be 1, 4, or 8");
            }
            clip.direction_count = declared_directions;
        } else if (command == "frames") {
            if (!(stream >> declared_frames) || declared_frames == 0u) return fail(line_number, "frames must be greater than zero");
            clip.frame_count = declared_frames;
        } else if (command == "fps") {
            if (!(stream >> clip.frames_per_second) || clip.frames_per_second < 0.0f) return fail(line_number, "fps must be non-negative");
        } else if (command == "loop") {
            int value = 0;
            if (!(stream >> value) || (value != 0 && value != 1)) return fail(line_number, "loop must be 0 or 1");
            clip.loop = value != 0;
        } else if (command == "texture") {
            std::string resource_name;
            std::string asset_path;
            if (!(stream >> std::quoted(resource_name) >> std::quoted(asset_path)) || resource_name.empty() || asset_path.empty()) {
                return fail(line_number, "texture requires a resource name and project-relative asset path");
            }
            const auto* asset = catalog.find(asset_path);
            if (!asset || asset->kind != AssetKind::Texture) return fail(line_number, "texture asset is missing: " + asset_path);
            const auto id = texture_id_by_name(world, resource_name);
            if (id == kInvalidTexture) return fail(line_number, "texture resource is not registered in the scene world: " + resource_name);
            clip.textures.push_back(id);
        } else if (command == "end_sprite_clip") {
            saw_end = true;
            break;
        } else {
            return fail(line_number, "unknown command '" + command + "'");
        }
    }

    if (!saw_header) return fail(0, "missing sprite clip header");
    if (!saw_end) return fail(0, "missing end_sprite_clip");
    if (clip.name.empty()) return fail(0, "clip name is missing");
    if (declared_directions == 0u || declared_frames == 0u) return fail(0, "directions/frames are missing");
    const auto expected = static_cast<std::size_t>(declared_directions) * declared_frames;
    if (clip.textures.size() != expected) {
        return fail(0, "expected " + std::to_string(expected) + " texture records, got " + std::to_string(clip.textures.size()));
    }
    if (!clip.valid()) return fail(0, "clip data failed validation");
    return {true, std::move(clip), "loaded sprite clip asset: " + path.string()};
}

} // namespace vespera
