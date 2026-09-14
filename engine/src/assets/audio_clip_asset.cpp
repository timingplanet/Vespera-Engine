#include <vespera/assets/audio_clip_asset.hpp>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

namespace vespera {
namespace {

AudioClipAssetResult fail(std::size_t line, std::string message) {
    if (line != 0) message = "audio clip line " + std::to_string(line) + ": " + message;
    return {false, {}, std::move(message)};
}

} // namespace

AudioClipAssetResult load_audio_clip_asset(
    const std::filesystem::path& path,
    const AssetCatalog& catalog
) {
    std::ifstream input(path);
    if (!input) return fail(0, "could not open audio clip asset: " + path.string());

    AudioClipAsset clip;
    AssetReference source_reference;
    int format_version = 0;
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
            if (command != "vespera_audio_clip" || !(stream >> format_version)
                || format_version < 1 || format_version > 2) {
                return fail(line_number, "expected 'vespera_audio_clip 1|2' header");
            }
            saw_header = true;
            continue;
        }
        if (command == "name") {
            if (!(stream >> std::quoted(clip.name)) || clip.name.empty()) return fail(line_number, "invalid name");
        } else if (command == "source") {
            std::string source_path;
            if (!(stream >> std::quoted(source_path)) || source_path.empty()) return fail(line_number, "invalid source audio path");
            source_reference.path = source_path;
        } else if (command == "source_asset") {
            if (format_version < 2) return fail(line_number, "source_asset requires audio clip format 2");
            std::string source_path;
            if (!(stream >> std::quoted(source_reference.asset_id) >> std::quoted(source_path))
                || source_reference.asset_id.empty()) return fail(line_number, "invalid stable source asset reference");
            source_reference.path = source_path;
        } else if (command == "volume") {
            if (!(stream >> clip.volume) || clip.volume < 0.0f || clip.volume > 4.0f) return fail(line_number, "volume must be between 0 and 4");
        } else if (command == "loop") {
            int value = 0;
            if (!(stream >> value) || (value != 0 && value != 1)) return fail(line_number, "loop must be 0 or 1");
            clip.loop = value != 0;
        } else if (command == "spatial") {
            int value = 0;
            if (!(stream >> value) || (value != 0 && value != 1)) return fail(line_number, "spatial must be 0 or 1");
            clip.spatial = value != 0;
        } else if (command == "min_distance") {
            if (!(stream >> clip.min_distance) || clip.min_distance < 0.0f) return fail(line_number, "min_distance must be non-negative");
        } else if (command == "max_distance") {
            if (!(stream >> clip.max_distance) || clip.max_distance <= 0.0f) return fail(line_number, "max_distance must be positive");
        } else if (command == "end_audio_clip") {
            saw_end = true;
            break;
        } else {
            return fail(line_number, "unknown command '" + command + "'");
        }
    }

    if (!saw_header) return fail(0, "missing audio clip header");
    if (!saw_end) return fail(0, "missing end_audio_clip");
    if (clip.name.empty()) return fail(0, "name is missing");
    if (source_reference.empty()) return fail(0, "source audio is missing");
    if (clip.max_distance <= clip.min_distance) return fail(0, "max_distance must be greater than min_distance");

    const auto source_resolution = catalog.resolve_reference(source_reference);
    const auto* source_asset = source_resolution.record;
    if (!source_asset || source_asset->kind != AssetKind::Audio) {
        const std::string authored = source_reference.path.empty() ? source_reference.asset_id : source_reference.path.generic_string();
        return fail(0, "source audio asset is missing: " + authored);
    }
    if (source_asset->absolute_path.extension() != ".wav") {
        return fail(0, "current runtime audio backend requires a WAV source: " + source_asset->relative_path.generic_string());
    }

    clip.source_asset_id = source_asset->asset_id;
    clip.source_path = source_asset->relative_path;
    clip.source_resolved_by_id = source_resolution.resolved_by_id;
    clip.source_fallback_stale = source_resolution.stale_fallback_path;
    return {true, std::move(clip), "loaded audio clip asset: " + path.string()};
}

} // namespace vespera
